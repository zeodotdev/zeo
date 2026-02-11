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

#include "insert_connection.h"
#include "../search/congestion_map.h"
#include <board.h>
#include <pcb_track.h>
#include <pad.h>
#include <footprint.h>
#include <netinfo.h>
#include <layer_ids.h>
#include <zone.h>
#include <geometry/shape_poly_set.h>
#include <connectivity/connectivity_data.h>
#include <commit.h>
#include <sstream>
#include <cmath>


INSERT_CONNECTION::INSERT_CONNECTION()
{
}


INSERT_RESULT INSERT_CONNECTION::Insert( const ROUTING_PATH& aPath )
{
    INSERT_RESULT result;

    if( !aPath.IsValid() )
    {
        result.error_message = "Invalid path";
        return result;
    }

    if( !m_board )
    {
        result.error_message = "No board set";
        return result;
    }

    // Insert track segments
    for( const auto& seg : aPath.segments )
    {
        if( InsertTrackSegment( seg ) )
        {
            result.tracks_added++;

            // Record segment in congestion map for future routing decisions
            if( m_congestionMap && seg.points.size() >= 2 )
            {
                for( size_t i = 0; i < seg.points.size() - 1; ++i )
                {
                    m_congestionMap->AddSegment( seg.points[i], seg.points[i + 1],
                                                  seg.layer, seg.width );
                }
            }
        }
    }

    // Insert vias
    for( const auto& via : aPath.via_locations )
    {
        if( InsertVia( via ) )
        {
            result.vias_added++;

            // Record via in congestion map
            if( m_congestionMap )
            {
                m_congestionMap->AddVia( via.position, 0, m_control.via_diameter,
                                          m_control.via_diameter );
            }
        }
    }

    // Update board connectivity after adding tracks (only if not using commit)
    // When using a commit, connectivity will be rebuilt on Push()
    if( !m_commit && ( result.tracks_added > 0 || result.vias_added > 0 ) )
    {
        if( auto connectivity = m_board->GetConnectivity() )
        {
            connectivity->Build( m_board );
        }
    }

    result.success = ( result.tracks_added > 0 || result.vias_added > 0 );
    return result;
}


