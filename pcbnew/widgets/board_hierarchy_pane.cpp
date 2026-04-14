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

#include "board_hierarchy_pane.h"

#include <bitmaps.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/project_file.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/wupdlock.h>


wxIMPLEMENT_ABSTRACT_CLASS( BOARD_HIERARCHY_TREE, wxTreeCtrl );


BOARD_HIERARCHY_TREE::BOARD_HIERARCHY_TREE( wxWindow* parent ) :
        wxTreeCtrl( parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                    wxTR_HAS_BUTTONS | wxTR_EDIT_LABELS | wxTR_HIDE_ROOT | wxTR_SINGLE,
                    wxDefaultValidator, wxT( "BoardHierarchyTree" ) )
{
}


int BOARD_HIERARCHY_TREE::OnCompareItems( const wxTreeItemId& item1, const wxTreeItemId& item2 )
{
    // Sort by display name
    return GetItemText( item1 ).CmpNoCase( GetItemText( item2 ) );
}


BOARD_HIERARCHY_PANE::BOARD_HIERARCHY_PANE( PCB_EDIT_FRAME* aParent ) :
        WX_PANEL( aParent ),
        m_frame( aParent ),
        m_eventsEnabled( false )
{
    wxASSERT( dynamic_cast<PCB_EDIT_FRAME*>( aParent ) );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    SetSizer( sizer );

    m_tree = new BOARD_HIERARCHY_TREE( this );

    // Set up tree images
    wxVector<wxBitmapBundle> images;
    images.push_back( KiBitmapBundle( BITMAPS::directory ) );      // 0: project folder
    images.push_back( KiBitmapBundle( BITMAPS::icon_pcbnew_24 ) ); // 1: board (inactive)
    images.push_back( KiBitmapBundle( BITMAPS::icon_pcbnew_24 ) ); // 2: board (active)
    m_tree->SetImages( images );

    sizer->Add( m_tree, 1, wxEXPAND, wxBORDER_NONE );

    // Build initial tree
    UpdateHierarchyTree();

    // Bind events
    m_tree->Bind( wxEVT_TREE_SEL_CHANGED, &BOARD_HIERARCHY_PANE::onSelectBoard, this );
    m_tree->Bind( wxEVT_TREE_ITEM_ACTIVATED, &BOARD_HIERARCHY_PANE::onSelectBoard, this );
    m_tree->Bind( wxEVT_TREE_ITEM_RIGHT_CLICK, &BOARD_HIERARCHY_PANE::onTreeItemRightClick, this );
    m_tree->Bind( wxEVT_TREE_END_LABEL_EDIT, &BOARD_HIERARCHY_PANE::onTreeEditFinished, this );
    Bind( wxEVT_CHAR_HOOK, &BOARD_HIERARCHY_PANE::onCharHook, this );

    m_eventsEnabled = true;

    // Register as board listener for the current board
    if( m_frame->GetBoard() )
        m_frame->GetBoard()->AddListener( this );
}


BOARD_HIERARCHY_PANE::~BOARD_HIERARCHY_PANE()
{
    // Unregister as board listener
    if( m_frame && m_frame->GetBoard() )
        m_frame->GetBoard()->RemoveListener( this );

    // Unbind events
    m_tree->Unbind( wxEVT_TREE_SEL_CHANGED, &BOARD_HIERARCHY_PANE::onSelectBoard, this );
    m_tree->Unbind( wxEVT_TREE_ITEM_ACTIVATED, &BOARD_HIERARCHY_PANE::onSelectBoard, this );
    m_tree->Unbind( wxEVT_TREE_ITEM_RIGHT_CLICK, &BOARD_HIERARCHY_PANE::onTreeItemRightClick, this );
    m_tree->Unbind( wxEVT_TREE_END_LABEL_EDIT, &BOARD_HIERARCHY_PANE::onTreeEditFinished, this );
    Unbind( wxEVT_CHAR_HOOK, &BOARD_HIERARCHY_PANE::onCharHook, this );
}


