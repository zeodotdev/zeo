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

#include <diff_manager.h>
#include <preview_items/diff_overlay_item.h>
#include <gal/graphics_abstraction_layer.h>
#include <pgm_base.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <view/view.h>
#include <wx/log.h>

DIFF_MANAGER& DIFF_MANAGER::GetInstance()
{
    static DIFF_MANAGER instance;
    return instance;
}

DIFF_MANAGER::DIFF_MANAGER() :
        m_currentView( nullptr )
{
}

DIFF_MANAGER::~DIFF_MANAGER()
{
    // Clear all views
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    for( auto& [view, state] : m_viewStates )
    {
        if( view && state.item )
        {
            view->Remove( state.item );
            delete state.item;
        }
    }
    m_viewStates.clear();
}

void DIFF_MANAGER::ShowDiff( const BOX2I& aBBox )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    if( !m_currentView )
        return;

    COMMON_SETTINGS* settings = Pgm().GetSettingsManager().GetCommonSettings();

    if( settings && !settings->m_Agent.enable_diff_view )
        return;

    wxLogInfo( "Agent diff: showing overlay at (%d,%d) size (%d,%d)",
               aBBox.GetX(), aBBox.GetY(), aBBox.GetWidth(), aBBox.GetHeight() );

    DIFF_VIEW_STATE& state = m_viewStates[m_currentView];
    state.active = true;
    state.currentBBox = aBBox;

    // If we already have an item, remove it
    if( state.item )
    {
        m_currentView->Remove( state.item );
        delete state.item;
        state.item = nullptr;
    }

    state.item = new KIGFX::PREVIEW::DIFF_OVERLAY_ITEM( aBBox );

    m_currentView->Add( state.item );
    m_currentView->SetVisible( state.item, true );
    m_currentView->Update( state.item );
    m_currentView->MarkDirty();

    // Trigger canvas refresh so the overlay is actually rendered
    if( state.callbacks.onRefresh )
        state.callbacks.onRefresh();
}


void DIFF_MANAGER::ClearDiff()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    if( m_currentView )
        ClearDiff( m_currentView );
}

void DIFF_MANAGER::ClearDiff( KIGFX::VIEW* aView )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    if( state.active )
        wxLogInfo( "Agent diff: clearing overlay" );

    state.active = false;

    if( aView && state.item )
    {
        aView->Remove( state.item );
        aView->MarkDirty();

        // Trigger canvas refresh so the overlay removal is visible
        if( state.callbacks.onRefresh )
            state.callbacks.onRefresh();
    }

    if( state.item )
    {
        delete state.item;
        state.item = nullptr;
    }
}

void DIFF_MANAGER::RegisterOverlay( KIGFX::VIEW* aView, DIFF_CALLBACKS aCallbacks )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    m_currentView = aView;

    // Initialize or update state for this view
    DIFF_VIEW_STATE& state = m_viewStates[aView];
    state.callbacks = aCallbacks;
    state.tracker = nullptr;
    state.sheetPath.clear();
    state.bboxCallback = nullptr;
}