bool INSERT_CONNECTION::InsertTrackSegment( const PATH_SEGMENT& aSegment )
{
    if( !m_board || aSegment.points.size() < 2 )
        return false;

    // Get the net
    NETINFO_ITEM* net = nullptr;
    if( !m_netName.empty() )
    {
        net = m_board->FindNet( wxString::FromUTF8( m_netName ) );
    }

    // Convert autoroute layer to KiCad layer
    PCB_LAYER_ID layer = F_Cu;
    int layerCount = m_board->GetCopperLayerCount();
    if( aSegment.layer == 0 )
        layer = F_Cu;
    else if( aSegment.layer == layerCount - 1 )
        layer = B_Cu;
    else if( aSegment.layer >= 1 && aSegment.layer < layerCount - 1 )
        layer = static_cast<PCB_LAYER_ID>( In1_Cu + aSegment.layer - 1 );

    int defaultWidth = aSegment.width > 0 ? aSegment.width : m_control.GetTraceWidth( aSegment.layer );

    // Create track segments between consecutive points
    bool anyAdded = false;
    for( size_t i = 0; i < aSegment.points.size() - 1; ++i )
    {
        const VECTOR2I& start = aSegment.points[i];
        const VECTOR2I& end = aSegment.points[i + 1];

        // Validate segment before inserting - check for collisions with obstacles and keepout zones
        if( !ValidateSegment( start, end, aSegment.layer ) )
        {
            // Skip this segment - it would cause a collision
            continue;
        }

        // Check for pin neckdown at start and end points
        int startWidth = CalculateNeckdownWidth( start, aSegment.layer, defaultWidth );
        int endWidth = CalculateNeckdownWidth( end, aSegment.layer, defaultWidth );

        // If neckdown needed at either end, we may need multiple track segments
        if( startWidth != defaultWidth || endWidth != defaultWidth )
        {
            // Create necked-down segment(s)
            // If both ends need neckdown to same width, use that width for whole segment
            if( startWidth == endWidth )
            {
                PCB_TRACK* track = new PCB_TRACK( m_board );
                track->SetStart( start );
                track->SetEnd( end );
                track->SetWidth( startWidth );
                track->SetLayer( layer );

                if( net )
                    track->SetNet( net );

                if( m_commit )
                    m_commit->Add( track );
                else
                    m_board->Add( track );

                anyAdded = true;
            }
            else
            {
                // Different widths - create transition
                // Calculate transition point (1/3 from the end with smaller width)
                double t = ( startWidth < endWidth ) ? 0.33 : 0.67;
                VECTOR2I transitionPt(
                    static_cast<int>( start.x + ( end.x - start.x ) * t ),
                    static_cast<int>( start.y + ( end.y - start.y ) * t ) );

                // First segment (from start to transition)
                PCB_TRACK* track1 = new PCB_TRACK( m_board );
                track1->SetStart( start );
                track1->SetEnd( transitionPt );
                track1->SetWidth( startWidth );
                track1->SetLayer( layer );
                if( net )
                    track1->SetNet( net );

                if( m_commit )
                    m_commit->Add( track1 );
                else
                    m_board->Add( track1 );

                // Second segment (from transition to end)
                PCB_TRACK* track2 = new PCB_TRACK( m_board );
                track2->SetStart( transitionPt );
                track2->SetEnd( end );
                track2->SetWidth( endWidth );
                track2->SetLayer( layer );
                if( net )
                    track2->SetNet( net );

                if( m_commit )
                    m_commit->Add( track2 );
                else
                    m_board->Add( track2 );

                anyAdded = true;
            }
        }
        else
        {
            // No neckdown needed - use default width
            PCB_TRACK* track = new PCB_TRACK( m_board );
            track->SetStart( start );
            track->SetEnd( end );
            track->SetWidth( defaultWidth );
            track->SetLayer( layer );

            if( net )
                track->SetNet( net );

            if( m_commit )
                m_commit->Add( track );
            else
                m_board->Add( track );

            anyAdded = true;
        }
    }

    return anyAdded;
}


bool INSERT_CONNECTION::InsertVia( const PATH_POINT& aViaPoint )
{
    if( !m_board )
        return false;

    // Get the net
    NETINFO_ITEM* net = nullptr;
    if( !m_netName.empty() )
    {
        net = m_board->FindNet( wxString::FromUTF8( m_netName ) );
    }

    // Determine via type and layer pair
    int firstLayer = aViaPoint.via_first_layer;
    int lastLayer = aViaPoint.via_last_layer;

    // If layer pair not set, use defaults
    if( lastLayer < 0 )
    {
        firstLayer = 0;
        lastLayer = m_board->GetCopperLayerCount() - 1;
    }

    // Get the board layer IDs
    int layerCount = m_board->GetCopperLayerCount();
    PCB_LAYER_ID topLayerId = F_Cu;
    PCB_LAYER_ID bottomLayerId = B_Cu;

    // Map layer indices to layer IDs
    auto layerIndexToId = [layerCount]( int index ) -> PCB_LAYER_ID
    {
        if( index == 0 )
            return F_Cu;
        if( index == layerCount - 1 )
            return B_Cu;
        // Internal layers: In1_Cu, In2_Cu, etc.
        return static_cast<PCB_LAYER_ID>( In1_Cu + ( index - 1 ) );
    };

    PCB_LAYER_ID viaTopLayer = layerIndexToId( firstLayer );
    PCB_LAYER_ID viaBottomLayer = layerIndexToId( lastLayer );

    // Determine via type
    VIATYPE viaType = VIATYPE::THROUGH;
    int viaDiameter = m_control.via_diameter;
    int viaDrill = m_control.via_drill;

    if( firstLayer == 0 && lastLayer == layerCount - 1 )
    {
        // Through-hole via
        viaType = VIATYPE::THROUGH;
    }
    else if( firstLayer == 0 || lastLayer == layerCount - 1 )
    {
        // Blind via (connects outer layer to inner layer)
        viaType = VIATYPE::BLIND;
    }
    else
    {
        // Buried via (connects inner layers only)
        viaType = VIATYPE::BURIED;
    }

    // Check for microvia (single layer transition)
    if( std::abs( lastLayer - firstLayer ) == 1 )
    {
        if( m_control.microvias_allowed )
        {
            viaType = VIATYPE::MICROVIA;
            // Microvias are typically smaller
            viaDiameter = m_control.via_diameter / 2;
            viaDrill = m_control.via_drill / 2;
        }
    }

    // Create the via
    PCB_VIA* via = new PCB_VIA( m_board );
    via->SetPosition( aViaPoint.position );
    via->SetWidth( PADSTACK::ALL_LAYERS, viaDiameter );
    via->SetDrill( viaDrill );
    via->SetViaType( viaType );
    via->SetLayerPair( viaTopLayer, viaBottomLayer );

    if( net )
        via->SetNet( net );

    // Use commit if available, otherwise add directly
    if( m_commit )
        m_commit->Add( via );
    else
        m_board->Add( via );

    return true;
}


