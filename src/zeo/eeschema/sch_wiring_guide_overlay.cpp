/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "sch_wiring_guide_overlay.h"
#include "sch_wiring_guide_manager.h"
#include "sch_edit_frame.h"
#include <gal/graphics_abstraction_layer.h>
#include <view/view.h>
#include <base_units.h>
#include <wx/menu.h>

// Static color definitions - light blue for visibility
const KIGFX::COLOR4D SCH_WIRING_GUIDE_OVERLAY::GUIDE_COLOR( 0.3, 0.6, 1.0, 0.7 );
const KIGFX::COLOR4D SCH_WIRING_GUIDE_OVERLAY::GUIDE_COLOR_HOVER( 0.2, 0.5, 1.0, 0.95 );
const KIGFX::COLOR4D SCH_WIRING_GUIDE_OVERLAY::ENDPOINT_COLOR( 0.3, 0.6, 1.0, 0.8 );

// Colors for active wiring highlighting (like PCB ratsnest)
const KIGFX::COLOR4D SCH_WIRING_GUIDE_OVERLAY::GUIDE_COLOR_ACTIVE( 0.2, 0.5, 1.0, 0.95 );      // Bright - actively routing this
const KIGFX::COLOR4D SCH_WIRING_GUIDE_OVERLAY::GUIDE_COLOR_DIMMED( 0.3, 0.6, 1.0, 0.25 );     // Faded - not the active guide
const KIGFX::COLOR4D SCH_WIRING_GUIDE_OVERLAY::ENDPOINT_COLOR_DIMMED( 0.3, 0.6, 1.0, 0.2 );   // Faded endpoints


SCH_WIRING_GUIDE_OVERLAY::SCH_WIRING_GUIDE_OVERLAY( SCH_WIRING_GUIDE_MANAGER* aManager,
                                                      SCH_EDIT_FRAME* aFrame ) :
    SIMPLE_OVERLAY_ITEM(),
    m_manager( aManager ),
    m_frame( aFrame ),
    m_hoveredIndex( -1 )
{
    SetStrokeColor( GUIDE_COLOR );
    SetLineWidth( schIUScale.mmToIU( GUIDE_WIDTH ) );
}


SCH_WIRING_GUIDE_OVERLAY::~SCH_WIRING_GUIDE_OVERLAY()
{
}


const BOX2I SCH_WIRING_GUIDE_OVERLAY::ViewBBox() const
{
    // Return maximum possible bbox so we're always drawn
    return BOX2I( VECTOR2I( std::numeric_limits<int>::min() / 2,
                            std::numeric_limits<int>::min() / 2 ),
                  VECTOR2I( std::numeric_limits<int>::max(),
                            std::numeric_limits<int>::max() ) );
}


void SCH_WIRING_GUIDE_OVERLAY::ViewDraw( int aLayer, KIGFX::VIEW* aView ) const
{
    // Let the base class set up GAL
    SIMPLE_OVERLAY_ITEM::ViewDraw( aLayer, aView );
}


void SCH_WIRING_GUIDE_OVERLAY::drawPreviewShape( KIGFX::VIEW* aView ) const
{
    if( !m_manager || !m_frame )
        return;

    // Don't render during diff preview - DIFF_OVERLAY_ITEM handles wire guides then.
    // This overlay only renders guides for APPROVED symbols (after accept).
    if( m_frame->HasAgentPendingChanges() )
        return;

    KIGFX::GAL* gal = aView->GetGAL();
    if( !gal )
        return;

    // Refresh guide positions from current symbol locations for real-time tracking
    // during drag operations (const_cast is safe here as we're just updating cached positions)
    const_cast<SCH_WIRING_GUIDE_MANAGER*>( m_manager )->RefreshGuidePositions();

    double scale = aView->GetGAL()->GetWorldScale();
    auto guides = m_manager->GetActiveGuides();

    // Check if there's an active wiring position - if so, highlight one guide and dim others
    bool hasActiveWiring = m_manager->HasActiveWiringPosition();
    int activeGuideIndex = hasActiveWiring ? m_manager->GetActiveGuideIndex( guides ) : -1;

    // Only apply active/dimmed states if we found a matching guide
    // If no guide matches the active position, show all guides normally
    bool applyActiveHighlight = hasActiveWiring && activeGuideIndex >= 0;

    for( size_t i = 0; i < guides.size(); ++i )
    {
        const auto& guide = guides[i];
        int idx = static_cast<int>( i );

        // Determine the visual state for this guide
        GUIDE_STATE state;
        if( idx == m_hoveredIndex )
        {
            state = GUIDE_STATE::HOVERED;
        }
        else if( applyActiveHighlight )
        {
            // When actively wiring, highlight the matching guide and dim others
            state = ( idx == activeGuideIndex ) ? GUIDE_STATE::ACTIVE : GUIDE_STATE::DIMMED;
        }
        else
        {
            state = GUIDE_STATE::NORMAL;
        }

        drawGuideLine( gal, guide.sourcePos, guide.targetPos, state, scale );
        drawEndpointMarker( gal, guide.sourcePos, state, scale );
        drawEndpointMarker( gal, guide.targetPos, state, scale );
    }

    // Draw hover tooltip if applicable
    if( m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>( guides.size() ) )
    {
        drawHoverTooltip( gal, m_hoveredIndex, scale );
    }
}


