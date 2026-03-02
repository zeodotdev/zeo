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
#include <kiid.h>
#include <functional>
#include <vector>

namespace KIGFX
{
namespace PREVIEW
{

    // Callback to dynamically compute the bounding box
    using BBOX_CALLBACK = std::function<BOX2I()>;

    /**
     * Per-item highlight data for live diff overlay.
     * Each entry represents one changed item with its bounding box and change type color.
     */
    struct ITEM_HIGHLIGHT
    {
        BOX2I              bbox;
        COLOR4D            color;
        bool               hasBorder = true;  ///< false for wires (fill only, no border)
        std::vector<KIID>  itemIds;           ///< KIIDs covered by this highlight (for per-item actions)
    };

    // Callback to get per-item highlight data for live diff rendering
    using ITEM_HIGHLIGHTS_CALLBACK = std::function<std::vector<ITEM_HIGHLIGHT>()>;

    /**
     * DIFF_OVERLAY_ITEM represents pending changes from the Agent.
     * It draws per-item colored bounding boxes around changed items on the live canvas.
     */
    class DIFF_OVERLAY_ITEM : public SIMPLE_OVERLAY_ITEM
    {
    public:
        enum BUTTON_ID
        {
            BTN_NONE = 0,
            BTN_APPROVE,
            BTN_REJECT,
            BTN_VIEW_DIFF
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
         * Hit tests the buttons drawn by this overlay.
         * @param aPoint world coordinate point
         * @return BUTTON_ID of the clicked button, or BTN_NONE
         */
        BUTTON_ID HitTestButtons( const VECTOR2I& aPoint, KIGFX::VIEW* aView ) const;

        /**
         * Control whether action buttons (Approve/Reject/View Diff) are drawn.
         * Set to false for read-only diff viewers that have no action handlers.
         */
        void SetShowButtons( bool aVal ) { m_showButtons = aVal; }
        bool GetShowButtons() const { return m_showButtons; }

        /**
         * Set a callback to provide per-item highlight data.
         * When set, the overlay draws colored boxes around each changed item
         * instead of a single overall bounding box.
         */
        void SetItemHighlightsCallback( ITEM_HIGHLIGHTS_CALLBACK aCallback )
        {
            m_itemHighlightsCallback = aCallback;
        }

        /**
         * Update the bounding box for dynamic tracking.
         * @param aBBox The new bounding box.
         */
        void SetBBox( const BOX2I& aBBox ) { m_bbox = aBBox; }

        /**
         * Get the current bounding box.
         * If a callback is set, this recomputes the bbox.
         */
        BOX2I GetCurrentBBox() const;

        /**
         * Get the cached bounding box (without recomputing).
         */
        const BOX2I& GetBBox() const { return m_bbox; }

        // --- Per-item hover buttons ---

        enum ITEM_BUTTON_ID
        {
            IBTN_NONE = 0,
            IBTN_APPROVE,
            IBTN_REJECT
        };

        /**
         * Set the index of the currently hovered highlight (for per-item buttons).
         * Pass -1 to clear hover.
         * @return true if the hover state changed (caller should refresh canvas).
         */
        bool SetHoveredHighlightIndex( int aIndex );
        int  GetHoveredHighlightIndex() const { return m_hoveredHighlightIndex; }

        /**
         * Hit test the per-item approve/reject buttons.
         * @return IBTN_APPROVE, IBTN_REJECT, or IBTN_NONE.
         */
        ITEM_BUTTON_ID HitTestItemButtons( const VECTOR2I& aPoint, KIGFX::VIEW* aView ) const;

        /**
         * Get the cached per-item highlights (populated during draw).
         */
        const std::vector<ITEM_HIGHLIGHT>& GetCachedHighlights() const { return m_cachedHighlights; }

    private:
        void drawPreviewShape( KIGFX::VIEW* aView ) const override;

        // Helpers to get button geometries (world coords)
        BOX2I getButtonRect( int aIndex, double aScale ) const;
        void  drawButton( KIGFX::GAL* aGal, int aIndex, const wxString& aLabel, const COLOR4D& aColor,
                          double aScale ) const;

        // Per-item button helpers
        BOX2I getItemButtonRect( const BOX2I& aItemBBox, int aButtonIndex, double aScale ) const;
        void  drawItemButton( KIGFX::GAL* aGal, const BOX2I& aItemBBox, int aIndex,
                              const wxString& aLabel, const COLOR4D& aColor, double aScale ) const;

        mutable BOX2I m_bbox;  // mutable so we can update it in const methods
        bool  m_showButtons  = false;  ///< false by default — approve/reject lives in agent window
        BBOX_CALLBACK m_bboxCallback;  // Optional callback for dynamic bbox
        ITEM_HIGHLIGHTS_CALLBACK m_itemHighlightsCallback;  // Per-item highlight data

        // Per-item hover state
        mutable int m_hoveredHighlightIndex = -1;
        mutable std::vector<ITEM_HIGHLIGHT> m_cachedHighlights;

        static constexpr double BTN_WIDTH = 60.0;
        static constexpr double BTN_HEIGHT = 20.0;
        static constexpr double BTN_MARGIN = 5.0;

        // Per-item button sizes (pixels, converted to world coords at draw time)
        static constexpr double IBTN_WIDTH  = 65.0;
        static constexpr double IBTN_HEIGHT = 18.0;
        static constexpr double IBTN_MARGIN = 4.0;
    };

} // namespace PREVIEW
} // namespace KIGFX

#endif // PREVIEW_DIFF_OVERLAY_ITEM__H_
