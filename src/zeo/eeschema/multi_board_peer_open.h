/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef MULTI_BOARD_PEER_OPEN_H
#define MULTI_BOARD_PEER_OPEN_H

class EDA_DRAW_FRAME;
class KIID;


/**
 * Open a sub-project's schematic or PCB in a peer KIWAY_PLAYER alongside
 * the calling frame, instead of replacing the active project.
 *
 * Used by the MBS toolbar actions ("Open Sub-Board Schematic / PCB") and
 * by the module-block properties dialog when the user clicks "Open" on
 * the source-schematic field.
 *
 * Resolves @p aSubProjectUuid against the calling frame's container
 * `PROJECT_FILE::GetSubProjects()`. Refocuses an existing peer if one
 * already exists for this sub-project; otherwise spawns a new one and
 * registers it with the shared Kiway so cross-probes reach it.
 *
 * @return true if a peer is open and focused; false if the caller's
 *         frame isn't on a multi-board container, the UUID isn't a
 *         registered sub-project, or the kiface refused to load.
 */
bool OpenSubProjectInPeerEditor( EDA_DRAW_FRAME* aFrame, const KIID& aSubProjectUuid,
                                 bool aWantPcb );

#endif // MULTI_BOARD_PEER_OPEN_H
