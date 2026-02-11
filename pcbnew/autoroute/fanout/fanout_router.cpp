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

#include "fanout_router.h"
#include "../search/shape_search_tree.h"
#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <algorithm>
#include <cmath>


FANOUT_ROUTER::FANOUT_ROUTER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl )
    : m_board( aBoard )
    , m_control( aControl )
{
    m_viaSpacing = aControl.via_diameter + aControl.clearance;
    m_traceWidth = aControl.GetTraceWidth( 0 );
    m_viaDiameter = aControl.via_diameter;
    m_clearance = aControl.clearance;
}


FANOUT_RESULT FANOUT_ROUTER::GenerateFanout( FOOTPRINT* aFootprint )
{
    FANOUT_RESULT result;

    if( !aFootprint )
    {
        result.error_message = "No footprint provided";
        return result;
    }

    if( !IsFanoutCandidate( aFootprint ) )
    {
        result.error_message = "Footprint is not a fanout candidate";
        return result;
    }

    BOX2I fpBounds = aFootprint->GetBoundingBox();

    // Process each pad in the footprint
    for( PAD* pad : aFootprint->Pads() )
    {
        // Skip pads that don't need fanout (through-hole, mounting, etc.)
        if( pad->GetAttribute() == PAD_ATTRIB::NPTH )
            continue;

        // Skip pads with no net
        if( pad->GetNetCode() <= 0 )
            continue;

        // Skip through-hole pads (they don't need escape vias)
        if( pad->GetAttribute() == PAD_ATTRIB::PTH )
            continue;

        FANOUT_CONNECTION conn;
        if( GeneratePadFanout( pad, conn ) )
        {
            result.connections.push_back( conn );
            result.successful++;
        }
        else
        {
            result.failed++;
        }
    }

    result.complete = ( result.failed == 0 && result.successful > 0 );
    return result;
}


bool FANOUT_ROUTER::GeneratePadFanout( PAD* aPad, FANOUT_CONNECTION& aConnection )
{
    if( !aPad || !aPad->GetParentFootprint() )
        return false;

    FOOTPRINT* fp = aPad->GetParentFootprint();
    BOX2I fpBounds = fp->GetBoundingBox();

    // Get pad center position
    VECTOR2I padPos = aPad->GetPosition();

    // Determine escape direction based on pad position
    FANOUT_DIRECTION direction = GetBestEscapeDirection( aPad, fpBounds );

    // Calculate via position based on pattern
    VECTOR2I viaPos;
    VECTOR2I midpoint;
    bool hasMidpoint = false;

    switch( m_pattern )
    {
    case FANOUT_PATTERN::DIRECT:
        viaPos = CalculateDirectViaPosition( aPad, direction );
        break;

    case FANOUT_PATTERN::DOG_BONE:
        viaPos = CalculateDogBoneViaPosition( aPad, direction, midpoint );
        hasMidpoint = true;
        break;

    case FANOUT_PATTERN::STAGGERED:
        // Use pad index for stagger calculation
        {
            int staggerIndex = 0;
            for( PAD* p : fp->Pads() )
            {
                if( p == aPad )
                    break;
                staggerIndex++;
            }
            viaPos = CalculateStaggeredViaPosition( aPad, direction, staggerIndex );
        }
        break;

    case FANOUT_PATTERN::CHANNEL:
        // Channel routing - via placed in channel between pad rows
        viaPos = CalculateDirectViaPosition( aPad, direction );
        // Extend distance to reach channel
        {
            VECTOR2I dirVec = GetDirectionVector( direction );
            int channelDist = m_viaDiameter * 2 + m_clearance * 2;
            viaPos.x += dirVec.x * channelDist / 1000;
            viaPos.y += dirVec.y * channelDist / 1000;
        }
        break;
    }

    // Validate the via position
    if( !IsValidViaPosition( viaPos, aPad->GetNetCode() ) )
    {
        // Try alternative directions
        static const FANOUT_DIRECTION alternatives[] = {
            FANOUT_DIRECTION::NORTH, FANOUT_DIRECTION::SOUTH,
            FANOUT_DIRECTION::EAST, FANOUT_DIRECTION::WEST,
            FANOUT_DIRECTION::DIAGONAL_NE, FANOUT_DIRECTION::DIAGONAL_NW,
            FANOUT_DIRECTION::DIAGONAL_SE, FANOUT_DIRECTION::DIAGONAL_SW
        };

        bool found = false;
        for( FANOUT_DIRECTION altDir : alternatives )
        {
            if( altDir == direction )
                continue;

            if( m_pattern == FANOUT_PATTERN::DOG_BONE )
            {
                viaPos = CalculateDogBoneViaPosition( aPad, altDir, midpoint );
            }
            else
            {
                viaPos = CalculateDirectViaPosition( aPad, altDir );
            }

            if( IsValidViaPosition( viaPos, aPad->GetNetCode() ) )
            {
                direction = altDir;
                found = true;
                break;
            }
        }

        if( !found )
            return false;
    }

    // Validate the trace path
    VECTOR2I traceStart = padPos;
    VECTOR2I traceEnd = hasMidpoint ? midpoint : viaPos;

    if( !IsValidTrace( traceStart, traceEnd, aPad->GetLayer(), m_traceWidth, aPad->GetNetCode() ) )
        return false;

    if( hasMidpoint )
    {
        if( !IsValidTrace( midpoint, viaPos, aPad->GetLayer(), m_traceWidth, aPad->GetNetCode() ) )
            return false;
    }

    // Build the connection
    aConnection.pad_position = padPos;
    aConnection.via_position = viaPos;
    aConnection.midpoint = midpoint;
    aConnection.has_midpoint = hasMidpoint;
    aConnection.pad_layer = aPad->GetLayer();
    aConnection.escape_layer = m_escapeLayer;
    aConnection.net_code = aPad->GetNetCode();
    aConnection.net_name = aPad->GetNetname();
    aConnection.direction = direction;

    return true;
}


