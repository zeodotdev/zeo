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
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef DIALOG_MULTI_BOARD_SETUP_H
#define DIALOG_MULTI_BOARD_SETUP_H

#include <dialog_shim.h>

#include <wx/filename.h>

class PROJECT_FILE;
class wxButton;
class wxListCtrl;
class wxListEvent;


/**
 * Dialog for managing the sub-project list of a multi-board project.
 *
 * Users can:
 * - Import an existing single-board `.kicad_pro` (copied into the
 *   multi-board's `boards/<name>/` directory)
 * - Create a new board from the "default" project template
 * - Remove a sub-project from the list (files on disk are not deleted)
 * - Click Done to persist changes to the `.kicad_multi` container
 *
 * The dialog mutates the passed-in `MULTI_BOARD_PROJECT` in place and
 * writes it to disk when the user clicks Done. The caller can then
 * decide what to do with the updated container (e.g. load the first
 * sub-project in the project manager).
 */
class DIALOG_MULTI_BOARD_SETUP : public DIALOG_SHIM
{
public:
    /**
     * @param aParent parent window (pass the KICAD_MANAGER_FRAME)
     * @param aProject the in-memory container to edit; must already have its
     *                 root directory set (LoadFromFile or SetRootDir + SaveToFile).
     *                 The dialog will persist changes to `aProject->GetRootDir()`
     *                 using `aMultiProjectPath`.
     * @param aMultiProjectPath absolute path to the `.kicad_multi` file this
     *                          dialog should write back to when Done is clicked.
     */
    DIALOG_MULTI_BOARD_SETUP( wxWindow* aParent, PROJECT_FILE* aProject,
                              const wxFileName& aMultiProjectPath );

    ~DIALOG_MULTI_BOARD_SETUP() override;

private:
    void buildUI();
    void refreshList();

    void onImportExisting( wxCommandEvent& aEvent );
    void onCreateNew( wxCommandEvent& aEvent );
    void onRemove( wxCommandEvent& aEvent );
    void onSelectionChanged( wxListEvent& aEvent );

    /**
     * Copy a source `.kicad_pro` (and the other files in its directory) into
     * `<multi_root>/boards/<name>/`. Returns true on success.
     *
     * @param aSourceProFile absolute path to the `.kicad_pro` to import
     * @param aTargetName  the basename to use under boards/; usually the source
     *                     project's name, optionally de-duplicated
     */
    bool importExistingProject( const wxFileName& aSourceProFile,
                                const wxString&   aTargetName );

    /**
     * Create a new sub-project under `<multi_root>/boards/<aName>/` by copying
     * the "default" project template. Returns true on success.
     */
    bool createNewSubProject( const wxString& aName );

    /**
     * Ensure `<multi_root>/boards/` exists. Returns the path.
     */
    wxFileName ensureBoardsDir();

    /**
     * Pick a unique name under boards/ by appending _2, _3, ... if the
     * requested name is already taken.
     */
    wxString uniquifyName( const wxString& aDesiredName ) const;

    PROJECT_FILE* m_project;
    wxFileName           m_multiProjectPath;

    /// Directory containing the container `.kicad_pro` (where
    /// `boards/` subdirectory lives).
    wxFileName containerDir() const;

    wxListCtrl*          m_listCtrl;
    wxButton*            m_importButton;
    wxButton*            m_createButton;
    wxButton*            m_removeButton;
};

#endif // DIALOG_MULTI_BOARD_SETUP_H