std::string INSERT_CONNECTION::GenerateInsertCode( const ROUTING_PATH& aPath ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.geometry import Vector2\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "# Insert autorouted path\n";
    code << "tracks_added = 0\n";
    code << "vias_added = 0\n";
    code << "\n";

    // Generate code for each segment
    for( const auto& seg : aPath.segments )
    {
        code << GenerateTrackCode( seg );
    }

    // Generate code for vias
    for( const auto& via : aPath.via_locations )
    {
        code << GenerateViaCode( via );
    }

    code << "\n";
    code << "print(json.dumps({'tracks_added': tracks_added, 'vias_added': vias_added}))\n";

    return code.str();
}


std::string INSERT_CONNECTION::GenerateTrackCode( const PATH_SEGMENT& aSegment ) const
{
    std::ostringstream code;

    if( aSegment.points.size() < 2 )
        return "";

    std::string layerName = LayerToName( aSegment.layer );

    code << "# Track segment on " << layerName << "\n";
    code << "points = [\n";

    for( const auto& pt : aSegment.points )
    {
        code << "    Vector2.from_xy(" << pt.x << ", " << pt.y << "),\n";
    }

    code << "]\n";
    code << "try:\n";
    code << "    tracks = board.route_track(\n";
    code << "        points=points,\n";
    code << "        width=" << aSegment.width << ",\n";
    code << "        layer=BoardLayer.BL_" << layerName << ",\n";

    if( !m_netName.empty() )
    {
        code << "        net='" << m_netName << "'\n";
    }
    else
    {
        code << "        net=None\n";
    }

    code << "    )\n";
    code << "    tracks_added += len(tracks)\n";
    code << "except Exception as e:\n";
    code << "    print(f'Track error: {e}')\n";
    code << "\n";

    return code.str();
}


std::string INSERT_CONNECTION::GenerateViaCode( const PATH_POINT& aViaPoint ) const
{
    std::ostringstream code;

    code << "# Via at (" << aViaPoint.position.x / 1000000.0 << "mm, "
         << aViaPoint.position.y / 1000000.0 << "mm)\n";
    code << "try:\n";
    code << "    via = board.add_via(\n";
    code << "        position=Vector2.from_xy(" << aViaPoint.position.x << ", "
         << aViaPoint.position.y << "),\n";
    code << "        diameter=" << m_control.via_diameter << ",\n";
    code << "        drill=" << m_control.via_drill << ",\n";

    if( !m_netName.empty() )
    {
        code << "        net='" << m_netName << "'\n";
    }
    else
    {
        code << "        net=None\n";
    }

    code << "    )\n";
    code << "    vias_added += 1\n";
    code << "except Exception as e:\n";
    code << "    print(f'Via error: {e}')\n";
    code << "\n";

    return code.str();
}


