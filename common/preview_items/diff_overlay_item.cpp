/*
 * This program source code file is part of KICAD, a free EDA CAD application.
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

#include <preview_items/diff_overlay_item.h>
#include <diff_manager.h>
#include <gal/graphics_abstraction_layer.h>
#include <view/view.h>
#include <geometry/eda_angle.h> // For EDA_ANGLE
#include <font/font.h>
#include <font/text_attributes.h>


using namespace KIGFX::PREVIEW;

DIFF_OVERLAY_ITEM::DIFF_OVERLAY_ITEM( const BOX2I& aBBox ) :
        m_bbox( aBBox ),
        m_bboxCallback( nullptr )
{
    // Default style (used for overall bbox when no per-item highlights)
    SetStrokeColor( COLOR4D( 0.0, 0.8, 0.0, 1.0 ) ); // Green stroke
    SetFillColor( COLOR4D( 0.0, 0.8, 0.0, 0.1 ) );   // Faint green fill
    SetLineWidth( 2.0 );
}

DIFF_OVERLAY_ITEM::DIFF_OVERLAY_ITEM( BBOX_CALLBACK aBBoxCallback ) :
        m_bboxCallback( aBBoxCallback )
{
    // Compute initial bbox
    if( m_bboxCallback )
        m_bbox = m_bboxCallback();

    // Default style
    SetStrokeColor( COLOR4D( 0.0, 0.8, 0.0, 1.0 ) ); // Green stroke
    SetFillColor( COLOR4D( 0.0, 0.8, 0.0, 0.1 ) );   // Faint green fill
    SetLineWidth( 2.0 );
}

BOX2I DIFF_OVERLAY_ITEM::GetCurrentBBox() const
{
    if( m_bboxCallback )
        m_bbox = m_bboxCallback();
    return m_bbox;
}

void DIFF_OVERLAY_ITEM::ViewDraw( int aLayer, KIGFX::VIEW* aView ) const
{
    // Recompute bbox before drawing if we have a callback
    if( m_bboxCallback )
        m_bbox = m_bboxCallback();

    // We delegate to SIMPLE_OVERLAY_ITEM for setup, then drawPreviewShape
    SIMPLE_OVERLAY_ITEM::ViewDraw( aLayer, aView );
}

const BOX2I DIFF_OVERLAY_ITEM::ViewBBox() const
{
    // Return a very large bounding box to ensure the overlay is never culled
    BOX2I bbox;
    bbox.SetMaximum();
    return bbox;
}

void DIFF_OVERLAY_ITEM::drawPreviewShape( KIGFX::VIEW* aView ) const
{
    KIGFX::GAL* gal = aView->GetGAL();

    // Draw wiring guide previews first (underneath everything else)
    drawWiringGuides( gal );

    // Draw per-item highlights if we have a callback
    // Style matches the VCS diff view (SCH_DIFF_HIGHLIGHT_ITEM):
    //   - 20% opacity fill, 85% opacity border, 3-mil border width
    //   - Wires: fill only (no border) to avoid cluttered box grid
    if( m_itemHighlightsCallback )
    {
        m_cachedHighlights = m_itemHighlightsCallback();

        // Two-pass rendering: fills first, then borders on top.

        // Pass 1: fills
        gal->SetIsStroke( false );
        gal->SetLineWidth( 0 );
        gal->SetIsFill( true );

        for( const ITEM_HIGHLIGHT& hl : m_cachedHighlights )
        {
            if( hl.bbox.GetWidth() <= 0 || hl.bbox.GetHeight() <= 0 )
                continue;

            gal->SetFillColor( hl.color.WithAlpha( DIFF_FILL_ALPHA ) );
            gal->DrawRectangle( VECTOR2D( hl.bbox.GetLeft(),  hl.bbox.GetTop() ),
                                VECTOR2D( hl.bbox.GetRight(), hl.bbox.GetBottom() ) );
        }

        // Pass 2: borders (world-coordinate width, scales with zoom)
        // 10 mils in schematic IU: 10 * 254 = 2540
        double borderWidth = 2540;

        gal->SetIsFill( false );
        gal->SetIsStroke( true );
        gal->SetLineWidth( borderWidth );

        for( size_t i = 0; i < m_cachedHighlights.size(); ++i )
        {
            const ITEM_HIGHLIGHT& hl = m_cachedHighlights[i];

            if( !hl.hasBorder || hl.bbox.GetWidth() <= 0 || hl.bbox.GetHeight() <= 0 )
                continue;

            // Brighten border if this highlight is hovered
            double alpha = ( (int) i == m_hoveredHighlightIndex )
                               ? std::min( DIFF_BORDER_ALPHA + 0.25, 1.0 )
                               : DIFF_BORDER_ALPHA;
            gal->SetStrokeColor( hl.color.WithAlpha( alpha ) );
            gal->DrawRectangle( VECTOR2D( hl.bbox.GetLeft(),  hl.bbox.GetTop() ),
                                VECTOR2D( hl.bbox.GetRight(), hl.bbox.GetBottom() ) );
        }

        // Draw per-item approve/reject buttons on hovered highlight.
        // Advance depth so buttons render on top of all highlight fills/borders.
        if( m_hoveredHighlightIndex >= 0
            && m_hoveredHighlightIndex < (int) m_cachedHighlights.size() )
        {
            gal->AdvanceDepth();

            double scale = 1.0 / gal->GetWorldScale();
            const ITEM_HIGHLIGHT& hovered = m_cachedHighlights[m_hoveredHighlightIndex];

            drawItemButton( gal, hovered.bbox, 0, wxT( "Approve" ),
                            COLOR4D( 0.0, 0.6, 0.0, 0.9 ), scale );
            drawItemButton( gal, hovered.bbox, 1, wxT( "Reject" ),
                            COLOR4D( 0.8, 0.0, 0.0, 0.9 ), scale );
        }
    }
    else
    {
        // Fallback: draw the overall bounding box
        gal->DrawRectangle( m_bbox.GetPosition(), m_bbox.GetEnd() );
    }

    if( !m_showButtons )
        return;

    double scale = 1.0 / gal->GetWorldScale();

    // Agent diff buttons: View Diff (dark), Approve (green), Reject (red)
    drawButton( gal, 0, "View Diff", COLOR4D( 0.2, 0.2, 0.2, 0.8 ), scale );
    drawButton( gal, 1, "Approve", COLOR4D( 0.0, 0.6, 0.0, 0.9 ), scale );
    drawButton( gal, 2, "Reject",  COLOR4D( 0.8, 0.0, 0.0, 0.9 ), scale );
}

BOX2I DIFF_OVERLAY_ITEM::getButtonRect( int aIndex, double aScale ) const
{
    double p_width = 80.0;
    double p_height = 20.0;
    double p_margin = 5.0;

    // Convert pixels to world units
    double w_width = p_width * aScale;
    double w_height = p_height * aScale;
    double w_margin = p_margin * aScale;

    // Start at Top Right
    VECTOR2I startPos = m_bbox.GetPosition();
    startPos.x += m_bbox.GetWidth() - ( ( aIndex + 1 ) * ( w_width + w_margin ) );
    startPos.y -= ( w_height + w_margin ); // Above box

    return BOX2I( startPos, VECTOR2I( w_width, w_height ) );
}

void DIFF_OVERLAY_ITEM::drawButton( KIGFX::GAL* aGal, int aIndex, const wxString& aLabel, const COLOR4D& aColor,
                                    double aScale ) const
{
    BOX2I rect = getButtonRect( aIndex, aScale );

    // Draw button background
    aGal->SetIsFill( true );
    aGal->SetIsStroke( true );
    aGal->SetFillColor( aColor );
    aGal->SetStrokeColor( COLOR4D( 1.0, 1.0, 1.0, 1.0 ) );
    aGal->SetLineWidth( 1.0 * aScale ); // Constant stroke width

    aGal->DrawRectangle( rect.GetPosition(), rect.GetEnd() );

    // Advance layer depth so text renders on top of button fill
    aGal->AdvanceDepth();

    // Draw Text using font->Draw() for reliable rendering
    KIFONT::FONT* font = KIFONT::FONT::GetFont();

    // Calculate text size - use pixels converted to world units
    double textHeightPx = 12.0;  // 12 pixel text height
    double textHeight = textHeightPx * aScale;
    double strokeWidth = textHeight * 0.2;  // 20% stroke width like other KiCad text

    TEXT_ATTRIBUTES textAttrs;
    textAttrs.m_Size = VECTOR2I( textHeight * 0.9, textHeight );
    textAttrs.m_StrokeWidth = strokeWidth;
    textAttrs.m_Halign = GR_TEXT_H_ALIGN_CENTER;
    textAttrs.m_Valign = GR_TEXT_V_ALIGN_CENTER;

    // Set GAL state for text drawing
    aGal->SetIsFill( false );
    aGal->SetIsStroke( true );
    aGal->SetStrokeColor( COLOR4D( 1.0, 1.0, 1.0, 1.0 ) ); // White text
    aGal->SetLineWidth( strokeWidth );

    font->Draw( aGal, aLabel, rect.Centre(), textAttrs, KIFONT::METRICS::Default() );
}

DIFF_OVERLAY_ITEM::BUTTON_ID DIFF_OVERLAY_ITEM::HitTestButtons( const VECTOR2I& aPoint, KIGFX::VIEW* aView ) const
{
    if( !aView )
        return BTN_NONE;

    KIGFX::GAL* gal = aView->GetGAL();
    if( !gal )
        return BTN_NONE;

    // Buttons are only visible when m_showButtons is true
    if( !m_showButtons )
        return BTN_NONE;

    double scale = 1.0 / gal->GetWorldScale();

    for( int i = 0; i < 3; ++i )
    {
        BOX2I rect = getButtonRect( i, scale );
        if( rect.Contains( aPoint ) )
        {
            switch( i )
            {
            case 0: return BTN_VIEW_DIFF;
            case 1: return BTN_APPROVE;
            case 2: return BTN_REJECT;
            }
        }
    }
    return BTN_NONE;
}


bool DIFF_OVERLAY_ITEM::SetHoveredHighlightIndex( int aIndex )
{
    if( aIndex == m_hoveredHighlightIndex )
        return false;

    m_hoveredHighlightIndex = aIndex;
    return true;
}


BOX2I DIFF_OVERLAY_ITEM::getItemButtonRect( const BOX2I& aItemBBox, int aButtonIndex,
                                             double aScale ) const
{
    double w_width  = IBTN_WIDTH  * aScale;
    double w_height = IBTN_HEIGHT * aScale;

    // Position buttons above the item bbox border with a small gap.
    // The border is drawn centered on the edge at 2540 IU width, so half (1270)
    // extends above GetTop(). Use world coords for the gap so it tracks the border.
    // Approve=0 (left), Reject=1 (right). Buttons are touching edge-to-edge.
    double borderOffset = 2540.0 / 2.0 + 1270.0;  // half border + small margin (world coords)

    VECTOR2I startPos;
    startPos.x = aItemBBox.GetRight() - ( 2 - aButtonIndex ) * w_width;
    startPos.y = aItemBBox.GetTop() - w_height - static_cast<int>( borderOffset );

    return BOX2I( startPos, VECTOR2I( w_width, w_height ) );
}


void DIFF_OVERLAY_ITEM::drawItemButton( KIGFX::GAL* aGal, const BOX2I& aItemBBox, int aIndex,
                                         const wxString& aLabel, const COLOR4D& aColor,
                                         double aScale ) const
{
    BOX2I rect = getItemButtonRect( aItemBBox, aIndex, aScale );

    // Draw button background
    aGal->SetIsFill( true );
    aGal->SetIsStroke( true );
    aGal->SetFillColor( aColor );
    aGal->SetStrokeColor( COLOR4D( 1.0, 1.0, 1.0, 1.0 ) );
    aGal->SetLineWidth( 1.0 * aScale );

    aGal->DrawRectangle( rect.GetPosition(), rect.GetEnd() );

    // Advance layer depth so text renders on top of button fill
    aGal->AdvanceDepth();

    KIFONT::FONT* font = KIFONT::FONT::GetFont();

    double textHeightPx = 11.0;
    double textHeight = textHeightPx * aScale;
    double strokeWidth = textHeight * 0.2;

    TEXT_ATTRIBUTES textAttrs;
    textAttrs.m_Size = VECTOR2I( textHeight * 0.9, textHeight );
    textAttrs.m_StrokeWidth = strokeWidth;
    textAttrs.m_Halign = GR_TEXT_H_ALIGN_CENTER;
    textAttrs.m_Valign = GR_TEXT_V_ALIGN_CENTER;

    aGal->SetIsFill( false );
    aGal->SetIsStroke( true );
    aGal->SetStrokeColor( COLOR4D( 1.0, 1.0, 1.0, 1.0 ) );
    aGal->SetLineWidth( strokeWidth );

    font->Draw( aGal, aLabel, rect.Centre(), textAttrs, KIFONT::METRICS::Default() );
}


DIFF_OVERLAY_ITEM::ITEM_BUTTON_ID DIFF_OVERLAY_ITEM::HitTestItemButtons(
        const VECTOR2I& aPoint, KIGFX::VIEW* aView ) const
{
    if( !aView || m_hoveredHighlightIndex < 0
        || m_hoveredHighlightIndex >= (int) m_cachedHighlights.size() )
    {
        return IBTN_NONE;
    }

    KIGFX::GAL* gal = aView->GetGAL();
    if( !gal )
        return IBTN_NONE;

    double scale = 1.0 / gal->GetWorldScale();
    const BOX2I& itemBBox = m_cachedHighlights[m_hoveredHighlightIndex].bbox;

    for( int i = 0; i < 2; ++i )
    {
        BOX2I rect = getItemButtonRect( itemBBox, i, scale );
        if( rect.Contains( aPoint ) )
        {
            switch( i )
            {
            case 0: return IBTN_APPROVE;
            case 1: return IBTN_REJECT;
            }
        }
    }

    return IBTN_NONE;
}


void DIFF_OVERLAY_ITEM::drawWiringGuides( KIGFX::GAL* aGal ) const
{
    // Get guides from callback or stored list
    std::vector<WIRING_GUIDE_PREVIEW> guides;

    if( m_wiringGuidesCallback )
        guides = m_wiringGuidesCallback();
    else
        guides = m_wiringGuides;

    if( guides.empty() )
        return;

    // Schematic IU scale: approximately 10,000 IU per mm
    // (Verified from bbox values: position 80mm ≈ 800,000 IU)
    constexpr double IU_PER_MM = 10000.0;

    // Light blue color for visibility
    const COLOR4D guideColor( 0.3, 0.6, 1.0, 0.7 );

    aGal->SetIsStroke( true );
    aGal->SetIsFill( false );
    aGal->SetStrokeColor( guideColor );
    aGal->SetLineWidth( GUIDE_LINE_WIDTH * IU_PER_MM );

    double dashLen = GUIDE_DASH_LEN * IU_PER_MM;
    double gapLen = GUIDE_GAP_LEN * IU_PER_MM;

    for( const auto& guide : guides )
    {
        drawDashedLine( aGal, VECTOR2D( guide.start ), VECTOR2D( guide.end ), dashLen, gapLen );

        // Draw small endpoint markers
        double radius = 0.3 * IU_PER_MM;  // 0.3mm radius circles
        aGal->SetIsFill( true );
        aGal->SetFillColor( guideColor.WithAlpha( 0.5 ) );
        aGal->DrawCircle( VECTOR2D( guide.start ), radius );
        aGal->DrawCircle( VECTOR2D( guide.end ), radius );
        aGal->SetIsFill( false );
    }
}


void DIFF_OVERLAY_ITEM::drawDashedLine( KIGFX::GAL* aGal, const VECTOR2D& aStart,
                                         const VECTOR2D& aEnd, double aDash, double aGap ) const
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
