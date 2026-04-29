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
 */

#ifndef KICAD_MULTI_BOARD_SCAN_H
#define KICAD_MULTI_BOARD_SCAN_H

#include <kicommon.h>
#include <pin_type.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <map>
#include <set>
#include <utility>
#include <vector>


/**
 * One pad on a connector footprint discovered on a sub-project's PCB.
 * Captures both the pad number used to address it and the net name
 * currently assigned to the pad (used to label MBS pins with their
 * semantic net, not just a raw pad number).
 */
struct MULTI_BOARD_PAD_INFO
{
    wxString padNumber;   ///< "1", "A12", "GND"...
    wxString netName;     ///< "" if the pad has no net, else e.g. "+3V3"

    /**
     * Best-effort electrical pin type derived from the pad's net name.
     * Lets MBS-side ERC drive the same pin-to-pin matrix the regular
     * schematic ERC uses — power_in pins on conflicting nets surface
     * as "power not driven" / "pin to pin" violations.
     *
     * Defaults to PT_PASSIVE; populated by `scanConnectorPads` based
     * on whether the netname looks like a power or ground net. Heuristic
     * — proper extraction would walk the connector symbol's pin defs
     * in the sub-project schematic, deferred for v2.
     */
    ELECTRICAL_PINTYPE electricalType = ELECTRICAL_PINTYPE::PT_PASSIVE;
};


/// Given a sub-project .kicad_pro path, return the matching .kicad_sch.
KICOMMON_API wxFileName MultiBoardMainSchematic( const wxFileName& aProFile );

/// Given a sub-project .kicad_pro path, return the matching .kicad_pcb.
KICOMMON_API wxFileName MultiBoardMainPcb( const wxFileName& aProFile );

/**
 * Scan a sub-project's main schematic (recursing into hierarchical sub-sheets)
 * for connector-class symbol references ("J1", "P3", etc.). Returns the
 * union, sorted alphabetically.
 */
KICOMMON_API std::vector<wxString>
MultiBoardScanConnectorReferences( const wxFileName& aSchFile );

/**
 * Scan a sub-project's .kicad_pcb for connector footprints and return
 * { reference → [pad info] }. Order preserves PCB order.
 */
KICOMMON_API std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>>
MultiBoardScanConnectorPads( const wxFileName& aPcbFile );


class PROJECT_FILE;

/**
 * Ensure the MBS schematic file for this multi-board container exists.
 *
 * If `aContainer.GetMbsFileName()` is empty it will be populated
 * (default: `<aContainerBasename>_mbs.kicad_sch`). If the file does not
 * exist yet, a minimal valid KiCad s-expression schematic is generated
 * with one module_block per sub-project connector so eeschema can open
 * the file.
 *
 * @return absolute path to the MBS file on success, empty on failure.
 */
KICOMMON_API wxFileName EnsureMbsFile( PROJECT_FILE& aContainer,
                                       const wxString& aContainerBasename );


/**
 * Pick a human-friendly display label for an MBS pin: prefer the pad's
 * local net name when meaningful; fall back to "<ref>.<padNum>" or just
 * the ref when no pad number is known.
 */
KICOMMON_API wxString MultiBoardPinLabel( const wxString& aRef,
                                          const MULTI_BOARD_PAD_INFO& aPad );


/**
 * Walk up from a sub-project's `.kicad_pro` looking for the enclosing
 * multi-board container, then collect every (componentRef, pinNumber)
 * pair on this sub-project that participates in a cross-board net.
 *
 * Returns an empty set if no container is found within 6 directory
 * levels, the container's project file fails to load, or this sub-
 * project has no cross-board endpoints declared in the container.
 *
 * Used by ERC / DRC to suppress no-driver / single-pad-net markers
 * on connector pins that are wired through the multi-board topology.
 */
KICOMMON_API std::set<std::pair<wxString, wxString>>
MultiBoardCollectCrossBoardEndpointsForSubProject( const wxFileName& aSubProjectPro );


/**
 * One MBS-declared binding: which connector pad on this sub-project
 * carries which cross-board net. Returned by
 * `MultiBoardCollectCrossBoardBindingsForSubProject` and consumed by
 * the cross-board binding DRC check.
 */
struct KICOMMON_API MULTI_BOARD_CROSS_BOARD_BINDING
{
    wxString componentRef;   ///< Connector ref on this sub-project, e.g. "J1"
    wxString pinNumber;      ///< Pad number on the connector, e.g. "3"
    wxString netName;        ///< MBS-declared canonical net name
};


/**
 * Like `MultiBoardCollectCrossBoardEndpointsForSubProject` but returns
 * full binding info: each entry carries the MBS-declared net name in
 * addition to the (componentRef, pinNumber) pair. Used by the DRC
 * check that verifies each connector pad on this board carries the
 * net the MBS declares for that block pin.
 *
 * Empty result has the same meanings as the simpler variant: no
 * container found, container failed to load, or no bindings declared
 * for this sub-project.
 */
