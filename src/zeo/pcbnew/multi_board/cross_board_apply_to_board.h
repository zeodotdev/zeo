/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef CROSS_BOARD_APPLY_TO_BOARD_H
#define CROSS_BOARD_APPLY_TO_BOARD_H

#include <wx/string.h>

class BOARD;
class REPORTER;


/**
 * Stats from `ApplyCrossBoardNetsToBoard`.
 */
struct CROSS_BOARD_APPLY_RESULT
{
    int padsUpdated      = 0;   ///< Connector pads whose net was changed
    int padsAlreadyMatch = 0;   ///< Pads that already carried the correct MBS net
    int padsMissing      = 0;   ///< Endpoints declared in MBS but pad not on this BOARD
    int netsAdded        = 0;   ///< NETINFO_ITEMs added to the board for new MBS-only nets
    bool isMultiBoard    = false;   ///< True if this board sits inside an MBS container
};


/**
 * Apply MBS-declared cross-board net assignments to the in-memory board.
 *
 * Walks up from the BOARD's project file to find the enclosing
 * `.kicad_pro` container, then for each cross-board endpoint that
 * targets this sub-project:
 *   - locates the connector footprint (by reference) and pad (by number)
 *     on `aBoard`;
 *   - resolves or creates a `NETINFO_ITEM` matching the MBS-declared
 *     net name;
 *   - assigns the pad to that net (idempotent — pads already on the
 *     correct net are left alone and counted in padsAlreadyMatch).
 *
 * MBS wins on conflict: if the local schematic-side sync has assigned
 * a different net to the pad, this function will override it. The MBS
 * is the authoritative source for cross-board net assignments — the
 * user's intent when wiring two module pins together is "make these
 * the same net everywhere", which has to override per-board labels.
 *
 * No-op (returns `isMultiBoard=false`) when this board isn't inside a
 * multi-board container, or the container declares no cross-board nets
 * for this sub-project. Safe to call from non-multi-board projects —
 * the cost is the directory walk + load of one PROJECT_FILE.
 *
 * Pass `aReporter = nullptr` for headless usage.
 */
CROSS_BOARD_APPLY_RESULT ApplyCrossBoardNetsToBoard( BOARD& aBoard, REPORTER* aReporter );

#endif // CROSS_BOARD_APPLY_TO_BOARD_H
