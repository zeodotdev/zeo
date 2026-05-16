/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "grid_tricks.h"
#include <functional>


class LIBRARY_TABLE_ROW;
class PROJECT;


class LIB_TABLE_GRID_TRICKS : public GRID_TRICKS
{
    enum
    {
        LIB_TABLE_GRID_TRICKS_ACTIVATE_SELECTED = GRIDTRICKS_FIRST_CLIENT_ID,
        LIB_TABLE_GRID_TRICKS_DEACTIVATE_SELECTED,
        LIB_TABLE_GRID_TRICKS_SET_VISIBLE,
        LIB_TABLE_GRID_TRICKS_UNSET_VISIBLE,
        LIB_TABLE_GRID_TRICKS_LIBRARY_SETTINGS,
        LIB_TABLE_GRID_TRICKS_OPEN_TABLE,
        LIB_TABLE_GRID_TRICKS_OPTIONS_EDITOR,
        LIB_TABLE_GRID_TRICKS_SHARE_TO_CONTAINER,  ///< M7.1 multi-board: promote local row
        LIB_TABLE_GRID_TRICKS_UNSHARE_LOCALLY      ///< M7.1 multi-board: drop shared flag
    };

public:
    explicit LIB_TABLE_GRID_TRICKS( WX_GRID* aGrid );
    LIB_TABLE_GRID_TRICKS( WX_GRID* aGrid, std::function<void( wxCommandEvent& )> aAddHandler );

    virtual ~LIB_TABLE_GRID_TRICKS(){};

    /// Multi-board (M7.1): tell the grid which PROJECT context the
    /// menu / actions should operate against. Without this the menu
    /// falls back to `Pgm().GetSettingsManager().Prj()` (the global
    /// active project), which is wrong for peer-player frames whose
    /// bound project differs from the active one.
    void SetProject( PROJECT* aProject ) { m_project = aProject; }

    void showPopupMenu( wxMenu& menu, wxGridEvent& aEvent ) override;
    void doPopupSelection( wxCommandEvent& event ) override;

    static void AppendRowHandler( WX_GRID* aGrid );

    /// @param aProject the PROJECT whose lib-table the grid is editing.
    /// Used by M7.1 to route deletion of `shared` rows through the
    /// replication API: container scope cascades via
    /// `LIBRARY_MANAGER::RemoveSharedLibrary`, sub-project scope
    /// converts the row to a non-shared local entry via
    /// `UnshareLibraryRow`. Pass nullptr to fall back to
    /// `SETTINGS_MANAGER::Prj()` — single-project use cases.
    static void DeleteRowHandler( WX_GRID* aGrid, PROJECT* aProject = nullptr );

    static void MoveUpHandler( WX_GRID* aGrid );
    static void MoveDownHandler( WX_GRID* aGrid );

    static bool VerifyTable( WX_GRID* aGrid, bool aSupportsVisibilityColumn,
                             std::function<void( int aRow, int aCol )> aErrorHandler );

protected:
    virtual void optionsEditor( int aRow ) = 0;
    virtual void openTable( const LIBRARY_TABLE_ROW& aRow ) = 0;

    void onGridCellLeftClick( wxGridEvent& aEvent ) override;
    bool handleDoubleClick( wxGridEvent& aEvent ) override;

    void onCharHook( wxKeyEvent& ev );

    /*
     * Handle specialized clipboard text, either s-expr syntax starting with a lib table preamble
     * (such as "(fp_lib_table"), or spreadsheet formatted text.
     */
    void paste_text( const wxString& cb_text ) override;

    virtual bool supportsVisibilityColumn() = 0;
    virtual wxString getTablePreamble() = 0;

    /// Project to operate against — see SetProject(). nullptr falls back
    /// to the global active project.
    PROJECT* m_project = nullptr;
};