void BOARD_HIERARCHY_PANE::UpdateHierarchyTree()
{
    wxWindowUpdateLocker updateLock( m_tree );

    // Save current selection
    KIID selectedUuid;
    wxTreeItemId selectedItem = m_tree->GetSelection();

    if( selectedItem.IsOk() )
    {
        BOARD_TREE_ITEM_DATA* data =
                static_cast<BOARD_TREE_ITEM_DATA*>( m_tree->GetItemData( selectedItem ) );
        if( data )
            selectedUuid = data->m_boardUuid;
    }

    m_tree->DeleteAllItems();
    m_boardTreeItems.clear();

    buildTree();

    // Restore selection
    if( selectedUuid != niluuid )
        SelectBoard( selectedUuid );
    else
        UpdateHierarchySelection();
}


void BOARD_HIERARCHY_PANE::buildTree()
{
    PROJECT* project = &m_frame->Prj();
    if( !project )
        return;

    PROJECT_FILE& projectFile = project->GetProjectFile();

    // Create root item (hidden)
    wxString projectName = project->GetProjectName();
    m_rootItem = m_tree->AddRoot( projectName, 0 );

    // Add boards
    const std::vector<BOARD_INFO>& boards = projectFile.GetBoardInfos();

    for( const BOARD_INFO& boardInfo : boards )
    {
        int componentCount = getComponentCount( boardInfo );
        wxString displayText = formatBoardString( boardInfo.displayName, componentCount,
                                                   boardInfo.isActive );

        int imageIndex = boardInfo.isActive ? 2 : 1;
        wxTreeItemId item = m_tree->AppendItem( m_rootItem, displayText, imageIndex, imageIndex );

        m_tree->SetItemData( item,
                             new BOARD_TREE_ITEM_DATA( boardInfo.uuid, boardInfo.filename,
                                                       boardInfo.isActive ) );

        m_boardTreeItems[boardInfo.uuid] = item;

        // Bold the active board
        if( boardInfo.isActive )
            m_tree->SetItemBold( item, true );
    }

    // If no boards in project file, add the current board as a single entry
    if( boards.empty() && m_frame->GetBoard() )
    {
        BOARD* board = m_frame->GetBoard();
        wxFileName fn( board->GetFileName() );
        wxString displayName = board->GetDisplayName().IsEmpty() ?
                               fn.GetName() : board->GetDisplayName();

        int componentCount = board->Footprints().size();
        wxString displayText = formatBoardString( displayName, componentCount, true );

        wxTreeItemId item = m_tree->AppendItem( m_rootItem, displayText, 2, 2 );
        m_tree->SetItemData( item,
                             new BOARD_TREE_ITEM_DATA( board->GetProjectBoardUuid(),
                                                       board->GetFileName(), true ) );

        m_tree->SetItemBold( item, true );
    }

    m_tree->ExpandAll();
}


wxString BOARD_HIERARCHY_PANE::formatBoardString( const wxString& aDisplayName,
                                                   int aComponentCount, bool aIsActive )
{
    wxString suffix;

    if( aComponentCount > 0 )
        suffix = wxString::Format( " (%d)", aComponentCount );

    return aDisplayName + suffix;
}


int BOARD_HIERARCHY_PANE::getComponentCount( const BOARD_INFO& aBoardInfo )
{
    // If this is the active board, get count from loaded board
    if( aBoardInfo.isActive && m_frame->GetBoard() )
        return m_frame->GetBoard()->Footprints().size();

    // For inactive boards, we'd need to load them or cache the count
    // For now, return -1 to indicate unknown
    return -1;
}


void BOARD_HIERARCHY_PANE::UpdateHierarchySelection()
{
    if( !m_frame->GetBoard() )
        return;

    KIID activeUuid = m_frame->GetBoard()->GetProjectBoardUuid();

    if( activeUuid != niluuid )
        SelectBoard( activeUuid );
}


