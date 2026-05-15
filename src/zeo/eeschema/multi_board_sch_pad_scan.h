/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MULTI_BOARD_SCH_PAD_SCAN_H
#define MULTI_BOARD_SCH_PAD_SCAN_H

#include <project/multi_board_scan.h>

#include <map>
#include <vector>
#include <wx/filename.h>
#include <wx/string.h>


/**
 * Load a sub-project's main .kicad_sch through the full eeschema schematic
 * loader (connectivity calculation included) and harvest the pin set + net
 * names for every connector symbol.
 *
 * Net names come from KiCad's CONNECTION_GRAPH — the same resolver eeschema
 * uses interactively — so this works even when the .kicad_pcb hasn't been
 * synced from the schematic yet (the symptom that motivated this helper:
 * brand-new sub-board, schematic drawn with labels, PCB still empty, and
 * the regex-only schematic scan would produce placeholder pin labels).
 *
 * Returns a per-reference list of MULTI_BOARD_PAD_INFO with padNumber and
 * netName populated for every pin. Electrical type stays at the default
 * (PT_PASSIVE) — the caller can layer PCB-derived electrical types on top
 * via the existing PCB scan.
 *
 * If schematic loading fails for any reason, returns an empty map (the
 * caller falls back to the regex-only scan in MultiBoardScanConnectorPads
 * which at least gets the pin SET, even if not the net names).
 *
 * Hierarchical sub-sheets are walked automatically by the schematic
 * loader, so connectors on sub-sheets surface in the result the same as
 * top-level ones.
 */
std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>>
MultiBoardLoadConnectorPadsFromSch( const wxFileName& aSchFile );

#endif // MULTI_BOARD_SCH_PAD_SCAN_H
