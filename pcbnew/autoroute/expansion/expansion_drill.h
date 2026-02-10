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

#ifndef EXPANSION_DRILL_H
#define EXPANSION_DRILL_H

#include "expansion_door.h"
#include <vector>

// Forward declarations
class EXPANSION_ROOM;
class AUTOROUTE_ENGINE;


/**
 * An expansion drill represents a potential via location for layer transitions.
 *
 * During the A* search, drills allow the router to switch between layers.
 * Each drill has a location and a range of layers it can connect.
 */
class EXPANSION_DRILL : public EXPANDABLE_OBJECT
{
public:
    EXPANSION_DRILL();
    EXPANSION_DRILL( const VECTOR2I& aLocation, int aFirstLayer, int aLastLayer );

    /**
     * Get the location of this drill.
     */
    const VECTOR2I& GetLocation() const { return m_location; }

    /**
     * Get the center (same as location for drills).
     */
    VECTOR2I GetCenter() const override { return m_location; }

    /**
     * Get the first (top-most) layer this drill can connect.
     */
    int GetFirstLayer() const { return m_firstLayer; }

    /**
     * Get the last (bottom-most) layer this drill can connect.
     */
    int GetLastLayer() const { return m_lastLayer; }

    /**
     * Get the "layer" for this drill (returns first layer).
     */
    int GetLayer() const override { return m_firstLayer; }

    /**
     * Get the number of sections (layers) this drill spans.
     */
    int GetSectionCount() const override { return m_lastLayer - m_firstLayer + 1; }

    /**
     * Check if this drill can connect to a specific layer.
     */
    bool CanReachLayer( int aLayer ) const
    {
        return aLayer >= m_firstLayer && aLayer <= m_lastLayer;
    }

    /**
     * Get the expansion room for a specific layer.
     * The room represents the area around the via on that layer.
     */
    EXPANSION_ROOM* GetRoomForLayer( int aLayer ) const;

    /**
     * Set the expansion room for a layer.
     */
    void SetRoomForLayer( int aLayer, EXPANSION_ROOM* aRoom );

    /**
     * Calculate expansion rooms for all layers.
     * This should be called after the drill is positioned.
     */
    void CalculateExpansionRooms( AUTOROUTE_ENGINE& aEngine, int aClearance );

    /**
     * Get the via diameter for this drill.
     */
    int GetViaDiameter() const { return m_viaDiameter; }

    /**
     * Set the via diameter.
     */
    void SetViaDiameter( int aDiameter ) { m_viaDiameter = aDiameter; }

    /**
     * Get the drill diameter.
     */
    int GetDrillDiameter() const { return m_drillDiameter; }

    /**
     * Set the drill diameter.
     */
    void SetDrillDiameter( int aDiameter ) { m_drillDiameter = aDiameter; }

private:
    VECTOR2I                      m_location;
    int                           m_firstLayer;
    int                           m_lastLayer;
    int                           m_viaDiameter = 800000;   // 0.8mm
    int                           m_drillDiameter = 400000; // 0.4mm
    std::vector<EXPANSION_ROOM*>  m_roomsPerLayer;
};


#endif // EXPANSION_DRILL_H
