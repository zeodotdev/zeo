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

#ifndef PREVIEW_DIFF_OVERLAY_ITEM__H_
#define PREVIEW_DIFF_OVERLAY_ITEM__H_

#include <preview_items/simple_overlay_item.h>
#include <math/box2.h>

namespace KIGFX
{
namespace PREVIEW
{

    /**
 * DIFF_OVERLAY_ITEM represents a pending change from the Agent.
 * It draws a bounding box around the affected area and interactive buttons
 * for Approve/Deny/View.
 */
    class DIFF_OVERLAY_ITEM : public SIMPLE_OVERLAY_ITEM
    {
    public:
        enum BUTTON_ID
        {
            BTN_NONE = 0,
            BTN_APPROVE,
            BTN_DENY,
            BTN_VIEW_BEFORE,
            BTN_VIEW_AFTER
        };

        DIFF_OVERLAY_ITEM( const BOX2I& aBBox );

        // Overrides
        void          ViewDraw( int aLayer, KIGFX::VIEW* aView ) const override;
        const BOX2I   ViewBBox() const override;
        wxString      GetClass() const override { return "DIFF_OVERLAY_ITEM"; }

        /**
     * hit tests the buttons drawn by this overlay.
     * @param aPoint world coordinate point
     * @return BUTTON_ID of the clicked button, or BTN_NONE
     */
        BUTTON_ID HitTestButtons( const VECTOR2I& aPoint, KIGFX::VIEW* aView ) const;

        void SetShowingBefore( bool aVal ) { m_showBefore = aVal; }
        bool IsShowingBefore() const { return m_showBefore; }

    private:
        void drawPreviewShape( KIGFX::VIEW* aView ) const override;

        // Helpers to get button geometries (world coords)
        // We place buttons at the top-right of the bbox
        BOX2I getButtonRect( int aIndex, double aScale ) const;
        void  drawButton( KIGFX::GAL* aGal, int aIndex, const wxString& aLabel, const COLOR4D& aColor,
                          double aScale ) const;

        BOX2I m_bbox;
        bool  m_showBefore;

        static constexpr double BTN_WIDTH = 60.0;
        static constexpr double BTN_HEIGHT = 20.0;
        static constexpr double BTN_MARGIN = 5.0;
    };

} // namespace PREVIEW
} // namespace KIGFX

#endif // PREVIEW_DIFF_OVERLAY_ITEM__H_
