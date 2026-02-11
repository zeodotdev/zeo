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

#include "drill_page.h"
#include "expansion_drill.h"


//-----------------------------------------------------------------------------
// DRILL_PAGE Implementation
//-----------------------------------------------------------------------------

DRILL_PAGE::DRILL_PAGE( const BOX2I& aBounds, int aFirstLayer, int aLastLayer ) :
    EXPANDABLE_OBJECT(),
    m_bounds( aBounds ),
    m_firstLayer( aFirstLayer ),
    m_lastLayer( aLastLayer ),
    m_calculated( false )
{
    // Initialize occupied array for single section
    m_occupied.resize( GetSectionCount(), false );
}


VECTOR2I DRILL_PAGE::GetCenter() const
{
    return m_bounds.Centre();
}


void DRILL_PAGE::AddDrill( EXPANSION_DRILL* aDrill )
{
    if( aDrill )
    {
        m_drills.push_back( aDrill );
    }
}


void DRILL_PAGE::ClearDrills()
{
    m_drills.clear();
    m_calculated = false;
}
