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

#include "differential_pair.h"
#include "../search/shape_search_tree.h"
#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <netinfo.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>


DIFF_PAIR_ROUTER::DIFF_PAIR_ROUTER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl )
    : m_board( aBoard )
    , m_control( aControl )
{
}


std::vector<DIFF_PAIR_CONNECTION> DIFF_PAIR_ROUTER::FindDifferentialPairs() const
{
    std::vector<DIFF_PAIR_CONNECTION> pairs;

    if( !m_board )
        return pairs;

    // Build map of base names to nets
    std::map<std::string, std::pair<NETINFO_ITEM*, NETINFO_ITEM*>> pairMap;

    for( NETINFO_ITEM* net : m_board->GetNetInfo() )
    {
        if( !net || net->GetNetCode() <= 0 )
            continue;

        std::string netName = net->GetNetname().ToStdString();

        // Check for positive suffix
        bool isPositive = false;
        bool isNegative = false;
        std::string baseName;

        if( netName.length() > m_config.suffix_positive.length() )
        {
            size_t pos = netName.length() - m_config.suffix_positive.length();
            if( netName.substr( pos ) == m_config.suffix_positive )
            {
                isPositive = true;
                baseName = netName.substr( 0, pos );
            }
        }

        if( !isPositive && netName.length() > m_config.suffix_negative.length() )
        {
            size_t pos = netName.length() - m_config.suffix_negative.length();
            if( netName.substr( pos ) == m_config.suffix_negative )
            {
                isNegative = true;
                baseName = netName.substr( 0, pos );
            }
        }

        if( isPositive )
        {
            pairMap[baseName].first = net;
        }
        else if( isNegative )
        {
            pairMap[baseName].second = net;
        }
    }

    // Create connections for complete pairs
    for( const auto& [baseName, netPair] : pairMap )
    {
        if( netPair.first && netPair.second )
        {
            DIFF_PAIR_CONNECTION conn;
            conn.base_name = baseName;
            conn.positive_net = netPair.first->GetNetname().ToStdString();
            conn.negative_net = netPair.second->GetNetname().ToStdString();
            conn.positive_net_code = netPair.first->GetNetCode();
            conn.negative_net_code = netPair.second->GetNetCode();

            // Find pad pairs
            if( FindPadPairs( conn ) )
            {
                pairs.push_back( conn );
            }
        }
    }

    return pairs;
}


bool DIFF_PAIR_ROUTER::FindPadPairs( DIFF_PAIR_CONNECTION& aConnection ) const
{
    if( !m_board )
        return false;

    // Collect pads for each net
    std::vector<PAD*> positivePads;
    std::vector<PAD*> negativePads;

    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            if( pad->GetNetCode() == aConnection.positive_net_code )
            {
                positivePads.push_back( pad );
            }
            else if( pad->GetNetCode() == aConnection.negative_net_code )
            {
                negativePads.push_back( pad );
            }
        }
    }

    // Need at least 2 pads per net for a connection
    if( positivePads.size() < 2 || negativePads.size() < 2 )
        return false;

    // For simplicity, assume the first two pads of each net form the pair
    // A more sophisticated algorithm would match by proximity
    aConnection.positive_start = positivePads[0];
    aConnection.positive_end = positivePads[1];

    // Find negative pads closest to positive pads
    auto findClosest = []( PAD* ref, std::vector<PAD*>& candidates ) -> PAD* {
        PAD* closest = nullptr;
        int64_t minDist = std::numeric_limits<int64_t>::max();

        for( PAD* p : candidates )
        {
            VECTOR2I diff = p->GetPosition() - ref->GetPosition();
            int64_t dist = int64_t( diff.x ) * diff.x + int64_t( diff.y ) * diff.y;
            if( dist < minDist )
            {
                minDist = dist;
                closest = p;
            }
        }
        return closest;
    };

    aConnection.negative_start = findClosest( aConnection.positive_start, negativePads );
    aConnection.negative_end = findClosest( aConnection.positive_end, negativePads );

    // Ensure start and end are different pads
    if( aConnection.negative_start == aConnection.negative_end )
    {
        // Swap to the other negative pad
        for( PAD* p : negativePads )
        {
            if( p != aConnection.negative_start )
            {
                aConnection.negative_end = p;
                break;
            }
        }
    }

    return aConnection.positive_start && aConnection.positive_end &&
           aConnection.negative_start && aConnection.negative_end;
}


