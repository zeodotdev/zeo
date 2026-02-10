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

#include "expansion_room.h"
#include "expansion_door.h"
#include <algorithm>


// Static member initialization
int EXPANSION_ROOM::s_nextId = 0;


//-----------------------------------------------------------------------------
// EXPANSION_ROOM Implementation
//-----------------------------------------------------------------------------

EXPANSION_ROOM::EXPANSION_ROOM( ROOM_TYPE aType, int aLayer ) :
    m_type( aType ),
    m_layer( aLayer ),
    m_id( s_nextId++ )
{
}


void EXPANSION_ROOM::AddDoor( EXPANSION_DOOR* aDoor )
{
    if( std::find( m_doors.begin(), m_doors.end(), aDoor ) == m_doors.end() )
        m_doors.push_back( aDoor );
}


void EXPANSION_ROOM::RemoveDoor( EXPANSION_DOOR* aDoor )
{
    auto it = std::find( m_doors.begin(), m_doors.end(), aDoor );
    if( it != m_doors.end() )
        m_doors.erase( it );
}


//-----------------------------------------------------------------------------
// FREE_SPACE_ROOM Implementation
//-----------------------------------------------------------------------------

FREE_SPACE_ROOM::FREE_SPACE_ROOM( int aLayer ) :
    EXPANSION_ROOM( ROOM_TYPE::FREE_SPACE, aLayer ),
    m_shape( std::make_unique<INT_BOX>() )
{
}


FREE_SPACE_ROOM::FREE_SPACE_ROOM( std::unique_ptr<TILE_SHAPE> aShape, int aLayer ) :
    EXPANSION_ROOM( ROOM_TYPE::FREE_SPACE, aLayer ),
    m_shape( std::move( aShape ) )
{
}


//-----------------------------------------------------------------------------
// OBSTACLE_ROOM Implementation
//-----------------------------------------------------------------------------

OBSTACLE_ROOM::OBSTACLE_ROOM( BOARD_ITEM* aItem, int aLayer ) :
    EXPANSION_ROOM( ROOM_TYPE::OBSTACLE, aLayer ),
    m_shape( std::make_unique<INT_BOX>() ),
    m_item( aItem )
{
}


OBSTACLE_ROOM::OBSTACLE_ROOM( std::unique_ptr<TILE_SHAPE> aShape, BOARD_ITEM* aItem, int aLayer ) :
    EXPANSION_ROOM( ROOM_TYPE::OBSTACLE, aLayer ),
    m_shape( std::move( aShape ) ),
    m_item( aItem )
{
}


//-----------------------------------------------------------------------------
// TARGET_ROOM Implementation
//-----------------------------------------------------------------------------

TARGET_ROOM::TARGET_ROOM( BOARD_ITEM* aItem, int aLayer ) :
    EXPANSION_ROOM( ROOM_TYPE::TARGET, aLayer ),
    m_shape( std::make_unique<INT_BOX>() ),
    m_item( aItem )
{
}


TARGET_ROOM::TARGET_ROOM( std::unique_ptr<TILE_SHAPE> aShape, BOARD_ITEM* aItem, int aLayer ) :
    EXPANSION_ROOM( ROOM_TYPE::TARGET, aLayer ),
    m_shape( std::move( aShape ) ),
    m_item( aItem )
{
}
