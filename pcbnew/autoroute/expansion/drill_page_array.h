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

#ifndef DRILL_PAGE_ARRAY_H
#define DRILL_PAGE_ARRAY_H

#include "drill_page.h"
#include <math/box2.h>
#include <memory>
#include <vector>


/**
 * A 2D grid of DrillPages covering the board area.
 *
 * DrillPageArray divides the board into a grid of rectangular pages.
 * Each page can lazily calculate via locations when expanded during
 * the maze search, preventing queue explosion.
 *
 * Based on FreeRouting's DrillPageArray.
 */
class DRILL_PAGE_ARRAY
{
public:
    /**
     * Construct a drill page array covering the board.
     *
     * @param aBoardBounds The bounding box of the board area.
     * @param aMaxPageWidth Maximum width/height for each page.
     * @param aFirstLayer First (top-most) layer for drills.
     * @param aLastLayer Last (bottom-most) layer for drills.
     */
    DRILL_PAGE_ARRAY( const BOX2I& aBoardBounds, int aMaxPageWidth,
                      int aFirstLayer, int aLastLayer );

    /**
     * Get all pages that overlap with a given shape.
     *
     * @param aShape The bounding box to check for overlap.
     * @return Vector of pointers to overlapping pages.
     */
    std::vector<DRILL_PAGE*> GetOverlappingPages( const BOX2I& aShape );

    /**
     * Invalidate all pages that intersect with a shape.
     * This should be called after board changes that affect via locations.
     *
     * @param aShape The bounding box of the changed area.
     */
    void Invalidate( const BOX2I& aShape );

    /**
     * Reset all pages for a new connection search.
     * Clears calculated drills and occupation markers.
     */
    void Reset();

    /**
     * Get the page at a specific grid position.
     *
     * @param aCol Column index (0-based).
     * @param aRow Row index (0-based).
     * @return Pointer to the page, or nullptr if out of bounds.
     */
    DRILL_PAGE* GetPage( int aCol, int aRow );

    /**
     * Get the number of columns in the grid.
     */
    int GetColCount() const { return m_colCount; }

    /**
     * Get the number of rows in the grid.
     */
    int GetRowCount() const { return m_rowCount; }

    /**
     * Get the bounding box of the entire array.
     */
    const BOX2I& GetBounds() const { return m_bounds; }

private:
    /**
     * Convert an X coordinate to a column index.
     */
    int GetCol( int aX ) const;

    /**
     * Convert a Y coordinate to a row index.
     */
    int GetRow( int aY ) const;

    std::vector<std::vector<std::unique_ptr<DRILL_PAGE>>> m_pages;  // [row][col]
    BOX2I m_bounds;
    int   m_pageWidth;
    int   m_pageHeight;
    int   m_colCount;
    int   m_rowCount;
    int   m_firstLayer;
    int   m_lastLayer;
};


#endif // DRILL_PAGE_ARRAY_H
