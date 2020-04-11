/*
    Genesis - A toolkit for working with phylogenetic data.
    Copyright (C) 2014-2020 Lucas Czech

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
    Lucas Czech <lucas.czech@h-its.org>
    Exelixis Lab, Heidelberg Institute for Theoretical Studies
    Schloss-Wolfsbrunnenweg 35, D-69118 Heidelberg, Germany
*/

/**
 * @brief
 *
 * @file
 * @ingroup sequence
 */

#include "genesis/sequence/functions/quality.hpp"

#include "genesis/sequence/formats/fastq_reader.hpp"
#include "genesis/sequence/sequence.hpp"
#include "genesis/utils/io/input_source.hpp"
#include "genesis/utils/io/input_stream.hpp"
#include "genesis/utils/text/char.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace genesis {
namespace sequence {

// =================================================================================================
//     Quality Encoding and Decoding
// =================================================================================================

/**
 * @brief Local helper function to throw if any invalid fastq quality chars are being used.
 */
inline void throw_invalid_quality_code_( char quality_code, QualityEncoding encoding )
{
    throw std::invalid_argument(
        "Invalid quality code: " + utils::char_to_hex( quality_code ) +
        " is not in the valid range for " + quality_encoding_name( encoding ) + " quality codes."
    );
}

std::string quality_encoding_name( QualityEncoding encoding )
{
    switch( encoding ) {
        case QualityEncoding::kSanger:
            return "Sanger";
        case QualityEncoding::kIllumina13:
            return "Illumina 1.3+";
        case QualityEncoding::kIllumina15:
            return "Illumina 1.5+";
        case QualityEncoding::kIllumina18:
            return "Illumina 1.8+";
        case QualityEncoding::kSolexa:
            return "Solexa";

        default:
            throw std::invalid_argument( "Invalid quality encoding type." );
    };
}

unsigned char quality_decode_to_phred_score( char quality_code, QualityEncoding encoding )
{
    // Convert using an offset. It's as simple as that.
    // Except that we have different offsets for different fastq encoding styles.
    // And also, Solexa needs special treatment, as we internally use phred scores only.
    // Basically, fastq is again one of those weird bioinformatics file formats that drives
    // everyone crazy by being ill-defined, and having contraticting variants and conventions...
    // NB: We do not check for upper bounds of the scores here, as higher-quality scores can come
    // from downstream processing.
    switch( encoding ) {
        case QualityEncoding::kSanger:
        case QualityEncoding::kIllumina18:
            if( quality_code < 33 || quality_code >= 127 ) {
                throw_invalid_quality_code_( quality_code, encoding );
            }
            return static_cast<unsigned char>( quality_code ) - 33;

        case QualityEncoding::kIllumina13:
        case QualityEncoding::kIllumina15:
            if( quality_code < 64 || quality_code >= 127 ) {
                throw_invalid_quality_code_( quality_code, encoding );
            }
            return static_cast<unsigned char>( quality_code ) - 64;

        case QualityEncoding::kSolexa:
            if( quality_code < 59 || quality_code >= 127 ) {
                throw_invalid_quality_code_( quality_code, encoding );
            }
            return solexa_score_to_phred_score( static_cast<unsigned char>( quality_code ) - 64 );

        default:
            throw std::invalid_argument( "Invalid quality encoding type." );
    };
}

std::vector<unsigned char> quality_decode_to_phred_score(
    std::string const& quality_codes,
    QualityEncoding encoding
) {
    // Reserve space for the result. Do this first, to facilitate copy elision.
    auto result = std::vector<unsigned char>( quality_codes.size() );

    // Only switch on the encoding once, for speed. We use a fake offset for Solexa,
    // as they can go in the negative range to -5. Doing it this way makes our error checking
    // code consistent. We correct for this later in the Solxa conversion loop below.
    unsigned char offset;
    switch( encoding ) {
        case QualityEncoding::kSanger:
        case QualityEncoding::kIllumina18:
            offset = 33;
            break;

        case QualityEncoding::kIllumina13:
        case QualityEncoding::kIllumina15:
            offset = 64;
            break;

        case QualityEncoding::kSolexa:
            offset = 59;
            break;

        default:
            throw std::invalid_argument( "Invalid quality encoding type." );
    };

    // Run the conversion. For now, we convert Solexa as if it was phred, and fix this below.
    // This avoids code duplication for the error checking.
    for( size_t i = 0; i < quality_codes.size(); ++i ) {

        // TODO speed this up by using words!
        // https://graphics.stanford.edu/~seander/bithacks.html#HasLessInWord
        // also for the subtraction: use a whole word and subtract.
        // probably need to cast the string to a unsigned char first, but that should be doable.

        // Strict error checking!
        if( quality_codes[i] < offset || quality_codes[i] >= 127 ) {
            throw_invalid_quality_code_( quality_codes[i], encoding );
        }

        result[i] = static_cast<unsigned char>( quality_codes[i] ) - offset;
    }

    // For Solexa, we iterate the sequence again in order to convert it to phred.
    // This is slower and could be avoided with a bit of code duplication, but no one uses that
    // format anyway any more, so that case should be rare.
    if( encoding == QualityEncoding::kSolexa ) {
        for( size_t i = 0; i < quality_codes.size(); ++i ) {
            result[i] = solexa_score_to_phred_score( result[i] - 5 );
        }
    }
    return result;
}

// =================================================================================================
//     Guess Quality Encoding Type
// =================================================================================================

QualityEncoding guess_fastq_quality_encoding( std::array<size_t, 128> const& char_counts )
{
    // Find the first entry that is not 0
    size_t min = 0;
    for( size_t i = 0; i < char_counts.size(); ++i ) {
        if( char_counts[i] > 0 ) {
            min = i;
            break;
        }
    }

    // Find the last entry that is not 0
    size_t max = 128;
    for( size_t i = char_counts.size(); i > 0; --i ) {
        if( char_counts[i-1] > 0 ) {
            max = i-1;
            break;
        }
    }

    // Some checks.
    if( min < 33 || max >= 127 ) {
        throw std::runtime_error(
            "Invalid char counts provided to guess quality score encoding. Only printable "
            "characters (ASCII range 33 to 127) are allowed in fastq quality encodings."
        );
    }
    assert( min <= max );

    // Sanger and Illumina 1.8 use an offset of 33. The next higher offset is 64, but with Solexa ranging into the negative until -5, we find that anything below 64-5=59 cannot have the 64 offset, and hence must have the 33 offset.
    if( min < 59 ) {
        // Sanger and Illumina 1.8 are basically the same, so it does not make a difference
        // whether we detect them correctly or not. However, we can still try to guess, for
        // completeness. Illumina 1.8 seems to have one more character that can be used. Thus, if
        // this character occurs, we can be sure. If not, it might just be that no base was that
        // accurate. But then, it doesn't really matter anyway.
        if( max > 73 ) {
            return QualityEncoding::kIllumina18;
        } else {
            return QualityEncoding::kSanger;
        }
    }

    // Solexa goes down to a score of -5, with an offset of 64 for 0, so anything below 64 is
    // negative, meaning that it cannot be Illumina 1.3 or 1.5.
    if( min < 64 ) {
        return QualityEncoding::kSolexa;
    }

    // At this point, we could use a heuristic to test how common 'B' is, which is special in Illumina 1.5,
    // see https://github.com/brentp/bio-playground/blob/master/reads-utils/guess-encoding.py for
    // details. This would enable more fine-grained distinction between Illumina 1.3 and 1.5.
    // But for now, we simply assume that an encoding without anything before 'B' is Illumina 1.5
    if( min < 66 ) {
        return QualityEncoding::kIllumina13;
    }
    return QualityEncoding::kIllumina15;
}

QualityEncoding guess_fastq_quality_encoding( std::shared_ptr< utils::BaseInputSource > source )
{
    // Init a counting array for each char, use value-initialization to 0
    auto char_counts = std::array<size_t, 128>();

    // Prepare a reader that simply increments all char counts for the quality chars
    // that are found in the sequences.
    auto reader = FastqReader();
    reader.quality_string_plugin(
        [&]( std::string const& quality_string, Sequence& sequence )
        {
            (void) sequence;
            for( auto q : quality_string ) {

                // Cast, so that we catch the issue that char can be signed or unsigned,
                // depending on the compiler. This might cause unnecessary warnings otherwise.
                auto qu = static_cast<unsigned char>(q);
                if( qu > 127 ) {
                    throw std::invalid_argument(
                        "Invalid quality score character outside of the ASCII range."
                    );
                }

                // assert( (q & ~0x7f) == 0 );
                assert( qu < char_counts.size() );
                ++char_counts[ qu ];
            }
        }
    );

    // Read the input, sequence by sequence.
    utils::InputStream input_stream( source );
    Sequence seq;
    while( reader.parse_sequence( input_stream, seq ) ) {
        // Do nothing. All the work is done in the plugin function above.
    }

    // Return our guess based on the quality characters that were found in the sequences.
    return guess_fastq_quality_encoding( char_counts );
}

// =================================================================================================
//     Quality Computations
// =================================================================================================

unsigned char error_probability_to_phred_score( double error_probability )
{
    if( ! std::isfinite( error_probability ) || error_probability < 0.0 || error_probability > 1.0 ) {
        throw std::invalid_argument(
            "Cannot convert error probability outside of range [0.0, 1.0] to phred score."
        );
    }

    // Compute the value and put into the valid range for unsigned chars. This might exceed
    // the encoding that is later used to store the scores in fastq, but this does not concern us
    // here. Instead, we offer the full range here, and clamp later to the value range when encoding.
    double v = std::round( -10.0 * std::log10( error_probability ));
    v = std::min( v, 255.0 );
    return static_cast<unsigned char>( v );
}

double phred_score_to_error_probability( unsigned char phred_score )
{
    return std::pow( 10.0, static_cast<double>( phred_score ) / -10.0 );
}

signed char error_probability_to_solexa_score( double error_probability )
{
    if( ! std::isfinite( error_probability ) || error_probability < 0.0 || error_probability > 1.0 ) {
        throw std::invalid_argument(
            "Cannot convert error probability outside of range [0.0, 1.0] to solexa score."
        );
    }

    // The following are the limits than can be encoded in typical fastq-solexa encoding.
    // We are not using them here, but instead use more relaxed limts, and will apply the actual
    // limits only when encoding to fastq.
    // // min that can be encoded in fastq with solexa encoding
    // if( error_probability < 6.30957344e-7 ) {
    //     return 62;
    // }
    // // max that can be encoded in fastq with solexa encoding
    // if( error_probability > 0.75 ) {
    //     return -5;
    // }

    // Compute the score, and check its boundaries. We did a check before, so this is duplication,
    // but leads to nicer user output.
    double v = std::round( -10.0 * std::log10( error_probability / ( 1.0 - error_probability )));
    v = std::min( v, 127.0 );
    v = std::max( v, -128.0 );
    return static_cast<signed char>( v );
}

double solexa_score_to_error_probability( signed char solexa_score )
{
    if( solexa_score < -5 ) {
        solexa_score = -5;
    }
    double const t = std::pow( 10.0, static_cast<double>( solexa_score ) / -10.0 );
    return t / ( 1.0 + t );
}

signed char phred_score_to_solexa_score( unsigned char phred_score )
{
    if( phred_score <= 1 ) {
        return -5;
    }
    double v = std::round(
        10.0 * std::log10( std::pow( 10.0, static_cast<double>( phred_score ) / 10.0 ) - 1.0 )
    );
    v = std::min( v, 127.0 );
    return static_cast<signed char>( v );
}

unsigned char solexa_score_to_phred_score( signed char solexa_score )
{
    // This cannot overflow, so no need to check.
    return static_cast<unsigned char>( std::round(
        10.0 * std::log10( std::pow( 10.0, static_cast<double>( solexa_score ) / 10.0 ) + 1.0 )
    ));
}

} // namespace sequence
} // namespace genesis
