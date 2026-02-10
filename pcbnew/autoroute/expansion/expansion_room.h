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

#ifndef EXPANSION_ROOM_H
#define EXPANSION_ROOM_H

#include "../geometry/tile_shape.h"
#include <memory>
#include <vector>

// Forward declarations
class EXPANSION_DOOR;
class BOARD_ITEM;


/**
 * Type of expansion room.
 */
enum class ROOM_TYPE
{
    FREE_SPACE,    ///< Routable free space
    OBSTACLE,      ///< Non-routable obstacle (pad, via, existing trace)
    TARGET         ///< Destination target (pin/pad to connect to)
};


/**
 * Base class for expansion rooms in the autorouter.
 *
 * An expansion room represents a region of the board that can be navigated
 * during the A* maze search. Rooms are connected by doors that represent
 * possible transition points.
 */
class EXPANSION_ROOM
{
public:
    EXPANSION_ROOM( ROOM_TYPE aType, int aLayer );
    virtual ~EXPANSION_ROOM() = default;

    /**
     * Get the type of this room.
     */
    ROOM_TYPE GetType() const { return m_type; }

    /**
     * Get the layer this room is on.
     */
    int GetLayer() const { return m_layer; }

    /**
     * Get the shape of this room.
     */
    virtual const TILE_SHAPE& GetShape() const = 0;

    /**
     * Get the center point of this room.
     */
    VECTOR2I GetCenter() const { return GetShape().Center(); }

    /**
     * Get the bounding box of this room.
     */
    BOX2I GetBoundingBox() const { return GetShape().BoundingBox(); }

    /**
     * Get all doors connecting to/from this room.
     */
    const std::vector<EXPANSION_DOOR*>& GetDoors() const { return m_doors; }

    /**
     * Add a door to this room.
     */
    void AddDoor( EXPANSION_DOOR* aDoor );

    /**
     * Remove a door from this room.
     */
    void RemoveDoor( EXPANSION_DOOR* aDoor );

    /**
     * Clear all doors.
     */
    void ClearDoors() { m_doors.clear(); }

    /**
     * Check if a point is contained in this room.
     */
    bool Contains( const VECTOR2I& aPt ) const { return GetShape().Contains( aPt ); }

    /**
     * Get a unique ID for this room.
     */
    int GetId() const { return m_id; }

    /**
     * Check if this room has been visited during the current search.
     */
    bool IsVisited() const { return m_visited; }

    /**
     * Mark this room as visited.
     */
    void SetVisited( bool aVisited ) { m_visited = aVisited; }

    /**
     * Get the net code this room belongs to (for obstacle rooms).
     */
    int GetNetCode() const { return m_netCode; }

    /**
     * Set the net code.
     */
    void SetNetCode( int aNetCode ) { m_netCode = aNetCode; }

protected:
    ROOM_TYPE                     m_type;
    int                           m_layer;
    int                           m_id;
    int                           m_netCode = 0;
    bool                          m_visited = false;
    std::vector<EXPANSION_DOOR*>  m_doors;

    static int s_nextId;
};


/**
 * Free space expansion room - represents routable area.
 */
class FREE_SPACE_ROOM : public EXPANSION_ROOM
{
public:
    FREE_SPACE_ROOM( int aLayer );
    FREE_SPACE_ROOM( std::unique_ptr<TILE_SHAPE> aShape, int aLayer );

    const TILE_SHAPE& GetShape() const override { return *m_shape; }

    /**
     * Set the shape of this room.
     */
    void SetShape( std::unique_ptr<TILE_SHAPE> aShape ) { m_shape = std::move( aShape ); }

    /**
     * Get the shape for modification.
     */
    TILE_SHAPE& GetMutableShape() { return *m_shape; }

private:
    std::unique_ptr<TILE_SHAPE> m_shape;
};


/**
 * Obstacle room - represents a non-routable object on the board.
 */
class OBSTACLE_ROOM : public EXPANSION_ROOM
{
public:
    OBSTACLE_ROOM( BOARD_ITEM* aItem, int aLayer );
    OBSTACLE_ROOM( std::unique_ptr<TILE_SHAPE> aShape, BOARD_ITEM* aItem, int aLayer );

    const TILE_SHAPE& GetShape() const override { return *m_shape; }

    /**
     * Get the board item this obstacle represents.
     */
    BOARD_ITEM* GetItem() const { return m_item; }

    /**
     * Set the shape (with clearance).
     */
    void SetShape( std::unique_ptr<TILE_SHAPE> aShape ) { m_shape = std::move( aShape ); }

private:
    std::unique_ptr<TILE_SHAPE> m_shape;
    BOARD_ITEM*                 m_item;
};


/**
 * Target room - represents a destination pin/pad.
 */
class TARGET_ROOM : public EXPANSION_ROOM
{
public:
    TARGET_ROOM( BOARD_ITEM* aItem, int aLayer );
    TARGET_ROOM( std::unique_ptr<TILE_SHAPE> aShape, BOARD_ITEM* aItem, int aLayer );

    const TILE_SHAPE& GetShape() const override { return *m_shape; }

    /**
     * Get the target board item (pad).
     */
    BOARD_ITEM* GetItem() const { return m_item; }

    /**
     * Set the shape.
     */
    void SetShape( std::unique_ptr<TILE_SHAPE> aShape ) { m_shape = std::move( aShape ); }

private:
    std::unique_ptr<TILE_SHAPE> m_shape;
    BOARD_ITEM*                 m_item;
};


#endif // EXPANSION_ROOM_H
