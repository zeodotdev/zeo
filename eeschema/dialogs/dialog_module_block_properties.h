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

#ifndef DIALOG_MODULE_BLOCK_PROPERTIES_H
#define DIALOG_MODULE_BLOCK_PROPERTIES_H

#include <dialog_shim.h>

class SCH_MODULE_BLOCK;
class wxTextCtrl;


/**
 * Properties editor for a SCH_MODULE_BLOCK in the multi-board schematic
 * editor. Exposes the user-editable fields (display name, MBS reference,
 * size) and shows the immutable sub-project metadata (componentRef,
 * subProjectPath) as read-only context.
 *
 * Pin layout is owned by the refresh flow, so pins are not editable here;
 * they're shown as a count for orientation.
 */
class DIALOG_MODULE_BLOCK_PROPERTIES : public DIALOG_SHIM
{
public:
    DIALOG_MODULE_BLOCK_PROPERTIES( wxWindow* aParent, SCH_MODULE_BLOCK* aBlock );

    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

private:
    SCH_MODULE_BLOCK* m_block;

    wxTextCtrl*       m_displayName;
    wxTextCtrl*       m_mbsReference;
    wxTextCtrl*       m_componentRef;     ///< read-only
    wxTextCtrl*       m_subProjectPath;   ///< read-only
    wxTextCtrl*       m_widthMm;
    wxTextCtrl*       m_heightMm;
    wxTextCtrl*       m_pinCount;         ///< read-only
};

#endif // DIALOG_MODULE_BLOCK_PROPERTIES_H
