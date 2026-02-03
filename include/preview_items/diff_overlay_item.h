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
#include <functional>

namespace KIGFX
{
namespace PREVIEW
{

    // Callback to dynamically compute the bounding box
    using BBOX_CALLBACK = std::function<BOX2I()>;

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
            BTN_REJECT,
            BTN_VIEW_BEFORE,
            BTN_VIEW_AFTER
        };

        // Static bbox constructor (legacy)
        DIFF_OVERLAY_ITEM( const BOX2I& aBBox );

        // Dynamic bbox constructor - computes bbox on each draw
        DIFF_OVERLAY_ITEM( BBOX_CALLBACK aBBoxCallback );

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

        /**
         * Update the bounding box for dynamic tracking.
         * Call this when tracked items may have moved.
         * @param aBBox The new bounding box.
         */
        void SetBBox( const BOX2I& aBBox ) { m_bbox = aBBox; }

        /**
         * Get the current bounding box.
         * If a callback is set, this recomputes the bbox.
         * @return The bounding box.
         */
        BOX2I GetCurrentBBox() const;

        /**
         * Get the cached bounding box (without recomputing).
         * @return The bounding box.
         */
        const BOX2I& GetBBox() const { return m_bbox; }

    private:
        void drawPreviewShape( KIGFX::VIEW* aView ) const override;

        // Helpers to get button geometries (world coords)
        // We place buttons at the top-right of the bbox
        BOX2I getButtonRect( int aIndex, double aScale ) const;
        void  drawButton( KIGFX::GAL* aGal, int aIndex, const wxString& aLabel, const COLOR4D& aColor,
                          double aScale ) const;

        mutable BOX2I m_bbox;  // mutable so we can update it in const methods
        bool  m_showBefore;
        BBOX_CALLBACK m_bboxCallback;  // Optional callback for dynamic bbox

        static constexpr double BTN_WIDTH = 60.0;
        static constexpr double BTN_HEIGHT = 20.0;
        static constexpr double BTN_MARGIN = 5.0;
    };

} // namespace PREVIEW
} // namespace KIGFX

#endif // PREVIEW_DIFF_OVERLAY_ITEM__H_
