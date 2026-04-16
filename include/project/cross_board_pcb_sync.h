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

#ifndef KICAD_CROSS_BOARD_PCB_SYNC_H
#define KICAD_CROSS_BOARD_PCB_SYNC_H

#include <kicommon.h>
#include <project/multi_board_project.h>

#include <wx/string.h>


struct KICOMMON_API CROSS_BOARD_SYNC_RESULT
{
    int      subProjectsTouched = 0;  ///< Count of sub-project PCBs modified
    int      endpointsApplied   = 0;  ///< Count of (pad,net) assignments written
    int      endpointsMissing   = 0;  ///< Count of endpoints whose pad wasn't found
    wxString summary;                 ///< Human-readable summary for UI display
};


/**
 * Apply the cross-board nets declared in a MULTI_BOARD_PROJECT to every
 * sub-project's .kicad_pcb. For each CROSS_BOARD_NET endpoint we locate
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
 */
KICOMMON_API CROSS_BOARD_SYNC_RESULT
ApplyCrossBoardNetsToSubProjectPCBs( const MULTI_BOARD_PROJECT& aProject );

#endif // KICAD_CROSS_BOARD_PCB_SYNC_H
