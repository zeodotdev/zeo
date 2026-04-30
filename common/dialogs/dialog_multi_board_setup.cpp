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

#include "dialog_multi_board_setup.h"

#include <env_vars.h>
#include <pgm_base.h>
#include <project.h>
#include <project/project_file.h>
#include <project_template.h>
#include <wildcards_and_files_ext.h>

#include <wx/button.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>


DIALOG_MULTI_BOARD_SETUP::DIALOG_MULTI_BOARD_SETUP( wxWindow*            aParent,
                                                    PROJECT_FILE* aProject,
                                                    const wxFileName&    aMultiProjectPath ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Multi-Board Project Setup" ), wxDefaultPosition,
                     wxSize( 560, 420 ),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_project( aProject ),
        m_multiProjectPath( aMultiProjectPath ),
        m_listCtrl( nullptr ),
        m_importButton( nullptr ),
        m_createButton( nullptr ),
        m_removeButton( nullptr )
{
    buildUI();
    refreshList();
    Centre();
}


/**
 * Persist the current sub-project list to disk and surface any failure to
 * the user. Called after every Add/Remove operation so that each edit is
 * locked in immediately — the previous "save only on Done" behaviour
 * silently lost edits if anything between the operation and Done caused
 * the in-memory PROJECT_FILE to be re-loaded from disk (peer save hooks,
 * sub-project open paths that touch SETTINGS_MANAGER, app crashes, etc.).
 *
 * `aContainerDir` MUST be the absolute directory containing the
 * `.kicad_pro`. SaveToFile derives the on-disk path from m_filename +
 * aDirectory; for a live PROJECT_FILE registered with SETTINGS_MANAGER,
 * m_filename is just the basename (set in settings_manager.cpp's
 * `loadProjectFile`), so passing an empty `aDirectory` would write
 * relative to CWD — usually the wrong place — and on a non-writable
 * CWD would silently fail. Always pass the project directory.
 *
 * Returns true on success, false otherwise; the caller is responsible
 * for reverting the in-memory change if persistence fails.
 */
static bool persistContainerOrWarn( wxWindow* aParent, PROJECT_FILE* aProject,
                                    const wxString& aContainerDir )
{
    if( !aProject )
        return false;

    // Force the save through even if Store()'s MatchesFile heuristic
    // believes nothing changed — we just performed a deliberate edit
    // and the user expects it on disk.
    if( aProject->SaveToFile( aContainerDir, /*aForce=*/true ) )
        return true;

    // Diagnose the failure: SaveToFile has a handful of early-exit gates
    // (read-only flag, empty filename, unwritable directory, file write
    // error). Reconstruct the same path it would have used and probe each
    // gate so the popup can tell the user exactly which one tripped.
    wxString filename = aProject->GetFilename();   // basename for live PFs
    wxString fileExt  = wxString::FromUTF8( FILEEXT::ProjectFileExtension );

    wxFileName probe;

    if( aContainerDir.IsEmpty() )
    {
        probe.Assign( filename );
        probe.SetExt( fileExt );
    }
    else
    {
        probe.Assign( aContainerDir, filename, fileExt );
    }

    wxString detail;

    if( aProject->IsReadOnly() )
        detail = _( "the project is marked read-only (m_writeFile=false)." );
    else if( filename.IsEmpty() )
        detail = _( "the PROJECT_FILE has no filename." );
    else if( !probe.DirExists() )
        detail = wxString::Format(
                _( "the project directory does not exist: %s" ), probe.GetPath() );
    else if( probe.FileExists() && !probe.IsFileWritable() )
        detail = wxString::Format(
                _( "the project file is not writable: %s" ), probe.GetFullPath() );
    else if( !probe.FileExists() && !probe.IsDirWritable() )
        detail = wxString::Format(
                _( "the project directory is not writable: %s" ), probe.GetPath() );
    else
        detail = wxString::Format(
                _( "writing %s failed (filesystem error or JSON serialisation)." ),
                probe.GetFullPath() );

    wxMessageBox(
            wxString::Format(
                    _( "Could not persist the multi-board project. Your edit is in "
                       "memory but is not yet on disk; closing the project now will "
                       "lose it.\n\nDiagnostic: %s\n\nTried directory: %s\nFilename: %s" ),
                    detail, aContainerDir, filename ),
            _( "Save Failed" ), wxOK | wxICON_ERROR, aParent );

    return false;
}


DIALOG_MULTI_BOARD_SETUP::~DIALOG_MULTI_BOARD_SETUP()
{
}


wxFileName DIALOG_MULTI_BOARD_SETUP::containerDir() const
{
    // Prefer the live PROJECT's absolute path (via the PROJECT_FILE
    // back-pointer): SETTINGS_MANAGER stores the canonical
    // "/abs/path/name.kicad_pro" on PROJECT, while PROJECT_FILE's own
    // m_filename is only the basename. Falling back to
    // m_multiProjectPath covers any free-standing PROJECT_FILE that
    // isn't owned by SETTINGS_MANAGER (no current callers, but defensive
    // — also covers the historic broken callers that passed only a
    // basename in m_multiProjectPath).
    wxFileName dir;

    if( m_project && m_project->GetProject() )
        dir.Assign( m_project->GetProject()->GetProjectFullName() );
    else
        dir = m_multiProjectPath;

    dir.SetFullName( wxEmptyString );
    return dir;
}


void DIALOG_MULTI_BOARD_SETUP::buildUI()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    mainSizer->Add(
            new wxStaticText( this, wxID_ANY,
                              _( "Boards in this multi-board project:" ) ),
            0, wxALL, 8 );

    m_listCtrl = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
    m_listCtrl->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, 180 );
    m_listCtrl->AppendColumn( _( "Relative Path" ), wxLIST_FORMAT_LEFT, 320 );
    mainSizer->Add( m_listCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    wxBoxSizer* addRemoveSizer = new wxBoxSizer( wxHORIZONTAL );
    m_importButton = new wxButton( this, wxID_ANY, _( "Import existing project…" ) );
    m_createButton = new wxButton( this, wxID_ANY, _( "Create new board…" ) );
    m_removeButton = new wxButton( this, wxID_ANY, _( "Remove" ) );
    m_removeButton->Enable( false );
    addRemoveSizer->Add( m_importButton, 0, wxRIGHT, 5 );
    addRemoveSizer->Add( m_createButton, 0, wxRIGHT, 5 );
    addRemoveSizer->AddStretchSpacer();
    addRemoveSizer->Add( m_removeButton, 0 );
    mainSizer->Add( addRemoveSizer, 0, wxEXPAND | wxALL, 8 );

    mainSizer->Add(
            new wxStaticText( this, wxID_ANY,
                              _( "Imported projects are copied into the boards/ "
                                 "subdirectory; the originals are left untouched." ) ),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    wxButton*               doneButton = new wxButton( this, wxID_OK, _( "Done" ) );
    doneButton->SetDefault();
    buttons->AddButton( doneButton );
    buttons->Realize();
    mainSizer->Add( buttons, 0, wxEXPAND | wxALL, 8 );

    SetSizer( mainSizer );

    m_importButton->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_SETUP::onImportExisting, this );
    m_createButton->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_SETUP::onCreateNew, this );
    m_removeButton->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_SETUP::onRemove, this );
    m_listCtrl->Bind( wxEVT_LIST_ITEM_SELECTED,
                      &DIALOG_MULTI_BOARD_SETUP::onSelectionChanged, this );
    m_listCtrl->Bind( wxEVT_LIST_ITEM_DESELECTED,
                      &DIALOG_MULTI_BOARD_SETUP::onSelectionChanged, this );

    Bind( wxEVT_BUTTON,
          [this]( wxCommandEvent& ) {
              // Final safety save. Per-op saves in onCreateNew / onImportExisting /
              // onRemove are the source of truth for state persistence; this is
              // a defensive forced save so that any field the dialog mutated
              // outside the per-op handlers also lands.
              persistContainerOrWarn( this, m_project, containerDir().GetPath() );
              EndModal( wxID_OK );
          },
          wxID_OK );
}


