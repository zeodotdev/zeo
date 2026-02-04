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

#ifndef PREVIEW_TRACKING_BORDER_OVERLAY__H_
#define PREVIEW_TRACKING_BORDER_OVERLAY__H_

#include <preview_items/simple_overlay_item.h>
#include <math/box2.h>

namespace KIGFX
{
namespace PREVIEW
{

/**
 * TRACKING_BORDER_OVERLAY draws a green border around the viewport edges
 * to indicate that "Track Agent" mode is active. The border maintains a
 * constant screen pixel width regardless of zoom level.
 */
class TRACKING_BORDER_OVERLAY : public SIMPLE_OVERLAY_ITEM
{
public:
    TRACKING_BORDER_OVERLAY();

    void ViewDraw( int aLayer, KIGFX::VIEW* aView ) const override;
    const BOX2I ViewBBox() const override;

    wxString GetClass() const override { return "TRACKING_BORDER_OVERLAY"; }

protected:
    void drawPreviewShape( KIGFX::VIEW* aView ) const override;

private:
    static constexpr double BORDER_WIDTH_PIXELS = 4.0;  // Border width in screen pixels
};

} // namespace PREVIEW
} // namespace KIGFX

#endif // PREVIEW_TRACKING_BORDER_OVERLAY__H_
