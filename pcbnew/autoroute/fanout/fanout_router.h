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

#ifndef FANOUT_ROUTER_H
#define FANOUT_ROUTER_H

#include "../autoroute_control.h"
#include <math/vector2d.h>
#include <math/box2.h>
#include <vector>
#include <string>

// Forward declarations
class BOARD;
class PAD;
class FOOTPRINT;
class SHAPE_SEARCH_TREE;


/**
 * Escape direction for fanout.
 */
enum class FANOUT_DIRECTION
{
    NORTH,      ///< Escape toward top
    SOUTH,      ///< Escape toward bottom
    EAST,       ///< Escape toward right
    WEST,       ///< Escape toward left
    DIAGONAL_NE,///< Escape toward top-right
    DIAGONAL_NW,///< Escape toward top-left
    DIAGONAL_SE,///< Escape toward bottom-right
    DIAGONAL_SW ///< Escape toward bottom-left
};


/**
 * Escape pattern types for BGA fanout.
 */
enum class FANOUT_PATTERN
{
    DIRECT,     ///< Via directly under/beside pad
    STAGGERED,  ///< Alternating via positions
    DOG_BONE,   ///< Via at end of short trace (dog-bone pattern)
    CHANNEL     ///< Via placed in routing channel between pads
};


/**
 * Single fanout connection from a pad to an escape via.
 */
struct FANOUT_CONNECTION
{
    VECTOR2I pad_position;      ///< Pad center position
    VECTOR2I via_position;      ///< Escape via position
    VECTOR2I midpoint;          ///< Optional midpoint for dog-bone routing
    int      pad_layer;         ///< Layer where pad is located
    int      escape_layer;      ///< Layer to escape to
    int      net_code;          ///< Net code of the connection
    std::string net_name;       ///< Net name
    FANOUT_DIRECTION direction; ///< Escape direction
    bool     has_midpoint;      ///< True if using dog-bone with midpoint

    FANOUT_CONNECTION()
        : pad_layer( 0 )
        , escape_layer( 1 )
        , net_code( 0 )
        , direction( FANOUT_DIRECTION::SOUTH )
        , has_midpoint( false )
    {}
};


/**
 * Result of fanout generation for a footprint.
 */
struct FANOUT_RESULT
{
    std::vector<FANOUT_CONNECTION> connections;  ///< Generated fanout connections
    int  successful = 0;        ///< Number of successful fanouts
    int  failed = 0;            ///< Number of failed fanouts (no valid escape)
    bool complete = false;      ///< True if all pads have fanouts
    std::string error_message;  ///< Error description if failed
};


/**
 * Fanout routing algorithm for BGA and QFP packages.
 *
 * Generates escape routing from component pads to vias that allow signals
 * to transition to inner layers for routing. Supports multiple patterns:
 *
 * - Direct: Via placed directly beside pad (minimal trace)
 * - Staggered: Alternating via positions for better density
 * - Dog-bone: Short trace to via for constrained spacing
 * - Channel: Via placed in routing channel between pad rows
 */
class FANOUT_ROUTER
{
public:
    FANOUT_ROUTER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl );

    /**
     * Set the search tree for collision detection.
     */
    void SetSearchTree( SHAPE_SEARCH_TREE* aTree ) { m_searchTree = aTree; }

    /**
     * Set the target layer to escape to.
     */
    void SetEscapeLayer( int aLayer ) { m_escapeLayer = aLayer; }

    /**
     * Set the fanout pattern to use.
     */
    void SetPattern( FANOUT_PATTERN aPattern ) { m_pattern = aPattern; }

    /**
     * Set minimum via-to-via spacing.
     */
    void SetViaSpacing( int aSpacing ) { m_viaSpacing = aSpacing; }

    /**
     * Generate fanout for a specific footprint.
     *
     * @param aFootprint The footprint to create fanout for
     * @return Fanout result with generated connections
     */
    FANOUT_RESULT GenerateFanout( FOOTPRINT* aFootprint );

    /**
     * Generate fanout for a specific pad.
     *
     * @param aPad The pad to create escape route for
     * @return True if fanout was created, false if no valid escape found
     */
    bool GeneratePadFanout( PAD* aPad, FANOUT_CONNECTION& aConnection );

    /**
     * Check if a footprint is suitable for fanout routing.
     * Returns true for BGA, QFP, and similar high-pin-count packages.
     */
    bool IsFanoutCandidate( FOOTPRINT* aFootprint ) const;

private:
    /**
     * Classify pads by their position in the package.
     * Returns zones: OUTER (edge), INNER (center area)
     */
    enum class PAD_ZONE { OUTER, INNER };
    PAD_ZONE ClassifyPad( PAD* aPad, const BOX2I& aFootprintBounds ) const;

    /**
     * Determine the best escape direction for a pad based on its position.
     */
    FANOUT_DIRECTION GetBestEscapeDirection( PAD* aPad, const BOX2I& aFootprintBounds ) const;

    /**
     * Calculate via position for direct pattern.
     */
    VECTOR2I CalculateDirectViaPosition( PAD* aPad, FANOUT_DIRECTION aDirection ) const;

    /**
     * Calculate via position for dog-bone pattern.
     */
    VECTOR2I CalculateDogBoneViaPosition( PAD* aPad, FANOUT_DIRECTION aDirection,
                                          VECTOR2I& aMidpoint ) const;

    /**
     * Calculate via position for staggered pattern.
     */
    VECTOR2I CalculateStaggeredViaPosition( PAD* aPad, FANOUT_DIRECTION aDirection,
                                            int aStaggerIndex ) const;

    /**
     * Check if a via at the given position would be valid.
     */
    bool IsValidViaPosition( const VECTOR2I& aPosition, int aNetCode ) const;

    /**
     * Check if a trace from pad to via would be valid.
     */
    bool IsValidTrace( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                       int aLayer, int aWidth, int aNetCode ) const;

    /**
     * Get the direction vector for an escape direction.
     */
    VECTOR2I GetDirectionVector( FANOUT_DIRECTION aDirection ) const;

    BOARD*              m_board;
    SHAPE_SEARCH_TREE*  m_searchTree = nullptr;
    AUTOROUTE_CONTROL   m_control;
    int                 m_escapeLayer = 1;  ///< Default to second layer
    FANOUT_PATTERN      m_pattern = FANOUT_PATTERN::DOG_BONE;
    int                 m_viaSpacing;       ///< Minimum via-to-via spacing
    int                 m_traceWidth;       ///< Trace width for fanout
    int                 m_viaDiameter;      ///< Via pad diameter
    int                 m_clearance;        ///< Clearance from other objects
};


#endif // FANOUT_ROUTER_H
