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

#ifndef MBSCH_EDIT_FRAME_H
#define MBSCH_EDIT_FRAME_H

#include <sch_edit_frame.h>


/**
 * Multi-board schematic editor frame.
 *
 * Thin subclass of SCH_EDIT_FRAME. Identifies itself as FRAME_MBSCH so
 * the shell can route `.kicad_mbs` files here and the menu / toolbar
 * builders can distinguish the trimmed MBS surface from the full
 * eeschema one. Shares parser, writer, painter, and connectivity with
 * eeschema — the on-disk format is identical to `.kicad_sch`.
 */
class MBSCH_EDIT_FRAME : public SCH_EDIT_FRAME
{
public:
    MBSCH_EDIT_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~MBSCH_EDIT_FRAME() override;

protected:
    /**
     * Extract cross-board nets from the MBS topology and persist them
     * back to the enclosing `.kicad_pro` container. Runs after each
     * successful save of the MBS schematic.
     */
    void onSchematicSaved() override;

    wxString windowTitleSuffix() const override;
};

#endif // MBSCH_EDIT_FRAME_H
