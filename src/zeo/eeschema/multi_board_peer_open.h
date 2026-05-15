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
