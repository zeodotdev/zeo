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

#ifndef MULTI_BOARD_MBS_REFRESH_H
#define MULTI_BOARD_MBS_REFRESH_H

#include <wx/string.h>

class SCH_SCREEN;
class PROJECT_FILE;


struct MBS_REFRESH_RESULT
{
    int      blocksAdded  = 0;  ///< New SCH_MODULE_BLOCKs created
    int      pinsAdded    = 0;  ///< New SCH_MODULE_PINs added to existing blocks
    wxString summary;
};


/**
 * Scan every sub-project referenced by `aMultiBoard` and additively update
 * the in-memory MBS screen so that every (sub_project, connector, pad)
 * triple from the scan has a corresponding block/pin on the MBS.
 *
 * Additive only — existing blocks and pins are preserved, as are wires/labels
 * already placed by the user. Nothing is removed, so pads that vanished from
 * a sub-project remain visible until the user deletes them manually.
 *
 * Returns a stats summary suitable for display to the user.
 */
MBS_REFRESH_RESULT RefreshMbsFromSubProjects( SCH_SCREEN& aMbsScreen,
                                              const PROJECT_FILE& aMultiBoard );

#endif // MULTI_BOARD_MBS_REFRESH_H
