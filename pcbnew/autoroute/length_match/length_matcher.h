/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LENGTH_MATCHER_H
#define LENGTH_MATCHER_H

#include "../autoroute_control.h"
#include "../locate/locate_connection.h"
#include <math/vector2d.h>
#include <vector>
#include <string>

// Forward declarations
class BOARD;
class SHAPE_SEARCH_TREE;


/**
 * Serpentine (meander) shape types.
 */
enum class MEANDER_STYLE
{
    TRAPEZOIDAL,    ///< Trapezoidal meanders (45-degree entry/exit)
    ROUNDED,        ///< Rounded meanders
    RECTANGULAR     ///< Rectangular meanders (90-degree corners)
};


/**
 * Configuration for length matching.
 */
struct LENGTH_MATCH_CONFIG
{
    int64_t       target_length = 0;      ///< Target length in nm (0 = match to longest)
    int64_t       tolerance = 100000;     ///< Tolerance in nm (default 0.1mm)
    int           min_amplitude = 100000; ///< Minimum meander amplitude in nm
    int           max_amplitude = 2000000;///< Maximum meander amplitude in nm
    int           spacing = 500000;       ///< Meander spacing in nm
    MEANDER_STYLE style = MEANDER_STYLE::TRAPEZOIDAL;
    bool          single_sided = true;    ///< Meanders on one side only
};


/**
 * A net or path to be length matched.
 */
struct LENGTH_MATCH_TARGET
{
    std::string   net_name;
    int           net_code = 0;
    ROUTING_PATH  path;
    int64_t       current_length = 0;     ///< Current path length
    int64_t       length_to_add = 0;      ///< Length needed to reach target
    bool          needs_matching = false;
};


/**
 * Result of length matching operation.
 */
struct LENGTH_MATCH_RESULT
{
    std::vector<LENGTH_MATCH_TARGET> targets;
    int64_t       max_length = 0;         ///< Length of the longest path
    int64_t       max_difference = 0;     ///< Largest difference from target
    int           matched_count = 0;      ///< Number of paths matched
    int           failed_count = 0;       ///< Number of paths that couldn't be matched
    bool          success = false;        ///< True if all within tolerance
};


/**
 * Length matching algorithm.
 *
 * Matches trace lengths by adding serpentine patterns to shorter traces.
 * Used for:
 * - DDR data/strobe matching
 * - High-speed differential pairs
 * - USB, HDMI, and other protocol-specific requirements
 */
class LENGTH_MATCHER
{
public:
    LENGTH_MATCHER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl );

    /**
     * Set the search tree for collision detection.
     */
    void SetSearchTree( SHAPE_SEARCH_TREE* aTree ) { m_searchTree = aTree; }

    /**
     * Set the length matching configuration.
     */
    void SetConfig( const LENGTH_MATCH_CONFIG& aConfig ) { m_config = aConfig; }

    /**
     * Match lengths for a group of paths.
     * All paths will be extended to match the longest (or target_length if set).
     */
    LENGTH_MATCH_RESULT MatchLengths( std::vector<ROUTING_PATH>& aPaths );

    /**
     * Add serpentine to a single path to reach target length.
     */
    bool AddMeanders( ROUTING_PATH& aPath, int64_t aTargetLength );

    /**
     * Calculate the total length of a path.
     */
    int64_t CalculateLength( const ROUTING_PATH& aPath ) const;

    /**
     * Calculate length of a single segment.
     */
    int64_t CalculateSegmentLength( const PATH_SEGMENT& aSegment ) const;

    /**
     * Find the best segment for adding meanders.
     * Prefers longer, straighter segments.
     */
    size_t FindBestSegmentForMeanders( const ROUTING_PATH& aPath ) const;

private:
    /**
     * Generate trapezoidal meander points.
     */
    std::vector<VECTOR2I> GenerateTrapezoidalMeander( const VECTOR2I& aStart,
                                                       const VECTOR2I& aEnd,
                                                       int aAmplitude, int aSpacing,
                                                       int aLengthToAdd, bool aSingleSided );

    /**
     * Generate rectangular meander points.
     */
    std::vector<VECTOR2I> GenerateRectangularMeander( const VECTOR2I& aStart,
                                                       const VECTOR2I& aEnd,
                                                       int aAmplitude, int aSpacing,
                                                       int aLengthToAdd, bool aSingleSided );

    /**
     * Calculate how much length a meander of given parameters will add.
     */
    int64_t CalculateMeanderLength( int aAmplitude, int aSpacing, int aMeanderCount ) const;

    /**
     * Check if meander at position is valid (no collisions).
     */
    bool IsValidMeander( const std::vector<VECTOR2I>& aMeanderPoints,
                         int aLayer, int aWidth, int aNetCode ) const;

    /**
     * Get perpendicular direction for meander.
     */
    VECTOR2I GetPerpendicularDirection( const VECTOR2I& aStart, const VECTOR2I& aEnd ) const;

    BOARD*              m_board;
    SHAPE_SEARCH_TREE*  m_searchTree = nullptr;
    AUTOROUTE_CONTROL   m_control;
    LENGTH_MATCH_CONFIG m_config;
};


#endif // LENGTH_MATCHER_H