void BOARD_HIERARCHY_PANE::UpdateLabels()
{
    // Update component counts and sync status without rebuilding tree
    for( auto& [uuid, itemId] : m_boardTreeItems )
    {
        if( !itemId.IsOk() )
            continue;

        BOARD_TREE_ITEM_DATA* data =
                static_cast<BOARD_TREE_ITEM_DATA*>( m_tree->GetItemData( itemId ) );

        if( !data )
            continue;

        // Get updated board info from project file
        PROJECT* project = &m_frame->Prj();
        if( !project )
            continue;

        PROJECT_FILE& projectFile = project->GetProjectFile();

        BOARD_INFO* boardInfo = projectFile.GetBoardInfo( uuid );
        if( !boardInfo )
            continue;

        int componentCount = getComponentCount( *boardInfo );
        wxString displayText = formatBoardString( boardInfo->displayName, componentCount,
                                                   boardInfo->isActive );

        m_tree->SetItemText( itemId, displayText );
        m_tree->SetItemBold( itemId, boardInfo->isActive );
    }
}


void BOARD_HIERARCHY_PANE::SelectBoard( const KIID& aBoardUuid )
{
    auto it = m_boardTreeItems.find( aBoardUuid );

    if( it != m_boardTreeItems.end() && it->second.IsOk() )
    {
        m_eventsEnabled = false;
        m_tree->SelectItem( it->second );
        m_tree->EnsureVisible( it->second );
        m_eventsEnabled = true;
    }
}


// BOARD_LISTENER implementation
void BOARD_HIERARCHY_PANE::OnBoardItemAdded( BOARD& aBoard, BOARD_ITEM* aItem )
{
    if( aItem->Type() == PCB_FOOTPRINT_T )
        UpdateLabels();
}


void BOARD_HIERARCHY_PANE::OnBoardItemRemoved( BOARD& aBoard, BOARD_ITEM* aItem )
{
    if( aItem->Type() == PCB_FOOTPRINT_T )
        UpdateLabels();
}


void BOARD_HIERARCHY_PANE::OnBoardItemsAdded( BOARD& aBoard, std::vector<BOARD_ITEM*>& aItems )
{
    for( BOARD_ITEM* item : aItems )
    {
        if( item->Type() == PCB_FOOTPRINT_T )
        {
            UpdateLabels();
            return;
        }
    }
}


void BOARD_HIERARCHY_PANE::OnBoardItemsRemoved( BOARD& aBoard, std::vector<BOARD_ITEM*>& aItems )
{
    for( BOARD_ITEM* item : aItems )
    {
        if( item->Type() == PCB_FOOTPRINT_T )
        {
            UpdateLabels();
            return;
        }
    }
}


void BOARD_HIERARCHY_PANE::onSelectBoard( wxTreeEvent& aEvent )
{
    if( !m_eventsEnabled )
        return;

    wxTreeItemId selectedItem = aEvent.GetItem();

    if( !selectedItem.IsOk() )
        return;

    BOARD_TREE_ITEM_DATA* data =
            static_cast<BOARD_TREE_ITEM_DATA*>( m_tree->GetItemData( selectedItem ) );

    if( !data )
        return;

    // Don't reload if this is already the active board
    if( data->m_isActive )
        return;

    m_selectedBoardUuid = data->m_boardUuid;

    // Switch to the selected board
    m_frame->SwitchToBoard( data->m_boardUuid );
}


void BOARD_HIERARCHY_PANE::onTreeItemRightClick( wxTreeEvent& aEvent )
{
    wxTreeItemId item = aEvent.GetItem();

    if( item.IsOk() )
    {
        m_tree->SelectItem( item );
        onContextMenu( item );
    }
}