void DIFF_MANAGER::RegisterOverlay( KIGFX::VIEW* aView, AGENT_CHANGE_TRACKER* aTracker,
                                     const wxString& aSheetPath, DIFF_CALLBACKS aCallbacks,
                                     BBOX_COMPUTE_CALLBACK aBBoxCallback,
                                     ITEM_HIGHLIGHTS_CALLBACK aHighlightsCallback,
                                     WIRING_GUIDES_CALLBACK aWiringGuidesCallback )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    m_currentView = aView;

    // Initialize or update state for this view with tracker support
    DIFF_VIEW_STATE& state = m_viewStates[aView];
    state.callbacks = aCallbacks;
    state.tracker = aTracker;
    state.sheetPath = aSheetPath;
    state.bboxCallback = aBBoxCallback;

    // Check if diff view overlays are enabled in settings
    COMMON_SETTINGS* settings = Pgm().GetSettingsManager().GetCommonSettings();

    if( settings && !settings->m_Agent.enable_diff_view )
        return;

    // If we have a bbox callback, create overlay with dynamic bbox
    if( aBBoxCallback )
    {
        BOX2I bbox = aBBoxCallback();
        if( bbox.GetWidth() > 0 && bbox.GetHeight() > 0 )
        {
            // Remove old overlay if it exists
            if( state.item )
            {
                aView->Remove( state.item );
                delete state.item;
            }

            // Create overlay with callback - it will recompute bbox on each draw
            state.item = new KIGFX::PREVIEW::DIFF_OVERLAY_ITEM( aBBoxCallback );
            state.active = true;
            state.currentBBox = bbox;

            // Set per-item highlights callback for live diff coloring
            if( aHighlightsCallback )
            {
                state.item->SetItemHighlightsCallback(
                    [aHighlightsCallback]() -> std::vector<KIGFX::PREVIEW::ITEM_HIGHLIGHT>
                    {
                        // Convert from DIFF_ITEM_HIGHLIGHT to ITEM_HIGHLIGHT
                        auto diffHighlights = aHighlightsCallback();
                        std::vector<KIGFX::PREVIEW::ITEM_HIGHLIGHT> result;
                        result.reserve( diffHighlights.size() );
                        for( const auto& dh : diffHighlights )
                            result.push_back( { dh.bbox, dh.color, dh.hasBorder,
                                                dh.itemIds } );
                        return result;
                    } );
            }

            // Set wiring guides callback for sch_draft_circuit preview
            if( aWiringGuidesCallback )
            {
                state.item->SetWiringGuidesCallback(
                    [aWiringGuidesCallback]() -> std::vector<KIGFX::PREVIEW::WIRING_GUIDE_PREVIEW>
                    {
                        auto wiringGuides = aWiringGuidesCallback();
                        std::vector<KIGFX::PREVIEW::WIRING_GUIDE_PREVIEW> result;
                        result.reserve( wiringGuides.size() );
                        for( const auto& wg : wiringGuides )
                            result.push_back( { wg.start, wg.end, wg.sourceSymbolId, wg.label } );
                        return result;
                    } );
            }

            aView->Add( state.item );
            aView->SetVisible( state.item, true );
            aView->Update( state.item );
            aView->MarkDirty();

            wxLogInfo( "Agent diff: showing dynamic overlay at (%d,%d) size (%d,%d)",
                       bbox.GetX(), bbox.GetY(), bbox.GetWidth(), bbox.GetHeight() );
        }
    }
}

void DIFF_MANAGER::NullifyOverlayItem( KIGFX::VIEW* aView )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it != m_viewStates.end() )
    {
        // The view is about to Clear() all its items, so the overlay will be
        // removed by the view itself. Just null our pointer to avoid dangling.
        it->second.item = nullptr;
    }
}


void DIFF_MANAGER::UnregisterOverlay()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    if( m_currentView )
        UnregisterOverlay( m_currentView );
}

void DIFF_MANAGER::UnregisterOverlay( KIGFX::VIEW* aView )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    if( aView && state.item )
    {
        aView->Remove( state.item );
    }

    if( state.item )
    {
        delete state.item;
        state.item = nullptr;
    }

    // Clear callbacks to prevent dangling references
    state.callbacks = {};

    m_viewStates.erase( it );

    if( m_currentView == aView )
        m_currentView = nullptr;
}

bool DIFF_MANAGER::IsDiffActive() const
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    if( m_currentView )
        return IsDiffActive( m_currentView );

    return false;
}

bool DIFF_MANAGER::IsDiffActive( KIGFX::VIEW* aView ) const
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return false;

    return it->second.active;
}

bool DIFF_MANAGER::HandleClick( const VECTOR2I& aPoint )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    if( m_currentView )
        return HandleClick( m_currentView, aPoint );

    return false;
}

bool DIFF_MANAGER::HandleClick( KIGFX::VIEW* aView, const VECTOR2I& aPoint )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
    {
        wxLogDebug( "DIFF_MANAGER::HandleClick: view %p not registered (%zu states)",
                    aView, m_viewStates.size() );
        return false;
    }

    DIFF_VIEW_STATE& state = it->second;

    if( !state.active || !state.item || !aView )
    {
        wxLogDebug( "DIFF_MANAGER::HandleClick: state not ready (active=%d item=%p)",
                    state.active, state.item );
        return false;
    }

    // Check overall buttons first
    auto btn = state.item->HitTestButtons( aPoint, aView );
    wxLogDebug( "DIFF_MANAGER::HandleClick: point=(%d,%d) btn=%d", aPoint.x, aPoint.y, (int)btn );

    switch( btn )
    {
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_APPROVE: OnApprove( aView ); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_REJECT: OnReject( aView ); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_VIEW_DIFF: OnViewDiff( aView ); return true;
    default: break;
    }

    // Check per-item hover buttons
    auto itemBtn = state.item->HitTestItemButtons( aPoint, aView );
    if( itemBtn != KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::IBTN_NONE )
    {
        int hovIdx = state.item->GetHoveredHighlightIndex();
        const auto& highlights = state.item->GetCachedHighlights();

        if( hovIdx >= 0 && hovIdx < (int) highlights.size()
            && !highlights[hovIdx].itemIds.empty() )
        {
            const std::vector<KIID>& itemIds = highlights[hovIdx].itemIds;

            if( itemBtn == KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::IBTN_APPROVE
                && state.callbacks.onApproveItems )
            {
                state.callbacks.onApproveItems( itemIds );
                return true;
            }
            else if( itemBtn == KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::IBTN_REJECT
                     && state.callbacks.onRejectItems )
            {
                state.callbacks.onRejectItems( itemIds );
                return true;
            }
        }
    }

    return false;
}

