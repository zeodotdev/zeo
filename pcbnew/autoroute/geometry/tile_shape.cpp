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

#include "tile_shape.h"
#include <algorithm>
#include <cmath>
#include <limits>


//-----------------------------------------------------------------------------
// INT_BOX Implementation
//-----------------------------------------------------------------------------

INT_BOX::INT_BOX() :
    m_min( 0, 0 ),
    m_max( 0, 0 )
{
}


INT_BOX::INT_BOX( const VECTOR2I& aMin, const VECTOR2I& aMax ) :
    m_min( std::min( aMin.x, aMax.x ), std::min( aMin.y, aMax.y ) ),
    m_max( std::max( aMin.x, aMax.x ), std::max( aMin.y, aMax.y ) )
{
}


INT_BOX::INT_BOX( const BOX2I& aBox )
{
    m_min = aBox.GetPosition();
    m_max = m_min + VECTOR2I( aBox.GetWidth(), aBox.GetHeight() );

    // Normalize
    if( m_min.x > m_max.x )
        std::swap( m_min.x, m_max.x );
    if( m_min.y > m_max.y )
        std::swap( m_min.y, m_max.y );
}


INT_BOX::INT_BOX( const VECTOR2I& aPos, int aWidth, int aHeight ) :
    m_min( aPos ),
    m_max( aPos.x + aWidth, aPos.y + aHeight )
{
    // Normalize
    if( m_min.x > m_max.x )
        std::swap( m_min.x, m_max.x );
    if( m_min.y > m_max.y )
        std::swap( m_min.y, m_max.y );
}


BOX2I INT_BOX::BoundingBox() const
{
    return BOX2I( m_min, VECTOR2L( m_max.x - m_min.x, m_max.y - m_min.y ) );
}


bool INT_BOX::Contains( const VECTOR2I& aPt ) const
{
    return aPt.x >= m_min.x && aPt.x <= m_max.x &&
           aPt.y >= m_min.y && aPt.y <= m_max.y;
}


bool INT_BOX::Intersects( const TILE_SHAPE& aOther ) const
{
    // Check if bounding boxes intersect first
    BOX2I otherBox = aOther.BoundingBox();
    BOX2I thisBox = BoundingBox();

    return thisBox.Intersects( otherBox );
}


bool INT_BOX::IntersectsBox( const INT_BOX& aOther ) const
{
    return !( m_max.x < aOther.m_min.x || aOther.m_max.x < m_min.x ||
              m_max.y < aOther.m_min.y || aOther.m_max.y < m_min.y );
}


std::unique_ptr<TILE_SHAPE> INT_BOX::Intersection( const TILE_SHAPE& aOther ) const
{
    // For now, only handle INT_BOX to INT_BOX intersection
    const INT_BOX* otherBox = dynamic_cast<const INT_BOX*>( &aOther );
    if( otherBox )
    {
        auto result = IntersectionBox( *otherBox );
        if( result )
            return std::make_unique<INT_BOX>( *result );
    }

    return nullptr;
}


std::optional<INT_BOX> INT_BOX::IntersectionBox( const INT_BOX& aOther ) const
{
    int left = std::max( m_min.x, aOther.m_min.x );
    int right = std::min( m_max.x, aOther.m_max.x );
    int top = std::max( m_min.y, aOther.m_min.y );
    int bottom = std::min( m_max.y, aOther.m_max.y );

    if( left < right && top < bottom )
        return INT_BOX( VECTOR2I( left, top ), VECTOR2I( right, bottom ) );

    return std::nullopt;
}


VECTOR2I INT_BOX::Center() const
{
    return VECTOR2I( ( m_min.x + m_max.x ) / 2, ( m_min.y + m_max.y ) / 2 );
}


std::vector<SEG> INT_BOX::GetEdges() const
{
    std::vector<SEG> edges;
    edges.reserve( 4 );

    VECTOR2I tl = m_min;
    VECTOR2I tr( m_max.x, m_min.y );
    VECTOR2I br = m_max;
    VECTOR2I bl( m_min.x, m_max.y );

    edges.emplace_back( tl, tr );  // Top
    edges.emplace_back( tr, br );  // Right
    edges.emplace_back( br, bl );  // Bottom
    edges.emplace_back( bl, tl );  // Left

    return edges;
}


SEG INT_BOX::GetEdge( int aIndex ) const
{
    VECTOR2I tl = m_min;
    VECTOR2I tr( m_max.x, m_min.y );
    VECTOR2I br = m_max;
    VECTOR2I bl( m_min.x, m_max.y );

    switch( aIndex % 4 )
    {
    case 0: return SEG( tl, tr );  // Top
    case 1: return SEG( tr, br );  // Right
    case 2: return SEG( br, bl );  // Bottom
    case 3: return SEG( bl, tl );  // Left
    }

    return SEG( tl, tr );
}