KICOMMON_API std::vector<MULTI_BOARD_CROSS_BOARD_BINDING>
MultiBoardCollectCrossBoardBindingsForSubProject( const wxFileName& aSubProjectPro );


/**
 * One cross-board "ping" the originating frame should send so that a
 * peer sub-project's editor highlights the corresponding pad/pin.
 *
 * Each probe targets a specific (sub-project, connector pin) and is
 * scoped via `targetSubProjectAbsPath` so the receiving editor can
 * filter on its own project path before acting (avoids highlighting
 * unrelated boards that happen to share a connector reference).
 */
struct KICOMMON_API MULTI_BOARD_CROSS_BOARD_PROBE
{
    wxString targetSubProjectAbsPath;   ///< Absolute path to target .kicad_pro
    wxString componentRef;              ///< e.g. "J2"
    wxString pinNumber;                 ///< e.g. "3"
};


/**
 * Given a sub-project + the local net name the user just clicked on,
 * walk up to the enclosing multi-board container and return the list
 * of cross-board probes the sender should fan out so that every other
 * sub-project on the same cross-board net highlights its corresponding
 * connector pin.
 *
 * Match logic:
 *  - The container's `cross_board_nets` is consulted.
 *  - For each cross-board net, an endpoint matches the sender when its
 *    `subProjectUuid` equals the sender's sub-project's UUID AND the
 *    endpoint's `pinName` (after Unescape + sheet-prefix strip + `_N`
 *    disambig strip) equals the sender's local net name (same
 *    normalisation).
 *  - The net's canonical `name` is also checked against the sender's
 *    local name as a fallback (covers MBSCH-canonical broadcasts).
 *
 * Probes are emitted for every endpoint of each matching net EXCEPT
 * the sender's own — the sender already highlighted its local net
 * directly.
 *
 * Returns an empty list when:
 *  - The sender isn't part of a multi-board container
 *  - No cross-board net touches this sender on this local net
 *  - The container's project file fails to load
 */
KICOMMON_API std::vector<MULTI_BOARD_CROSS_BOARD_PROBE>
MultiBoardCollectCrossBoardProbesForLocalNet( const wxFileName& aSubProjectPro,
                                              const wxString& aLocalNetName );


/**
 * Helper: take a probe and serialise it into the
 * `$PART: "ref" $PAD: "num" $PROJECT: "<path>"` packet format that the
 * existing `ExecuteRemoteCommand` handlers in eeschema and pcbnew
 * already understand for cross-board net highlighting.
 */
KICOMMON_API wxString MultiBoardFormatCrossBoardProbe(
        const MULTI_BOARD_CROSS_BOARD_PROBE& aProbe );


/**
 * One endpoint of a cross-board net, expressed in this-board-or-sibling
 * terms. Carries the resolved sub-project display name so callers can
 * print user-friendly violation messages without re-loading the
 * container project.
 */
struct KICOMMON_API MULTI_BOARD_NET_ENDPOINT_VIEW
{
    KIID     subProjectUuid;
    wxString subProjectName;        ///< displayName falling back to name
    wxString subProjectAbsPath;     ///< absolute .kicad_pro path of the sibling
    wxString componentRef;
    wxString pinNumber;
    wxString pinName;
};


/**
 * One cross-board net that touches the queried sub-project. The local
 * (this-board) endpoints are split from sibling endpoints so a per-board
 * DRC test provider can iterate sibling boards without re-filtering.
 */
struct KICOMMON_API MULTI_BOARD_CROSS_BOARD_NET_VIEW
{
    wxString netName;
    KIID     netUuid;
    std::vector<MULTI_BOARD_NET_ENDPOINT_VIEW> myEndpoints;
    std::vector<MULTI_BOARD_NET_ENDPOINT_VIEW> siblingEndpoints;
};


/**
 * Composite view of the multi-board container as seen from one sub-
 * project. Aggregates everything a per-board cross-board check needs
 * in a single resolution pass:
 *  - the container's absolute .kicad_pro path (for path-based sibling
 *    board loading),
 *  - this sub-project's UUID inside the container,
 *  - the cross-board nets that touch this sub-project, with each net's
 *    endpoints split into local vs sibling sets.
 *
 * Empty `containerProAbsPath` means resolution failed: no container
 * within 6 directory levels, container failed to load, or this sub-
 * project isn't a member of any container.
 */
struct KICOMMON_API MULTI_BOARD_CONTAINER_VIEW
{
    wxString containerProAbsPath;
    KIID     mySubProjectUuid;
    std::vector<MULTI_BOARD_CROSS_BOARD_NET_VIEW> crossBoardNets;
};


KICOMMON_API MULTI_BOARD_CONTAINER_VIEW
MultiBoardBuildContainerView( const wxFileName& aSubProjectPro );

#endif // KICAD_MULTI_BOARD_SCAN_H