void DIALOG_MULTI_BOARD_SETUP::refreshList()
{
    m_listCtrl->DeleteAllItems();

    if( !m_project )
        return;

    long row = 0;

    for( const SUB_PROJECT_INFO& info : m_project->GetSubProjects() )
    {
        m_listCtrl->InsertItem( row, info.displayName.IsEmpty() ? info.name : info.displayName );
        m_listCtrl->SetItem( row, 1, info.relativePath );
        row++;
    }

    m_removeButton->Enable( m_listCtrl->GetSelectedItemCount() > 0 );
}


void DIALOG_MULTI_BOARD_SETUP::onSelectionChanged( wxListEvent& )
{
    m_removeButton->Enable( m_listCtrl->GetSelectedItemCount() > 0 );
}


wxFileName DIALOG_MULTI_BOARD_SETUP::ensureBoardsDir()
{
    wxFileName boardsDir = containerDir();
    boardsDir.AppendDir( wxT( "boards" ) );

    if( !boardsDir.DirExists() )
        boardsDir.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    return boardsDir;
}


wxString DIALOG_MULTI_BOARD_SETUP::uniquifyName( const wxString& aDesiredName ) const
{
    wxFileName boardsDir = containerDir();
    boardsDir.AppendDir( wxT( "boards" ) );

    wxString candidate = aDesiredName;
    int      suffix = 2;

    while( true )
    {
        wxFileName probe = boardsDir;
        probe.AppendDir( candidate );

        bool nameInUse = probe.DirExists();

        if( !nameInUse )
        {
            for( const SUB_PROJECT_INFO& info : m_project->GetSubProjects() )
            {
                if( info.name == candidate )
                {
                    nameInUse = true;
                    break;
                }
            }
        }

        if( !nameInUse )
            return candidate;

        candidate = wxString::Format( "%s_%d", aDesiredName, suffix++ );
    }
}