void BOARD_HIERARCHY_PANE::onContextMenu( wxTreeItemId aItem )
{
    BOARD_TREE_ITEM_DATA* data =
            static_cast<BOARD_TREE_ITEM_DATA*>( m_tree->GetItemData( aItem ) );

    if( !data )
        return;

    m_selectedBoardUuid = data->m_boardUuid;

    wxMenu menu;

    if( !data->m_isActive )
    {
        menu.Append( ID_SET_ACTIVE, _( "Set as Active Board" ) );
        menu.AppendSeparator();
    }

    menu.Append( ID_NEW_BOARD, _( "New Board..." ) );
    menu.Append( ID_DUPLICATE_BOARD, _( "Duplicate Board..." ) );
    menu.AppendSeparator();
    menu.Append( ID_RENAME_BOARD, _( "Rename..." ) );
    menu.Append( ID_BOARD_PROPERTIES, _( "Properties..." ) );
    menu.AppendSeparator();
    menu.Append( ID_DELETE_BOARD, _( "Delete Board..." ) );

    // Bind event handlers
    menu.Bind( wxEVT_COMMAND_MENU_SELECTED, &BOARD_HIERARCHY_PANE::onNewBoard, this, ID_NEW_BOARD );
    menu.Bind( wxEVT_COMMAND_MENU_SELECTED, &BOARD_HIERARCHY_PANE::onDuplicateBoard, this,
               ID_DUPLICATE_BOARD );
    menu.Bind( wxEVT_COMMAND_MENU_SELECTED, &BOARD_HIERARCHY_PANE::onRenameBoard, this,
               ID_RENAME_BOARD );
    menu.Bind( wxEVT_COMMAND_MENU_SELECTED, &BOARD_HIERARCHY_PANE::onBoardProperties, this,
               ID_BOARD_PROPERTIES );
    menu.Bind( wxEVT_COMMAND_MENU_SELECTED, &BOARD_HIERARCHY_PANE::onDeleteBoard, this,
               ID_DELETE_BOARD );
    menu.Bind( wxEVT_COMMAND_MENU_SELECTED, &BOARD_HIERARCHY_PANE::onSetActive, this,
               ID_SET_ACTIVE );

    PopupMenu( &menu );
}


void BOARD_HIERARCHY_PANE::onCharHook( wxKeyEvent& aEvent )
{
    wxTreeItemId selectedItem = m_tree->GetSelection();

    if( !selectedItem.IsOk() )
    {
        aEvent.Skip();
        return;
    }

    int keyCode = aEvent.GetKeyCode();

    switch( keyCode )
    {
    case WXK_F2:
        m_tree->EditLabel( selectedItem );
        break;

    case WXK_DELETE:
    case WXK_BACK:
    {
        wxCommandEvent evt;
        onDeleteBoard( evt );
        break;
    }

    default:
        aEvent.Skip();
        break;
    }
}


void BOARD_HIERARCHY_PANE::onTreeEditFinished( wxTreeEvent& aEvent )
{
    if( aEvent.IsEditCancelled() )
        return;

    wxTreeItemId item = aEvent.GetItem();
    BOARD_TREE_ITEM_DATA* data =
            static_cast<BOARD_TREE_ITEM_DATA*>( m_tree->GetItemData( item ) );

    if( !data )
        return;

    wxString newName = aEvent.GetLabel();

    if( newName.IsEmpty() )
    {
        aEvent.Veto();
        return;
    }

    // Update the board info in the project file
    PROJECT* project = &m_frame->Prj();
    if( !project )
        return;

    PROJECT_FILE& projectFile = project->GetProjectFile();

    BOARD_INFO* boardInfo = projectFile.GetBoardInfo( data->m_boardUuid );
    if( boardInfo )
    {
        boardInfo->displayName = newName;

        // If this is the active board, update its display name too
        if( data->m_isActive && m_frame->GetBoard() )
            m_frame->GetBoard()->SetDisplayName( newName );
    }
}


