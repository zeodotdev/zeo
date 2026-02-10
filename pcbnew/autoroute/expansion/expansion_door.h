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

#ifndef EXPANSION_DOOR_H
#define EXPANSION_DOOR_H

#include <geometry/seg.h>
#include <math/vector2d.h>

// Forward declarations
class EXPANSION_ROOM;


/**
 * Base class for expandable objects in the maze search.
 *
 * Both doors (between rooms) and drills (layer transitions) are expandable.
 */
class EXPANDABLE_OBJECT
{
public:
    virtual ~EXPANDABLE_OBJECT() = default;

    /**
     * Get the center point of this expandable object.
     */
    virtual VECTOR2I GetCenter() const = 0;

    /**
     * Get the primary layer of this object.
     */
    virtual int GetLayer() const = 0;

    /**
     * Get a unique ID.
     */
    int GetId() const { return m_id; }

    /**
     * Check if this object has been occupied (used) in the current search.
     */
    bool IsOccupied( int aSection = 0 ) const;

    /**
     * Mark a section of this object as occupied.
     */
    void SetOccupied( int aSection, bool aOccupied );

    /**
     * Get the number of sections (segments) this object has.
     */
    virtual int GetSectionCount() const { return 1; }

    /**
     * Clear all occupation markers.
     */
    void ClearOccupied();

protected:
    EXPANDABLE_OBJECT();

    int               m_id;
    std::vector<bool> m_occupied;

    static int s_nextId;
};


/**
 * A door represents a connection between two adjacent expansion rooms.
 *
 * Doors are line segments along the shared edge of two rooms. During the
 * A* search, doors are added to the priority queue as candidates for expansion.
 */
class EXPANSION_DOOR : public EXPANDABLE_OBJECT
{
public:
    EXPANSION_DOOR();
    EXPANSION_DOOR( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2, const SEG& aSegment );

    /**
     * Get the line segment representing this door.
     */
    const SEG& GetSegment() const { return m_segment; }

    /**
     * Get the center point of the door segment.
     */
    VECTOR2I GetCenter() const override;

    /**
     * Get the layer this door is on.
     */
    int GetLayer() const override;

    /**
     * Get the first room connected by this door.
     */
    EXPANSION_ROOM* GetRoom1() const { return m_room1; }

    /**
     * Get the second room connected by this door.
     */
    EXPANSION_ROOM* GetRoom2() const { return m_room2; }

    /**
     * Get the other room given one of the rooms.
     */
    EXPANSION_ROOM* GetOtherRoom( const EXPANSION_ROOM* aRoom ) const;

    /**
     * Get the length of this door segment.
     */
    int GetLength() const { return m_segment.Length(); }

    /**
     * Check if a point lies on this door segment.
     */
    bool ContainsPoint( const VECTOR2I& aPt ) const;

    /**
     * Get the number of sections this door has.
     * Long doors may be divided into sections for more precise routing.
     */
    int GetSectionCount() const override;

    /**
     * Get the segment for a specific section.
     */
    SEG GetSectionSegment( int aSection ) const;

    /**
     * Get the midpoint of a section.
     */
    VECTOR2I GetSectionCenter( int aSection ) const;

    /**
     * Get section segments with offset applied (shrunk from edges).
     * This is like FreeRouting's get_section_segments() which shrinks
     * door segments to avoid traces touching the door boundaries.
     *
     * @param aOffset The offset distance (typically half trace width + clearance).
     * @return Vector of shrunken segments, one per section. Empty segments are omitted.
     */
    std::vector<SEG> GetSectionSegmentsWithOffset( int aOffset ) const;

    /**
     * Get the dimension of this door (1 for line segment, 2 for area).
     */
    int GetDimension() const { return m_dimension; }

    /**
     * Set the minimum section length for door subdivision.
     */
    static void SetMinSectionLength( int aLength ) { s_minSectionLength = aLength; }

private:
    EXPANSION_ROOM* m_room1;
    EXPANSION_ROOM* m_room2;
    SEG             m_segment;
    int             m_dimension = 1;  ///< 1 for line segment, 2 for area overlap

    static int s_minSectionLength;
};


#endif // EXPANSION_DOOR_H