void SCH_WIRING_GUIDE_OVERLAY::drawGuideLine( KIGFX::GAL* aGal, const VECTOR2I& aStart,
                                               const VECTOR2I& aEnd, GUIDE_STATE aState,
                                               double aScale ) const
{
    KIGFX::COLOR4D color;
    double lineWidth;

    switch( aState )
    {
    case GUIDE_STATE::HOVERED:
        color = GUIDE_COLOR_HOVER;
        lineWidth = GUIDE_WIDTH * 1.5;
        break;
    case GUIDE_STATE::ACTIVE:
        color = GUIDE_COLOR_ACTIVE;
        lineWidth = GUIDE_WIDTH * 1.5;
        break;
    case GUIDE_STATE::DIMMED:
        color = GUIDE_COLOR_DIMMED;
        lineWidth = GUIDE_WIDTH;
        break;
    case GUIDE_STATE::NORMAL:
    default:
        color = GUIDE_COLOR;
        lineWidth = GUIDE_WIDTH;
        break;
    }

    aGal->SetIsStroke( true );
    aGal->SetIsFill( false );
    aGal->SetStrokeColor( color );
    aGal->SetLineWidth( schIUScale.mmToIU( lineWidth ) );

    // Draw dashed line
    double dashLen = schIUScale.mmToIU( GUIDE_DASH );
    double gapLen = schIUScale.mmToIU( GUIDE_GAP );

    drawDashedLine( aGal, VECTOR2D( aStart ), VECTOR2D( aEnd ), dashLen, gapLen );
}


void SCH_WIRING_GUIDE_OVERLAY::drawDashedLine( KIGFX::GAL* aGal, const VECTOR2D& aStart,
                                                const VECTOR2D& aEnd, double aDash,
                                                double aGap ) const
{
    VECTOR2D delta = aEnd - aStart;
    double length = delta.EuclideanNorm();

    if( length < 1 )
        return;

    VECTOR2D dir = delta / length;
    double pos = 0;

    while( pos < length )
    {
        VECTOR2D dashStart = aStart + dir * pos;
        double dashEnd = std::min( pos + aDash, length );
        VECTOR2D dashEndPt = aStart + dir * dashEnd;

        aGal->DrawLine( dashStart, dashEndPt );
        pos = dashEnd + aGap;
    }
}


void SCH_WIRING_GUIDE_OVERLAY::drawEndpointMarker( KIGFX::GAL* aGal, const VECTOR2I& aPos,
                                                    GUIDE_STATE aState, double aScale ) const
{
    KIGFX::COLOR4D color;

    switch( aState )
    {
    case GUIDE_STATE::HOVERED:
        color = GUIDE_COLOR_HOVER;
        break;
    case GUIDE_STATE::ACTIVE:
        color = GUIDE_COLOR_ACTIVE;
        break;
    case GUIDE_STATE::DIMMED:
        color = ENDPOINT_COLOR_DIMMED;
        break;
    case GUIDE_STATE::NORMAL:
    default:
        color = ENDPOINT_COLOR;
        break;
    }

    double radius = schIUScale.mmToIU( ENDPOINT_RADIUS );

    aGal->SetIsStroke( true );
    aGal->SetIsFill( true );
    aGal->SetStrokeColor( color );
    aGal->SetFillColor( color.WithAlpha( 0.3 ) );
    aGal->SetLineWidth( schIUScale.mmToIU( 0.08 ) );

    aGal->DrawCircle( VECTOR2D( aPos ), radius );
}