int INT_BOX::DistanceToEdge( const VECTOR2I& aPt ) const
{
    // If point is inside, return distance to nearest edge
    if( Contains( aPt ) )
    {
        int dx = std::min( aPt.x - m_min.x, m_max.x - aPt.x );
        int dy = std::min( aPt.y - m_min.y, m_max.y - aPt.y );
        return std::min( dx, dy );
    }

    // Point is outside - find distance to nearest edge
    int minDist = std::numeric_limits<int>::max();

    for( const SEG& edge : GetEdges() )
    {
        int dist = edge.Distance( aPt );
        minDist = std::min( minDist, dist );
    }

    return minDist;
}


VECTOR2I INT_BOX::NearestPointOnBoundary( const VECTOR2I& aPt ) const
{
    // Clamp to box bounds
    int x = std::clamp( aPt.x, m_min.x, m_max.x );
    int y = std::clamp( aPt.y, m_min.y, m_max.y );

    // If point is inside, find nearest edge
    if( x > m_min.x && x < m_max.x && y > m_min.y && y < m_max.y )
    {
        int dx_left = x - m_min.x;
        int dx_right = m_max.x - x;
        int dy_top = y - m_min.y;
        int dy_bottom = m_max.y - y;

        int minDist = std::min( { dx_left, dx_right, dy_top, dy_bottom } );

        if( minDist == dx_left )
            return VECTOR2I( m_min.x, y );
        if( minDist == dx_right )
            return VECTOR2I( m_max.x, y );
        if( minDist == dy_top )
            return VECTOR2I( x, m_min.y );
        return VECTOR2I( x, m_max.y );
    }

    return VECTOR2I( x, y );
}


std::unique_ptr<TILE_SHAPE> INT_BOX::Clone() const
{
    return std::make_unique<INT_BOX>( *this );
}


void INT_BOX::Inflate( int aAmount )
{
    m_min.x -= aAmount;
    m_min.y -= aAmount;
    m_max.x += aAmount;
    m_max.y += aAmount;
}


std::optional<SEG> INT_BOX::TouchingSegment( const INT_BOX& aOther ) const
{
    // Check if boxes touch on any edge

    // Touching on right edge of this box (left edge of other)
    if( m_max.x == aOther.m_min.x )
    {
        int top = std::max( m_min.y, aOther.m_min.y );
        int bottom = std::min( m_max.y, aOther.m_max.y );
        if( top < bottom )
            return SEG( VECTOR2I( m_max.x, top ), VECTOR2I( m_max.x, bottom ) );
    }

    // Touching on left edge of this box (right edge of other)
    if( m_min.x == aOther.m_max.x )
    {
        int top = std::max( m_min.y, aOther.m_min.y );
        int bottom = std::min( m_max.y, aOther.m_max.y );
        if( top < bottom )
            return SEG( VECTOR2I( m_min.x, top ), VECTOR2I( m_min.x, bottom ) );
    }

    // Touching on bottom edge of this box (top edge of other)
    if( m_max.y == aOther.m_min.y )
    {
        int left = std::max( m_min.x, aOther.m_min.x );
        int right = std::min( m_max.x, aOther.m_max.x );
        if( left < right )
            return SEG( VECTOR2I( left, m_max.y ), VECTOR2I( right, m_max.y ) );
    }

    // Touching on top edge of this box (bottom edge of other)
    if( m_min.y == aOther.m_max.y )
    {
        int left = std::max( m_min.x, aOther.m_min.x );
        int right = std::min( m_max.x, aOther.m_max.x );
        if( left < right )
            return SEG( VECTOR2I( left, m_min.y ), VECTOR2I( right, m_min.y ) );
    }

    return std::nullopt;
}


//-----------------------------------------------------------------------------
// CONVEX_POLY_SHAPE Implementation
//-----------------------------------------------------------------------------

CONVEX_POLY_SHAPE::CONVEX_POLY_SHAPE()
{
}


CONVEX_POLY_SHAPE::CONVEX_POLY_SHAPE( const std::vector<VECTOR2I>& aVertices ) :
    m_vertices( aVertices )
{
}


void CONVEX_POLY_SHAPE::updateBBox() const
{
    if( m_bboxValid )
        return;

    if( m_vertices.empty() )
    {
        m_bbox = BOX2I();
        m_bboxValid = true;
        return;
    }

    VECTOR2I minPt = m_vertices[0];
    VECTOR2I maxPt = m_vertices[0];

    for( const auto& v : m_vertices )
    {
        minPt.x = std::min( minPt.x, v.x );
        minPt.y = std::min( minPt.y, v.y );
        maxPt.x = std::max( maxPt.x, v.x );
        maxPt.y = std::max( maxPt.y, v.y );
    }

    m_bbox = BOX2I( minPt, VECTOR2L( maxPt.x - minPt.x, maxPt.y - minPt.y ) );
    m_bboxValid = true;
}


