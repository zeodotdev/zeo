/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef DIALOG_MULTI_BOARD_SYNC_H
#define DIALOG_MULTI_BOARD_SYNC_H

#include <dialog_shim.h>
#include <kiid.h>
#include <netlist_reader/multi_board_netlist_updater.h>

#include <map>
#include <memory>

class COMPONENT_ASSIGNMENT_MANAGER;
class NETLIST;
class PCB_EDIT_FRAME;
class PROJECT;
class REPORTER;
class WX_HTML_REPORT_BOX;

class wxButton;
class wxCheckBox;
class wxDataViewCtrl;
class wxNotebook;
class wxPanel;
class wxStaticText;


/**
 * Panel showing sync status for a single board.
 */
class BOARD_SYNC_PANEL : public wxPanel
{
public:
    BOARD_SYNC_PANEL( wxWindow* aParent, const KIID& aBoardUuid, const wxString& aBoardName );

    void UpdateStatus( const BOARD_SYNC_STATUS& aStatus );

    const KIID& GetBoardUuid() const { return m_boardUuid; }

private:
    void createControls();

    KIID            m_boardUuid;
    wxString        m_boardName;

    wxStaticText*   m_statusLabel;
    wxStaticText*   m_addLabel;
    wxStaticText*   m_removeLabel;
    wxStaticText*   m_updateLabel;
    wxDataViewCtrl* m_componentList;
};


/**
 * Dialog for synchronizing multiple boards from a schematic netlist.
 *
 * This dialog allows users to:
 * - View sync status for each board in the project
 * - Preview changes before applying
 * - Apply changes to selected boards or all boards
 * - View and manage cross-board connector connections
 */
class DIALOG_MULTI_BOARD_SYNC : public DIALOG_SHIM
{
public:
    DIALOG_MULTI_BOARD_SYNC( PCB_EDIT_FRAME* aParent, NETLIST* aNetlist );
    ~DIALOG_MULTI_BOARD_SYNC();

    /**
     * Set the component assignment manager.
     */
    void SetAssignmentManager( COMPONENT_ASSIGNMENT_MANAGER* aManager );

    /**
     * Run a test sync (dry run) and update the display.
     */
    void RunTestSync();

    /**
     * Apply sync to the currently selected board tab.
     */
    bool ApplyToSelected();

    /**
     * Apply sync to all boards.
     */
    bool ApplyToAll();

private:
    void createControls();
    void createBoardTabs();
    void createCrossBoardPanel();
    void createOptionsPanel();
    void updateStatusSummary();
    void refreshBoardPanels();

    void onTestSync( wxCommandEvent& aEvent );
    void onApplySelected( wxCommandEvent& aEvent );
    void onApplyAll( wxCommandEvent& aEvent );
    void onClose( wxCommandEvent& aEvent );
    void onNotebookPageChanged( wxBookCtrlEvent& aEvent );

    PCB_EDIT_FRAME*                         m_frame;
    PROJECT*                                m_project;
    NETLIST*                                m_netlist;
    COMPONENT_ASSIGNMENT_MANAGER*           m_assignmentManager;
    std::unique_ptr<MULTI_BOARD_NETLIST_UPDATER> m_updater;

    // Controls
    wxNotebook*                             m_boardNotebook;
    std::map<KIID, BOARD_SYNC_PANEL*>       m_boardPanels;
    wxPanel*                                m_crossBoardPanel;
    wxDataViewCtrl*                         m_crossBoardView;
    wxPanel*                                m_optionsPanel;
    wxStaticText*                           m_summaryLabel;
    WX_HTML_REPORT_BOX*                     m_reportBox;

    // Options
    wxCheckBox*                             m_deleteUnusedCheck;
    wxCheckBox*                             m_replaceFootprintsCheck;
    wxCheckBox*                             m_updateFieldsCheck;

    // Buttons
    wxButton*                               m_testSyncButton;
    wxButton*                               m_applySelectedButton;
    wxButton*                               m_applyAllButton;
    wxButton*                               m_closeButton;

    // Cached sync status
    std::map<KIID, BOARD_SYNC_STATUS>       m_syncStatus;
};

#endif // DIALOG_MULTI_BOARD_SYNC_H