std::string INSERT_CONNECTION::LayerToName( int aLayer ) const
{
    // KiCad layer numbering: F.Cu = 0, In1.Cu = 1, ..., B.Cu = 31 (for 2-layer board)
    // Simplified for now
    switch( aLayer )
    {
    case 0:  return "F_Cu";
    case 1:  return "In1_Cu";
    case 2:  return "In2_Cu";
    case 3:  return "In3_Cu";
    case 4:  return "In4_Cu";
    case 31: return "B_Cu";
    default:
        // For inner layers
        if( aLayer >= 1 && aLayer <= 30 )
            return "In" + std::to_string( aLayer ) + "_Cu";
        return "F_Cu";
    }
}


bool INSERT_CONNECTION::ValidateSegment( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer )
{
    if( !m_board )
        return true;  // Can't validate without board

    int clearance = m_control.clearance;
    int trackWidth = m_control.GetTraceWidth( aLayer );
    int halfWidth = trackWidth / 2 + clearance;

    // Create bounding box for the segment with clearance
    int minX = std::min( aStart.x, aEnd.x ) - halfWidth;
    int maxX = std::max( aStart.x, aEnd.x ) + halfWidth;
    int minY = std::min( aStart.y, aEnd.y ) - halfWidth;
    int maxY = std::max( aStart.y, aEnd.y ) + halfWidth;

    BOX2I segBounds( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );

    // Convert autoroute layer to KiCad layer
    PCB_LAYER_ID pcbLayer = F_Cu;
    int layerCount = m_board->GetCopperLayerCount();
    if( aLayer == 0 )
        pcbLayer = F_Cu;
    else if( aLayer == layerCount - 1 )
        pcbLayer = B_Cu;
    else if( aLayer >= 1 && aLayer < layerCount - 1 )
        pcbLayer = static_cast<PCB_LAYER_ID>( In1_Cu + aLayer - 1 );

    // Check against existing tracks from other nets
    for( PCB_TRACK* track : m_board->Tracks() )
    {
        // Skip tracks from the same net
        if( track->GetNetCode() == m_netCode )
            continue;

        // Skip tracks on different layers
        if( track->GetLayer() != pcbLayer )
            continue;

        // Check if track bounding box intersects our segment
        BOX2I trackBounds = track->GetBoundingBox();
        trackBounds.Inflate( clearance );

        if( !segBounds.Intersects( trackBounds ) )
            continue;

        // More precise check: segment to segment distance
        VECTOR2I trackStart = track->GetStart();
        VECTOR2I trackEnd = track->GetEnd();

        // Calculate minimum distance between segments
        // Using simplified approach: check if segments intersect
        auto ccw = []( const VECTOR2I& A, const VECTOR2I& B, const VECTOR2I& C ) -> bool
        {
            return (int64_t)(C.y - A.y) * (B.x - A.x) > (int64_t)(B.y - A.y) * (C.x - A.x);
        };

        auto segmentsIntersect = [&ccw]( const VECTOR2I& A, const VECTOR2I& B,
                                          const VECTOR2I& C, const VECTOR2I& D ) -> bool
        {
            return ccw(A, C, D) != ccw(B, C, D) && ccw(A, B, C) != ccw(A, B, D);
        };

        if( segmentsIntersect( aStart, aEnd, trackStart, trackEnd ) )
        {
            return false;  // Collision detected
        }
    }

    // Check keepout zones
    if( SegmentCrossesKeepout( aStart, aEnd, aLayer ) )
    {
        return false;
    }

    // Check against pads from other nets
    if( SegmentCrossesPad( aStart, aEnd, aLayer ) )
    {
        return false;
    }

    return true;  // No collision
}


