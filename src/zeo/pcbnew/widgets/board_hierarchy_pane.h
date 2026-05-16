/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef BOARD_HIERARCHY_PANE_H
#define BOARD_HIERARCHY_PANE_H

#include <kiid.h>
#include <widgets/wx_panel.h>
#include <board.h>
#include <wx/treectrl.h>
#include <map>

class PCB_EDIT_FRAME;
class PROJECT;
struct BOARD_INFO;


/**
 * Store board information for each tree item.
 */
class BOARD_TREE_ITEM_DATA : public wxTreeItemData
{
public:
    KIID     m_boardUuid;
    wxString m_filename;
    bool     m_isActive;

    BOARD_TREE_ITEM_DATA( const KIID& aUuid, const wxString& aFilename, bool aActive )
        : wxTreeItemData(), m_boardUuid( aUuid ), m_filename( aFilename ), m_isActive( aActive )
    {}
};


/**
 * Tree control for board hierarchy with custom sorting.
 */
class BOARD_HIERARCHY_TREE : public wxTreeCtrl
{
public:
    BOARD_HIERARCHY_TREE( wxWindow* parent );

    int OnCompareItems( const wxTreeItemId& item1, const wxTreeItemId& item2 ) override;

private:
    wxDECLARE_ABSTRACT_CLASS( BOARD_HIERARCHY_TREE );
};


/**
 * Panel showing the board hierarchy for multi-board projects.
 *
 * Displays a tree of all boards in the project with their component counts
 * and sync status. Allows switching between boards and managing boards.
 */
class BOARD_HIERARCHY_PANE : public WX_PANEL, public BOARD_LISTENER
{
public:
    enum ContextMenuAction
    {
        ID_NEW_BOARD = wxID_HIGHEST + 1,
        ID_RENAME_BOARD,
        ID_DUPLICATE_BOARD,
        ID_DELETE_BOARD,
        ID_BOARD_PROPERTIES,
        ID_SET_ACTIVE
    };

    BOARD_HIERARCHY_PANE( PCB_EDIT_FRAME* aParent );
    virtual ~BOARD_HIERARCHY_PANE();

    /**
     * Rebuild the hierarchy tree from the project's board list.
     */
    void UpdateHierarchyTree();

    /**
     * Update the tree selection to match the currently active board.
     */
    void UpdateHierarchySelection();

    /**
     * Update only the labels (component count, sync status) without rebuilding.
     */
    void UpdateLabels();

    /**
     * Select a specific board in the tree.
     * @param aBoardUuid The UUID of the board to select
     */
    void SelectBoard( const KIID& aBoardUuid );

    // BOARD_LISTENER implementation
    void OnBoardItemAdded( BOARD& aBoard, BOARD_ITEM* aItem ) override;
    void OnBoardItemRemoved( BOARD& aBoard, BOARD_ITEM* aItem ) override;
    void OnBoardItemsAdded( BOARD& aBoard, std::vector<BOARD_ITEM*>& aItems ) override;
    void OnBoardItemsRemoved( BOARD& aBoard, std::vector<BOARD_ITEM*>& aItems ) override;

private:
    /**
     * Build the tree hierarchy from the project's board infos.
     */
    void buildTree();

    /**
     * Format the display string for a board.
     * @param aDisplayName The board's display name
     * @param aComponentCount Number of footprints on the board
     * @param aIsActive Whether this is the active board
     */
    wxString formatBoardString( const wxString& aDisplayName, int aComponentCount, bool aIsActive );

    /**
     * Get the component count for a board.
     */
    int getComponentCount( const BOARD_INFO& aBoardInfo );

    // Event handlers
    void onSelectBoard( wxTreeEvent& aEvent );
    void onTreeItemRightClick( wxTreeEvent& aEvent );
    void onContextMenu( wxTreeItemId aItem );
    void onCharHook( wxKeyEvent& aEvent );
    void onTreeEditFinished( wxTreeEvent& aEvent );

    // Context menu action handlers
    void onNewBoard( wxCommandEvent& aEvent );
    void onRenameBoard( wxCommandEvent& aEvent );
    void onDuplicateBoard( wxCommandEvent& aEvent );
    void onDeleteBoard( wxCommandEvent& aEvent );
    void onBoardProperties( wxCommandEvent& aEvent );
    void onSetActive( wxCommandEvent& aEvent );

private:
    PCB_EDIT_FRAME*       m_frame;
    BOARD_HIERARCHY_TREE* m_tree;
    wxTreeItemId          m_rootItem;

    // Map board UUID to tree item for quick lookup
    std::map<KIID, wxTreeItemId> m_boardTreeItems;

    // Currently selected board UUID (for context menu actions)
    KIID m_selectedBoardUuid;

    bool m_eventsEnabled;
};

#endif // BOARD_HIERARCHY_PANE_H
