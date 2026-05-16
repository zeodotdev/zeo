/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef DIALOG_CROSS_BOARD_CONNECTORS_H
#define DIALOG_CROSS_BOARD_CONNECTORS_H

#include <dialog_shim.h>
#include <kiid.h>
#include <project/project_file.h>

#include <map>
#include <vector>

class PCB_EDIT_FRAME;
class PROJECT;

class wxButton;
class wxChoice;
class wxDataViewCtrl;
class wxDataViewListCtrl;
class wxListBox;
class wxStaticText;


/**
 * Represents a connector pair for display in the dialog.
 */
struct CONNECTOR_PAIR_INFO
{
    wxString    board1Name;
    KIID        board1Uuid;
    wxString    connector1Ref;
    wxString    board2Name;
    KIID        board2Uuid;
    wxString    connector2Ref;
    int         pinCount;
    int         matchedPins;
    int         mismatchedPins;
};


/**
 * Dialog for managing cross-board connector pairs.
 *
 * This dialog allows users to:
 * - View existing connector pairs between boards
 * - Add new connector pairs
 * - Remove connector pairs
 * - View pin-to-pin mappings and net matching status
 * - Auto-detect connectors from schematic netlist
 */
class DIALOG_CROSS_BOARD_CONNECTORS : public DIALOG_SHIM
{
public:
    DIALOG_CROSS_BOARD_CONNECTORS( PCB_EDIT_FRAME* aParent );
    ~DIALOG_CROSS_BOARD_CONNECTORS();

    /**
     * Refresh the connector list from the project file.
     */
    void RefreshConnectorList();

    /**
     * Get the selected connector pair index.
     * @return Index of selected pair, or -1 if none selected
     */
    int GetSelectedConnectorPair() const;

private:
    void createControls();
    void populateConnectorList();
    void populatePinMappingView( int aConnectorPairIndex );
    void updateStatusDisplay();

    // Event handlers
    void onConnectorSelected( wxCommandEvent& aEvent );
    void onAddConnectorPair( wxCommandEvent& aEvent );
    void onRemoveConnectorPair( wxCommandEvent& aEvent );
    void onAutoDetect( wxCommandEvent& aEvent );
    void onClose( wxCommandEvent& aEvent );
    void onSave( wxCommandEvent& aEvent );

    /**
     * Show dialog to add a new connector pair.
     */
    bool showAddConnectorDialog();

    /**
     * Validate net matching for a connector pair.
     */
    void validateConnectorPair( CONNECTOR_PAIR_INFO& aPair );

    PCB_EDIT_FRAME*                     m_frame;
    PROJECT*                            m_project;

    // Controls
    wxListBox*                          m_connectorListBox;
    wxDataViewListCtrl*                 m_pinMappingView;
    wxStaticText*                       m_statusLabel;
    wxButton*                           m_addButton;
    wxButton*                           m_removeButton;
    wxButton*                           m_autoDetectButton;
    wxButton*                           m_saveButton;
    wxButton*                           m_closeButton;

    // Data
    std::vector<CONNECTOR_PAIR_INFO>    m_connectorPairs;
    int                                 m_selectedPairIndex;
};


/**
 * Dialog for adding a new connector pair.
 */
class DIALOG_ADD_CONNECTOR_PAIR : public DIALOG_SHIM
{
public:
    DIALOG_ADD_CONNECTOR_PAIR( wxWindow* aParent, PROJECT* aProject );

    wxString GetBoard1Name() const;
    KIID GetBoard1Uuid() const;
    wxString GetConnector1Ref() const;
    wxString GetBoard2Name() const;
    KIID GetBoard2Uuid() const;
    wxString GetConnector2Ref() const;

private:
    void createControls();
    void populateBoardChoices();
    void populateConnectorChoices( wxChoice* aChoice, const KIID& aBoardUuid );

    void onBoard1Changed( wxCommandEvent& aEvent );
    void onBoard2Changed( wxCommandEvent& aEvent );

    PROJECT*        m_project;

    wxChoice*       m_board1Choice;
    wxChoice*       m_connector1Choice;
    wxChoice*       m_board2Choice;
    wxChoice*       m_connector2Choice;

    std::vector<BOARD_INFO>     m_boardInfos;
};

#endif // DIALOG_CROSS_BOARD_CONNECTORS_H
