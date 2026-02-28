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

    // Draw per-item highlights if we have a callback
    // Style matches the VCS diff view (SCH_DIFF_HIGHLIGHT_ITEM):
    //   - 20% opacity fill, 85% opacity border, 3-mil border width
    //   - Wires: fill only (no border) to avoid cluttered box grid
    if( m_itemHighlightsCallback )
    {
        std::vector<ITEM_HIGHLIGHT> highlights = m_itemHighlightsCallback();

        // Two-pass rendering: fills first, then borders on top.

        // Pass 1: fills
        gal->SetIsStroke( false );
        gal->SetLineWidth( 0 );
        gal->SetIsFill( true );

        for( const ITEM_HIGHLIGHT& hl : highlights )
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

        for( const ITEM_HIGHLIGHT& hl : highlights )
        {
            if( !hl.hasBorder || hl.bbox.GetWidth() <= 0 || hl.bbox.GetHeight() <= 0 )
                continue;

            gal->SetStrokeColor( hl.color.WithAlpha( DIFF_BORDER_ALPHA ) );
            gal->DrawRectangle( VECTOR2D( hl.bbox.GetLeft(),  hl.bbox.GetTop() ),
                                VECTOR2D( hl.bbox.GetRight(), hl.bbox.GetBottom() ) );
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
