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

#include <preview_items/tracking_border_overlay.h>
#include <gal/graphics_abstraction_layer.h>
#include <view/view.h>

using namespace KIGFX;
using namespace KIGFX::PREVIEW;


TRACKING_BORDER_OVERLAY::TRACKING_BORDER_OVERLAY() :
        SIMPLE_OVERLAY_ITEM()
{
    // Set green color for the tracking border
    SetStrokeColor( COLOR4D( 0.0, 0.9, 0.0, 1.0 ) );  // Bright green
    SetFillColor( COLOR4D( 0.0, 0.0, 0.0, 0.0 ) );    // No fill
    SetLineWidth( 1.0 );  // Will be overridden in drawPreviewShape
}


void TRACKING_BORDER_OVERLAY::ViewDraw( int aLayer, KIGFX::VIEW* aView ) const
{
    // We need custom drawing for viewport-relative coordinates
    // but still use the parent's setup
    drawPreviewShape( aView );
}


const BOX2I TRACKING_BORDER_OVERLAY::ViewBBox() const
{
    // Return a maximum bounding box so we're always drawn
    BOX2I bbox;
    bbox.SetMaximum();
    return bbox;
}


void TRACKING_BORDER_OVERLAY::drawPreviewShape( KIGFX::VIEW* aView ) const
{
    if( !aView )
        return;

    GAL* gal = aView->GetGAL();
    if( !gal )
        return;

    // Get viewport in world coordinates
    BOX2D viewport = aView->GetViewport();

    // Calculate line width in world coordinates (constant screen pixels)
    double scale = 1.0 / gal->GetWorldScale();
    double worldLineWidth = BORDER_WIDTH_PIXELS * scale;

    // Set up GAL for drawing
    gal->SetIsStroke( true );
    gal->SetIsFill( false );
    gal->SetStrokeColor( COLOR4D( 0.0, 0.9, 0.0, 1.0 ) );  // Bright green
    gal->SetLineWidth( worldLineWidth );

    // Draw rectangle at viewport edges (inset by half line width to keep it fully visible)
    double inset = worldLineWidth / 2.0;
    VECTOR2D topLeft( viewport.GetLeft() + inset, viewport.GetTop() + inset );
    VECTOR2D topRight( viewport.GetRight() - inset, viewport.GetTop() + inset );
    VECTOR2D bottomRight( viewport.GetRight() - inset, viewport.GetBottom() - inset );
    VECTOR2D bottomLeft( viewport.GetLeft() + inset, viewport.GetBottom() - inset );

    // Draw the four edges of the border
    gal->DrawLine( topLeft, topRight );
    gal->DrawLine( topRight, bottomRight );
    gal->DrawLine( bottomRight, bottomLeft );
    gal->DrawLine( bottomLeft, topLeft );
}
