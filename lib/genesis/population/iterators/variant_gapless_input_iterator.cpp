/*
    Genesis - A toolkit for working with phylogenetic data.
    Copyright (C) 2014-2024 Lucas Czech

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

#include "genesis/population/iterators/variant_gapless_input_iterator.hpp"

#include "genesis/population/functions/functions.hpp"
#include "genesis/sequence/functions/codes.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>
#include <stdexcept>

namespace genesis {
namespace population {

// =================================================================================================
//     Iterator Constructors and Rule of Five
// =================================================================================================

VariantGaplessInputIterator::Iterator::Iterator( VariantGaplessInputIterator* parent )
    : parent_(parent)
{
    // We use the parent as a check if this Iterator is intended to be a begin() or end()
    // iterator. We here check this to avoid accidentally starting an iteration without data.
    if( ! parent_ ) {
        return;
    }

    // Start the iteration, which already makes the first Variant ready in the input.
    // We don't need to store the end, as the iterator itself is able to tell us that.
    iterator_ = parent_->input_.begin();

    // We get the number of samples in the Variant to initialize the dummy Variant
    // for missing positions where this iterator does not have data.
    auto const sample_name_count = parent_->input_.data().sample_names.size();
    if( iterator_ ) {
        check_iterator_();
        missing_variant_.samples.resize( iterator_->samples.size() );

        // We assume that the sample_names are of the correct size, if given.
        if( sample_name_count > 0 && iterator_->samples.size() != sample_name_count ) {
            throw std::runtime_error(
                "Input source for VariantGaplessInputIterator contains " +
                std::to_string( iterator_->samples.size() ) + " samples, but its sample " +
                "name list contains " + std::to_string( sample_name_count ) + " names."
            );
        }

        // Now we want to start the iteration on the first chromosome where the iterator starts.
        current_locus_ = GenomeLocus( iterator_->chromosome, 1 );
    } else {
        // If we have no data in the input at all (for instance, because of some aggressive
        // filter settings), we use the sample names as an indicator for the number of dummy
        // samples to create. This might still be needed when we want to iterator genome
        // positions from the ref genome or sequence dict.
        missing_variant_.samples.resize( sample_name_count );

        // We have no actual input data. Still let's see if there are extra chromsomes we want.
        // We might not have anything, in which case we are done already.
        auto const chr = find_next_extra_chromosome_();
        if( chr.empty() ) {
            parent_ = nullptr;
            return;
        }
        current_locus_ = GenomeLocus( chr, 1 );
    }

    // If we are here, we have initialized the current locus to the first position on some valid
    // chromosome, and we can start the processing.
    assert( current_locus_.chromosome != "" && current_locus_.position != 0 );
    start_chromosome_();
    prepare_current_variant_();
}

// =================================================================================================
//     Iterator Internal Members
// =================================================================================================

// -------------------------------------------------------------------------
//     start_chromosome_
// -------------------------------------------------------------------------

void VariantGaplessInputIterator::Iterator::start_chromosome_()
{
    // Check that we are not done yet (parent still valid), and that we have either
    // a ref genome or a seq dict, but not both (neither is also fine).
    assert( parent_ );
    assert( !( parent_->ref_genome_ && parent_->seq_dict_ ));

    // Check that we are indeed at the beginning of a new chromosome.
    assert( current_locus_.chromosome != "" );
    assert( current_locus_.position == 1 );
     std::string const& chr = current_locus_.chromosome;

    // Check that we do not accidentally duplicate any chromosomes.
    if( processed_chromosomes_.count( chr ) > 0 ) {
        throw std::runtime_error(
            "In VariantGaplessInputIterator: Chromosome \"" + chr + "\" occurs multiple times. "
            "Likely, this means that the input is not sorted by chromosome and position."
        );
    }
    processed_chromosomes_.insert( chr );

    // If we have a reference genome, set the cache value for fast finding of the sequence.
    if( parent_->ref_genome_ ) {
        ref_genome_it_ = parent_->ref_genome_->find( chr );
        if( ref_genome_it_ == parent_->ref_genome_->end() ) {
            throw std::runtime_error(
                "In VariantGaplessInputIterator: Chromosome \"" + chr + "\" requested "
                "in the input data, which does not occur in the reference genome."
            );
        }
    }

    // Same for sequence dict.
    if( parent_->seq_dict_ ) {
        seq_dict_it_ = parent_->seq_dict_->find( chr );
        if( seq_dict_it_ == parent_->seq_dict_->end() ) {
            throw std::runtime_error(
                "In VariantGaplessInputIterator: Chromosome \"" + chr + "\" requested "
                "in the input data, which does not occur in the sequence dictionary."
            );
        }
    }
}

// -------------------------------------------------------------------------
//     advance_current_locus_
// -------------------------------------------------------------------------

void VariantGaplessInputIterator::Iterator::advance_current_locus_()
{
    // If we have no more input data, we are processing positions (and potential extra chromsomes)
    // as provided by the ref genome or seq dict.
    if( !iterator_ ) {
        advance_current_locus_beyond_input_();
        return;
    }

    // If the input data is at exactly where we are in our iteration (i.e., there was data
    // for the current position), we need to advance the iterator. That could lead to its end,
    // in which case we do the same as above: See if there are positions beyond.
    // If this is not the case, the iterator is somewhere ahead of us here, and so we just leave
    // it there until we reach its position (in which case the condition here will then trigger).
    if( iterator_->chromosome == current_locus_.chromosome ) {
        assert( iterator_->position >= current_locus_.position );
        if( iterator_->position == current_locus_.position ) {
            ++iterator_;
            if( !iterator_ ) {
                advance_current_locus_beyond_input_();
                return;
            }
            check_iterator_();
        }
    }
    assert( iterator_ );

    // If the iterator still has data on the chromosome, or the ref genome or seq dict has,
    // we just move a position forward. We here do not care if the iterator actually has
    // data for that next position; this is checked when populating the data. All we need
    // to know here is that there will be some more data at some point on this chromosome.
    // If not, we start a new chromosome.
    if(
        iterator_->chromosome == current_locus_.chromosome ||
        has_more_ref_loci_on_current_chromosome_()
    ) {
        ++current_locus_.position;
    } else {
        current_locus_ = GenomeLocus( iterator_->chromosome, 1 );
    }
}

// -------------------------------------------------------------------------
//     advance_current_locus_beyond_input_
// -------------------------------------------------------------------------

void VariantGaplessInputIterator::Iterator::advance_current_locus_beyond_input_()
{
    // Assumptions about the caller. We only get called when there is no more data in the iterator,
    // but we are not yet fully done with the ref genome or seq dict extra chromosomes.
    assert( parent_ );
    assert( !iterator_ );

    // We first check if the next incremental position is still valid according to the
    // ref genome or seq dict. If so, we just move to it and are done.
    if( has_more_ref_loci_on_current_chromosome_() ) {
        ++current_locus_.position;
        return;
    }

    // Once we are here, we have processed a chromosome and might want to move to the next.
    // As we are already beyond the input data when this function is called, this can only
    // mean that we want to check for extra chromosomes that are only in the ref genome or
    // seq dict, but not in the input data. Check if we want to do that at all.
    if( !parent_->iterate_extra_chromosomes_ ) {
        current_locus_.clear();
        return;
    }

    // If not, we reached the end of one extra chr and want to move to the next,
    // or if there isn't any, indicate to the caller that we are done.
    auto const next_chr = find_next_extra_chromosome_();
    if( next_chr.empty() ) {
        current_locus_.clear();
    } else {
        current_locus_.chromosome = next_chr;
        current_locus_.position = 1;
    }
}

// -------------------------------------------------------------------------
//     has_more_ref_loci_on_current_chromosome_
// -------------------------------------------------------------------------

bool VariantGaplessInputIterator::Iterator::has_more_ref_loci_on_current_chromosome_()
{
    assert( parent_ );
    assert( !( parent_->ref_genome_ && parent_->seq_dict_ ));

    // Check if there is a next position on the chromosome that we are currently at.
    // Positions are 1-based, so we need smaller or equal comparison here.
    // If neither ref genome nor seq dict are given, we just return false.
    if( parent_->ref_genome_ ) {
        assert( ref_genome_it_ != parent_->ref_genome_->end() );
        assert( ref_genome_it_->label() == current_locus_.chromosome );
        if( current_locus_.position + 1 <= ref_genome_it_->size() ) {
            return true;
        }
    }
    if( parent_->seq_dict_ ) {
        assert( seq_dict_it_ != parent_->seq_dict_->end() );
        assert( seq_dict_it_->name == current_locus_.chromosome );
        if( current_locus_.position + 1 <= seq_dict_it_->length ) {
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------------------
//     find_next_extra_chromosome_
// -------------------------------------------------------------------------

std::string VariantGaplessInputIterator::Iterator::find_next_extra_chromosome_()
{
    assert( parent_ );

    // We might not want to do extra chromosomes at all.
    if( !parent_->iterate_extra_chromosomes_ ) {
        return "";
    }

    // Check for extra ref genome chromosomes.
    if( parent_->ref_genome_ ) {
        // Check if there are any more chromosomes that we have not done yet.
        // During the nornal iteration with data, we always check that a chromosome that is found
        // in the data also is in the ref genome (or seq dict, same there below). So, when we reach
        // the end of the data, and then process the extra chromosomes here, we can be sure that
        // there are no chromosomes in the data that are not also in the ref genome - hence, the
        // size check here works to test completness of the chromosomes.
        if( processed_chromosomes_.size() == parent_->ref_genome_->size() ) {
            return "";
        }

        // If yes, find the next one.
        for( auto const& ref_chr : *parent_->ref_genome_ ) {
            if( ref_chr.label().empty() ) {
                throw std::runtime_error(
                    "Invalid empty chromosome name in reference genome."
                );
            }
            if( processed_chromosomes_.count( ref_chr.label() ) == 0 ) {
                return ref_chr.label();
            }
        }

        // We are guaranteed to find one, so this here cannot be reached.
        assert( false );
    }

    // Same for extra seq dict chromosomes.
    // Unfortunate code duplication due to the slightly different interfaces.
    if( parent_->seq_dict_ ) {
        // Check if there are any more chromosomes that we have not done yet.
        if( processed_chromosomes_.size() == parent_->seq_dict_->size() ) {
            return "";
        }

        // If yes, find the next one.
        for( auto const& entry : *parent_->seq_dict_ ) {
            if( entry.name.empty() ) {
                throw std::runtime_error(
                    "Invalid empty chromosome name in sequence dictionary."
                );
            }
            if( processed_chromosomes_.count( entry.name ) == 0 ) {
                return entry.name;
            }
        }

        // We are guaranteed to find one, so this here cannot be reached.
        assert( false );
    }

    // If neither is given, just return an empty string to indiate that we do not have
    // any extra chromosomes to process.
    return "";
}

// -------------------------------------------------------------------------
//     prepare_current_variant_
// -------------------------------------------------------------------------

void VariantGaplessInputIterator::Iterator::prepare_current_variant_()
{
    // We expect to be at a valid current locus.
    assert( parent_ );
    assert( current_locus_.chromosome != "" && current_locus_.position != 0 );

    // Check that the current locus is valid according to the ref genome or seq dict.
    if( parent_->ref_genome_ ) {
        assert( ref_genome_it_ != parent_->ref_genome_->end() );
        assert( ref_genome_it_->label() == current_locus_.chromosome );

        // We use 1-based positions here, hence the greater-than comparison.
        if( current_locus_.position > ref_genome_it_->length() ) {
            throw std::runtime_error(
                "In VariantGaplessInputIterator: Invalid input data, which has data "
                "beyond the reference genome at " + to_string( current_locus_ )
            );
        }
    }
    if( parent_->seq_dict_ ) {
        assert( seq_dict_it_ != parent_->seq_dict_->end() );
        assert( seq_dict_it_->name == current_locus_.chromosome );
        if( current_locus_.position > seq_dict_it_->length ) {
            throw std::runtime_error(
                "In VariantGaplessInputIterator: Invalid input data, which has data "
                "beyond the reference genome at " + to_string( current_locus_ )
            );
        }
    }

    // Check if the current locus has data. If so, we point our data to the input data.
    // If not, we point to our internal "missing" variant dummy, and reset it from prev iterations.
    if( iterator_ && locus_equal( iterator_->chromosome, iterator_->position, current_locus_ )) {
        current_variant_ = &*iterator_;

        // Error check for consistent sample size.
        if( iterator_->samples.size() != missing_variant_.samples.size() ) {
            throw std::runtime_error(
                "In VariantGaplessInputIterator: Invalid input data that has an inconsistent "
                "number of samples throughout, first occurring at " + to_string( current_locus_ ) +
                ". Expected " + std::to_string( missing_variant_.samples.size() ) +
                " samples based on first iteration, but found " +
                std::to_string( iterator_->samples.size() ) + " samples instead."
            );
        }
    } else {
        current_variant_ = &missing_variant_;
        missing_variant_.chromosome = current_locus_.chromosome;
        missing_variant_.position   = current_locus_.position;
        missing_variant_.reference_base   = 'N';
        missing_variant_.alternative_base = 'N';
    }

    prepare_current_variant_ref_base_();
}

// -------------------------------------------------------------------------
//     prepare_current_variant_ref_base_
// -------------------------------------------------------------------------

void VariantGaplessInputIterator::Iterator::prepare_current_variant_ref_base_()
{
    // This function expects current_variant_ to be set up for the locus already.
    assert( parent_ );
    assert( current_variant_ );
    assert( !current_locus_.chromosome.empty() && current_locus_.position > 0 );
    assert( locus_equal(
        current_variant_->chromosome, current_variant_->position, current_locus_
    ));

    // If we have a ref genome, we use it to get or check the reference base.
    // If not, we are done.
    if( !parent_->ref_genome_ ) {
        return;
    }
    assert( ref_genome_it_ != parent_->ref_genome_->end() );
    assert( ref_genome_it_->label() == current_locus_.chromosome );

    // Get the reference base and check it against the Variant.
    // This throws an exception for mismatches; sets the ref and alt base if not
    // already done so; does nothing if already set in the Variant.
    // We use 1-based positions, but the ref genome is a simple sequence in string
    // format, so we need to offset by one here.
    assert(
        current_locus_.position > 0 &&
        current_locus_.position <= ref_genome_it_->length()
    );
    auto const ref_base = ref_genome_it_->site_at( current_locus_.position - 1 );
    // guess_and_set_ref_and_alt_bases( *current_variant_, ref_base, true );
    if( is_valid_base( current_variant_->reference_base )) {
        bool contains = false;
        try {
            using genesis::sequence::nucleic_acid_code_containment;
            contains = nucleic_acid_code_containment(
                ref_base, current_variant_->reference_base
            );
        } catch(...) {
            // The above throws an error if the given bases are not valid.
            // Catch this, and re-throw a nicer, more understandable exception instead.
            throw std::runtime_error(
                "At chromosome \"" + current_locus_.chromosome + "\" position " +
                std::to_string( current_locus_.position ) + ", the reference genome has base '" +
                std::string( 1, ref_base ) + "', which is not a valid nucleic acid code"
            );
        }
        if( ! contains ) {
            throw std::runtime_error(
                "At chromosome \"" + current_locus_.chromosome + "\" position " +
                std::to_string( current_locus_.position ) +
                ", the reference base in the data is '" +
                std::string( 1, current_variant_->reference_base ) + "'. " +
                "However, the reference genome has base '" + std::string( 1, ref_base ) +
                "', which does not code for that base, and hence likely points to "
                "some kind of mismatch"
            );
        }
    } else {
        current_variant_->reference_base = ref_base;
    }
}

// -------------------------------------------------------------------------
//     check_iterator_
// -------------------------------------------------------------------------

void VariantGaplessInputIterator::Iterator::check_iterator_()
{
    if( iterator_->chromosome.empty() || iterator_->position == 0 ) {
        throw std::runtime_error(
            "In VariantGaplessInputIterator: Invalid position "
            "with empty chromosome name or zero position."
        );
    }
}

} // namespace population
} // namespace genesis
