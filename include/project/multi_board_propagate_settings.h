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

#ifndef MULTI_BOARD_PROPAGATE_SETTINGS_H
#define MULTI_BOARD_PROPAGATE_SETTINGS_H

#include <kicommon.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <wx/string.h>

class NETCLASS;
class PROJECT;
class PROJECT_FILE;


/*
    M7.2 — net_settings replication from container to sub-projects.

    Each sub-project's `.kicad_pro` carries a complete `net_settings`
    block — same shape as today, no flags. When the container project
    is saved, this propagator pushes the container's net classes into
    every loaded sub-project. Per-class merge policy:

        - Sub-project has no entry with that name  → copy in silently
        - Same name + identical settings           → no-op silently
        - Same name + different settings           → CONFLICT — caller
                                                     resolves via the
                                                     supplied callback
        - Sub-project has a class not in container → leave untouched
                                                     (local-only, kept)

    Sub-projects that are NOT loaded in SETTINGS_MANAGER are skipped
    (we don't write to their `.kicad_pro` from disk in v1; they pick up
    the new container state next time they're opened, where the same
    propagation runs again on the next container save).

    The propagator does NOT save sub-projects automatically. Caller
    decides whether to flush each sub-project's `.kicad_pro` after
    propagation completes — typically via the existing T3 SUSPEND_NOTIFY
    + SaveToFile path so observers fire once per affected sub-project.
*/


/// One per-class conflict surfaced to the caller for resolution.
struct KICOMMON_API MULTI_BOARD_NET_CLASS_CONFLICT
{
    /// Sub-project owning the conflicting entry. Display string for the
    /// dialog (uses display name if set, else internal name).
    wxString subProjectDisplayName;

    /// Absolute path to the sub-project's `.kicad_pro` (so callers can
    /// route a save back to the right file).
    wxString subProjectAbsPath;

    /// Class name (same on both sides — that's what makes it a conflict).
    wxString netClassName;

    /// Snapshot of the container's net class (what would be written if
    /// the user picks "Use container value").
    std::shared_ptr<NETCLASS> containerNetClass;

    /// Snapshot of the sub-project's current net class (what would be
    /// preserved if the user picks "Keep sub-project value").
    std::shared_ptr<NETCLASS> subProjectNetClass;
};


/// User's resolution choice per conflict.
enum class MULTI_BOARD_NET_CLASS_RESOLUTION
{
    USE_CONTAINER,    ///< Overwrite the sub-project's class with the container's.
    KEEP_SUB_PROJECT, ///< Leave the sub-project's class alone.
    SKIP              ///< Don't apply this class on this sub-project at all.
};


/**
 * Callback the propagator invokes once per conflict. Returns the user's
 * resolution. Called synchronously from the propagator — typically the
 * caller pumps a modal dialog and returns the chosen value.
 *
 * To suppress conflict handling entirely (test paths, headless saves
 * where the policy is "always use container"), pass a callback that
 * always returns USE_CONTAINER.
 */
using MULTI_BOARD_NET_CLASS_CONFLICT_RESOLVER =
        std::function<MULTI_BOARD_NET_CLASS_RESOLUTION( const MULTI_BOARD_NET_CLASS_CONFLICT& )>;


/**
 * Field-by-field equivalence test for two NETCLASS instances.
 *
 * `NETCLASS::operator==` is unsuitable for cross-instance comparison
 * (it only inspects `m_constituents`, which always contains `this`, so
 * two distinct instances are NEVER equal regardless of their actual
 * settings). This helper compares the user-meaningful fields that
 * round-trip through the `.kicad_pro` JSON: name, priority, tuning
 * profile, every optional clearance / track / via / diff-pair / wire /
 * bus, line style, and both colors.
 *
 * Use this anywhere you'd be tempted to write `*a == *b` for two
 * NETCLASSes — the propagator and the net-class panel's Status column
 * both rely on it to decide Shared vs Conflict.
 */
KICOMMON_API bool MultiBoardNetclassesEquivalent( const class NETCLASS& aA,
                                                   const class NETCLASS& aB );


/// Aggregated outcome of one propagation pass.
struct KICOMMON_API MULTI_BOARD_PROPAGATE_RESULT
{
    /// Sub-projects iterated (loaded + currently in SETTINGS_MANAGER).
    int subProjectsTouched = 0;

    /// Net classes copied into a sub-project that previously had no
    /// entry with that name.
    int classesAdded = 0;

    /// Net classes that already matched — no write performed.
    int classesUnchanged = 0;

    /// Net classes overwritten via USE_CONTAINER resolution.
    int classesOverwritten = 0;

    /// Net classes the user chose to keep on the sub-project (KEEP_SUB_PROJECT).
    int classesKept = 0;

    /// Conflicts the user explicitly skipped (SKIP).
    int classesSkipped = 0;

    /// Sub-project paths whose `.kicad_pro` was mutated. Caller should
    /// SaveToFile each of these (ideally inside a SUSPEND_NOTIFY guard).
    std::vector<wxString> mutatedSubProjectPaths;

