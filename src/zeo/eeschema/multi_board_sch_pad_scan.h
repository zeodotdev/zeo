/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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