DIFF_PAIR_PATH DIFF_PAIR_ROUTER::Route( const DIFF_PAIR_CONNECTION& aConnection )
{
    DIFF_PAIR_PATH result;

    if( !m_board )
        return result;

    // Try coupled routing
    if( RouteCoupled( aConnection, result ) )
    {
        // Calculate lengths
        result.positive_length = CalculatePathLength( result.positive_path );
        result.negative_length = CalculatePathLength( result.negative_path );
        result.length_difference = std::abs( result.positive_length - result.negative_length );

        // Check length matching
        if( m_config.length_tolerance > 0 &&
            result.length_difference > static_cast<int64_t>( m_config.length_tolerance ) )
        {
            EqualizeLengths( result );
            result.positive_length = CalculatePathLength( result.positive_path );
            result.negative_length = CalculatePathLength( result.negative_path );
            result.length_difference = std::abs( result.positive_length - result.negative_length );
        }

        result.length_matched = ( result.length_difference <=
                                   static_cast<int64_t>( m_config.length_tolerance ) );
        result.valid = true;
    }

    return result;
}


bool DIFF_PAIR_ROUTER::RouteCoupled( const DIFF_PAIR_CONNECTION& aConnection,
                                      DIFF_PAIR_PATH& aResult )
{
    if( !aConnection.positive_start || !aConnection.positive_end ||
        !aConnection.negative_start || !aConnection.negative_end )
        return false;

    // Get start and end positions
    VECTOR2I posStart = aConnection.positive_start->GetPosition();
    VECTOR2I posEnd = aConnection.positive_end->GetPosition();
    VECTOR2I negStart = aConnection.negative_start->GetPosition();
    VECTOR2I negEnd = aConnection.negative_end->GetPosition();

    // Determine which signal is on which side
    // Based on relative positions of start pads
    VECTOR2I startDiff = negStart - posStart;
    VECTOR2I endDir = posEnd - posStart;

    // Cross product to determine side
    int64_t cross = int64_t( endDir.x ) * startDiff.y - int64_t( endDir.y ) * startDiff.x;
    bool positiveOnLeft = ( cross > 0 );

    // Calculate centerline
    VECTOR2I centerStart( ( posStart.x + negStart.x ) / 2, ( posStart.y + negStart.y ) / 2 );
    VECTOR2I centerEnd( ( posEnd.x + negEnd.x ) / 2, ( posEnd.y + negEnd.y ) / 2 );

    // For now, create a simple straight path with perpendicular offset
    // A full implementation would use the maze search for more complex paths

    int layer = aConnection.positive_start->GetLayer();
    int halfGap = m_config.gap / 2;

    // Create path segments
    PATH_SEGMENT posSeg;
    posSeg.layer = layer;
    posSeg.width = m_config.trace_width;

    PATH_SEGMENT negSeg;
    negSeg.layer = layer;
    negSeg.width = m_config.trace_width;

    // Calculate offset direction (perpendicular to path)
    VECTOR2I pathDir = centerEnd - centerStart;
    double pathLen = std::sqrt( double( pathDir.x ) * pathDir.x +
                                 double( pathDir.y ) * pathDir.y );

    if( pathLen < 1.0 )
        return false;

    // Perpendicular unit vector * halfGap
    VECTOR2I perpOffset( static_cast<int>( -pathDir.y * halfGap / pathLen ),
                         static_cast<int>( pathDir.x * halfGap / pathLen ) );

    // Generate path points
    if( positiveOnLeft )
    {
        posSeg.points.push_back( posStart );
        posSeg.points.push_back( VECTOR2I( centerStart.x + perpOffset.x,
                                            centerStart.y + perpOffset.y ) );
        posSeg.points.push_back( VECTOR2I( centerEnd.x + perpOffset.x,
                                            centerEnd.y + perpOffset.y ) );
        posSeg.points.push_back( posEnd );

        negSeg.points.push_back( negStart );
        negSeg.points.push_back( VECTOR2I( centerStart.x - perpOffset.x,
                                            centerStart.y - perpOffset.y ) );
        negSeg.points.push_back( VECTOR2I( centerEnd.x - perpOffset.x,
                                            centerEnd.y - perpOffset.y ) );
        negSeg.points.push_back( negEnd );
    }
    else
    {
        posSeg.points.push_back( posStart );
        posSeg.points.push_back( VECTOR2I( centerStart.x - perpOffset.x,
                                            centerStart.y - perpOffset.y ) );
        posSeg.points.push_back( VECTOR2I( centerEnd.x - perpOffset.x,
                                            centerEnd.y - perpOffset.y ) );
        posSeg.points.push_back( posEnd );

        negSeg.points.push_back( negStart );
        negSeg.points.push_back( VECTOR2I( centerStart.x + perpOffset.x,
                                            centerStart.y + perpOffset.y ) );
        negSeg.points.push_back( VECTOR2I( centerEnd.x + perpOffset.x,
                                            centerEnd.y + perpOffset.y ) );
        negSeg.points.push_back( negEnd );
    }

    // Validate paths
    if( !IsValidCoupledPath( posSeg.points[0], posSeg.points.back(),
                              negSeg.points[0], negSeg.points.back(),
                              layer, aConnection.positive_net_code,
                              aConnection.negative_net_code ) )
    {
        return false;
    }

    // Build result paths
    aResult.positive_path.segments.push_back( posSeg );
    aResult.positive_path.start_point = posStart;
    aResult.positive_path.end_point = posEnd;
    aResult.positive_path.start_layer = layer;
    aResult.positive_path.end_layer = layer;

    aResult.negative_path.segments.push_back( negSeg );
    aResult.negative_path.start_point = negStart;
    aResult.negative_path.end_point = negEnd;
    aResult.negative_path.start_layer = layer;
    aResult.negative_path.end_layer = layer;

    return true;
}