void SCH_WIRING_GUIDE_OVERLAY::drawHoverTooltip( KIGFX::GAL* aGal, int aGuideIndex,
                                                  double aScale ) const
{
    // Tooltip rendering would go here
    // For now, we'll skip the tooltip and rely on context menu
    // A proper implementation would use GAL text rendering
}


int SCH_WIRING_GUIDE_OVERLAY::HitTestGuide( const VECTOR2I& aPos ) const
{
    if( !m_manager )
        return -1;

    auto guides = m_manager->GetActiveGuides();
    double margin = schIUScale.mmToIU( HIT_TEST_MARGIN );

    for( size_t i = 0; i < guides.size(); ++i )
    {
        const auto& guide = guides[i];

        // Check if point is near the line segment
        VECTOR2D start( guide.sourcePos );
        VECTOR2D end( guide.targetPos );
        VECTOR2D point( aPos );

        // Calculate distance from point to line segment
        VECTOR2D line = end - start;
        double lineLen = line.EuclideanNorm();

        if( lineLen < 1 )
            continue;

        VECTOR2D lineDir = line / lineLen;
        VECTOR2D toPoint = point - start;

        double projection = toPoint.Dot( lineDir );
        projection = std::max( 0.0, std::min( projection, lineLen ) );

        VECTOR2D closest = start + lineDir * projection;
        double distance = ( point - closest ).EuclideanNorm();

        if( distance <= margin )
            return static_cast<int>( i );
    }

    return -1;
}


bool SCH_WIRING_GUIDE_OVERLAY::SetHoveredGuide( int aIndex )
{
    if( m_hoveredIndex != aIndex )
    {
        m_hoveredIndex = aIndex;
        return true;
    }
    return false;
}


void SCH_WIRING_GUIDE_OVERLAY::ShowContextMenu( int aGuideIndex, const wxPoint& aScreenPos )
{
    if( !m_manager || !m_frame )
        return;

    auto guides = m_manager->GetActiveGuides();
    if( aGuideIndex < 0 || aGuideIndex >= static_cast<int>( guides.size() ) )
        return;

    const auto& guide = guides[aGuideIndex];

    wxMenu menu;

    // Wire This Connection
    wxString wireLabel = wxString::Format( _( "Wire %s:%s to %s" ),
                                           guide.sourceRef, guide.sourcePin, guide.targetRef );
    menu.Append( 1, wireLabel );

    menu.AppendSeparator();

    // Dismiss This Recommendation
    menu.Append( 2, _( "Dismiss This Recommendation" ) );

    // Dismiss All for Symbol
    wxString dismissAllLabel = wxString::Format( _( "Dismiss All for %s" ), guide.sourceRef );
    menu.Append( 3, dismissAllLabel );

    menu.AppendSeparator();

    // Edit Properties
    wxString propsLabel = wxString::Format( _( "Edit %s Properties..." ), guide.sourceRef );
    menu.Append( 4, propsLabel );

    int selection = m_frame->GetPopupMenuSelectionFromUser( menu, aScreenPos );

    switch( selection )
    {
    case 1: // Wire This Connection
        // TODO: Start wire tool at source pin position
        // m_frame->GetToolManager()->RunAction( EE_ACTIONS::drawWire );
        break;

    case 2: // Dismiss This Recommendation
        m_manager->DismissGuide( guide.sourceSymbolId, guide.sourcePin );
        break;

    case 3: // Dismiss All for Symbol
        m_manager->DismissAllForSymbol( guide.sourceSymbolId );
        break;

    case 4: // Edit Properties
        // TODO: Open symbol properties dialog
        // m_frame->EditSymbolProperties( guide.sourceSymbolId );
        break;

    default:
        break;
    }
}


void SCH_WIRING_GUIDE_OVERLAY::Refresh()
{
    // The manager already has the latest data
    // Just trigger a view update
    if( m_frame && m_frame->GetCanvas() )
    {
        m_frame->GetCanvas()->GetView()->Update( this );
        m_frame->GetCanvas()->Refresh();
    }
}
