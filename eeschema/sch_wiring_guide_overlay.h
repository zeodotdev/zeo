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

#ifndef SCH_WIRING_GUIDE_OVERLAY_H
#define SCH_WIRING_GUIDE_OVERLAY_H

#include <preview_items/simple_overlay_item.h>
#include <math/box2.h>
#include <gal/color4d.h>

class SCH_WIRING_GUIDE_MANAGER;
class SCH_EDIT_FRAME;

/**
 * @class SCH_WIRING_GUIDE_OVERLAY
 * @brief Renders wiring guide lines for the sch_draft_circuit tool.
 *
 * This overlay draws dashed lines between source pins and their recommended
 * targets. It supports hover states, selection, and context menus for
 * dismissing or wiring connections.
 *
 * The overlay only renders guides for approved symbols (not pending in diff).
 * During diff review, guide previews are shown by DIFF_OVERLAY_ITEM instead.
 */
class SCH_WIRING_GUIDE_OVERLAY : public KIGFX::PREVIEW::SIMPLE_OVERLAY_ITEM
{
public:
    SCH_WIRING_GUIDE_OVERLAY( SCH_WIRING_GUIDE_MANAGER* aManager, SCH_EDIT_FRAME* aFrame );
    ~SCH_WIRING_GUIDE_OVERLAY() override;

    // VIEW_ITEM interface
    const BOX2I  ViewBBox() const override;
    void         ViewDraw( int aLayer, KIGFX::VIEW* aView ) const override;
    wxString     GetClass() const override { return wxT( "SCH_WIRING_GUIDE_OVERLAY" ); }

    /**
     * Hit test to find which guide is at the given position.
     *
     * @param aPos Position in world coordinates
     * @return Index of the guide, or -1 if none
     */
    int HitTestGuide( const VECTOR2I& aPos ) const;

    /**
     * Set the currently hovered guide index.
     *
     * @param aIndex Index of the hovered guide, or -1 for none
     * @return true if the hover state changed (caller should refresh)
     */
    bool SetHoveredGuide( int aIndex );

    /**
     * Get the currently hovered guide index.
     */
    int GetHoveredGuide() const { return m_hoveredIndex; }

    /**
     * Show context menu for a guide.
     *
     * @param aGuideIndex Index of the guide
     * @param aScreenPos Screen position for the menu
     */
    void ShowContextMenu( int aGuideIndex, const wxPoint& aScreenPos );

    /**
     * Refresh the overlay from the manager.
     * Call this after guide states change.
     */
    void Refresh();

protected:
    void drawPreviewShape( KIGFX::VIEW* aView ) const override;

private:
    /**
     * Draw a single guide line.
     */
    void drawGuideLine( KIGFX::GAL* aGal, const VECTOR2I& aStart, const VECTOR2I& aEnd,
                        bool aHovered, double aScale ) const;

    /**
     * Draw a dashed line between two points.
     */
    void drawDashedLine( KIGFX::GAL* aGal, const VECTOR2D& aStart, const VECTOR2D& aEnd,
                         double aDash, double aGap ) const;

    /**
     * Draw endpoint markers (small circles at pin positions).
     */
    void drawEndpointMarker( KIGFX::GAL* aGal, const VECTOR2I& aPos, bool aHovered,
                             double aScale ) const;

    /**
     * Draw hover tooltip showing connection info.
     */
    void drawHoverTooltip( KIGFX::GAL* aGal, int aGuideIndex, double aScale ) const;

private:
    SCH_WIRING_GUIDE_MANAGER* m_manager;
    SCH_EDIT_FRAME*           m_frame;
    mutable int               m_hoveredIndex;

    // Visual settings
    static const KIGFX::COLOR4D GUIDE_COLOR;
    static const KIGFX::COLOR4D GUIDE_COLOR_HOVER;
    static const KIGFX::COLOR4D ENDPOINT_COLOR;

    static constexpr double GUIDE_WIDTH      = 0.15;   // mm
    static constexpr double GUIDE_DASH       = 1.2;    // mm
    static constexpr double GUIDE_GAP        = 0.6;    // mm
    static constexpr double GUIDE_ALPHA      = 0.6;
    static constexpr double GUIDE_HOVER_ALPHA = 0.9;
    static constexpr double ENDPOINT_RADIUS  = 0.4;    // mm
    static constexpr double HIT_TEST_MARGIN  = 1.0;    // mm
};

#endif // SCH_WIRING_GUIDE_OVERLAY_H
