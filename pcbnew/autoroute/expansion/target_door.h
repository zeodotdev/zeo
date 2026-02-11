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

#ifndef TARGET_DOOR_H
#define TARGET_DOOR_H

#include "expansion_door.h"

class BOARD_ITEM;
class FREE_SPACE_ROOM;

/**
 * Special expansion door that represents connection to an own-net item.
 * This can be either a start item (source pad) or a destination item.
 * When maze search reaches a destination door, the path is complete.
 *
 * Equivalent to FreeRouting's TargetItemExpansionDoor.
 *
 * Key design from FreeRouting:
 * - Both start and destination pads get TargetItemExpansionDoor entries
 * - is_destination_door() returns !is_start_info() - distinguishes the two
 * - During init, start item target doors are added to queue
 * - During expansion, destination target doors trigger path found
 */
class TARGET_EXPANSION_DOOR : public EXPANSION_DOOR
{
public:
    /**
     * Create a target door to an own-net item.
     * @param aItem The board item (typically a PAD)
     * @param aRoom The free space room containing this target
     * @param aConnectionShape The shape where trace can connect to item
     * @param aIsStartItem true if this is a start item, false if destination
     */
    TARGET_EXPANSION_DOOR( BOARD_ITEM* aItem, FREE_SPACE_ROOM* aRoom,
                           const SEG& aConnectionShape, bool aIsStartItem = false );

    /// Get the target item
    BOARD_ITEM* GetItem() const { return m_item; }

    /**
     * Check if this is a destination door (not a start door).
     * FreeRouting: returns !item.get_autoroute_info().is_start_info()
     */
    bool IsDestinationDoor() const { return !m_isStartItem; }

    /**
     * Check if this is a start door (not a destination door).
     */
    bool IsStartDoor() const { return m_isStartItem; }

    /// Get connection point (center of connection shape)
    VECTOR2I GetConnectionPoint() const;

private:
    BOARD_ITEM* m_item;
    bool        m_isStartItem;  ///< true if start item, false if destination
};

#endif // TARGET_DOOR_H