    /// Sub-project paths the propagator loaded into SETTINGS_MANAGER
    /// itself (because they weren't already open) so it could mutate them.
    /// Caller MUST `UnloadProject` each of these after handling
    /// `mutatedSubProjectPaths` — otherwise they leak as phantom open
    /// projects and confuse downstream Prj()/active-project semantics.
    /// Always a strict subset of the propagator's iteration set; never
    /// includes a sub-project that was already open before the call.
    std::vector<wxString> ephemerallyLoadedSubProjectPaths;
};


/**
 * Propagate net classes from a multi-board container to every loaded
 * sub-project. Returns aggregated stats + the list of sub-project paths
 * that were mutated and need a SaveToFile.
 *
 * No-op when `aContainer` is not a multi-board container.
 *
 * The propagator handles per-class merge policy in-process; the
 * resolver callback is only invoked for the "same name, different
 * settings" case. On conflict-free runs, no callback fires and the
 * caller can pass `nullptr` (which is treated as USE_CONTAINER for any
 * conflicts that surface — defensive fallback).
 */
KICOMMON_API MULTI_BOARD_PROPAGATE_RESULT MultiBoardPropagateNetSettings(
        PROJECT& aContainer,
        const MULTI_BOARD_NET_CLASS_CONFLICT_RESOLVER& aResolver = nullptr );


/**
 * UI-aware wrapper around `MultiBoardPropagateNetSettings`. Builds a
 * resolver that pops a modal `DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT`
 * for each conflict, honouring the dialog's "Apply choice to all
 * remaining conflicts" checkbox so the user only sees subsequent
 * conflicts when they ask to.
 *
 * After propagation, calls `SaveToFile` on each mutated sub-project
 * (the propagator only mutates in-memory `NET_SETTINGS`; it doesn't
 * touch disk on its own — that's the caller's job here). Saves are
 * wrapped in `PROJECT_FILE_SUSPEND_NOTIFY` so observers fire once
 * per affected sub-project rather than per-class.
 *
 * Returns the same result struct as the underlying propagator. The
 * mutatedSubProjectPaths field reflects what was actually written to
 * disk (KEEP_SUB_PROJECT / SKIP entries don't appear).
 */
MULTI_BOARD_PROPAGATE_RESULT MultiBoardPropagateNetSettingsWithDialog(
        class PROJECT& aContainer, class wxWindow* aDialogParent );


/// Classification for a container net class in the aggregated MBS view.
/// Sub-project-only classes are represented separately via
/// `MULTI_BOARD_NETCLASS_LOCAL_ROW`, so this enum only covers the three
/// container-side cases.
enum class MULTI_BOARD_NETCLASS_VIEW_STATUS
{
    SOURCE,      ///< No sub-project defines a class with this name.
    SHARED,      ///< Every sub-project that has this class matches the container.
    CONFLICT,    ///< At least one sub-project's same-name class diverges.
};


/// One row representing a class that exists on a sub-project but NOT on the
/// container. Surfaced in the MBS Setup → Net Classes panel as a read-only
/// "Only on <board>" row so the user can see every cross-board class in one
/// place.
struct KICOMMON_API MULTI_BOARD_NETCLASS_LOCAL_ROW
{
    /// Owned clone (not shared with the transient sub-project PROJECT_FILE
    /// that produced it — that transient is destroyed before this row is
    /// returned). Safe for the panel to hold for its full lifetime.
    std::shared_ptr<class NETCLASS> netclass;

    /// Sub-project's display name (or short name if no display set). Used to
    /// format the status cell as "Only on <name>".
    wxString subProjectDisplayName;
};


/// Aggregated view of net classes across a multi-board container and its
/// sub-projects, as the MBS Setup → Net Classes panel needs to display
/// them.
struct KICOMMON_API MULTI_BOARD_NETCLASS_VIEW
{
    /// For each container class name: how it compares against the sub-projects.
    /// Includes the Default class.
    std::map<wxString, MULTI_BOARD_NETCLASS_VIEW_STATUS> containerStatusByName;

    /// Sub-project classes whose name is NOT present on the container. One
    /// row per (sub-project × class) occurrence: if two sub-projects each
    /// define a class not on the container with the same name, both appear
    /// here as separate rows so the user can see each board's local copy.
    std::vector<MULTI_BOARD_NETCLASS_LOCAL_ROW> localOnlyRows;
};


/**
 * Build the aggregated cross-board net class view consumed by the MBS
 * Setup → Net Classes panel.
 *
 * Walks every sub-project listed on `aContainer`, loading each one's
 * `.kicad_pro` into a transient `PROJECT_FILE` long enough to read its
 * `net_settings`. Classifies each container class as SOURCE / SHARED /
 * CONFLICT based on what (if anything) the sub-projects say about it, and
 * collects any sub-project classes the container hasn't adopted into
 * `localOnlyRows` (cloned, so they outlive the transients).
 *
 * Returns an empty view when `aContainer` is not a multi-board container.
 * Sub-projects that fail to load are logged and skipped — the rest of the
 * view still builds.
 */
KICOMMON_API MULTI_BOARD_NETCLASS_VIEW
BuildMultiBoardNetclassView( PROJECT_FILE& aContainer );


#endif // MULTI_BOARD_PROPAGATE_SETTINGS_H