bool DIFF_PAIR_ROUTER::IsValidCoupledPath( const VECTOR2I& aPosStart, const VECTOR2I& aPosEnd,
                                            const VECTOR2I& aNegStart, const VECTOR2I& aNegEnd,
                                            int aLayer, int aPosNetCode, int aNegNetCode ) const
{
    if( !m_searchTree )
        return true;

    int halfWidth = m_config.trace_width / 2 + m_control.clearance;

    // Check positive trace
    int minX = std::min( aPosStart.x, aPosEnd.x ) - halfWidth;
    int minY = std::min( aPosStart.y, aPosEnd.y ) - halfWidth;
    int maxX = std::max( aPosStart.x, aPosEnd.x ) + halfWidth;
    int maxY = std::max( aPosStart.y, aPosEnd.y ) + halfWidth;

    BOX2I posBounds( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );
    if( m_searchTree->HasOverlap( posBounds, aLayer, aPosNetCode ) )
        return false;

    // Check negative trace
    minX = std::min( aNegStart.x, aNegEnd.x ) - halfWidth;
    minY = std::min( aNegStart.y, aNegEnd.y ) - halfWidth;
    maxX = std::max( aNegStart.x, aNegEnd.x ) + halfWidth;
    maxY = std::max( aNegStart.y, aNegEnd.y ) + halfWidth;

    BOX2I negBounds( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );
    if( m_searchTree->HasOverlap( negBounds, aLayer, aNegNetCode ) )
        return false;

    return true;
}


void DIFF_PAIR_ROUTER::EqualizeLengths( DIFF_PAIR_PATH& aPath )
{
    if( aPath.positive_length == aPath.negative_length )
        return;

    // Add serpentine to the shorter path
    bool positiveIsShorter = aPath.positive_length < aPath.negative_length;
    int64_t lengthToAdd = aPath.length_difference;

    if( positiveIsShorter )
    {
        if( !aPath.positive_path.segments.empty() )
        {
            AddSerpentine( aPath.positive_path.segments.back(),
                           static_cast<int>( lengthToAdd ) );
        }
    }
    else
    {
        if( !aPath.negative_path.segments.empty() )
        {
            AddSerpentine( aPath.negative_path.segments.back(),
                           static_cast<int>( lengthToAdd ) );
        }
    }
}


void DIFF_PAIR_ROUTER::AddSerpentine( PATH_SEGMENT& aSegment, int aLengthToAdd )
{
    if( aSegment.points.size() < 2 )
        return;

    // Find a suitable segment to add serpentine
    // For simplicity, add it in the middle of the path
    size_t midIdx = aSegment.points.size() / 2;

    if( midIdx < 1 )
        return;

    VECTOR2I p1 = aSegment.points[midIdx - 1];
    VECTOR2I p2 = aSegment.points[midIdx];

    // Calculate segment direction
    VECTOR2I dir = p2 - p1;
    double segLen = std::sqrt( double( dir.x ) * dir.x + double( dir.y ) * dir.y );

    if( segLen < 1.0 )
        return;

    // Perpendicular direction
    VECTOR2I perp( -dir.y, dir.x );
    double perpLen = std::sqrt( double( perp.x ) * perp.x + double( perp.y ) * perp.y );

    if( perpLen < 1.0 )
        return;

    // Calculate serpentine parameters
    // Each meander adds 2 * amplitude to the path length
    int amplitude = aLengthToAdd / 4;  // Two meanders
    int spacing = std::max( m_config.gap * 2, amplitude );

    // Limit amplitude to reasonable value
    amplitude = std::min( amplitude, 2000000 );  // 2mm max

    // Normalize perpendicular
    double perpNormX = perp.x / perpLen;
    double perpNormY = perp.y / perpLen;

    // Calculate meander points
    double t = 0.25;
    VECTOR2I mid1( static_cast<int>( p1.x + dir.x * t ),
                   static_cast<int>( p1.y + dir.y * t ) );
    VECTOR2I mid2( static_cast<int>( p1.x + dir.x * ( 1 - t ) ),
                   static_cast<int>( p1.y + dir.y * ( 1 - t ) ) );

    // Add meander points
    VECTOR2I meander1( static_cast<int>( mid1.x + perpNormX * amplitude ),
                       static_cast<int>( mid1.y + perpNormY * amplitude ) );
    VECTOR2I meander2( static_cast<int>( mid2.x + perpNormX * amplitude ),
                       static_cast<int>( mid2.y + perpNormY * amplitude ) );

    // Insert meander points
    std::vector<VECTOR2I> newPoints;
    for( size_t i = 0; i < midIdx; ++i )
    {
        newPoints.push_back( aSegment.points[i] );
    }
    newPoints.push_back( mid1 );
    newPoints.push_back( meander1 );
    newPoints.push_back( meander2 );
    newPoints.push_back( mid2 );
    for( size_t i = midIdx; i < aSegment.points.size(); ++i )
    {
        newPoints.push_back( aSegment.points[i] );
    }

    aSegment.points = std::move( newPoints );
}