void BOARD_HIERARCHY_PANE::onNewBoard( wxCommandEvent& aEvent )
{
    wxTextEntryDialog dlg( this, _( "Enter name for new board:" ), _( "New Board" ),
                           _( "New Board" ) );

    if( dlg.ShowModal() != wxID_OK )
        return;

    wxString boardName = dlg.GetValue();

    if( boardName.IsEmpty() )
        return;

    PROJECT* project = &m_frame->Prj();
    if( !project )
        return;

    PROJECT_FILE& projectFile = project->GetProjectFile();

    // Create new board info
    KIID newUuid;
    wxString filename = boardName + wxT( ".kicad_pcb" );
    BOARD_INFO newInfo( newUuid, filename, boardName, false );

    projectFile.AddBoard( newInfo );

    // Refresh tree
    UpdateHierarchyTree();

    // Select new board
    SelectBoard( newUuid );
}


void BOARD_HIERARCHY_PANE::onRenameBoard( wxCommandEvent& aEvent )
{
    auto it = m_boardTreeItems.find( m_selectedBoardUuid );

    if( it != m_boardTreeItems.end() && it->second.IsOk() )
        m_tree->EditLabel( it->second );
}


void BOARD_HIERARCHY_PANE::onDuplicateBoard( wxCommandEvent& aEvent )
{
    PROJECT* project = &m_frame->Prj();
    if( !project )
        return;

    PROJECT_FILE& projectFile = project->GetProjectFile();

    const BOARD_INFO* sourceInfo = projectFile.GetBoardInfo( m_selectedBoardUuid );
    if( !sourceInfo )
        return;

    wxString defaultName = sourceInfo->displayName + _( " (Copy)" );

    wxTextEntryDialog dlg( this, _( "Enter name for duplicated board:" ), _( "Duplicate Board" ),
                           defaultName );

    if( dlg.ShowModal() != wxID_OK )
        return;

    wxString newName = dlg.GetValue();

    if( newName.IsEmpty() )
        return;

    // Create new board info as a copy with new UUID and name
    KIID newUuid;
    wxString filename = newName + wxT( ".kicad_pcb" );
    BOARD_INFO newInfo( newUuid, filename, newName, false );

    projectFile.AddBoard( newInfo );

    // TODO: Copy the actual board file on disk if it exists

    UpdateHierarchyTree();
    SelectBoard( newUuid );
}


void BOARD_HIERARCHY_PANE::onDeleteBoard( wxCommandEvent& aEvent )
{
    PROJECT* project = &m_frame->Prj();
    if( !project )
        return;

    PROJECT_FILE& projectFile = project->GetProjectFile();

    const BOARD_INFO* boardInfo = projectFile.GetBoardInfo( m_selectedBoardUuid );
    if( !boardInfo )
        return;

    // Don't allow deleting if this is the only board
    if( projectFile.GetBoardInfos().size() <= 1 )
    {
        wxMessageBox( _( "Cannot delete the only board in the project." ), _( "Delete Board" ),
                      wxOK | wxICON_WARNING, this );
        return;
    }

    // Don't allow deleting the active board directly
    if( boardInfo->isActive )
    {
        wxMessageBox( _( "Cannot delete the active board. Please switch to another board first." ),
                      _( "Delete Board" ), wxOK | wxICON_WARNING, this );
        return;
    }

    wxString msg = wxString::Format( _( "Are you sure you want to delete the board '%s'?\n\n"
                                        "This will remove the board from the project. "
                                        "The board file will NOT be deleted from disk." ),
                                     boardInfo->displayName );

    if( wxMessageBox( msg, _( "Delete Board" ), wxYES_NO | wxICON_QUESTION, this ) != wxYES )
        return;

    projectFile.RemoveBoard( m_selectedBoardUuid );
    UpdateHierarchyTree();
}


void BOARD_HIERARCHY_PANE::onBoardProperties( wxCommandEvent& aEvent )
{
    // TODO: Implement board properties dialog
    wxMessageBox( _( "Board properties dialog not yet implemented." ), _( "Properties" ),
                  wxOK | wxICON_INFORMATION, this );
}


void BOARD_HIERARCHY_PANE::onSetActive( wxCommandEvent& aEvent )
{
    if( m_selectedBoardUuid == niluuid )
        return;

    // Switch to the selected board
    m_frame->SwitchToBoard( m_selectedBoardUuid );
}
