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

#ifndef DESTINATION_DISTANCE_H
#define DESTINATION_DISTANCE_H

#include <math/vector2d.h>
#include <math/box2.h>
#include <vector>
#include <set>

// Forward declarations
class BOARD_ITEM;
class EXPANSION_ROOM;


/**
 * Computes the A* heuristic (h(n)) - estimated distance to destination.
 *
 * This class maintains information about the destination targets and
 * computes the minimum estimated cost from any point to the nearest target.
 */
class DESTINATION_DISTANCE
{
public:
    DESTINATION_DISTANCE();

    /**
     * Initialize with a set of destination items.
     */
    void Initialize( const std::set<BOARD_ITEM*>& aDestItems );

    /**
     * Add a target room as a destination.
     */
    void AddTargetRoom( EXPANSION_ROOM* aRoom );

    /**
     * Clear all destinations.
     */
    void Clear();

    /**
     * Calculate the estimated cost from a point to the nearest destination.
     *
     * @param aPt The current point
     * @param aLayer The current layer
     * @param aViaCost Cost multiplier for layer changes
     * @return Estimated distance (heuristic value)
     */
    double Calculate( const VECTOR2I& aPt, int aLayer, double aViaCost = 50.0 ) const;

    /**
     * Calculate just the geometric distance (no layer cost).
     */
    double CalculateDistance( const VECTOR2I& aPt ) const;

    /**
     * Check if a room is a destination.
     */
    bool IsDestination( const EXPANSION_ROOM* aRoom ) const;

    /**
     * Check if a point is inside any destination.
     */
    bool IsAtDestination( const VECTOR2I& aPt, int aLayer ) const;

    /**
     * Get the center of the nearest destination.
     */
    VECTOR2I GetNearestDestCenter( const VECTOR2I& aPt ) const;

    /**
     * Get the set of layers that have destinations.
     */
    const std::set<int>& GetDestinationLayers() const { return m_destLayers; }

private:
    struct DestinationTarget
    {
        VECTOR2I center;
        BOX2I    bbox;
        int      layer;
        EXPANSION_ROOM* room;
    };

    std::vector<DestinationTarget> m_targets;
    std::set<int>                  m_destLayers;
    BOX2I                          m_totalBBox;  // Bounding box of all targets
};


#endif // DESTINATION_DISTANCE_H