int64_t DIFF_PAIR_ROUTER::CalculatePathLength( const ROUTING_PATH& aPath ) const
{
    int64_t total = 0;

    for( const auto& seg : aPath.segments )
    {
        for( size_t i = 1; i < seg.points.size(); ++i )
        {
            int64_t dx = seg.points[i].x - seg.points[i - 1].x;
            int64_t dy = seg.points[i].y - seg.points[i - 1].y;
            total += static_cast<int64_t>( std::sqrt( double( dx * dx + dy * dy ) ) );
        }
    }

    return total;
}


bool DIFF_PAIR_ROUTER::IsDifferentialPair( const std::string& aNet1,
                                            const std::string& aNet2 ) const
{
    std::string base1 = GetBaseName( aNet1 );
    std::string base2 = GetBaseName( aNet2 );

    if( base1.empty() || base2.empty() )
        return false;

    return base1 == base2;
}


std::string DIFF_PAIR_ROUTER::GetBaseName( const std::string& aNetName ) const
{
    // Check for positive suffix
    if( aNetName.length() > m_config.suffix_positive.length() )
    {
        size_t pos = aNetName.length() - m_config.suffix_positive.length();
        if( aNetName.substr( pos ) == m_config.suffix_positive )
        {
            return aNetName.substr( 0, pos );
        }
    }

    // Check for negative suffix
    if( aNetName.length() > m_config.suffix_negative.length() )
    {
        size_t pos = aNetName.length() - m_config.suffix_negative.length();
        if( aNetName.substr( pos ) == m_config.suffix_negative )
        {
            return aNetName.substr( 0, pos );
        }
    }

    return "";
}


VECTOR2I DIFF_PAIR_ROUTER::CalculateOffset( const VECTOR2I& aDirection, int aDistance,
                                             bool aLeft ) const
{
    double len = std::sqrt( double( aDirection.x ) * aDirection.x +
                             double( aDirection.y ) * aDirection.y );

    if( len < 1.0 )
        return VECTOR2I( 0, 0 );

    // Perpendicular direction
    double perpX = -aDirection.y / len;
    double perpY = aDirection.x / len;

    if( !aLeft )
    {
        perpX = -perpX;
        perpY = -perpY;
    }

    return VECTOR2I( static_cast<int>( perpX * aDistance ),
                     static_cast<int>( perpY * aDistance ) );
}


bool DIFF_PAIR_ROUTER::GenerateCoupledSegment( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                                int aGap, bool aPositiveOnLeft,
                                                std::vector<VECTOR2I>& aPositivePoints,
                                                std::vector<VECTOR2I>& aNegativePoints )
{
    VECTOR2I dir = aEnd - aStart;
    int halfGap = aGap / 2;

    VECTOR2I posOffset = CalculateOffset( dir, halfGap, aPositiveOnLeft );
    VECTOR2I negOffset = CalculateOffset( dir, halfGap, !aPositiveOnLeft );

    aPositivePoints.push_back( VECTOR2I( aStart.x + posOffset.x, aStart.y + posOffset.y ) );
    aPositivePoints.push_back( VECTOR2I( aEnd.x + posOffset.x, aEnd.y + posOffset.y ) );

    aNegativePoints.push_back( VECTOR2I( aStart.x + negOffset.x, aStart.y + negOffset.y ) );
    aNegativePoints.push_back( VECTOR2I( aEnd.x + negOffset.x, aEnd.y + negOffset.y ) );

    return true;
}
