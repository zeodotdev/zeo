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

#include "destination_distance.h"
#include "../expansion/expansion_room.h"
#include <algorithm>
#include <cmath>
#include <limits>


DESTINATION_DISTANCE::DESTINATION_DISTANCE()
{
}


void DESTINATION_DISTANCE::Initialize( const std::set<BOARD_ITEM*>& aDestItems )
{
    // This will be called by the engine to set up destinations
    // For now, we expect AddTargetRoom to be called for each destination
    Clear();
}


void DESTINATION_DISTANCE::AddTargetRoom( EXPANSION_ROOM* aRoom )
{
    if( !aRoom )
        return;

    DestinationTarget target;
    target.center = aRoom->GetCenter();
    target.bbox = aRoom->GetBoundingBox();
    target.layer = aRoom->GetLayer();
    target.room = aRoom;

    m_targets.push_back( target );
    m_destLayers.insert( target.layer );

    // Update total bounding box
    if( m_targets.size() == 1 )
    {
        m_totalBBox = target.bbox;
    }
    else
    {
        m_totalBBox.Merge( target.bbox );
    }
}


void DESTINATION_DISTANCE::Clear()
{
    m_targets.clear();
    m_destLayers.clear();
    m_totalBBox = BOX2I();
}


double DESTINATION_DISTANCE::Calculate( const VECTOR2I& aPt, int aLayer, double aViaCost ) const
{
    if( m_targets.empty() )
        return 0.0;

    double minCost = std::numeric_limits<double>::max();

    for( const auto& target : m_targets )
    {
        // Calculate Manhattan distance (better heuristic for grid-based routing)
        int dx = std::abs( aPt.x - target.center.x );
        int dy = std::abs( aPt.y - target.center.y );
        double distance = static_cast<double>( dx + dy );

        // Add layer change cost if on different layer
        int layerDiff = std::abs( aLayer - target.layer );
        double layerCost = layerDiff * aViaCost;

        double totalCost = distance + layerCost;
        minCost = std::min( minCost, totalCost );
    }

    return minCost;
}


double DESTINATION_DISTANCE::CalculateDistance( const VECTOR2I& aPt ) const
{
    if( m_targets.empty() )
        return 0.0;

    double minDist = std::numeric_limits<double>::max();

    for( const auto& target : m_targets )
    {
        // Euclidean distance to center
        int64_t dx = aPt.x - target.center.x;
        int64_t dy = aPt.y - target.center.y;
        double dist = std::sqrt( static_cast<double>( dx * dx + dy * dy ) );

        minDist = std::min( minDist, dist );
    }

    return minDist;
}


bool DESTINATION_DISTANCE::IsDestination( const EXPANSION_ROOM* aRoom ) const
{
    for( const auto& target : m_targets )
    {
        if( target.room == aRoom )
            return true;
    }
    return false;
}


bool DESTINATION_DISTANCE::IsAtDestination( const VECTOR2I& aPt, int aLayer ) const
{
    for( const auto& target : m_targets )
    {
        if( target.layer == aLayer && target.bbox.Contains( aPt ) )
            return true;
    }
    return false;
}


VECTOR2I DESTINATION_DISTANCE::GetNearestDestCenter( const VECTOR2I& aPt ) const
{
    if( m_targets.empty() )
        return aPt;

    double minDist = std::numeric_limits<double>::max();
    VECTOR2I nearest = aPt;

    for( const auto& target : m_targets )
    {
        int64_t dx = aPt.x - target.center.x;
        int64_t dy = aPt.y - target.center.y;
        double dist = dx * dx + dy * dy;  // Squared distance for comparison

        if( dist < minDist )
        {
            minDist = dist;
            nearest = target.center;
        }
    }

    return nearest;
}