void DIALOG_MULTI_BOARD_SETUP::onImportExisting( wxCommandEvent& )
{
    wxFileDialog dlg( this, _( "Select a KiCad project to import" ), wxEmptyString,
                      wxEmptyString, FILEEXT::ProjectFileWildcard(),
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    if( dlg.ShowModal() != wxID_OK )
        return;

    wxFileName proFile( dlg.GetPath() );
    wxString   targetName = uniquifyName( proFile.GetName() );

    if( !importExistingProject( proFile, targetName ) )
        return;

    refreshList();
}


bool DIALOG_MULTI_BOARD_SETUP::importExistingProject( const wxFileName& aSourceProFile,
                                                      const wxString&   aTargetName )
{
    wxFileName boardsDir = ensureBoardsDir();
    wxFileName targetDir = boardsDir;
    targetDir.AppendDir( aTargetName );

    if( !targetDir.DirExists() && !targetDir.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
    {
        wxMessageBox(
                wxString::Format( _( "Could not create directory:\n%s" ), targetDir.GetFullPath() ),
                _( "Import Failed" ), wxOK | wxICON_ERROR, this );
        return false;
    }

    // Copy every file in the source project's directory that shares the
    // source project's basename, renaming to the target basename. Other
    // files (libraries, shared sheets) are copied as-is.
    wxDir        sourceDir( aSourceProFile.GetPath() );
    wxString     filename;
    const wxString sourceBase = aSourceProFile.GetName();
    bool         continueScan = sourceDir.GetFirst( &filename, wxEmptyString, wxDIR_FILES );

    while( continueScan )
    {
        wxFileName srcFile( aSourceProFile.GetPath(), filename );
        wxFileName dstFile = targetDir;
        wxString   dstName = srcFile.GetName();

        dstName.Replace( sourceBase, aTargetName );

        dstFile.SetName( dstName );
        dstFile.SetExt( srcFile.GetExt() );

        if( !wxCopyFile( srcFile.GetFullPath(), dstFile.GetFullPath(), /*overwrite*/ false ) )
        {
            wxLogWarning( wxT( "Failed to copy %s -> %s" ), srcFile.GetFullPath(),
                          dstFile.GetFullPath() );
        }

        continueScan = sourceDir.GetNext( &filename );
    }

    // Register the sub-project in the container
    SUB_PROJECT_INFO info;
    info.uuid = KIID();
    info.name = aTargetName;
    info.displayName = aSourceProFile.GetName();
    info.role = wxT( "standard" );

    wxFileName relProFile = targetDir;
    relProFile.SetName( aTargetName );
    relProFile.SetExt( wxString::FromUTF8( FILEEXT::ProjectFileExtension ) );
    relProFile.MakeRelativeTo( containerDir().GetFullPath() );
    info.relativePath = relProFile.GetFullPath( wxPATH_UNIX );

    m_project->AddSubProject( info );

    // Persist immediately so the import is locked into the .kicad_pro
    // before any peer-frame side effects (MBSCH save hook, sub-project
    // open paths) can race with us.
    if( !persistContainerOrWarn( this, m_project, containerDir().GetPath() ) )
    {
        m_project->RemoveSubProject( info.uuid );
        return false;
    }

    return true;
}


void DIALOG_MULTI_BOARD_SETUP::onCreateNew( wxCommandEvent& )
{
    wxTextEntryDialog nameDlg( this, _( "Name for the new board:" ),
                               _( "Create New Board" ), wxEmptyString );

    if( nameDlg.ShowModal() != wxID_OK )
        return;

    wxString desiredName = nameDlg.GetValue().Trim().Trim( false );

    if( desiredName.IsEmpty() )
        return;

    wxString name = uniquifyName( desiredName );

    if( !createNewSubProject( name ) )
        return;

    refreshList();
}


bool DIALOG_MULTI_BOARD_SETUP::createNewSubProject( const wxString& aName )
{
    // Locate the user templates directory and the "default" template.
    ENV_VAR_MAP_CITER it = Pgm().GetLocalEnvVariables().find( wxT( "KICAD_USER_TEMPLATE_DIR" ) );

    if( it == Pgm().GetLocalEnvVariables().end() || it->second.GetValue().IsEmpty() )
    {
        wxMessageBox(
                _( "Cannot locate the KiCad user template directory. "
                   "Set KICAD_USER_TEMPLATE_DIR in Preferences." ),
                _( "Create New Board Failed" ), wxOK | wxICON_ERROR, this );
        return false;
    }

    wxFileName templateDir;
    templateDir.AssignDir( it->second.GetValue() );
    templateDir.AppendDir( wxT( "default" ) );

    if( !templateDir.DirExists() )
    {
        wxMessageBox(
                wxString::Format( _( "Default project template not found at:\n%s" ),
                                  templateDir.GetFullPath() ),
                _( "Create New Board Failed" ), wxOK | wxICON_ERROR, this );
        return false;
    }

    PROJECT_TEMPLATE tmpl( templateDir.GetFullPath() );

    wxFileName boardsDir = ensureBoardsDir();
    wxFileName targetDir = boardsDir;
    targetDir.AppendDir( aName );
    targetDir.SetName( aName );
    targetDir.SetExt( wxString::FromUTF8( FILEEXT::ProjectFileExtension ) );

    wxString errorMsg;

    if( !tmpl.CreateProject( targetDir, &errorMsg ) )
    {
        wxMessageBox(
                wxString::Format( _( "Could not create new board from template:\n%s" ), errorMsg ),
                _( "Create New Board Failed" ), wxOK | wxICON_ERROR, this );
        return false;
    }

    SUB_PROJECT_INFO info;
    info.uuid = KIID();
    info.name = aName;
    info.displayName = aName;
    info.role = wxT( "standard" );

    wxFileName relProFile = targetDir;
    relProFile.MakeRelativeTo( containerDir().GetFullPath() );
    info.relativePath = relProFile.GetFullPath( wxPATH_UNIX );

    m_project->AddSubProject( info );

    // Persist immediately — see comment in importExistingProject.
    if( !persistContainerOrWarn( this, m_project, containerDir().GetPath() ) )
    {
        m_project->RemoveSubProject( info.uuid );
        return false;
    }

    return true;
}


void DIALOG_MULTI_BOARD_SETUP::onRemove( wxCommandEvent& )
{
    long selection = m_listCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( selection < 0 )
        return;

    const auto& subs = m_project->GetSubProjects();

    if( selection >= (long) subs.size() )
        return;

    const SUB_PROJECT_INFO toRemove = subs[selection];

    if( wxMessageBox(
                wxString::Format(
                        _( "Remove '%s' from the multi-board project?\n\n"
                           "(Files on disk at '%s' are not deleted.)" ),
                        toRemove.displayName, toRemove.relativePath ),
                _( "Confirm Remove" ), wxYES_NO | wxICON_QUESTION, this )
        != wxYES )
    {
        return;
    }

    m_project->RemoveSubProject( toRemove.uuid );

    // Persist immediately so the removal is locked into the .kicad_pro
    // before any subsequent operation (Add D, peer save hook, etc.) can
    // race with us. Roll back the in-memory removal if the disk write
    // failed — otherwise the dialog state would diverge from disk.
    if( !persistContainerOrWarn( this, m_project, containerDir().GetPath() ) )
        m_project->AddSubProject( toRemove );

    refreshList();
}
