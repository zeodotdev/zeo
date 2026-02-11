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

#ifndef DRILL_PAGE_H
#define DRILL_PAGE_H

#include "expansion_door.h"
#include <math/box2.h>
#include <vector>

// Forward declarations
class EXPANSION_DRILL;


/**
 * A drill page represents a rectangular region that can contain multiple drills.
 *
 * This is an EXPANDABLE_OBJECT so it can be added to the maze search queue.
 * DrillPages are used to prevent queue explosion during via expansion by
 * lazily calculating drills only when the page is expanded from the queue.
 *
 * Based on FreeRouting's DrillPage abstraction.
 */
class DRILL_PAGE : public EXPANDABLE_OBJECT
{
public:
    DRILL_PAGE( const BOX2I& aBounds, int aFirstLayer, int aLastLayer );

    /**
     * Get the center point of this drill page.
     */
    VECTOR2I GetCenter() const override;

    /**
     * Get the layer of this object.
     * Returns -1 to indicate this is a multi-layer object.
     */
    int GetLayer() const override { return -1; }

    /**
     * Get the number of sections this object has.
     */
    int GetSectionCount() const override { return 1; }

    /**
     * Get the bounding box of this page.
     */
    const BOX2I& GetBounds() const { return m_bounds; }

    /**
     * Get the first (top-most) layer this page covers.
     */
    int GetFirstLayer() const { return m_firstLayer; }

    /**
     * Get the last (bottom-most) layer this page covers.
     */
    int GetLastLayer() const { return m_lastLayer; }

    /**
     * Add a drill to this page.
     * Drills are calculated lazily when the page is expanded.
     */
    void AddDrill( EXPANSION_DRILL* aDrill );

    /**
     * Get all drills in this page.
     */
    const std::vector<EXPANSION_DRILL*>& GetDrills() const { return m_drills; }

    /**
     * Clear all drills from this page.
     */
    void ClearDrills();

    /**
     * Check if drills have been calculated for this page.
     */
    bool IsCalculated() const { return m_calculated; }

    /**
     * Set whether drills have been calculated.
     */
    void SetCalculated( bool aCalc ) { m_calculated = aCalc; }

private:
    BOX2I                          m_bounds;
    int                            m_firstLayer;
    int                            m_lastLayer;
    bool                           m_calculated = false;
    std::vector<EXPANSION_DRILL*>  m_drills;
};


#endif // DRILL_PAGE_H
