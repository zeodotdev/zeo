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

#include "item_autoroute_info.h"
#include "expansion_room.h"
#include "../geometry/tile_shape.h"
#include <board_connected_item.h>


ITEM_AUTOROUTE_INFO::ITEM_AUTOROUTE_INFO( BOARD_ITEM* aItem ) :
    m_item( aItem )
{
}


ITEM_AUTOROUTE_INFO::~ITEM_AUTOROUTE_INFO()
{
}


OBSTACLE_ROOM* ITEM_AUTOROUTE_INFO::GetExpansionRoom( int aLayer, int aClearance )
{
    auto it = m_expansionRooms.find( aLayer );

    if( it != m_expansionRooms.end() )
        return it->second.get();

    // Create a new obstacle room for this layer
    auto room = std::make_unique<OBSTACLE_ROOM>( m_item, aLayer );

    // Set shape with clearance applied
    BOX2I itemBox = m_item->GetBoundingBox();
    itemBox.Inflate( aClearance );

    auto shape = std::make_unique<INT_BOX>(
        itemBox.GetOrigin(),
        itemBox.GetOrigin() + VECTOR2I( itemBox.GetWidth(), itemBox.GetHeight() ) );
    room->SetShape( std::move( shape ) );

    // Set net code from item if it's a connected item
    if( m_item->IsConnected() )
    {
        const BOARD_CONNECTED_ITEM* connectedItem =
            static_cast<const BOARD_CONNECTED_ITEM*>( m_item );
        room->SetNetCode( connectedItem->GetNetCode() );
    }

    OBSTACLE_ROOM* roomPtr = room.get();
    m_expansionRooms[aLayer] = std::move( room );

    return roomPtr;
}


void ITEM_AUTOROUTE_INFO::ResetDoors()
{
    for( auto& [layer, room] : m_expansionRooms )
    {
        if( room )
            room->ClearDoors();
    }
}


bool ITEM_AUTOROUTE_INFO::IsTraceObstacle( int aNetCode ) const
{
    // Check if the item is a connected item with a net code
    const BOARD_CONNECTED_ITEM* connectedItem = dynamic_cast<const BOARD_CONNECTED_ITEM*>( m_item );

    if( connectedItem )
    {
        // Same net items don't block each other
        if( connectedItem->GetNetCode() == aNetCode )
            return false;
    }

    // Different net or non-connected item - it's an obstacle
    return true;
}
