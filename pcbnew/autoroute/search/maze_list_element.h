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

#ifndef MAZE_LIST_ELEMENT_H
#define MAZE_LIST_ELEMENT_H

#include <geometry/seg.h>
#include <math/vector2d.h>

// Forward declarations
class EXPANDABLE_OBJECT;
class EXPANSION_ROOM;


/**
 * Element in the A* search priority queue.
 *
 * Each element represents a candidate expansion through a door or drill.
 * The priority queue is ordered by sorting_value (f(n) = g(n) + h(n)).
 */
struct MAZE_LIST_ELEMENT
{
    /// The door or drill being expanded through
    EXPANDABLE_OBJECT* door = nullptr;

    /// Section number within the door (for subdivided doors)
    int section_no = 0;

    /// Previous door in the backtrack path
    EXPANDABLE_OBJECT* backtrack_door = nullptr;

    /// Section number of the backtrack door
    int backtrack_section = 0;

    /// g(n) - Cost from start to this element
    double expansion_value = 0.0;

    /// f(n) = g(n) + h(n) - Total estimated cost
    double sorting_value = 0.0;

    /// The room we're expanding into
    EXPANSION_ROOM* next_room = nullptr;

    /// Entry point segment on this door
    SEG shape_entry;

    /// The actual entry point used
    VECTOR2I entry_point;

    /// Layer we're on at this point
    int layer = 0;

    /// Whether this expansion involved a via
    bool via_placed = false;

    /// Whether the room was ripped up to make this path
    bool room_ripped = false;

    /**
     * Comparison for priority queue (min-heap based on sorting_value).
     */
    bool operator>( const MAZE_LIST_ELEMENT& aOther ) const
    {
        return sorting_value > aOther.sorting_value;
    }

    bool operator<( const MAZE_LIST_ELEMENT& aOther ) const
    {
        return sorting_value < aOther.sorting_value;
    }
};


#endif // MAZE_LIST_ELEMENT_H