bool FANOUT_ROUTER::IsFanoutCandidate( FOOTPRINT* aFootprint ) const
{
    if( !aFootprint )
        return false;

    // Count SMD pads
    int smdCount = 0;
    for( PAD* pad : aFootprint->Pads() )
    {
        if( pad->GetAttribute() == PAD_ATTRIB::SMD )
            smdCount++;
    }

    // BGA-style packages typically have 16+ pads in a grid
    // QFP-style packages typically have 20+ pads around the perimeter
    return smdCount >= 16;
}


FANOUT_ROUTER::PAD_ZONE FANOUT_ROUTER::ClassifyPad( PAD* aPad, const BOX2I& aFootprintBounds ) const
{
    VECTOR2I padPos = aPad->GetPosition();
    VECTOR2I center = aFootprintBounds.Centre();
    VECTOR2I size = aFootprintBounds.GetSize();

    // Calculate distance from center as percentage
    double dx = std::abs( padPos.x - center.x ) / ( double( size.x ) / 2 );
    double dy = std::abs( padPos.y - center.y ) / ( double( size.y ) / 2 );
    double dist = std::max( dx, dy );

    // Pads in outer 40% are OUTER, rest are INNER
    return ( dist > 0.6 ) ? PAD_ZONE::OUTER : PAD_ZONE::INNER;
}


FANOUT_DIRECTION FANOUT_ROUTER::GetBestEscapeDirection( PAD* aPad, const BOX2I& aFootprintBounds ) const
{
    VECTOR2I padPos = aPad->GetPosition();
    VECTOR2I center = aFootprintBounds.Centre();

    // Calculate direction from center to pad
    int dx = padPos.x - center.x;
    int dy = padPos.y - center.y;

    // Determine which edge/corner the pad is closest to
    int absDx = std::abs( dx );
    int absDy = std::abs( dy );

    // Check for corner cases (diagonal escape)
    double ratio = ( absDy > 0 ) ? double( absDx ) / double( absDy ) : 1000.0;
    bool isDiagonal = ( ratio > 0.5 && ratio < 2.0 );

    if( isDiagonal )
    {
        if( dx > 0 && dy < 0 )
            return FANOUT_DIRECTION::DIAGONAL_NE;
        else if( dx < 0 && dy < 0 )
            return FANOUT_DIRECTION::DIAGONAL_NW;
        else if( dx > 0 && dy > 0 )
            return FANOUT_DIRECTION::DIAGONAL_SE;
        else
            return FANOUT_DIRECTION::DIAGONAL_SW;
    }

    // Cardinal direction escape
    if( absDx > absDy )
    {
        return ( dx > 0 ) ? FANOUT_DIRECTION::EAST : FANOUT_DIRECTION::WEST;
    }
    else
    {
        return ( dy > 0 ) ? FANOUT_DIRECTION::SOUTH : FANOUT_DIRECTION::NORTH;
    }
}


VECTOR2I FANOUT_ROUTER::CalculateDirectViaPosition( PAD* aPad, FANOUT_DIRECTION aDirection ) const
{
    VECTOR2I padPos = aPad->GetPosition();
    VECTOR2I dirVec = GetDirectionVector( aDirection );

    // Calculate minimum distance from pad center to via center
    // Must clear pad edge + clearance + half via diameter
    VECTOR2I padSize = aPad->GetSize( aPad->GetLayer() );
    int padRadius = std::max( padSize.x, padSize.y ) / 2;
    int minDist = padRadius + m_clearance + m_viaDiameter / 2;

    // Position via in the escape direction
    return VECTOR2I( padPos.x + ( dirVec.x * minDist ) / 1000,
                     padPos.y + ( dirVec.y * minDist ) / 1000 );
}