bool INSERT_CONNECTION::IsInKeepoutZone( const VECTOR2I& aPoint, int aLayer )
{
    if( !m_board )
        return false;

    // Convert to PCB layer
    PCB_LAYER_ID pcbLayer = F_Cu;
    int layerCount = m_board->GetCopperLayerCount();
    if( aLayer == 0 )
        pcbLayer = F_Cu;
    else if( aLayer == layerCount - 1 )
        pcbLayer = B_Cu;
    else if( aLayer >= 1 && aLayer < layerCount - 1 )
        pcbLayer = static_cast<PCB_LAYER_ID>( In1_Cu + aLayer - 1 );

    for( ZONE* zone : m_board->Zones() )
    {
        if( !zone->GetIsRuleArea() )
            continue;

        if( !zone->GetDoNotAllowTracks() )
            continue;

        if( !zone->GetLayerSet().test( pcbLayer ) )
            continue;

        // Check if point is inside the zone outline
        if( zone->Outline()->Contains( aPoint ) )
        {
            return true;
        }
    }

    return false;
}


bool INSERT_CONNECTION::SegmentCrossesKeepout( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                                int aLayer )
{
    if( !m_board )
        return false;

    // Convert to PCB layer
    PCB_LAYER_ID pcbLayer = F_Cu;
    int layerCount = m_board->GetCopperLayerCount();
    if( aLayer == 0 )
        pcbLayer = F_Cu;
    else if( aLayer == layerCount - 1 )
        pcbLayer = B_Cu;
    else if( aLayer >= 1 && aLayer < layerCount - 1 )
        pcbLayer = static_cast<PCB_LAYER_ID>( In1_Cu + aLayer - 1 );

    for( ZONE* zone : m_board->Zones() )
    {
        if( !zone->GetIsRuleArea() )
            continue;

        if( !zone->GetDoNotAllowTracks() )
            continue;

        if( !zone->GetLayerSet().test( pcbLayer ) )
            continue;

        // Check if either endpoint is inside the zone
        const SHAPE_POLY_SET* outline = zone->Outline();
        if( outline->Contains( aStart ) || outline->Contains( aEnd ) )
        {
            return true;
        }

        // Check if segment crosses zone boundary
        // Create a segment and check intersection with zone outline
        SEG seg( aStart, aEnd );

        // Iterate through zone outline edges
        for( int i = 0; i < outline->OutlineCount(); ++i )
        {
            const SHAPE_LINE_CHAIN& chain = outline->Outline( i );
            for( int j = 0; j < chain.SegmentCount(); ++j )
            {
                SEG zoneSeg = chain.CSegment( j );
                if( seg.Intersects( zoneSeg ) )
                {
                    return true;
                }
            }
        }
    }

    return false;
}


PAD* INSERT_CONNECTION::FindPadAt( const VECTOR2I& aPoint, int aLayer )
{
    if( !m_board )
        return nullptr;

    // Convert to PCB layer
    PCB_LAYER_ID pcbLayer = F_Cu;
    int layerCount = m_board->GetCopperLayerCount();
    if( aLayer == 0 )
        pcbLayer = F_Cu;
    else if( aLayer == layerCount - 1 )
        pcbLayer = B_Cu;
    else if( aLayer >= 1 && aLayer < layerCount - 1 )
        pcbLayer = static_cast<PCB_LAYER_ID>( In1_Cu + aLayer - 1 );

    // Search for pads at this position
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            // Check if pad is on this layer
            if( !pad->GetLayerSet().test( pcbLayer ) )
                continue;

            // Check if point is within pad bounds
            BOX2I padBox = pad->GetBoundingBox();
            if( padBox.Contains( aPoint ) )
            {
                return pad;
            }
        }
    }

    return nullptr;
}


int INSERT_CONNECTION::CalculateNeckdownWidth( const VECTOR2I& aPoint, int aLayer, int aCurrentWidth )
{
    PAD* pad = FindPadAt( aPoint, aLayer );
    if( !pad )
        return aCurrentWidth;  // No pad nearby, use current width

    // Get pad size
    VECTOR2I padSize = pad->GetSize( PADSTACK::ALL_LAYERS );
    int minPadDimension = std::min( padSize.x, padSize.y );

    // Calculate clearance from pad edge
    int clearance = m_control.clearance;

    // Maximum track width that can connect to pad
    // Width should be less than pad size minus clearance on each side
    int maxWidth = minPadDimension - 2 * clearance;

    // Don't allow too narrow traces (minimum reasonable width)
    int minWidth = m_control.clearance / 2;

    if( maxWidth < minWidth )
        maxWidth = minWidth;

    // If current width fits, no neckdown needed
    if( aCurrentWidth <= maxWidth )
        return aCurrentWidth;

    // Calculate neckdown width
    // Use the maximum allowed width, but not less than minimum
    return std::max( maxWidth, minWidth );
}


