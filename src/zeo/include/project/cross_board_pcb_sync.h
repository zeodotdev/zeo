/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef KICAD_CROSS_BOARD_PCB_SYNC_H
#define KICAD_CROSS_BOARD_PCB_SYNC_H

#include <kicommon.h>
#include <project/project_file.h>

#include <wx/string.h>

#include <vector>


/**
 * One cross-board net where two or more endpoints had different meaningful
 * local PCB net names. The resolver picks alphabetically first as the
 * canonical name; the others are surfaced so the user can fix them.
 */
struct KICOMMON_API MB_NET_NAME_CONFLICT
{
    wxString              chosen;      ///< Canonical name adopted (alphabetical first)
    std::vector<wxString> rejected;    ///< Other meaningful names we ignored
};


/**
 * One line in the preview / log shown by `DIALOG_MBS_SYNC_PCB`. Grouped by
 * `subProjectDisplayName` so the dialog can render a board-by-board summary.
 *
 * Severity carries the visual signal for the report panel:
 *   - INFO    — ordinary pad assignment (J1.5 = USB_DP)
 *   - WARNING — bulk net rename in the PCB ("D+" → "USB_DP" applies to N pads;
 *               this is the dangerous case the user must visually confirm
 *               since the renamer is a whole-file string replace and may
 *               touch pads beyond the connector if the local name happens
 *               to appear elsewhere)
 *   - ERROR   — missing footprint or missing pad (sync target not found)
 */
struct KICOMMON_API MB_SYNC_PREVIEW_LINE
{
    enum class SEVERITY
    {
        INFO,
        WARNING,
        ERR,
    };

    wxString subProjectDisplayName; ///< Empty for project-level lines (conflicts).
    wxString text;                  ///< Localized, ready to display.
    SEVERITY severity = SEVERITY::INFO;
};


struct KICOMMON_API MB_CROSS_BOARD_SYNC_RESULT
{
    int      subProjectsTouched = 0;  ///< Count of sub-project PCBs modified
    int      endpointsApplied   = 0;  ///< Count of (pad,net) assignments written
    int      endpointsMissing   = 0;  ///< Count of endpoints whose pad wasn't found
    int      netsRenamed        = 0;  ///< Cross-board nets renamed from local PCB nets
    std::vector<MB_NET_NAME_CONFLICT> conflicts;
    wxString summary;                 ///< Human-readable summary for UI display

    /// Itemized preview / log lines. Populated by both dry-run and live
    /// passes so the same rendering code can drive the preview AND the
    /// post-apply log in `DIALOG_MBS_SYNC_PCB`.
    std::vector<MB_SYNC_PREVIEW_LINE> previewLines;
};


/**
 * Apply the cross-board nets declared in a multi-board container `.kicad_pro`
 * (`PROJECT_FILE::IsMultiBoardContainer() == true`) to every sub-project's
 * `.kicad_pcb`. For each `MB_CROSS_BOARD_NET` endpoint we locate
 * `footprint(componentRef).pad(pinNumber)` in the matching sub-project's PCB
 * and set its net name to the cross-board net name. Footprints / pads that
 * can't be found are counted and reported but don't abort the operation.
 *
 * The transformation is performed as a text-level edit of the .kicad_pcb
 * files (replace / insert `(net "...")` inside the pad block) to avoid a
 * link-time dependency on pcbnew's parser. That trades some fragility for
 * module-boundary cleanliness; pcbnew re-reading the file will re-validate
 * on load.
 *
 * Safe to call with an empty cross-board net list (no-op).
 *
 * @param aDryRun When true, computes the full preview (counts, conflicts,
 *                per-sub-board preview lines) without writing any
 *                `.kicad_pcb` file AND without mutating the in-memory
 *                cross-board-net names on the container PROJECT_FILE.
 *                The caller can show the result to the user, then re-call
 *                with `aDryRun = false` to commit.
 */
KICOMMON_API MB_CROSS_BOARD_SYNC_RESULT
ApplyCrossBoardNetsToSubProjectPCBs( PROJECT_FILE& aProject, bool aDryRun = false );

#endif // KICAD_CROSS_BOARD_PCB_SYNC_H