VECTOR2I FANOUT_ROUTER::CalculateDogBoneViaPosition( PAD* aPad, FANOUT_DIRECTION aDirection,
                                                      VECTOR2I& aMidpoint ) const
{
    VECTOR2I padPos = aPad->GetPosition();
    VECTOR2I dirVec = GetDirectionVector( aDirection );

    // For dog-bone, the midpoint is at the pad edge
    // The via is further along the escape direction
    VECTOR2I padSize = aPad->GetSize( aPad->GetLayer() );
    int padRadius = std::max( padSize.x, padSize.y ) / 2;

    // Midpoint just outside pad
    int midDist = padRadius + m_clearance / 2;
    aMidpoint = VECTOR2I( padPos.x + ( dirVec.x * midDist ) / 1000,
                          padPos.y + ( dirVec.y * midDist ) / 1000 );

    // Via position further out
    int viaDist = padRadius + m_clearance + m_viaDiameter;
    return VECTOR2I( padPos.x + ( dirVec.x * viaDist ) / 1000,
                     padPos.y + ( dirVec.y * viaDist ) / 1000 );
}


VECTOR2I FANOUT_ROUTER::CalculateStaggeredViaPosition( PAD* aPad, FANOUT_DIRECTION aDirection,
                                                        int aStaggerIndex ) const
{
    VECTOR2I basePos = CalculateDirectViaPosition( aPad, aDirection );

    // Stagger alternating pads perpendicular to escape direction
    bool stagger = ( aStaggerIndex % 2 ) == 1;

    if( !stagger )
        return basePos;

    // Calculate perpendicular direction
    VECTOR2I dirVec = GetDirectionVector( aDirection );
    VECTOR2I perpVec( -dirVec.y, dirVec.x );  // Rotate 90 degrees

    // Stagger by half via spacing
    int staggerDist = m_viaSpacing / 2;
    return VECTOR2I( basePos.x + ( perpVec.x * staggerDist ) / 1000,
                     basePos.y + ( perpVec.y * staggerDist ) / 1000 );
}


bool FANOUT_ROUTER::IsValidViaPosition( const VECTOR2I& aPosition, int aNetCode ) const
{
    if( !m_searchTree )
        return true;

    int halfDia = m_viaDiameter / 2 + m_clearance;

    BOX2I viaBounds( VECTOR2I( aPosition.x - halfDia, aPosition.y - halfDia ),
                     VECTOR2I( halfDia * 2, halfDia * 2 ) );

    // Check all layers for through via
    // For a proper implementation, would check only relevant layers
    return !m_searchTree->HasOverlap( viaBounds, 0, aNetCode );
}


bool FANOUT_ROUTER::IsValidTrace( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                   int aLayer, int aWidth, int aNetCode ) const
{
    if( !m_searchTree )
        return true;

    // Build bounding box for trace segment
    int halfWidth = aWidth / 2 + m_clearance;

    int minX = std::min( aStart.x, aEnd.x ) - halfWidth;
    int minY = std::min( aStart.y, aEnd.y ) - halfWidth;
    int maxX = std::max( aStart.x, aEnd.x ) + halfWidth;
    int maxY = std::max( aStart.y, aEnd.y ) + halfWidth;

    BOX2I traceBounds( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );

    return !m_searchTree->HasOverlap( traceBounds, aLayer, aNetCode );
}


VECTOR2I FANOUT_ROUTER::GetDirectionVector( FANOUT_DIRECTION aDirection ) const
{
    // Return unit vector * 1000 for precision
    switch( aDirection )
    {
    case FANOUT_DIRECTION::NORTH:
        return VECTOR2I( 0, -1000 );
    case FANOUT_DIRECTION::SOUTH:
        return VECTOR2I( 0, 1000 );
    case FANOUT_DIRECTION::EAST:
        return VECTOR2I( 1000, 0 );
    case FANOUT_DIRECTION::WEST:
        return VECTOR2I( -1000, 0 );
    case FANOUT_DIRECTION::DIAGONAL_NE:
        return VECTOR2I( 707, -707 );  // Approximately 1/sqrt(2) * 1000
    case FANOUT_DIRECTION::DIAGONAL_NW:
        return VECTOR2I( -707, -707 );
    case FANOUT_DIRECTION::DIAGONAL_SE:
        return VECTOR2I( 707, 707 );
    case FANOUT_DIRECTION::DIAGONAL_SW:
        return VECTOR2I( -707, 707 );
    default:
        return VECTOR2I( 0, 1000 );
    }
}
