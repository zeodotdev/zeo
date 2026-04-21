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
#include <wx/filename.h>
#include <wx/string.h>

#include <map>
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

#endif // KICAD_MULTI_BOARD_SCAN_H
