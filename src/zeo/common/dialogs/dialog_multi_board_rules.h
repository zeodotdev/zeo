/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef DIALOG_MULTI_BOARD_RULES_H
#define DIALOG_MULTI_BOARD_RULES_H

#include <dialog_shim.h>


class PROJECT_FILE;
class wxButton;
class wxGrid;
class wxNotebook;


/**
 * Modal authoring dialog for the cross-board ERC/DRC rules persisted on a
 * multi-board container `.kicad_pro` under `multi_board.*`.
 *
 * Five tabs, one per rule map:
 *   1. Min power pins  — net → min pin count
 *   2. Max length      — net → max total length across boards (mm)
 *   3. Diff pairs      — list of (P-net, N-net) pairs that must be coherent
 *   4. Current         — net → expected current + per-pin rating
 *   5. Voltage drop    — net → expected current + max drop (+ optional R overrides)
 *
 * Each tab is a `wxGrid` plus Add Row / Remove Selected buttons. OK / Apply
 * write through the T3 observable setters wrapped in `PROJECT_FILE_SUSPEND_NOTIFY`
 * so subscribers (peer windows, DRC providers re-reading on next Run) see one
 * coalesced event per rule type. Cancel discards in-memory changes — the dialog
 * works against grid state until Apply.
 */
class DIALOG_MULTI_BOARD_RULES : public DIALOG_SHIM
{
public:
    /**
     * @param aParent  any wxWindow (frame or other dialog).
     * @param aProject the container PROJECT_FILE to edit. Must be non-null.
     *                 The dialog reads current rule maps in ctor and writes
     *                 them back via setters on Apply / OK.
     */
    DIALOG_MULTI_BOARD_RULES( wxWindow* aParent, PROJECT_FILE* aProject );

    ~DIALOG_MULTI_BOARD_RULES() override;

private:
    void buildUI();

    void buildMinPowerPinsTab( wxNotebook* aNotebook );
    void buildMaxLengthTab( wxNotebook* aNotebook );
    void buildDiffPairsTab( wxNotebook* aNotebook );
    void buildCurrentTab( wxNotebook* aNotebook );
    void buildVoltageTab( wxNotebook* aNotebook );

    /// Helper that lays out a `wxGrid` + Add Row / Remove Selected buttons
    /// in a given panel. Returns the grid for the caller to populate.
    wxGrid* buildGridPanel( wxWindow* aParent, const wxArrayString& aColumnLabels,
                            const wxArrayInt& aColumnWidths, wxButton** aAddBtnOut,
                            wxButton** aRemoveBtnOut );

    /// Pull current rule maps off the project and populate each grid.
    void populateGridsFromProject();

    /// Apply: write each grid's rows back to the project via T3 setters,
    /// wrapped in PROJECT_FILE_SUSPEND_NOTIFY so observers get one event
    /// per rule type. Returns false on validation failure (and reports
    /// the offending row + column to the user).
    bool applyChanges();

    void onAddRow( wxCommandEvent& aEvent );
    void onRemoveSelected( wxCommandEvent& aEvent );
    void onOK( wxCommandEvent& aEvent );
    void onApply( wxCommandEvent& aEvent );
    void onCancel( wxCommandEvent& aEvent );

private:
    PROJECT_FILE* m_project;

    // Per-tab grids. Allocated in build*Tab, owned by the wxNotebook hierarchy
    // (Wx parent-child cleanup), referenced here for populate / apply paths.
    wxGrid* m_minPowerGrid;
    wxGrid* m_maxLengthGrid;
    wxGrid* m_diffPairGrid;
    wxGrid* m_currentGrid;
    wxGrid* m_voltageGrid;

    // Each Add Row button is bound to its tab's grid via wxClientData on the
    // event so a single handler can route correctly. The button → grid map
    // is the source of truth.
    std::map<int, wxGrid*> m_buttonToGrid;
};


#endif // DIALOG_MULTI_BOARD_RULES_H
