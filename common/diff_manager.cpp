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
                                     BBOX_COMPUTE_CALLBACK aBBoxCallback )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    m_currentView = aView;

    // Initialize or update state for this view with tracker support
    DIFF_VIEW_STATE& state = m_viewStates[aView];
    state.callbacks = aCallbacks;
    state.tracker = aTracker;
    state.sheetPath = aSheetPath;
    state.bboxCallback = aBBoxCallback;

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

            aView->Add( state.item );
            aView->SetVisible( state.item, true );
            aView->Update( state.item );
            aView->MarkDirty();

            wxLogInfo( "Agent diff: showing dynamic overlay at (%d,%d) size (%d,%d)",
                       bbox.GetX(), bbox.GetY(), bbox.GetWidth(), bbox.GetHeight() );
        }
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
        return false;

    DIFF_VIEW_STATE& state = it->second;

    if( !state.active || !state.item || !aView )
        return false;

    auto btn = state.item->HitTestButtons( aPoint, aView );

    switch( btn )
    {
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_APPROVE: OnApprove( aView ); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_REJECT: OnReject( aView ); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_VIEW_BEFORE: OnViewBefore( aView ); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_VIEW_AFTER: OnViewAfter( aView ); return true;
    default: return false;
    }
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

void DIFF_MANAGER::OnViewBefore( KIGFX::VIEW* aView )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    if( state.item && aView )
    {
        // If we are currently showing AFTER (default), then Before means UNDO.
        if( !state.item->IsShowingBefore() )
        {
            if( state.callbacks.onUndo )
                state.callbacks.onUndo();

            // Re-check: the callback may have triggered ClearDiff() which deletes state.item
            if( !state.item )
                return;

            state.item->SetShowingBefore( true );
        }

        aView->Update( state.item );
        aView->MarkDirty();

        // Trigger canvas refresh
        if( state.callbacks.onRefresh )
            state.callbacks.onRefresh();
    }
}

void DIFF_MANAGER::OnViewAfter( KIGFX::VIEW* aView )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );

    auto it = m_viewStates.find( aView );
    if( it == m_viewStates.end() )
        return;

    DIFF_VIEW_STATE& state = it->second;

    if( state.item && aView )
    {
        // If we are currently showing BEFORE, then After means REDO.
        if( state.item->IsShowingBefore() )
        {
            if( state.callbacks.onRedo )
                state.callbacks.onRedo();

            // Re-check: the callback may have triggered ClearDiff() which deletes state.item
            if( !state.item )
                return;

            state.item->SetShowingBefore( false );
        }

        aView->Update( state.item );
        aView->MarkDirty();

        // Trigger canvas refresh
        if( state.callbacks.onRefresh )
            state.callbacks.onRefresh();
    }
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

    // If we have a bbox callback, recompute and update the overlay
    if( state.bboxCallback )
    {
        BOX2I newBBox = state.bboxCallback();

        // Only update if bbox has changed
        if( newBBox != state.currentBBox )
        {
            state.currentBBox = newBBox;

            // Update the overlay item's bbox
            if( state.item )
            {
                aView->Remove( state.item );
                delete state.item;
            }

            state.item = new KIGFX::PREVIEW::DIFF_OVERLAY_ITEM( newBBox );
            aView->Add( state.item );
            aView->SetVisible( state.item, true );
            aView->Update( state.item );
            aView->MarkDirty();

            if( state.callbacks.onRefresh )
                state.callbacks.onRefresh();
        }
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



