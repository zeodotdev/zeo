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

#include "drill_page_array.h"
#include <algorithm>


//-----------------------------------------------------------------------------
// DRILL_PAGE_ARRAY Implementation
//-----------------------------------------------------------------------------

DRILL_PAGE_ARRAY::DRILL_PAGE_ARRAY( const BOX2I& aBoardBounds, int aMaxPageWidth,
                                    int aFirstLayer, int aLastLayer ) :
    m_bounds( aBoardBounds ),
    m_firstLayer( aFirstLayer ),
    m_lastLayer( aLastLayer )
{
    // Calculate grid dimensions
    int boardWidth = m_bounds.GetWidth();
    int boardHeight = m_bounds.GetHeight();

    // Ensure at least one column and row
    m_colCount = std::max( 1, ( boardWidth + aMaxPageWidth - 1 ) / aMaxPageWidth );
    m_rowCount = std::max( 1, ( boardHeight + aMaxPageWidth - 1 ) / aMaxPageWidth );

    // Calculate actual page dimensions (may be smaller than max)
    m_pageWidth = ( boardWidth + m_colCount - 1 ) / m_colCount;
    m_pageHeight = ( boardHeight + m_rowCount - 1 ) / m_rowCount;

    // Ensure minimum page size
    m_pageWidth = std::max( 1, m_pageWidth );
    m_pageHeight = std::max( 1, m_pageHeight );

    // Create the grid of pages
    m_pages.resize( m_rowCount );

    for( int row = 0; row < m_rowCount; ++row )
    {
        m_pages[row].reserve( m_colCount );

        for( int col = 0; col < m_colCount; ++col )
        {
            // Calculate page bounds
            int left = m_bounds.GetLeft() + col * m_pageWidth;
            int top = m_bounds.GetTop() + row * m_pageHeight;
            int right = std::min( left + m_pageWidth, m_bounds.GetRight() );
            int bottom = std::min( top + m_pageHeight, m_bounds.GetBottom() );

            BOX2I pageBounds;
            pageBounds.SetOrigin( left, top );
            pageBounds.SetEnd( right, bottom );

            m_pages[row].push_back(
                std::make_unique<DRILL_PAGE>( pageBounds, m_firstLayer, m_lastLayer ) );
        }
    }
}


std::vector<DRILL_PAGE*> DRILL_PAGE_ARRAY::GetOverlappingPages( const BOX2I& aShape )
{
    std::vector<DRILL_PAGE*> result;

    // Get the range of grid cells that could overlap
    int minCol = GetCol( aShape.GetLeft() );
    int maxCol = GetCol( aShape.GetRight() );
    int minRow = GetRow( aShape.GetTop() );
    int maxRow = GetRow( aShape.GetBottom() );

    // Clamp to valid range
    minCol = std::max( 0, minCol );
    maxCol = std::min( m_colCount - 1, maxCol );
    minRow = std::max( 0, minRow );
    maxRow = std::min( m_rowCount - 1, maxRow );

    // Collect all overlapping pages
    for( int row = minRow; row <= maxRow; ++row )
    {
        for( int col = minCol; col <= maxCol; ++col )
        {
            DRILL_PAGE* page = m_pages[row][col].get();

            if( page && page->GetBounds().Intersects( aShape ) )
            {
                result.push_back( page );
            }
        }
    }

    return result;
}


void DRILL_PAGE_ARRAY::Invalidate( const BOX2I& aShape )
{
    std::vector<DRILL_PAGE*> pages = GetOverlappingPages( aShape );

    for( DRILL_PAGE* page : pages )
    {
        page->ClearDrills();
    }
}


void DRILL_PAGE_ARRAY::Reset()
{
    for( int row = 0; row < m_rowCount; ++row )
    {
        for( int col = 0; col < m_colCount; ++col )
        {
            DRILL_PAGE* page = m_pages[row][col].get();

            if( page )
            {
                page->ClearDrills();
                page->ClearOccupied();
            }
        }
    }
}


DRILL_PAGE* DRILL_PAGE_ARRAY::GetPage( int aCol, int aRow )
{
    if( aRow < 0 || aRow >= m_rowCount || aCol < 0 || aCol >= m_colCount )
        return nullptr;

    return m_pages[aRow][aCol].get();
}


int DRILL_PAGE_ARRAY::GetCol( int aX ) const
{
    if( m_pageWidth <= 0 )
        return 0;

    int offset = aX - m_bounds.GetLeft();
    return offset / m_pageWidth;
}


int DRILL_PAGE_ARRAY::GetRow( int aY ) const
{
    if( m_pageHeight <= 0 )
        return 0;

    int offset = aY - m_bounds.GetTop();
    return offset / m_pageHeight;
}
