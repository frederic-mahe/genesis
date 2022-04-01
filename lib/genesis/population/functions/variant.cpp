/*
    Genesis - A toolkit for working with phylogenetic data.
    Copyright (C) 2014-2022 Lucas Czech

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Contact:
    Lucas Czech <lczech@carnegiescience.edu>
    Department of Plant Biology, Carnegie Institution For Science
    260 Panama Street, Stanford, CA 94305, USA
*/

/**
 * @brief
 *
 * @file
 * @ingroup population
 */

#include "genesis/population/functions/variant.hpp"

#include "genesis/population/functions/base_counts.hpp"
#include "genesis/utils/io/char.hpp"

#ifdef GENESIS_HTSLIB

extern "C" {
    #include <htslib/hts.h>
    #include <htslib/vcf.h>
}

#endif // htslib guard

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace genesis {
namespace population {

// =================================================================================================
//     Helper Functions
// =================================================================================================

BaseCounts total_base_counts( Variant const& variant )
{
    return merge( variant.samples );
}

SortedBaseCounts sorted_base_counts(
    Variant const& variant, bool reference_first
) {
    // We use sorting networks for speed here. See f_st_asymptotically_unbiased_a1n1a2n2()
    // for details on the technique.

    auto const total = total_base_counts( variant );
    if( reference_first ) {
        SortedBaseCounts result;
        switch( variant.reference_base ) {
            case 'a':
            case 'A': {
                result[0] = { 'A', total.a_count };
                result[1] = { 'C', total.c_count };
                result[2] = { 'G', total.g_count };
                result[3] = { 'T', total.t_count };
                break;
            }
            case 'c':
            case 'C': {
                result[0] = { 'C', total.c_count };
                result[1] = { 'A', total.a_count };
                result[2] = { 'G', total.g_count };
                result[3] = { 'T', total.t_count };
                break;
            }
            case 'g':
            case 'G': {
                result[0] = { 'G', total.g_count };
                result[1] = { 'A', total.a_count };
                result[2] = { 'C', total.c_count };
                result[3] = { 'T', total.t_count };
                break;
            }
            case 't':
            case 'T': {
                result[0] = { 'T', total.t_count };
                result[1] = { 'A', total.a_count };
                result[2] = { 'C', total.c_count };
                result[3] = { 'G', total.g_count };
                break;
            }
            default: {
                throw std::runtime_error(
                    "Invalid reference base character " + utils::char_to_hex( variant.reference_base )
                );
            }
        }
        if( result[1].count < result[2].count ) {
            std::swap( result[1], result[2] );
        }
        if( result[1].count < result[3].count ) {
            std::swap( result[1], result[3] );
        }
        if( result[2].count < result[3].count ) {
            std::swap( result[2], result[3] );
        }
        return result;
    } else {
        return sorted_base_counts( total );
    }
}

char guess_reference_base( Variant const& variant )
{
    auto const ref = utils::to_upper( variant.reference_base );
    if( ref == 'A' || ref == 'C' || ref == 'G' || ref == 'T' ) {
        return ref;
    } else {
        auto const sorted = sorted_base_counts( variant, false );
        if( sorted[0].count > 0 ) {
            return utils::to_upper( sorted[0].base );
        }
    }

    // Last else case outside, so that compilers always see that this function returns a value.
    return 'N';
}

char guess_alternative_base( Variant const& variant, bool force )
{
    auto const alt = utils::to_upper( variant.alternative_base );
    if( ! force && ( alt == 'A' || alt == 'C' || alt == 'G' || alt == 'T' )) {
        return alt;
    } else {
        auto const ref = utils::to_upper( variant.reference_base );
        if( ref == 'A' || ref == 'C' || ref == 'G' || ref == 'T' ) {
            auto const sorted = sorted_base_counts( variant, true );
            if( sorted[1].count > 0 ) {
                return utils::to_upper( sorted[1].base );
            }
        }
    }

    // Else case outside, so that compilers always see that this function returns a value.
    return 'N';
}

// =================================================================================================
//     Conversion Functions
// =================================================================================================

std::ostream& to_sync( Variant const& var, std::ostream& os )
{
    os << var.chromosome << "\t" << var.position << "\t" << var.reference_base;
    for( auto const& bs : var.samples ) {
        os << "\t";
        to_sync( bs, os );
    }
    os << "\n";
    return os;
}

Variant convert_to_variant(
    SimplePileupReader::Record const& record,
    unsigned char min_phred_score
) {
    // Set basic data
    Variant result;
    result.chromosome     = record.chromosome;
    result.position       = record.position;
    result.reference_base = utils::to_upper( record.reference_base );

    // Convert the individual samples
    result.samples.reserve( record.samples.size() );
    for( size_t i = 0; i < record.samples.size(); ++i ) {
        result.samples.push_back( convert_to_base_counts( record.samples[i], min_phred_score ));
    }

    // Pileup does not contain ALT bases, so infer them from counts,
    // using the base with the most counts that is not the reference base.
    // We only do this if we have a reference base though, as otherwise, the sorting and alternative
    // is meaningless anyway. Only need to check upper case here, as we converted above.
    // Also, we do not set the alt base if it does not have any counts, and in that case is also
    // meaningless to have an alt base.
    if(
        result.reference_base == 'A' ||
        result.reference_base == 'C' ||
        result.reference_base == 'G' ||
        result.reference_base == 'T'
    ) {
        auto const sorted = sorted_base_counts( result, true );
        if( sorted[1].count > 0 ) {
            result.alternative_base = utils::to_upper( sorted[1].base );
        }
    }

    return result;
}

#ifdef GENESIS_HTSLIB

/**
 * @brief Local helper function that returns the REF and ALT chars of a VcfRecord for SNPs.
 *
 * This function expects the @p record to only contain SNP REF and ALT (single nucleotides),
 * and throws when not. It then fills the resulting array with these chars. That is, result[0]
 * is the REF char, result[1] the first ALT char, and so forth.
 *
 * To keep it speedy, we always return an array that is large enough for all `ACGTND`,
 * and return the number of used entries as the second value of the pair.
 */
std::pair<std::array<char, 6>, size_t> get_vcf_record_snp_ref_alt_chars_( VcfRecord const& record )
{
    // Get all variants (REF and ALT), and check them. We manually add deletion here if ALT == ".",
    // as this is not part of the variants provided from htslib.
    // There are only 6 possible single nucleotide variants (ACGTN.), so we save the effort of
    // creating an intermediate vector, and use a fixed size array and a counter instead.
    // Also, we use htslib functions directly on the vcf record to not have to go through
    // string allocations for each alternative.
    record.unpack();
    auto rec_data = record.data();

    // The n_allele count does not include deletions ('.'), meaning that if there is only a single
    // variant, we manually adjust this to also include the deletion.
    // To avoid too much branching, we init the array so that we have all deletions initially,
    // and hence do not need to overwrite in the case that we added that deletion manually
    // to the counter.
    size_t const var_cnt = rec_data->n_allele == 1 ? rec_data->n_allele + 1 : rec_data->n_allele;
    auto vars = std::array<char, 6>{ '.', '.', '.', '.', '.', '.' };
    if( var_cnt > 6 ) {
        throw std::runtime_error(
            "Invalid VCF Record that contains a REF or ALT sequence/allele with "
            "invalid nucleitides where only `[ACGTN.]` are allowed, at " +
            record.get_chromosome() + ":" + std::to_string( record.get_position() )
        );
    }

    // Now store all single nucleotide alleles that are in the record
    // (we only count up to the actual number that is there, so that the missing deletion [in case
    // that this record has a deletion] is not touched).
    for( size_t i = 0; i < rec_data->n_allele; ++i ) {
        if( std::strlen( rec_data->d.allele[i] ) != 1 ) {
            throw std::runtime_error(
                "Cannot convert VcfRecord to Variant, as one of the VcfRecord REF or ALT "
                "sequences/alleles is not a single nucleotide (it is not a SNP), at " +
                record.get_chromosome() + ":" + std::to_string( record.get_position() )
            );
        }
        vars[i] = *rec_data->d.allele[i];
    }

    return { vars, var_cnt };
}

Variant convert_to_variant_as_pool( VcfRecord const& record )
{
    // Error check.
    if( ! record.has_format( "AD" )) {
        throw std::runtime_error(
            "Cannot convert VcfRecord to Variant, as the VcfRecord does not have "
            "the required FORMAT field 'AD' for alleleic depth."
        );
    }

    // Get the ref and alt chars of the SNP.
    auto const snp_chars = get_vcf_record_snp_ref_alt_chars_( record );

    // Prepare common fields of the result.
    // For the reference base, we use the first nucleotide of the first variant (REF);
    // above, we have ensured that this exists and is in fact a single nucleotide only.
    // Same for the alternative base, where we use the first ALT in the record.
    Variant result;
    result.chromosome       = record.get_chromosome();
    result.position         = record.get_position();
    result.reference_base   = snp_chars.first[0];
    result.alternative_base = snp_chars.first[1]; // TODO this is only reasonable for biallelic SNPs

    // Process the samples that are present in the VCF record line.
    result.samples.reserve( record.header().get_sample_count() );
    for( auto const& sample_ad : record.get_format_int("AD") ) {
        if( sample_ad.valid_value_count() > 0 && sample_ad.valid_value_count() != snp_chars.second ) {
            throw std::runtime_error(
                "Invalid VCF Record that contains " + std::to_string( snp_chars.second ) +
                " REF and ALT sequences/alleles, but its FORMAT field 'AD' only contains " +
                std::to_string( sample_ad.valid_value_count() ) + " entries, at " +
                record.get_chromosome() + ":" + std::to_string( record.get_position() )
            );
        }

        // Go through all REF and ALT entries and their respective FORMAT 'AD' counts,
        // and sum them up, storing them in a new BaseCounts instance at the end of the vector.
        result.samples.emplace_back();
        auto& sample = result.samples.back();
        for( size_t i = 0; i < sample_ad.valid_value_count(); ++i ) {

            // Get the nucleotide and its count.
            auto const cnt = sample_ad.get_value_at(i);
            if( cnt < 0 ) {
                throw std::runtime_error(
                    "Invalid VCF Record with FORMAT field 'AD' value < 0 for a sample, at " +
                    record.get_chromosome() + ":" + std::to_string( record.get_position() )
                );
            }

            // Add it to the respective count variable of the sample.
            switch( snp_chars.first[i] ) {
                case 'a':
                case 'A': {
                    sample.a_count = cnt;
                    break;
                }
                case 'c':
                case 'C': {
                    sample.c_count = cnt;
                    break;
                }
                case 'g':
                case 'G': {
                    sample.g_count = cnt;
                    break;
                }
                case 't':
                case 'T': {
                    sample.t_count = cnt;
                    break;
                }
                case 'n':
                case 'N': {
                    sample.n_count = cnt;
                    break;
                }
                case '.': {
                    sample.d_count = cnt;
                    break;
                }
                default: {
                    throw std::runtime_error(
                        "Invalid VCF Record that contains a REF or ALT sequence/allele with "
                        "invalid nucleitide `" + std::string( 1, snp_chars.first[i] ) +
                        "` where only `[ACGTN.]` are allowed, at " +
                        record.get_chromosome() + ":" + std::to_string( record.get_position() )
                    );
                }
            }
        }
    }

    // Last proof check.
    if( result.samples.size() != record.header().get_sample_count() ) {
        throw std::runtime_error(
            "Invalid VCF Record with number of samples in the record (" +
            std::to_string( result.samples.size() ) + ") not equal to the number of samples given "
            "in the VCF header (" + std::to_string( record.header().get_sample_count() ) + "), at " +
            record.get_chromosome() + ":" + std::to_string( record.get_position() )
        );
    }

    return result;
}

Variant convert_to_variant_as_individuals(
    VcfRecord const& record,
    bool use_allelic_depth
) {
    Variant result;

    // Short solution for when we want to use the AD field:
    // Simply re-use the pool approach, and merge into one BaseCounts.
    if( use_allelic_depth ) {
        result = convert_to_variant_as_pool( record );
        result.samples = std::vector<BaseCounts>{ merge( result.samples ) };
        return result;
    }

    // Here we treat each individual just by counting genotypes.
    record.unpack();

    // Error check.
    if( ! record.has_format( "GT" )) {
        throw std::runtime_error(
            "Cannot convert VcfRecord to Variant, as the VcfRecord does not have "
            "the required FORMAT field 'GT' for genotypes."
        );
    }

    // Get the ref and alt chars of the SNP.
    auto const snp_chars = get_vcf_record_snp_ref_alt_chars_( record );

    // Prepare common fields of the result. Same as convert_to_variant_as_pool(), see there.
    result.chromosome       = record.get_chromosome();
    result.position         = record.get_position();
    result.reference_base   = snp_chars.first[0];
    result.alternative_base = snp_chars.first[1]; // TODO this is only reasonable for biallelic SNPs

    // We merge everything into one sample, representing the individuals as a pool.
    result.samples.resize( 1 );
    auto& sample = result.samples.back();

    // Go through all sample columns of the VCF, examining their GT field.
    for( auto const& sample_gt : record.get_format_genotype() ) {

        // Go through all REF and ALT entries and their respective GT values for the current sample.
        for( size_t i = 0; i < sample_gt.valid_value_count(); ++i ) {

            // Get the genoptype and immediately convert to the index
            // that we can look up in the snp array.
            auto const gt = sample_gt.get_value_at(i).variant_index();

            // If the VCF is not totally messed up, this needs to be within the number of
            // REF and ALT nucleotides (or -1 for deletions); check that.
            if( !( gt < 0 || static_cast<size_t>(gt) < snp_chars.second )) {
                throw std::runtime_error(
                    "Invalid VCF Record that contains an index " + std::to_string( gt ) +
                    " into the genotype list that does not exist, at " +
                    record.get_chromosome() + ":" + std::to_string( record.get_position() )
                );
            }

            // Special case deletion. The genotype value stored in VCF is -1 in that case.
            if( gt < 0 ) {
                ++sample.d_count;
                continue;
            }

            // Use the index to get what nucleotide the genotype is, and increment the count.
            switch( snp_chars.first[gt] ) {
                case 'a':
                case 'A': {
                    ++sample.a_count;
                    break;
                }
                case 'c':
                case 'C': {
                    ++sample.c_count;
                    break;
                }
                case 'g':
                case 'G': {
                    ++sample.g_count;
                    break;
                }
                case 't':
                case 'T': {
                    ++sample.t_count;
                    break;
                }
                case 'n':
                case 'N': {
                    ++sample.n_count;
                    break;
                }
                default: {
                    throw std::runtime_error(
                        "Invalid VCF Record that contains a REF or ALT sequence/allele with "
                        "invalid nucleitide `" + std::string( 1, snp_chars.first[i] ) +
                        "` where only `[ACGTN.]` are allowed, at " +
                        record.get_chromosome() + ":" + std::to_string( record.get_position() )
                    );
                }
            }
        }
    }
    return result;
}

#endif // htslib guard

} // namespace population
} // namespace genesis
