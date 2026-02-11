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

#ifndef TILE_SHAPE_H
#define TILE_SHAPE_H

#include <math/vector2d.h>
#include <math/box2.h>
#include <geometry/seg.h>
#include <optional>
#include <vector>
#include <memory>


/**
 * Abstract base class for convex tile shapes used in expansion room routing.
 *
 * The autorouter uses convex shapes to represent free space regions (expansion rooms).
 * Convex shapes simplify many geometric operations needed for the A* maze search.
 */
class TILE_SHAPE
{
public:
    virtual ~TILE_SHAPE() = default;

    /**
     * Get the axis-aligned bounding box of this shape.
     */
    virtual BOX2I BoundingBox() const = 0;

    /**
     * Check if a point is contained within this shape.
     */
    virtual bool Contains( const VECTOR2I& aPt ) const = 0;

    /**
     * Check if this shape intersects with another shape.
     */
    virtual bool Intersects( const TILE_SHAPE& aOther ) const = 0;

    /**
     * Compute the intersection of this shape with another.
     * Returns nullopt if shapes don't intersect.
     */
    virtual std::unique_ptr<TILE_SHAPE> Intersection( const TILE_SHAPE& aOther ) const = 0;

    /**
     * Get the center point of this shape.
     */
    virtual VECTOR2I Center() const = 0;

    /**
     * Get the edges of this shape as line segments.
     */
    virtual std::vector<SEG> GetEdges() const = 0;

    /**
     * Compute distance from a point to the nearest edge of this shape.
     */
    virtual int DistanceToEdge( const VECTOR2I& aPt ) const = 0;

    /**
     * Find the closest point on the shape boundary to a given point.
     */
    virtual VECTOR2I NearestPointOnBoundary( const VECTOR2I& aPt ) const = 0;

    /**
     * Create a copy of this shape.
     */
    virtual std::unique_ptr<TILE_SHAPE> Clone() const = 0;

    /**
     * Expand or shrink this shape by a given amount.
     * Positive values expand, negative shrink.
     */
    virtual void Inflate( int aAmount ) = 0;
};


/**
 * Axis-aligned integer box used as the primary tile shape.
 *
 * This is the most common shape for expansion rooms in the autorouter.
 * It's an axis-aligned rectangle with integer coordinates in nanometers.
 */
class INT_BOX : public TILE_SHAPE
{
public:
    INT_BOX();

    /**
     * Create a box from min and max corners.
     */
    INT_BOX( const VECTOR2I& aMin, const VECTOR2I& aMax );

    /**
     * Create a box from a BOX2I.
     */
    explicit INT_BOX( const BOX2I& aBox );

    /**
     * Create a box from position, width and height.
     */
    INT_BOX( const VECTOR2I& aPos, int aWidth, int aHeight );

    // TILE_SHAPE interface
    BOX2I BoundingBox() const override;
    bool Contains( const VECTOR2I& aPt ) const override;
    bool Intersects( const TILE_SHAPE& aOther ) const override;
    std::unique_ptr<TILE_SHAPE> Intersection( const TILE_SHAPE& aOther ) const override;
    VECTOR2I Center() const override;
    std::vector<SEG> GetEdges() const override;
    int DistanceToEdge( const VECTOR2I& aPt ) const override;
    VECTOR2I NearestPointOnBoundary( const VECTOR2I& aPt ) const override;
    std::unique_ptr<TILE_SHAPE> Clone() const override;
    void Inflate( int aAmount ) override;

    // INT_BOX specific methods

    const VECTOR2I& Min() const { return m_min; }
    const VECTOR2I& Max() const { return m_max; }

    int Left() const { return m_min.x; }
    int Right() const { return m_max.x; }
    int Top() const { return m_min.y; }
    int Bottom() const { return m_max.y; }

    int Width() const { return m_max.x - m_min.x; }
    int Height() const { return m_max.y - m_min.y; }

    bool IsEmpty() const { return m_min.x >= m_max.x || m_min.y >= m_max.y; }

    /**
     * Check if this box intersects with another INT_BOX.
     * Boxes that only touch at an edge are considered intersecting.
     */
    bool IntersectsBox( const INT_BOX& aOther ) const;

    /**
     * Check if this box truly overlaps with another INT_BOX.
     * Boxes that only touch at an edge are NOT considered overlapping.
     * Use this when you need to know if boxes share interior area.
     */
    bool OverlapsBox( const INT_BOX& aOther ) const;

    /**
     * Compute intersection with another INT_BOX.
     */
    std::optional<INT_BOX> IntersectionBox( const INT_BOX& aOther ) const;

    /**
     * Get the edge segment at a given index (0-3 for top, right, bottom, left).
     */
    SEG GetEdge( int aIndex ) const;

    /**
     * Compute the segment where this box and another box touch.
     * Returns nullopt if boxes don't touch.
     */
    std::optional<SEG> TouchingSegment( const INT_BOX& aOther ) const;

private:
    VECTOR2I m_min;  ///< Top-left corner (minimum coordinates)
    VECTOR2I m_max;  ///< Bottom-right corner (maximum coordinates)
};


/**
 * Convex polygon tile shape.
 *
 * Used for non-rectangular expansion rooms (less common).
 */
class CONVEX_POLY_SHAPE : public TILE_SHAPE
{
public:
    CONVEX_POLY_SHAPE();
    explicit CONVEX_POLY_SHAPE( const std::vector<VECTOR2I>& aVertices );

