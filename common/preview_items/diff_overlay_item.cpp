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
#include <gal/graphics_abstraction_layer.h>
#include <view/view.h>
#include <geometry/eda_angle.h> // For EDA_ANGLE
#include <font/font.h>
#include <font/text_attributes.h>


using namespace KIGFX::PREVIEW;

DIFF_OVERLAY_ITEM::DIFF_OVERLAY_ITEM( const BOX2I& aBBox ) :
        m_bbox( aBBox ),
        m_showBefore( false )
{
    // Default style
    SetStrokeColor( COLOR4D( 0.0, 0.8, 0.0, 1.0 ) ); // Green stroke
    SetFillColor( COLOR4D( 0.0, 0.8, 0.0, 0.1 ) );   // Faint green fill
    SetLineWidth( 2.0 );
}

void DIFF_OVERLAY_ITEM::ViewDraw( int aLayer, KIGFX::VIEW* aView ) const
{
    // We delegate to SIMPLE_OVERLAY_ITEM for setup, then drawPreviewShape
    SIMPLE_OVERLAY_ITEM::ViewDraw( aLayer, aView );
}

const BOX2I DIFF_OVERLAY_ITEM::ViewBBox() const
{
    // Return a very large bounding box to ensure the overlay is never culled
    // The buttons are positioned relative to the view, so we can't compute a fixed bbox
    BOX2I bbox;
    bbox.SetMaximum();
    return bbox;
}

void DIFF_OVERLAY_ITEM::drawPreviewShape( KIGFX::VIEW* aView ) const
{
    KIGFX::GAL* gal = aView->GetGAL();

    // Draw the main bounding box
    gal->DrawRectangle( m_bbox.GetPosition(), m_bbox.GetEnd() );

    // Draw Buttons
    // We need the view scale to keep buttons constant size on screen ideally,
    // or we just draw them in world units relative to zoom.
    // For simplicity V1: Draw in world units but scaled by inverse zoom to be constant screen size.

    double scale = 1.0 / gal->GetWorldScale();

    // Button 1: View Before/After (Toggle)
    drawButton( gal, 0, m_showBefore ? "View After" : "View Before", COLOR4D( 0.2, 0.2, 0.2, 0.8 ), scale );

    // Button 2: Approve (Green)
    drawButton( gal, 1, "Approve", COLOR4D( 0.0, 0.6, 0.0, 0.9 ), scale );

    // Button 3: Deny (Red)
    drawButton( gal, 2, "Deny", COLOR4D( 0.8, 0.0, 0.0, 0.9 ), scale );
}

BOX2I DIFF_OVERLAY_ITEM::getButtonRect( int aIndex, double aScale ) const
{
    // Place buttons at top right of bbox, arranged vertically or horizontally?
    // Let's do horizontal row above the box.

    // double btnW = BTN_WIDTH * aScale / 0.1; // Magic scaler adjustment for rough nm/mil

    // Wait, GAL units are usually mm or nm?
    // PCB uses nm, Schematic uses mils/10.
    // We need to be careful about units.
    // BUT we are passing a scale factor derived from GetWorldScale.
    // If standard text height is e.g. 10 world units at 1.0 zoom.

    // Let's rely on pixel size roughly.
    // 1.0 scale usually means 1 world unit = 1 pixel? No.
    // We want constant screen size.
    // Dimensions in pixels:
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

    // If box is too small, this might look weird, but okay for V1.
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
    // Need scale to reconstruct boxes
    // This is tricky because HitTest is usually called from tool which has view access
    if( !aView )
        return BTN_NONE;

    double scale = 1.0 / aView->GetGAL()->GetWorldScale();

    for( int i = 0; i < 3; ++i )
    {
        BOX2I rect = getButtonRect( i, scale );
        if( rect.Contains( aPoint ) )
        {
            switch( i )
            {
            case 0: return m_showBefore ? BTN_VIEW_AFTER : BTN_VIEW_BEFORE;
            case 1: return BTN_APPROVE;
            case 2: return BTN_DENY;
            }
        }
    }
    return BTN_NONE;
}
