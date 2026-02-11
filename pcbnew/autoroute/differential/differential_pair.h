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

#ifndef DIFFERENTIAL_PAIR_H
#define DIFFERENTIAL_PAIR_H

#include "../autoroute_control.h"
#include "../locate/locate_connection.h"
#include <math/vector2d.h>
#include <vector>
#include <string>
#include <utility>

// Forward declarations
class BOARD;
class PAD;
class SHAPE_SEARCH_TREE;


/**
 * Configuration for differential pair routing.
 */
struct DIFF_PAIR_CONFIG
{
    int     gap = 200000;            ///< Gap between traces in nm (default 0.2mm)
    int     trace_width = 150000;    ///< Trace width in nm (default 0.15mm)
    int     via_gap = 250000;        ///< Gap between vias in nm
    double  length_tolerance = 0.0;  ///< Max length difference in nm (0 = no matching)
    bool    coupled_width = true;    ///< Use coupled trace width (narrower than single-ended)
    std::string suffix_positive = "_P";  ///< Suffix for positive net
    std::string suffix_negative = "_N";  ///< Suffix for negative net
};


/**
 * Represents a differential pair connection.
 */
struct DIFF_PAIR_CONNECTION
{
    std::string base_name;           ///< Base net name (without _P/_N suffix)
    std::string positive_net;        ///< Name of positive net
    std::string negative_net;        ///< Name of negative net
    int         positive_net_code;   ///< Net code for positive
    int         negative_net_code;   ///< Net code for negative

    PAD*        positive_start;      ///< Starting pad for positive signal
    PAD*        negative_start;      ///< Starting pad for negative signal
    PAD*        positive_end;        ///< Ending pad for positive signal
    PAD*        negative_end;        ///< Ending pad for negative signal

    DIFF_PAIR_CONNECTION()
        : positive_net_code( 0 )
        , negative_net_code( 0 )
        , positive_start( nullptr )
        , negative_start( nullptr )
        , positive_end( nullptr )
        , negative_end( nullptr )
    {}
};


/**
 * Result of differential pair routing.
 */
struct DIFF_PAIR_PATH
{
    ROUTING_PATH positive_path;      ///< Path for positive signal
    ROUTING_PATH negative_path;      ///< Path for negative signal
    int64_t      positive_length;    ///< Total length of positive path
    int64_t      negative_length;    ///< Total length of negative path
    int64_t      length_difference;  ///< Difference in lengths (abs)
    bool         valid = false;      ///< True if both paths are valid
    bool         length_matched = false; ///< True if within tolerance
};


/**
 * Differential pair routing algorithm.
 *
 * Routes two signals as a coupled pair with constant spacing.
 * Supports:
 * - Coupled trace routing with configurable gap
 * - Length matching between the two traces
 * - Via pair placement for layer transitions
 * - Automatic net pairing by naming convention
 */
class DIFF_PAIR_ROUTER
{
public:
    DIFF_PAIR_ROUTER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl );

    /**
     * Set the search tree for collision detection.
     */
    void SetSearchTree( SHAPE_SEARCH_TREE* aTree ) { m_searchTree = aTree; }

    /**
     * Set the differential pair configuration.
     */
    void SetConfig( const DIFF_PAIR_CONFIG& aConfig ) { m_config = aConfig; }

    /**
     * Find all differential pairs in the design.
     * Identifies pairs by matching _P/_N suffix naming convention.
     */
    std::vector<DIFF_PAIR_CONNECTION> FindDifferentialPairs() const;

    /**
     * Route a differential pair connection.
     *
     * @param aConnection The differential pair to route
     * @return The resulting paths for both signals
     */
    DIFF_PAIR_PATH Route( const DIFF_PAIR_CONNECTION& aConnection );

    /**
     * Check if two nets form a differential pair.
     */
    bool IsDifferentialPair( const std::string& aNet1, const std::string& aNet2 ) const;

    /**
     * Get the base name from a differential pair net name.
     * E.g., "USB_D_P" -> "USB_D"
     */
    std::string GetBaseName( const std::string& aNetName ) const;

private:
    /**
     * Route both traces of the differential pair together.
     * Uses coupled routing to maintain constant gap.
     */
    bool RouteCoupled( const DIFF_PAIR_CONNECTION& aConnection, DIFF_PAIR_PATH& aResult );

    /**
     * Generate a coupled path segment.
     * Returns points for both positive and negative traces.
     */
    bool GenerateCoupledSegment( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                  int aGap, bool aPositiveOnLeft,
                                  std::vector<VECTOR2I>& aPositivePoints,
                                  std::vector<VECTOR2I>& aNegativePoints );

    /**
     * Calculate perpendicular offset for coupled traces.
     */
    VECTOR2I CalculateOffset( const VECTOR2I& aDirection, int aDistance, bool aLeft ) const;

    /**
     * Add length equalization to a path.
     * Adds serpentine pattern to the shorter path to match the longer one.
     */
    void EqualizeLengths( DIFF_PAIR_PATH& aPath );

    /**
     * Add a serpentine segment to extend path length.
     */
    void AddSerpentine( PATH_SEGMENT& aSegment, int aLengthToAdd );

    /**
     * Check if a coupled path segment is valid (no collisions).
     */
    bool IsValidCoupledPath( const VECTOR2I& aPosStart, const VECTOR2I& aPosEnd,
                              const VECTOR2I& aNegStart, const VECTOR2I& aNegEnd,
                              int aLayer, int aPosNetCode, int aNegNetCode ) const;

    /**
     * Calculate path length.
     */
    int64_t CalculatePathLength( const ROUTING_PATH& aPath ) const;

    /**
     * Find pad pairs for differential connection.
     * Matches pads by proximity at each end.
     */
    bool FindPadPairs( DIFF_PAIR_CONNECTION& aConnection ) const;

    BOARD*              m_board;
    SHAPE_SEARCH_TREE*  m_searchTree = nullptr;
    AUTOROUTE_CONTROL   m_control;
    DIFF_PAIR_CONFIG    m_config;
};


#endif // DIFFERENTIAL_PAIR_H