int INSERT_CONNECTION::CalculateNeckdownDistance( PAD* aPad, int aTrackWidth )
{
    if( !aPad )
        return 0;

    // Distance from pad center where neckdown should begin
    // This is typically 2-3x the track width from pad edge
    VECTOR2I padSize = aPad->GetSize( PADSTACK::ALL_LAYERS );
    int padRadius = std::min( padSize.x, padSize.y ) / 2;

    // Neckdown starts at pad radius + 2x track width
    return padRadius + aTrackWidth * 2;
}


bool INSERT_CONNECTION::SegmentCrossesPad( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer )
{
    if( !m_board )
        return false;

    int clearance = m_control.clearance;
    int trackWidth = m_control.GetTraceWidth( aLayer );
    int halfWidth = trackWidth / 2;

    // Convert to PCB layer
    PCB_LAYER_ID pcbLayer = F_Cu;
    int layerCount = m_board->GetCopperLayerCount();
    if( aLayer == 0 )
        pcbLayer = F_Cu;
    else if( aLayer == layerCount - 1 )
        pcbLayer = B_Cu;
    else if( aLayer >= 1 && aLayer < layerCount - 1 )
        pcbLayer = static_cast<PCB_LAYER_ID>( In1_Cu + aLayer - 1 );

    // Create segment for intersection testing
    SEG trackSeg( aStart, aEnd );

    // Check all pads
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            // Skip pads from the same net
            if( pad->GetNetCode() == m_netCode )
                continue;

            // Skip pads not on this layer
            if( !pad->GetLayerSet().test( pcbLayer ) )
                continue;

            // Get pad bounding box with clearance
            BOX2I padBox = pad->GetBoundingBox();
            padBox.Inflate( clearance + halfWidth );

            // Quick bounding box check
            BOX2I segBox( aStart, VECTOR2I( 0, 0 ) );
            segBox.Merge( aEnd );
            segBox.Inflate( halfWidth );

            if( !padBox.Intersects( segBox ) )
                continue;

            // More precise check: does segment pass through pad area?
            // Check if segment endpoints are inside pad (with clearance)
            VECTOR2I padCenter = pad->GetPosition();
            VECTOR2I padSize = pad->GetSize( PADSTACK::ALL_LAYERS );
            int padHalfW = padSize.x / 2 + clearance + halfWidth;
            int padHalfH = padSize.y / 2 + clearance + halfWidth;

            // Simple rectangle check (works for rectangular pads)
            auto pointInPad = [&]( const VECTOR2I& pt ) -> bool
            {
                return std::abs( pt.x - padCenter.x ) < padHalfW &&
                       std::abs( pt.y - padCenter.y ) < padHalfH;
            };

            if( pointInPad( aStart ) || pointInPad( aEnd ) )
                return true;

            // Check if segment crosses pad edges
            // Create rectangle edges and check intersection
            VECTOR2I corners[4] = {
                VECTOR2I( padCenter.x - padHalfW, padCenter.y - padHalfH ),
                VECTOR2I( padCenter.x + padHalfW, padCenter.y - padHalfH ),
                VECTOR2I( padCenter.x + padHalfW, padCenter.y + padHalfH ),
                VECTOR2I( padCenter.x - padHalfW, padCenter.y + padHalfH )
            };

            for( int i = 0; i < 4; ++i )
            {
                SEG padEdge( corners[i], corners[( i + 1 ) % 4] );
                if( trackSeg.Intersects( padEdge ) )
                    return true;
            }
        }
    }

    return false;
}
