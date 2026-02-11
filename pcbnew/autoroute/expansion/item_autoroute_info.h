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

#ifndef ITEM_AUTOROUTE_INFO_H
#define ITEM_AUTOROUTE_INFO_H

#include <map>
#include <memory>

// Forward declarations
class BOARD_ITEM;
class OBSTACLE_ROOM;


/**
 * Provides lazy creation of OBSTACLE_ROOM for board items.
 *
 * This class follows FreeRouting's ItemAutorouteInfo pattern, caching
 * expansion rooms for board items on each layer. The rooms are created
 * on-demand with the appropriate clearance and cached for reuse.
 */
class ITEM_AUTOROUTE_INFO
{
public:
    ITEM_AUTOROUTE_INFO( BOARD_ITEM* aItem );
    ~ITEM_AUTOROUTE_INFO();

    /**
     * Get or create obstacle room for this item's shape on given layer.
     *
     * @param aLayer The layer to get the room for.
     * @param aClearance The clearance to apply to the room shape.
     * @return Cached room if already created, otherwise creates and caches a new room.
     */
    OBSTACLE_ROOM* GetExpansionRoom( int aLayer, int aClearance );

    /**
     * Reset doors on all expansion rooms.
     *
     * Called when starting a new connection to clear previous routing state.
     */
    void ResetDoors();

    /**
     * Get the underlying board item.
     */
    BOARD_ITEM* GetItem() const { return m_item; }

    /**
     * Check if this item is a trace obstacle for a given net.
     *
     * @param aNetCode The net code to check against.
     * @return false if item belongs to the same net (not an obstacle), true otherwise.
     */
    bool IsTraceObstacle( int aNetCode ) const;

    /**
     * Check if this item was marked as a start item for the current route.
     * Used to distinguish start items from destination items when creating
     * TARGET_EXPANSION_DOORs.
     *
     * FreeRouting equivalent: ItemAutorouteInfo.is_start_info()
     */
    bool IsStartInfo() const { return m_isStartInfo; }

    /**
     * Mark this item as a start or destination item.
     * true = start item, false = destination item
     *
     * FreeRouting equivalent: ItemAutorouteInfo.set_start_info()
     */
    void SetStartInfo( bool aIsStart ) { m_isStartInfo = aIsStart; }

private:
    BOARD_ITEM*                                  m_item;
    std::map<int, std::unique_ptr<OBSTACLE_ROOM>> m_expansionRooms;  ///< layer -> room
    bool                                         m_isStartInfo = false;  ///< true if start, false if dest
};


#endif // ITEM_AUTOROUTE_INFO_H
