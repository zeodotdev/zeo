/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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

#ifndef DIALOG_VARIANT_DIFF_H
#define DIALOG_VARIANT_DIFF_H

#include <dialog_shim.h>

class SCH_EDIT_FRAME;
class wxChoice;
class wxListView;
class wxStaticText;


/**
 * Side-by-side field-level diff between two variants.
 *
 * Each row shows a single field on a single symbol whose value differs between the two
 * variants picked at the top.  Visual canvas diff (highlighting changed symbols on the
 * schematic itself) is intentionally out of scope and deferred to 0.2.3.
 */
class DIALOG_VARIANT_DIFF : public DIALOG_SHIM
{
public:
    DIALOG_VARIANT_DIFF( SCH_EDIT_FRAME* aParent );

private:
    void rebuildList();
    void onChoiceChanged( wxCommandEvent& );

    SCH_EDIT_FRAME* m_parent;
    wxChoice*       m_variantAChoice = nullptr;
    wxChoice*       m_variantBChoice = nullptr;
    wxListView*     m_diffList       = nullptr;
    wxStaticText*   m_summary        = nullptr;
};


#endif  // DIALOG_VARIANT_DIFF_H
