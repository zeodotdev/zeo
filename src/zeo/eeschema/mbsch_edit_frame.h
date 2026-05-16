/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef MBSCH_EDIT_FRAME_H
#define MBSCH_EDIT_FRAME_H

#include <sch_edit_frame.h>
#include <project/project_file_observer.h>

#include <memory>


/**
 * Multi-board schematic editor frame.
 *
 * Thin subclass of SCH_EDIT_FRAME. Identifies itself as FRAME_MBSCH so
 * the shell can route `.kicad_mbs` files here and the menu / toolbar
 * builders can distinguish the trimmed MBS surface from the full
 * eeschema one. Shares parser, writer, painter, and connectivity with
 * eeschema — the on-disk format is identical to `.kicad_sch`.
 */
class MBSCH_EDIT_FRAME : public SCH_EDIT_FRAME, public PROJECT_FILE_OBSERVER
{
public:
    MBSCH_EDIT_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~MBSCH_EDIT_FRAME() override;

    /// T3: invoked when our container PROJECT_FILE's `multi_board.*`
    /// state changes (locally, from a peer window, from agent IPC, or
    /// from an external file edit picked up by the watcher). Refresh
    /// canvas / ERC markers / status bar appropriately.
    void OnMultiBoardFieldChanged( MULTI_BOARD_FIELD aField ) override;

protected:
    /**
     * Extract cross-board nets from the MBS topology and persist them
     * back to the enclosing `.kicad_pro` container. Runs after each
     * successful save of the MBS schematic.
     */
    void onSchematicSaved() override;

    wxString windowTitleSuffix() const override;

    /**
     * Rebuild the menu bar with the MBS-specific trimmed surface —
     * drops annotation, ERC, simulator, BOM, assign-footprints,
     * symbol/footprint editors, and other single-board features that
     * don't apply to a cross-board wiring sheet.
     */
    void doReCreateMenuBar() override;

    /**
     * Intercept cross-probe and selection-sync mails so they can
     * highlight / select the matching `SCH_MODULE_BLOCK` or subgraph
     * rather than falling through to the standard SCH_EDIT_FRAME
     * handlers (which look up `SCH_SYMBOL` items that don't exist
     * in a multi-board schematic).
     */
    void KiwayMailIn( KIWAY_MAIL_EVENT& aEvent ) override;

private:
    /**
     * Find and highlight the module block (and optionally pin) whose
     * component reference matches `aRef`. If `aPadOrPin` is non-empty,
     * select the specific pin on that block instead of the whole block.
     * No-op when no matching block exists — every peer FRAME_MBSCH
     * instance receives every cross-probe broadcast, so silently
     * ignoring non-matches is the correct behavior.
     */
    void crossProbeHighlightPart( const wxString& aRef, const wxString& aPadOrPin,
                                  const wxString& aSenderProjectPath = wxEmptyString );

    /**
     * Highlight the subgraph whose net name matches `aNetName` on this
     * MBS. Uses the standard CONNECTION_GRAPH lookup, so labels placed
     * on wires resolve to their canonical net name. Empty net name
     * clears any active highlight.
     */
    void crossProbeHighlightNet( const wxString& aNetName );

    /// T3: keeps us subscribed to the container PROJECT_FILE for the
    /// frame's lifetime. RAII unregisters in the dtor.
    std::unique_ptr<SCOPED_PROJECT_FILE_OBSERVER> m_projectFileObserver;
};

#endif // MBSCH_EDIT_FRAME_H
