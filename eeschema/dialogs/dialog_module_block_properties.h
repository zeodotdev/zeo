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
#include <widgets/unit_binder.h>

#include <memory>

class SCH_MODULE_BLOCK;
class SCH_EDIT_FRAME;
class WX_GRID;
class wxNotebook;
class wxStaticText;
class wxTextCtrl;
class wxButton;
class wxCommandEvent;


/**
 * Properties editor for a SCH_MODULE_BLOCK in the multi-board schematic
 * editor. Mirrors `DIALOG_SYMBOL_PROPERTIES` shape — wxNotebook with a
 * General tab (fields grid + geometry + source link) and a Pin Functions
 * tab (read-only pin enumeration). Footprint / attributes / library
 * sections from the symbol dialog are intentionally omitted because they
 * have no analogue for module blocks (the connector lives on the
 * sub-project; pins are managed by refresh).
 */
class DIALOG_MODULE_BLOCK_PROPERTIES : public DIALOG_SHIM
{
public:
    DIALOG_MODULE_BLOCK_PROPERTIES( SCH_EDIT_FRAME* aFrame, SCH_MODULE_BLOCK* aBlock );
    ~DIALOG_MODULE_BLOCK_PROPERTIES() override;

    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

private:
    void onOpenSourceSchematic( wxCommandEvent& aEvent );

    SCH_EDIT_FRAME*   m_frame;
    SCH_MODULE_BLOCK* m_block;

    wxNotebook*       m_notebook;

    // General tab
    WX_GRID*          m_fieldsGrid;

    /// Width / height geometry inputs are routed through UNIT_BINDER
    /// so the dialog honours the user's display-unit preference
    /// (mm / mils / inches), validates input, and matches the styling
    /// of every peer property dialog (DIALOG_SYMBOL_PROPERTIES,
    /// DIALOG_LABEL_PROPERTIES, etc.). The label / control / units
    /// triplets below back the UNIT_BINDER instances.
    wxStaticText*                m_widthLabel;
    wxTextCtrl*                  m_widthCtrl;
    wxStaticText*                m_widthUnits;
    wxStaticText*                m_heightLabel;
    wxTextCtrl*                  m_heightCtrl;
    wxStaticText*                m_heightUnits;
    std::unique_ptr<UNIT_BINDER> m_width;
    std::unique_ptr<UNIT_BINDER> m_height;

    wxTextCtrl*       m_sourceSchPath;   ///< read-only display of resolved .kicad_sch
    wxButton*         m_openSourceBtn;

    // Pin Functions tab
    WX_GRID*          m_pinGrid;
};

#endif // DIALOG_MODULE_BLOCK_PROPERTIES_H