    // TILE_SHAPE interface
    BOX2I BoundingBox() const override;
    bool Contains( const VECTOR2I& aPt ) const override;
    bool Intersects( const TILE_SHAPE& aOther ) const override;
    std::unique_ptr<TILE_SHAPE> Intersection( const TILE_SHAPE& aOther ) const override;
    VECTOR2I Center() const override;
    std::vector<SEG> GetEdges() const override;
    int DistanceToEdge( const VECTOR2I& aPt ) const override;
    VECTOR2I NearestPointOnBoundary( const VECTOR2I& aPt ) const override;
    std::unique_ptr<TILE_SHAPE> Clone() const override;
    void Inflate( int aAmount ) override;

    const std::vector<VECTOR2I>& Vertices() const { return m_vertices; }

private:
    std::vector<VECTOR2I> m_vertices;  ///< Vertices in counter-clockwise order
    mutable BOX2I         m_bbox;      ///< Cached bounding box
    mutable bool          m_bboxValid = false;

    void updateBBox() const;
};


/**
 * Represents a half-plane defined by a line.
 * Points on the left side of the line (looking in direction) are inside.
 * This matches FreeRouting's Line class.
 */
class HALF_PLANE
{
public:
    HALF_PLANE() = default;

    /**
     * Create half-plane from a line through two points.
     * The half-plane includes points on the left of the line from A to B.
     */
    HALF_PLANE( const VECTOR2I& aPointA, const VECTOR2I& aPointB );

    /**
     * Create half-plane from a line segment (uses segment endpoints).
     */
    explicit HALF_PLANE( const SEG& aSeg );

    /**
     * Check if a point is inside this half-plane (on the left side of the line).
     */
    bool Contains( const VECTOR2I& aPt ) const;

    /**
     * Get which side of the line a point is on.
     * Returns positive for left (inside), negative for right (outside), 0 for on line.
     */
    int64_t SideOf( const VECTOR2I& aPt ) const;

    /**
     * Check if two half-planes are parallel.
     */
    bool IsParallel( const HALF_PLANE& aOther ) const;

    /**
     * Compute intersection point with another half-plane line.
     */
    std::optional<VECTOR2I> IntersectionPoint( const HALF_PLANE& aOther ) const;

    /**
     * Get the direction vector of this line.
     */
    VECTOR2I Direction() const { return m_dir; }

    /**
     * Get a point on this line.
     */
    VECTOR2I Point() const { return m_point; }

private:
    VECTOR2I m_point;  ///< A point on the line
    VECTOR2I m_dir;    ///< Direction vector of the line
};


/**
 * A convex shape defined by the intersection of half-planes.
 * This matches FreeRouting's Simplex class.
 *
 * The simplex is the intersection of all half-planes, which is always convex.
 * This allows efficient construction of shapes from arbitrary line constraints.
 */
class SIMPLEX : public TILE_SHAPE
{
public:
    SIMPLEX();

    /**
     * Create simplex from an array of half-planes (lines).
     * The simplex is the intersection of all half-plane interiors.
     */
    explicit SIMPLEX( const std::vector<HALF_PLANE>& aHalfPlanes );

    /**
     * Create simplex from a bounding box (4 half-planes).
     */
    static SIMPLEX FromBox( const BOX2I& aBox );

    /**
     * Create simplex from a bounding box extended to infinity in one direction.
     * Used for creating rooms that extend to board edge.
     */
    static SIMPLEX FromHalfSpace( const SEG& aBoundary, const BOX2I& aBoardBounds );

    // TILE_SHAPE interface
    BOX2I BoundingBox() const override;
    bool Contains( const VECTOR2I& aPt ) const override;
    bool Intersects( const TILE_SHAPE& aOther ) const override;
    std::unique_ptr<TILE_SHAPE> Intersection( const TILE_SHAPE& aOther ) const override;
    VECTOR2I Center() const override;
    std::vector<SEG> GetEdges() const override;
    int DistanceToEdge( const VECTOR2I& aPt ) const override;
    VECTOR2I NearestPointOnBoundary( const VECTOR2I& aPt ) const override;
    std::unique_ptr<TILE_SHAPE> Clone() const override;
    void Inflate( int aAmount ) override;

    /**
     * Check if the simplex is empty (half-planes don't intersect).
     */
    bool IsEmpty() const { return m_vertices.empty(); }

    /**
     * Get the half-planes defining this simplex.
     */
    const std::vector<HALF_PLANE>& HalfPlanes() const { return m_halfPlanes; }

    /**
     * Get the number of edges (border lines).
     */
    int EdgeCount() const { return m_halfPlanes.size(); }

    /**
     * Remove a border line (enlarges the simplex).
     * Returns a new simplex without the specified edge.
     */
    SIMPLEX RemoveBorderLine( int aEdgeIndex ) const;

    /**
     * Compute intersection with another simplex.
     */
    SIMPLEX IntersectionSimplex( const SIMPLEX& aOther ) const;

    /**
     * Get the dimension of intersection with another shape.
     * 0 = point, 1 = edge, 2 = area, -1 = empty
     */
    int IntersectionDimension( const TILE_SHAPE& aOther ) const;

    /**
     * Get corner point at specified index.
     */
    VECTOR2I Corner( int aIndex ) const;

    /**
     * Get index of next edge in counterclockwise order.
     */
    int NextEdge( int aIndex ) const { return ( aIndex + 1 ) % EdgeCount(); }

    /**
     * Get index of previous edge in counterclockwise order.
     */
    int PrevEdge( int aIndex ) const { return ( aIndex + EdgeCount() - 1 ) % EdgeCount(); }

private:
    std::vector<HALF_PLANE> m_halfPlanes;  ///< Half-planes defining this simplex
    std::vector<VECTOR2I>   m_vertices;    ///< Computed vertices (corners)

    void computeVertices();
};


#endif // TILE_SHAPE_H
