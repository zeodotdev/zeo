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

#include "expansion_drill.h"
#include "expansion_room.h"


//-----------------------------------------------------------------------------
// EXPANSION_DRILL Implementation
//-----------------------------------------------------------------------------

EXPANSION_DRILL::EXPANSION_DRILL() :
    EXPANDABLE_OBJECT(),
    m_location( 0, 0 ),
    m_firstLayer( 0 ),
    m_lastLayer( 0 )
{
}


EXPANSION_DRILL::EXPANSION_DRILL( const VECTOR2I& aLocation, int aFirstLayer, int aLastLayer ) :
    EXPANDABLE_OBJECT(),
    m_location( aLocation ),
    m_firstLayer( aFirstLayer ),
    m_lastLayer( aLastLayer )
{
    // Initialize occupied array for all layer sections
    m_occupied.resize( GetSectionCount(), false );

    // Initialize rooms per layer
    m_roomsPerLayer.resize( m_lastLayer - m_firstLayer + 1, nullptr );
}


EXPANSION_ROOM* EXPANSION_DRILL::GetRoomForLayer( int aLayer ) const
{
    int index = aLayer - m_firstLayer;

    if( index < 0 || index >= static_cast<int>( m_roomsPerLayer.size() ) )
        return nullptr;

    return m_roomsPerLayer[index];
}


void EXPANSION_DRILL::SetRoomForLayer( int aLayer, EXPANSION_ROOM* aRoom )
{
    int index = aLayer - m_firstLayer;

    if( index < 0 )
        return;

    if( index >= static_cast<int>( m_roomsPerLayer.size() ) )
        m_roomsPerLayer.resize( index + 1, nullptr );

    m_roomsPerLayer[index] = aRoom;
}


void EXPANSION_DRILL::CalculateExpansionRooms( AUTOROUTE_ENGINE& aEngine, int aClearance )
{
    // This will be implemented when we have the AUTOROUTE_ENGINE.
    // The engine will create expansion rooms around the via location
    // for each layer, accounting for obstacles and clearances.

    // For now, we just ensure the rooms vector is properly sized
    m_roomsPerLayer.resize( m_lastLayer - m_firstLayer + 1, nullptr );
}
