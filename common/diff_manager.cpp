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
        m_active( false ),
        m_view( nullptr ),
        m_item( nullptr )
{
}

DIFF_MANAGER::~DIFF_MANAGER()
{
    ClearDiff();
}

void DIFF_MANAGER::ShowDiff( const BOX2I& aBBox )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    m_active = true;
    m_currentBBox = aBBox;

    if( m_view )
    {
        // If we already have an item, remove it
        if( m_item )
        {
            m_view->Remove( m_item );
            delete m_item;
            m_item = nullptr;
        }

        m_item = new KIGFX::PREVIEW::DIFF_OVERLAY_ITEM( aBBox );
        m_view->Add( m_item );
        m_view->SetVisible( m_item, true );
        m_view->Update( m_item );
        m_view->MarkDirty();
    }
}

void DIFF_MANAGER::ClearDiff()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    m_active = false;

    if( m_view && m_item )
    {
        m_view->Remove( m_item );
        m_view->MarkDirty();
    }

    if( m_item )
    {
        delete m_item;
        m_item = nullptr;
    }
}

void DIFF_MANAGER::RegisterOverlay( KIGFX::VIEW* aView, DIFF_CALLBACKS aCallbacks )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    m_view = aView;
    m_callbacks = aCallbacks;
}

void DIFF_MANAGER::UnregisterOverlay()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    if( m_view && m_item )
    {
        m_view->Remove( m_item );
    }
    m_view = nullptr;
    // Clear callbacks to prevent dangling references if frame is destroyed
    m_callbacks = {};

    if( m_item )
    {
        delete m_item;
        m_item = nullptr;
    }
}

// Interaction
// Returns true if the click was handled by the overlay
bool DIFF_MANAGER::HandleClick( const VECTOR2I& aPoint )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    if( !m_active || !m_item || !m_view )
        return false;

    auto btn = m_item->HitTestButtons( aPoint, m_view );

    switch( btn )
    {
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_APPROVE: OnApprove(); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_DENY: OnDeny(); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_VIEW_BEFORE: OnViewBefore(); return true;
    case KIGFX::PREVIEW::DIFF_OVERLAY_ITEM::BTN_VIEW_AFTER: OnViewAfter(); return true;
    default: return false;
    }
}

void DIFF_MANAGER::OnApprove()
{
    // Call the approve callback before clearing (so callback can access state if needed)
    if( m_callbacks.onApprove )
        m_callbacks.onApprove();

    ClearDiff();
}

void DIFF_MANAGER::OnDeny()
{
    // Call the deny callback which should revert the changes
    if( m_callbacks.onDeny )
        m_callbacks.onDeny();

    ClearDiff();
}

void DIFF_MANAGER::OnViewBefore()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    if( m_item && m_view )
    {
        // If we are currently showing AFTER (default), then Before means UNDO.
        if( !m_item->IsShowingBefore() )
        {
            if( m_callbacks.onUndo )
                m_callbacks.onUndo();
            m_item->SetShowingBefore( true );
        }

        m_view->Update( m_item );
        m_view->MarkDirty();
    }
}

void DIFF_MANAGER::OnViewAfter()
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    if( m_item && m_view )
    {
        // If we are currently showing BEFORE, then After means REDO.
        if( m_item->IsShowingBefore() )
        {
            if( m_callbacks.onRedo )
                m_callbacks.onRedo();
            m_item->SetShowingBefore( false );
        }

        m_view->Update( m_item );
        m_view->MarkDirty();
    }
}
