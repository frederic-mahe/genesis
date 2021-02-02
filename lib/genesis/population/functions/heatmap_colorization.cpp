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
 * @ingroup population
 */

#include "genesis/population/functions/heatmap_colorization.hpp"

#include "genesis/utils/formats/bmp/writer.hpp"

#include <algorithm>
#include <cassert>
#include <cassert>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace genesis {
namespace population {

// =================================================================================================
//     Allele Frequency Heatmap
// =================================================================================================

std::pair<utils::Matrix<utils::Color>, size_t> HeatmapColorization::spectrum_to_image(
    Spectrum const& spectrum
) const {
    using namespace utils;

    // Check.
    if( color_map_.empty() ) {
        throw std::runtime_error(
            "ColorMap has to be assigned a palette before using HeatmapColorization."
        );
    }

    // Edge case.
    if( spectrum.values.empty() ) {
        return { {}, 0 };
    }

    // Get the row size that we need. We later also confirm that this is consistent
    // across all spetra, to make sure that the data is actually a matrix/image.
    assert( ! spectrum.values.empty() );
    size_t rows = spectrum.values[0].size();

    // We need two passes through the data: first, find the max entry, then convert to scale.
    // While doing the first pass, make sure that the data is actually a matrix.
    size_t abs_max = 0;
    for( auto const& col : spectrum.values ) {
        if( col.size() != rows ) {
            throw std::runtime_error(
                "Invalid allele frequency spectrum with inconsistent number of rows."
            );
        }
        for( auto const& val : col ) {
            abs_max = std::max( abs_max, val );
        }
    }

    // Now convert to color values.
    auto image = Matrix<Color>( rows, spectrum.values.size() );
    for( size_t c = 0; c < spectrum.values.size(); ++c ) {
        auto const& col = spectrum.values[c];
        assert( col.size() == rows );

        // Get the max value of the current column.
        size_t col_max = 0;
        for( auto const& val : col ) {
            col_max = std::max( col_max, val );
        }

        // Get the max value that we want to use for normalization.
        double const used_max = static_cast<double>( normalize_per_column_ ? col_max : abs_max );

        // Do the actual per-bin convertion to color.
        for( size_t r = 0; r < col.size(); ++r ) {
            assert( col[r] <= abs_max );
            assert( col[r] <= col_max );

            // Get the row where to write the color to.
            size_t row_idx = invert_vertically_ ? rows - r - 1 : r;
            assert( row_idx < image.rows() );

            // Special case: no bin filled at all in this window. That means, there were no variants
            // in the whole window. If needed, mark with special "empty" color, which we
            // stored in the mask color of the color map.
            if( col_max == 0 && use_empty_window_color_ ) {
                image( row_idx, c ) = color_map_( std::numeric_limits<double>::quiet_NaN() );
                continue;
            }

            // Set the color for the current pixel.
            if( log_scale_ ) {
                // Special case for log scaling: If either the pixel value or the total max
                // for the colum is 1 or below, we cannot use log scaling for them (as we are working
                // with integer number counts here), so we simply use the min value instead.
                if( col[r] <= 1 || used_max <= 1 ) {
                    image( row_idx, c ) = color_map_( 0.0 );
                } else {
                    double frac = std::log( static_cast<double>( col[r] )) / std::log( used_max );
                    image( row_idx, c ) = color_map_( frac );
                }
            } else {
                double frac = static_cast<double>( col[r] ) / used_max;
                image( row_idx, c ) = color_map_( frac );
            }
        }
    }

    // Return the image and the appropriate max value used for the color scaling.
    return { image, normalize_per_column_ ? 1 : abs_max };
}

std::pair<utils::SvgGroup, size_t> HeatmapColorization::spectrum_to_svg(
    Spectrum const& spectrum,
    utils::SvgMatrixSettings settings
) const {
    // Generate the pixel color image matrix.
    auto const spec_img_and_max = spectrum_to_image( spectrum );

    // Return the svg group and the max value here.
    return { make_svg_matrix( spec_img_and_max.first, settings ), spec_img_and_max.second };
}

size_t HeatmapColorization::spectrum_to_bmp_file(
    HeatmapColorization::Spectrum const& spectrum,
    std::shared_ptr<utils::BaseOutputTarget> target
) const {

    // Generate the pixel color image matrix, and write the image to file.
    auto const spec_img_and_max = spectrum_to_image( spectrum );
    utils::BmpWriter().write( spec_img_and_max.first, target );

    // Return only the max value here.
    return spec_img_and_max.second;
}

} // namespace population
} // namespace genesis