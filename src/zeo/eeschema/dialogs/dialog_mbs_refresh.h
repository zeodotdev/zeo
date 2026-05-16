/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef DIALOG_MBS_REFRESH_H
#define DIALOG_MBS_REFRESH_H

#include <dialog_shim.h>
#include <multi_board_mbs_refresh.h>

#include <vector>

class wxButton;
class wxCheckBox;
class WX_HTML_REPORT_PANEL;
class SCH_EDIT_FRAME;


/**
 * "Refresh Module Blocks from Sub-Projects" preview + apply dialog.
 *
 * Layout mirrors `DIALOG_UPDATE_FROM_PCB`:
 *   - Top: category checkboxes (one per `MBS_CHANGE::KIND`).
 *   - Middle: `WX_HTML_REPORT_PANEL` showing dry-run preview while
 *     settings change, and streaming apply log on Update click.
 *   - Bottom: Update / Cancel buttons.
 *
 * Construction takes the active SCH_EDIT_FRAME (used to read the
 * MBS screen + canvas view) and the precomputed change list. The
 * dialog mutates `aChanges`'s `checked` flags in place to honour
 * the category toggles, and runs `ApplyMbsRefreshChanges` itself
 * on Update so the report panel can stream progress.
 *
 * Returns from `ShowModal` with `wxID_OK` once an apply has run
 * successfully (so the caller can hand off newly-added blocks to
 * the move tool); `wxID_CANCEL` if the user closed without
 * applying. The completed result is exposed via `GetResult`.
 */
class DIALOG_MBS_REFRESH : public DIALOG_SHIM
{
public:
    DIALOG_MBS_REFRESH( SCH_EDIT_FRAME* aFrame, std::vector<MBS_CHANGE>& aChanges );

    ~DIALOG_MBS_REFRESH() override = default;

    /// Result of the most recent Apply pass (valid when ShowModal returned wxID_OK).
    const MBS_REFRESH_RESULT& GetResult() const { return m_result; }

private:
    void buildUI();

    /// Update each change's `checked` flag to match the category toggles.
    void syncCheckedFlagsFromSettings();

    /// Render dry-run preview into the report panel (info per change).
    void renderPreview();

    void onCategoryToggled( wxCommandEvent& aEvent );
    void onUpdate( wxCommandEvent& aEvent );

    SCH_EDIT_FRAME*          m_frame;
    std::vector<MBS_CHANGE>& m_changes;
    MBS_REFRESH_RESULT       m_result;
    bool                     m_applied;

    // Category toggles — one per MBS_CHANGE::KIND
    wxCheckBox*           m_cbAddBlocks;
    wxCheckBox*           m_cbRemoveBlocks;
    wxCheckBox*           m_cbAddPins;
    wxCheckBox*           m_cbRemovePins;
    wxCheckBox*           m_cbRenamePins;
    wxCheckBox*           m_cbUpdatePaths;
    wxCheckBox*           m_cbUpgradeUuids;

    WX_HTML_REPORT_PANEL* m_messagePanel;
    wxButton*             m_updateButton;
    wxButton*             m_cancelButton;
};

#endif // DIALOG_MBS_REFRESH_H