BOX2I CONVEX_POLY_SHAPE::BoundingBox() const
{
    updateBBox();
    return m_bbox;
}


bool CONVEX_POLY_SHAPE::Contains( const VECTOR2I& aPt ) const
{
    if( m_vertices.size() < 3 )
        return false;

    // Use cross product to check if point is on same side of all edges
    // For a convex polygon in CCW order, point should be to the left of all edges
    int n = m_vertices.size();

    for( int i = 0; i < n; ++i )
    {
        const VECTOR2I& v1 = m_vertices[i];
        const VECTOR2I& v2 = m_vertices[( i + 1 ) % n];

        // Cross product of edge vector and point vector
        int64_t cross = (int64_t)( v2.x - v1.x ) * ( aPt.y - v1.y ) -
                        (int64_t)( v2.y - v1.y ) * ( aPt.x - v1.x );

        if( cross < 0 )
            return false;  // Point is to the right of this edge (outside)
    }

    return true;
}


bool CONVEX_POLY_SHAPE::Intersects( const TILE_SHAPE& aOther ) const
{
    // Simple bounding box check first
    updateBBox();
    if( !m_bbox.Intersects( aOther.BoundingBox() ) )
        return false;

    // For more precise check, would need SAT (Separating Axis Theorem)
    // For now, just use bounding box approximation
    return true;
}


std::unique_ptr<TILE_SHAPE> CONVEX_POLY_SHAPE::Intersection( const TILE_SHAPE& aOther ) const
{
    // Complex operation - would need Sutherland-Hodgman clipping
    // For now, return bounding box intersection
    const INT_BOX* otherBox = dynamic_cast<const INT_BOX*>( &aOther );
    if( otherBox )
    {
        updateBBox();
        auto result = INT_BOX( m_bbox ).IntersectionBox( *otherBox );
        if( result )
            return std::make_unique<INT_BOX>( *result );
    }

    return nullptr;
}


VECTOR2I CONVEX_POLY_SHAPE::Center() const
{
    if( m_vertices.empty() )
        return VECTOR2I( 0, 0 );

    int64_t sumX = 0, sumY = 0;
    for( const auto& v : m_vertices )
    {
        sumX += v.x;
        sumY += v.y;
    }

    return VECTOR2I( sumX / m_vertices.size(), sumY / m_vertices.size() );
}


std::vector<SEG> CONVEX_POLY_SHAPE::GetEdges() const
{
    std::vector<SEG> edges;
    int n = m_vertices.size();

    if( n < 2 )
        return edges;

    edges.reserve( n );
    for( int i = 0; i < n; ++i )
        edges.emplace_back( m_vertices[i], m_vertices[( i + 1 ) % n] );

    return edges;
}


int CONVEX_POLY_SHAPE::DistanceToEdge( const VECTOR2I& aPt ) const
{
    int minDist = std::numeric_limits<int>::max();

    for( const SEG& edge : GetEdges() )
    {
        int dist = edge.Distance( aPt );
        minDist = std::min( minDist, dist );
    }

    return minDist;
}


VECTOR2I CONVEX_POLY_SHAPE::NearestPointOnBoundary( const VECTOR2I& aPt ) const
{
    int minDist = std::numeric_limits<int>::max();
    VECTOR2I nearest = aPt;

    for( const SEG& edge : GetEdges() )
    {
        VECTOR2I closest = edge.NearestPoint( aPt );
        int dist = ( closest - aPt ).EuclideanNorm();

        if( dist < minDist )
        {
            minDist = dist;
            nearest = closest;
        }
    }

    return nearest;
}


std::unique_ptr<TILE_SHAPE> CONVEX_POLY_SHAPE::Clone() const
{
    return std::make_unique<CONVEX_POLY_SHAPE>( *this );
}


void CONVEX_POLY_SHAPE::Inflate( int aAmount )
{
    // For a proper inflate, would need to offset each edge outward
    // and recompute intersections. This is a simplified version.
    VECTOR2I center = Center();

    for( auto& v : m_vertices )
    {
        VECTOR2I dir = v - center;
        int len = dir.EuclideanNorm();
        if( len > 0 )
        {
            v.x = center.x + ( (int64_t)dir.x * ( len + aAmount ) ) / len;
            v.y = center.y + ( (int64_t)dir.y * ( len + aAmount ) ) / len;
        }
    }

    m_bboxValid = false;
}