void DIFF_MANAGER::OnApprove( KIGFX::VIEW* aView )
{
    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    // Call the approve callback before clearing (so callback can access state if needed)
    if( state.callbacks.onApprove )
        state.callbacks.onApprove();

    ClearDiff( aView );
}

void DIFF_MANAGER::OnReject( KIGFX::VIEW* aView )
{
    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    // Call the reject callback which should revert the changes
    if( state.callbacks.onReject )
        state.callbacks.onReject();

    ClearDiff( aView );
}

void DIFF_MANAGER::OnViewDiff( KIGFX::VIEW* aView )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    if( state.callbacks.onViewDiff )
        state.callbacks.onViewDiff();
}


void DIFF_MANAGER::RefreshOverlay()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    if( m_currentView )
        RefreshOverlay( m_currentView );
}


void DIFF_MANAGER::RefreshOverlay( KIGFX::VIEW* aView )
{
    // Note: With dynamic bbox overlays, this method is less critical since the
    // overlay recomputes its bbox on each draw. However, it can still be used
    // to force an immediate update if needed.
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    if( !state.active )
        return;

    // Force the view to redraw the existing overlay item (which will recompute
    // its bbox and highlights via its callbacks on the next draw).
    if( state.item && aView )
    {
        aView->Update( state.item );
        aView->MarkDirty();

        if( state.callbacks.onRefresh )
            state.callbacks.onRefresh();
    }
}


void DIFF_MANAGER::RefreshAllOverlays()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    for( auto& [view, state] : m_viewStates )
    {
        if( state.active && view )
            RefreshOverlay( view );
    }
}


AGENT_CHANGE_TRACKER* DIFF_MANAGER::GetTracker( KIGFX::VIEW* aView ) const
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return nullptr;

    return it->second.tracker;
}


wxString DIFF_MANAGER::GetSheetPath( KIGFX::VIEW* aView ) const
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return wxEmptyString;

    return it->second.sheetPath;
}


bool DIFF_MANAGER::HandleMouseMotion( KIGFX::VIEW* aView, const VECTOR2I& aPoint )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return false;

    DIFF_VIEW_STATE& state = it->second;
    if( !state.active || !state.item )
        return false;

    const auto& highlights = state.item->GetCachedHighlights();
    int curHover = state.item->GetHoveredHighlightIndex();
    int newIndex = -1;

    // Compute button dimensions in world coords to extend the hover zone.
    KIGFX::GAL* gal = aView ? aView->GetGAL() : nullptr;
    int buttonZoneY = 0;
    int buttonsWidth = 0;

    if( gal )
    {
        double scale = 1.0 / gal->GetWorldScale();
        // Height: IBTN_HEIGHT in screen px + border offset in world coords
        buttonZoneY = static_cast<int>( 20.0 * scale + 2540.0 );
        // Width: two buttons side by side (IBTN_WIDTH * 2)
        buttonsWidth = static_cast<int>( 130.0 * scale );
    }

    for( size_t i = 0; i < highlights.size(); ++i )
    {
        if( highlights[i].itemIds.empty() )
            continue;

        // Expand hit-test bbox: upward for button height, leftward if buttons
        // are wider than the highlight box (buttons are right-aligned).
        BOX2I hitBox = highlights[i].bbox;
        hitBox.SetY( hitBox.GetY() - buttonZoneY );
        hitBox.SetHeight( hitBox.GetHeight() + buttonZoneY );

        int overhang = buttonsWidth - hitBox.GetWidth();
        if( overhang > 0 )
        {
            hitBox.SetX( hitBox.GetX() - overhang );
            hitBox.SetWidth( hitBox.GetWidth() + overhang );
        }

        if( hitBox.Contains( aPoint ) )
        {
            newIndex = static_cast<int>( i );
            break;
        }
    }

    return state.item->SetHoveredHighlightIndex( newIndex );
}



