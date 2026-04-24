/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include <wildcards_and_files_ext.h>
#include <eda_base_frame.h>
#include <env_vars.h>
#include <executable_names.h>
#include <pgm_base.h>
#include <pgm_kicad.h>
#include <policy_keys.h>
#include <kiway.h>
#include <kicad_manager_frame.h>
#include <kiplatform/policy.h>
#include <kiplatform/secrets.h>
#include <kiplatform/ui.h>
#include <confirm.h>
#include <kidialog.h>
#include <project/project_file.h>
#include <project/multi_board_scan.h>
#include <project/cross_board_pcb_sync.h>
#include <project/project_file.h>
#include <project/project_local_settings.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <settings/kicad_settings.h>
#include <tool/selection.h>
#include <tool/tool_event.h>
#include <tool/tool_manager.h>
#include <tool/common_control.h>
#include <tools/kicad_manager_actions.h>
#include <tools/kicad_manager_control.h>
#include <dialogs/panel_design_block_lib_table.h>
#include <dialogs/dialog_multi_board_setup.h>
#include <dialogs/dialog_template_selector.h>
#include <dialogs/git/dialog_git_repository.h>
#include <git/git_clone_handler.h>
#include <gestfich.h>
#include <paths.h>
#include <eda_draw_frame.h>
#include <algorithm>
#include <wx/choicdlg.h>
#include <wx/dir.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/ffile.h>
#include "dialog_pcm.h"
#include <project/project_archiver.h>
#include <project_tree_pane.h>
#include <project_tree.h>
#include <project_tree_traverser.h>
#include <launch_ext.h>

#include "widgets/filedlg_new_project.h"
#include "dialogs/dialog_ai_assistant.h"
#include <mail_type.h>
#include "../session_manager.h"

KICAD_MANAGER_CONTROL::KICAD_MANAGER_CONTROL() :
        TOOL_INTERACTIVE( "kicad.Control" ),
        m_frame( nullptr ),
        m_inShowPlayer( false )
{
}


void KICAD_MANAGER_CONTROL::Reset( RESET_REASON aReason )
{
    m_frame = getEditFrame<KICAD_MANAGER_FRAME>();
}


wxFileName KICAD_MANAGER_CONTROL::newProjectDirectory( wxString* aFileName, bool isRepo )
{
    wxString default_filename = aFileName ? *aFileName : wxString();

    wxString     default_dir = m_frame->GetMruPath();
    wxFileDialog dlg( m_frame, _( "Create New Project" ), default_dir, default_filename,
                      ( isRepo ? wxString( "" ) : FILEEXT::ProjectFileWildcard() ), wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    dlg.AddShortcut( PATHS::GetDefaultUserProjectsPath() );

    // Add a "Create a new directory" checkbox
    FILEDLG_NEW_PROJECT newProjectHook;
    dlg.SetCustomizeHook( newProjectHook );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return wxFileName();

    wxFileName pro( dlg.GetPath() );

    // wxFileName automatically extracts an extension.  But if it isn't
    // a .pro extension, we should keep it as part of the filename
    if( !pro.GetExt().IsEmpty() && pro.GetExt().ToStdString() != FILEEXT::ProjectFileExtension )
        pro.SetName( pro.GetName() + wxT( "." ) + pro.GetExt() );

    pro.SetExt( FILEEXT::ProjectFileExtension ); // enforce extension

    if( !pro.IsAbsolute() )
        pro.MakeAbsolute();

    // Append a new directory with the same name of the project file.
    bool createNewDir = false;

    createNewDir = newProjectHook.GetCreateNewDir();

    if( createNewDir )
        pro.AppendDir( pro.GetName() );

    // Check if the project directory is empty if it already exists.
    wxDir directory( pro.GetPath() );

    if( !pro.DirExists() )
    {
        if( !pro.Mkdir() )
        {
            wxString msg;
            msg.Printf( _( "Folder '%s' could not be created.\n\n"
                           "Make sure you have write permissions and try again." ),
                        pro.GetPath() );
            DisplayErrorMessage( m_frame, msg );
            return wxFileName();
        }
    }
    else if( directory.HasFiles() )
    {
        wxString msg = _( "The selected folder is not empty.  It is recommended that you "
                          "create projects in their own empty folder.\n\n"
                          "Do you want to continue?" );

        if( !IsOK( m_frame, msg ) )
            return wxFileName();
    }

    return pro;
}


static wxFileName ensureDefaultProjectTemplate()
{
    ENV_VAR_MAP_CITER it = Pgm().GetLocalEnvVariables().find( "KICAD_USER_TEMPLATE_DIR" );

    if( it == Pgm().GetLocalEnvVariables().end() || it->second.GetValue() == wxEmptyString )
        return wxFileName();

    wxFileName templatePath;
    templatePath.AssignDir( it->second.GetValue() );
    templatePath.AppendDir( "default" );

    if( !templatePath.DirExists() && !templatePath.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        return wxFileName();

    wxFileName metaDir = templatePath;
    metaDir.AppendDir( METADIR );

    if( !metaDir.DirExists() && !metaDir.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        return wxFileName();

    wxFileName infoFile = metaDir;
    infoFile.SetFullName( METAFILE_INFO_HTML );

    if( !infoFile.FileExists() )
    {
        wxFFile info( infoFile.GetFullPath(), wxT( "w" ) );

        if( !info.IsOpened() )
            return wxFileName();

        info.Write( wxT( "<html><head><title>Default</title></head><body><h3>Default KiCad project template.</h3></body></html>" ) );
        info.Close();
    }

    wxFileName proFile = templatePath;
    proFile.SetFullName( wxT( "default.kicad_pro" ) );

    if( !proFile.FileExists() )
    {
        wxFFile proj( proFile.GetFullPath(), wxT( "w" ) );

        if( !proj.IsOpened() )
            return wxFileName();

        proj.Write( wxT( "{}" ) );
        proj.Close();
    }

    // Stub .kicad_sch so the schematic editor can open the new project
    // without prompting the user to create a file first.
    wxFileName schFile = templatePath;
    schFile.SetFullName( wxT( "default.kicad_sch" ) );

    if( !schFile.FileExists() )
    {
        wxFFile sch( schFile.GetFullPath(), wxT( "w" ) );

        if( sch.IsOpened() )
        {
            sch.Write( wxString::Format(
                    wxT( "(kicad_sch\n"
                         "\t(version 20250610)\n"
                         "\t(generator \"zeo\")\n"
                         "\t(generator_version \"9.99\")\n"
                         "\t(uuid \"%s\")\n"
                         "\t(paper \"A4\")\n"
                         "\t(lib_symbols)\n"
                         "\t(sheet_instances\n"
                         "\t\t(path \"/\"\n"
                         "\t\t\t(page \"1\")\n"
                         "\t\t)\n"
                         "\t)\n"
                         ")\n" ),
                    KIID().AsString() ) );
            sch.Close();
        }
    }

    // Stub .kicad_pcb so the board editor can open the new project.
    wxFileName pcbFile = templatePath;
    pcbFile.SetFullName( wxT( "default.kicad_pcb" ) );

    if( !pcbFile.FileExists() )
    {
        wxFFile pcb( pcbFile.GetFullPath(), wxT( "w" ) );

        if( pcb.IsOpened() )
        {
            pcb.Write(
                    wxT( "(kicad_pcb\n"
                         "\t(version 20250610)\n"
                         "\t(generator \"zeo\")\n"
                         "\t(generator_version \"9.99\")\n"
                         "\t(general\n"
                         "\t\t(thickness 1.6)\n"
                         "\t\t(legacy_teardrops no)\n"
                         "\t)\n"
                         "\t(paper \"A4\")\n"
                         "\t(layers\n"
                         "\t\t(0 \"F.Cu\" signal)\n"
                         "\t\t(2 \"B.Cu\" signal)\n"
                         "\t\t(9 \"F.Adhes\" user \"F.Adhesive\")\n"
                         "\t\t(11 \"B.Adhes\" user \"B.Adhesive\")\n"
                         "\t\t(13 \"F.Paste\" user)\n"
                         "\t\t(15 \"B.Paste\" user)\n"
                         "\t\t(5 \"F.SilkS\" user \"F.Silkscreen\")\n"
                         "\t\t(7 \"B.SilkS\" user \"B.Silkscreen\")\n"
                         "\t\t(1 \"F.Mask\" user)\n"
                         "\t\t(3 \"B.Mask\" user)\n"
                         "\t\t(17 \"Dwgs.User\" user \"User.Drawings\")\n"
                         "\t\t(19 \"Cmts.User\" user \"User.Comments\")\n"
                         "\t\t(21 \"Eco1.User\" user \"User.Eco1\")\n"
                         "\t\t(23 \"Eco2.User\" user \"User.Eco2\")\n"
                         "\t\t(25 \"Edge.Cuts\" user)\n"
                         "\t\t(27 \"Margin\" user)\n"
                         "\t\t(31 \"F.CrtYd\" user \"F.Courtyard\")\n"
                         "\t\t(29 \"B.CrtYd\" user \"B.Courtyard\")\n"
                         "\t\t(35 \"F.Fab\" user)\n"
                         "\t\t(33 \"B.Fab\" user)\n"
                         "\t)\n"
                         ")\n" ) );
            pcb.Close();
        }
    }

    if( infoFile.FileExists() && proFile.FileExists() )
        return templatePath;
    else
        return wxFileName();
}


/**
 * Ensure the built-in "Multi-Board" template exists in the user template directory.
 *
 * The template is a bare directory containing `meta/info.html` and a stub
 * `multi_board.kicad_pro` whose JSON carries `multi_board.container = true`.
 * When the user picks this template and clicks Create, the normal
 * template-copy flow runs (renaming the .kicad_pro to match the new project
 * name); a post-create hook in NewProject() then launches the Multi-Board
 * Setup dialog so the user can add sub-projects.
 */
static wxFileName ensureMultiBoardProjectTemplate()
{
    ENV_VAR_MAP_CITER it = Pgm().GetLocalEnvVariables().find( "KICAD_USER_TEMPLATE_DIR" );

    if( it == Pgm().GetLocalEnvVariables().end() || it->second.GetValue() == wxEmptyString )
        return wxFileName();

    wxFileName templatePath;
    templatePath.AssignDir( it->second.GetValue() );
    templatePath.AppendDir( "multi_board" );

    if( !templatePath.DirExists() && !templatePath.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        return wxFileName();

    wxFileName metaDir = templatePath;
    metaDir.AppendDir( METADIR );

    if( !metaDir.DirExists() && !metaDir.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        return wxFileName();

    wxFileName infoFile = metaDir;
    infoFile.SetFullName( METAFILE_INFO_HTML );

    if( !infoFile.FileExists() )
    {
        wxFFile info( infoFile.GetFullPath(), wxT( "w" ) );

        if( !info.IsOpened() )
            return wxFileName();

        info.Write(
                wxT( "<html><head><title>Multi-Board</title></head><body>"
                     "<h3>Multi-Board Project</h3>"
                     "<p>Groups several single-board projects under one container. "
                     "After creation you will be prompted to import existing boards "
                     "or create new ones.</p>"
                     "</body></html>" ) );
        info.Close();
    }

    // Retire any legacy `.kicad_multi` stub from earlier template versions
    // so the picker no longer sees a non-`.kicad_pro` top-level file.
    wxFileName legacyFile = templatePath;
    legacyFile.SetFullName( wxT( "multi_board.kicad_multi" ) );

    if( legacyFile.FileExists() )
        wxRemoveFile( legacyFile.GetFullPath() );

    wxFileName proFile = templatePath;
    proFile.SetFullName( wxT( "multi_board.kicad_pro" ) );

    if( !proFile.FileExists() )
    {
        wxFFile pro( proFile.GetFullPath(), wxT( "w" ) );

        if( !pro.IsOpened() )
            return wxFileName();

        pro.Write( wxT( "{\n"
                        "  \"multi_board\": {\n"
                        "    \"container\": true,\n"
                        "    \"sub_projects\": []\n"
                        "  }\n"
                        "}\n" ) );
        pro.Close();
    }

    if( infoFile.FileExists() && proFile.FileExists() )
        return templatePath;
    else
        return wxFileName();
}


/**
 * Return true if the template's directory is the canonical built-in
 * "multi_board" template (the only template that produces a container).
 */
static bool isMultiBoardTemplate( PROJECT_TEMPLATE& aTemplate )
{
    wxFileName htmlFile = aTemplate.GetHtmlFile();
    htmlFile.RemoveLastDir();  // drop /meta
    return htmlFile.GetDirs().Last().IsSameAs( wxT( "multi_board" ), false );
}


int KICAD_MANAGER_CONTROL::NewProject( const TOOL_EVENT& aEvent )
{
    wxFileName defaultTemplate = ensureDefaultProjectTemplate();
    ensureMultiBoardProjectTemplate();   // best-effort; picker still works if this fails

    if( !defaultTemplate.IsOk() )
    {
        wxFileName pro = newProjectDirectory();

        if( !pro.IsOk() )
            return -1;

        m_frame->CreateNewProject( pro );
        m_frame->LoadProject( pro );

        return 0;
    }

    KICAD_SETTINGS* settings = GetAppSettings<KICAD_SETTINGS>( "kicad" );

    wxString userTemplatesPath;
    wxString systemTemplatesPath;

    ENV_VAR_MAP_CITER itUser = Pgm().GetLocalEnvVariables().find( "KICAD_USER_TEMPLATE_DIR" );

    if( itUser != Pgm().GetLocalEnvVariables().end() && itUser->second.GetValue() != wxEmptyString )
    {
        wxFileName templatePath;
        templatePath.AssignDir( itUser->second.GetValue() );
        templatePath.Normalize( FN_NORMALIZE_FLAGS | wxPATH_NORM_ENV_VARS );
        userTemplatesPath = templatePath.GetFullPath();
    }

    std::optional<wxString> v = ENV_VAR::GetVersionedEnvVarValue( Pgm().GetLocalEnvVariables(), wxT( "TEMPLATE_DIR" ) );

    if( v && !v->IsEmpty() )
    {
        wxFileName templatePath;
        templatePath.AssignDir( *v );
        templatePath.Normalize( FN_NORMALIZE_FLAGS | wxPATH_NORM_ENV_VARS );
        systemTemplatesPath = templatePath.GetFullPath();
    }

    // Use RunMainStack to show the dialog on the main stack instead of the coroutine stack.
    // This is necessary because the template selector uses a WebView which triggers WebKit's
    // JavaScript VM initialization. WebKit's stack validation fails on coroutine stacks.
    int      result = wxID_CANCEL;
    wxString selectedTemplatePath;
    wxPoint  templateWindowPos;
    wxSize   templateWindowSize;
    wxString projectToEdit;

    RunMainStack(
            [&]()
            {
                DIALOG_TEMPLATE_SELECTOR ps( m_frame, settings->m_TemplateWindowPos,
                                             settings->m_TemplateWindowSize, userTemplatesPath,
                                             systemTemplatesPath, settings->m_RecentTemplates );

                result = ps.ShowModal();
                templateWindowPos = ps.GetPosition();
                templateWindowSize = ps.GetSize();
                projectToEdit = ps.GetProjectToEdit();

                PROJECT_TEMPLATE* templ = ps.GetSelectedTemplate();

                if( templ )
                {
                    wxFileName htmlFile = templ->GetHtmlFile();
                    htmlFile.RemoveLastDir();
                    selectedTemplatePath = htmlFile.GetPath();
                }
            } );

    settings->m_TemplateWindowPos = templateWindowPos;
    settings->m_TemplateWindowSize = templateWindowSize;

    // Check if user wants to edit a template instead of creating new project
    if( result == wxID_APPLY )
    {
        if( !projectToEdit.IsEmpty() && wxFileExists( projectToEdit ) )
        {
            m_frame->LoadProject( wxFileName( projectToEdit ) );
            return 0;
        }
    }

    if( result != wxID_OK )
        return -1;

    if( selectedTemplatePath.IsEmpty() )
    {
        wxMessageBox( _( "No project template was selected.  Cannot generate new project." ), _( "Error" ),
                      wxOK | wxICON_ERROR, m_frame );

        return -1;
    }

    // Recreate the template object from the saved path
    PROJECT_TEMPLATE selectedTemplate( selectedTemplatePath );

    bool isMultiBoard = isMultiBoardTemplate( selectedTemplate );

    wxString        default_dir = wxFileName( Prj().GetProjectFullName() ).GetPathWithSep();
    wxString        title = _( "New Project Folder" );
    wxString        wildcard = FILEEXT::ProjectFileWildcard();
    wxFileDialog    dlg( m_frame, title, default_dir, wxEmptyString, wildcard,
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    dlg.AddShortcut( PATHS::GetDefaultUserProjectsPath() );

    FILEDLG_NEW_PROJECT newProjectHook;
    dlg.SetCustomizeHook( newProjectHook );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return -1;

    wxFileName fn( dlg.GetPath() );

    const std::string& targetExt = FILEEXT::ProjectFileExtension;

    if( !fn.GetExt().IsEmpty() && fn.GetExt().ToStdString() != targetExt )
        fn.SetName( fn.GetName() + wxT( "." ) + fn.GetExt() );

    fn.SetExt( wxString::FromUTF8( targetExt ) );

    if( !fn.IsAbsolute() )
        fn.MakeAbsolute();

    bool createNewDir = false;
    createNewDir = newProjectHook.GetCreateNewDir();

    if( createNewDir )
        fn.AppendDir( fn.GetName() );

    if( !fn.DirExists() && !fn.Mkdir() )
    {
        DisplayErrorMessage( m_frame, wxString::Format( _( "Folder '%s' could not be created.\n\n"
                                                           "Make sure you have write permissions and try again." ),
                                                        fn.GetPath() ) );
        return -1;
    }

    if( !fn.IsDirWritable() )
    {
        DisplayErrorMessage(
                m_frame, wxString::Format( _( "Insufficient permissions to write to folder '%s'." ), fn.GetPath() ) );
        return -1;
    }

    std::vector<wxFileName> destFiles;

    if( selectedTemplate.GetDestinationFiles( fn, destFiles ) )
    {
        std::vector<wxFileName> overwrittenFiles;

        for( const wxFileName& file : destFiles )
        {
            if( file.FileExists() )
                overwrittenFiles.push_back( file );
        }

        if( !overwrittenFiles.empty() )
        {
            wxString extendedMsg = _( "Overwriting files:" ) + "\n";

            for( const wxFileName& file : overwrittenFiles )
                extendedMsg += "\n" + file.GetFullName();

            KIDIALOG msgDlg( m_frame, _( "Similar files already exist in the destination folder." ),
                             _( "Confirmation" ), wxOK | wxCANCEL | wxICON_WARNING );
            msgDlg.SetExtendedMessage( extendedMsg );
            msgDlg.SetOKLabel( _( "Overwrite" ) );
            msgDlg.DoNotShowCheckbox( __FILE__, __LINE__ );

            if( msgDlg.ShowModal() == wxID_CANCEL )
                return -1;
        }
    }

    wxString errorMsg;

    if( !selectedTemplate.CreateProject( fn, &errorMsg ) )
    {
        DisplayErrorMessage( m_frame, _( "A problem occurred creating new project from template." ), errorMsg );
        return -1;
    }

    // Update MRU list with the used template
    wxFileName templateDir = selectedTemplate.GetHtmlFile();
    templateDir.RemoveLastDir();
    wxString templatePath = templateDir.GetPath();

    settings->m_LastUsedTemplate = templatePath;

    // Add to front of recent templates, remove duplicates, trim to 5
    std::vector<wxString>& recentTemplates = settings->m_RecentTemplates;
    recentTemplates.erase( std::remove( recentTemplates.begin(), recentTemplates.end(), templatePath ),
                           recentTemplates.end() );
    recentTemplates.insert( recentTemplates.begin(), templatePath );

    if( recentTemplates.size() > 5 )
        recentTemplates.resize( 5 );

    if( isMultiBoard )
    {
        // Delegate to the frame loader, which handles the Setup dialog (for
        // populating sub-projects), loads the first sub-project so editors
        // have a PROJECT, and stores the multi-board container on the frame.
        m_frame->LoadMultiBoardProject( fn );
        return 0;
    }

    m_frame->CreateNewProject( fn.GetFullPath() );
    m_frame->LoadProject( fn );
    return 0;
}


int KICAD_MANAGER_CONTROL::EditMultiBoardSchematic( const TOOL_EVENT& aEvent )
{
    PROJECT_FILE* multi = m_frame->GetMultiBoardProject();

    if( !multi )
    {
        wxMessageBox( _( "The current session is not a multi-board project." ),
                      _( "No Multi-Board Project" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    wxString containerBasename = wxFileName( multi->GetFullFilename() ).GetName();

    wxFileName mbs = ::EnsureMbsFile( *multi, containerBasename );

    if( !mbs.IsOk() || !mbs.FileExists() )
    {
        DisplayErrorMessage( m_frame,
                             _( "Could not create or locate the multi-board schematic file." ) );
        return 0;
    }

    // Persist any mbs_file update.
    multi->SaveToFile();

    // Launch the schematic editor via Kiway (in-process; Zeo does not ship a
    // stand-alone eeschema executable) and open the MBS file in it.
    if( m_inShowPlayer )
        return -1;

    REENTRANCY_GUARD guard( &m_inShowPlayer );

    KIWAY_PLAYER* player = nullptr;

    try
    {
        player = m_frame->Kiway().Player( FRAME_MBSCH, true );
    }
    catch( const IO_ERROR& err )
    {
        wxLogError( _( "Application failed to load:\n" ) + err.What() );
        return -1;
    }

    if( !player )
    {
        wxLogError( _( "Schematic editor could not start." ) );
        return -1;
    }

    // Pin this MBSCH frame to the container PROJECT regardless of which
    // sub-project SETTINGS_MANAGER currently has active. Without this,
    // opening the MBS while a peer PCB editor is active on a sub-project
    // would make SCH_EDIT_FRAME::OpenProjectFiles think the container is
    // a "different project" and unload the sub-project out from under
    // the PCB editor, dangling its BOARD::m_project.
    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();

    if( PROJECT* containerProject =
                sm.GetProject( wxFileName( multi->GetFullFilename() ).GetFullPath() ) )
    {
        player->SetPrjOverride( containerProject );
    }

    std::vector<wxString> file_list{ mbs.GetFullPath() };

    if( !player->OpenProjectFiles( file_list ) )
    {
        player->Destroy();
        return -1;
    }

    wxBusyCursor busy;
    player->Show( true );

    if( player->IsIconized() )
        player->Iconize( false );

    player->Raise();
    player->SetFocus();

    return 0;
}


int KICAD_MANAGER_CONTROL::SpawnPeerSchematic( const TOOL_EVENT& aEvent )
{
    PROJECT_FILE* multi = m_frame->GetMultiBoardProject();

    if( !multi )
    {
        wxMessageBox( _( "The current session is not a multi-board project." ),
                      _( "No Multi-Board Project" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    const auto& subs = multi->GetSubProjects();

    if( subs.empty() )
    {
        wxMessageBox( _( "This multi-board project has no sub-boards yet." ),
                      _( "No Sub-Boards" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    if( !m_frame->SpawnPeerSchematicEditor( subs.front().uuid ) )
    {
        wxMessageBox( _( "Failed to open peer schematic editor." ),
                      _( "Open Failed" ), wxOK | wxICON_ERROR, m_frame );
        return 0;
    }

    return 0;
}


int KICAD_MANAGER_CONTROL::OpenSubProjectSchematicById( const TOOL_EVENT& aEvent )
{
    KIID uuid = aEvent.Parameter<KIID>();

    if( uuid == niluuid )
        return 0;

    if( !m_frame->SpawnPeerSchematicEditor( uuid ) )
    {
        wxMessageBox( _( "Failed to open the selected sub-board's schematic editor." ),
                      _( "Open Failed" ), wxOK | wxICON_ERROR, m_frame );
    }

    return 0;
}


int KICAD_MANAGER_CONTROL::OpenSubProjectPcbById( const TOOL_EVENT& aEvent )
{
    KIID uuid = aEvent.Parameter<KIID>();

    if( uuid == niluuid )
        return 0;

    if( !m_frame->SpawnPeerPcbEditor( uuid ) )
    {
        wxMessageBox( _( "Failed to open the selected sub-board's PCB editor." ),
                      _( "Open Failed" ), wxOK | wxICON_ERROR, m_frame );
    }

    return 0;
}


int KICAD_MANAGER_CONTROL::OpenAssemblyViewer( const TOOL_EVENT& aEvent )
{
    PROJECT_FILE* multi = m_frame->GetMultiBoardProject();

    if( !multi )
    {
        wxMessageBox( _( "The current session is not a multi-board project." ),
                      _( "No Multi-Board Project" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    if( multi->GetSubProjects().empty() )
    {
        wxMessageBox( _( "This multi-board project has no sub-boards yet." ),
                      _( "No Sub-Boards" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    // Reuse an existing assembly viewer launched from this manager if
    // one is already open. Window name is qualified by the manager
    // frame's name, distinct from any per-PCB 3D viewers that may also
    // be open.
    const wxString viewerName = QUALIFIED_VIEWER3D_FRAMENAME( m_frame );
    wxWindow*      viewer     = wxWindow::FindWindowByName( viewerName );

    if( !viewer )
    {
        // Dispatch through the pcbnew kiface (which already links against
        // 3d-viewer) rather than instantiating EDA_3D_VIEWER_FRAME here.
        // Bypasses Kiway's single-player cache so the manager-spawned
        // assembly viewer doesn't collide with any concurrent single-
        // board 3D viewer created from a PCB editor.
        KIFACE* kiface = m_frame->Kiway().KiFACE( KIWAY::FACE_PCB, true );

        if( !kiface )
        {
            wxLogError( _( "Could not load pcbnew kiface for 3D viewer." ) );
            return -1;
        }

        viewer = dynamic_cast<wxWindow*>(
                kiface->CreateKiWindow( m_frame, FRAME_PCB_DISPLAY3D, &m_frame->Kiway() ) );

        if( !viewer )
        {
            wxLogError( _( "3D Assembly viewer could not be created." ) );
            return -1;
        }
    }

    // IsIconized/Iconize live on wxTopLevelWindow; the CreateKiWindow
    // return is a wxWindow*, but the underlying object is always a
    // frame. Cast up to reach the minimize-state API.
    if( auto* tlw = dynamic_cast<wxTopLevelWindow*>( viewer ) )
    {
        if( tlw->IsIconized() )
            tlw->Iconize( false );
    }

    viewer->Raise();
    viewer->Show( true );

    if( wxWindow::FindFocus() != viewer )
        viewer->SetFocus();

    return 0;
}


int KICAD_MANAGER_CONTROL::SwitchSubBoard( const TOOL_EVENT& aEvent )
{
    PROJECT_FILE* multi = m_frame->GetMultiBoardProject();

    if( !multi )
    {
        wxMessageBox( _( "The current session is not a multi-board project." ),
                      _( "No Multi-Board Project" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    const auto& subs = multi->GetSubProjects();

    if( subs.empty() )
    {
        wxMessageBox(
                _( "This multi-board project has no sub-boards yet.\n"
                   "Use 'File > Manage Sub-Boards...' to add one." ),
                _( "No Sub-Boards" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    wxArrayString         choices;
    std::vector<KIID>     uuids;
    wxString              currentProPath = m_frame->GetProjectFileName();
    int                   initial = 0;

    for( size_t i = 0; i < subs.size(); i++ )
    {
        const SUB_PROJECT_INFO& info = subs[i];

        wxString label = info.displayName.IsEmpty() ? info.name : info.displayName;
        label += wxT( "   " );
        label += info.relativePath;

        wxFileName resolved = multi->ResolveSubProjectPath( info );

        if( resolved.GetFullPath() == currentProPath )
        {
            label = wxT( "\u2022  " ) + label;  // bullet indicates active
            initial = (int) i;
        }

        choices.Add( label );
        uuids.push_back( info.uuid );
    }

    wxSingleChoiceDialog dlg( m_frame, _( "Choose the sub-board to activate:" ),
                              _( "Switch Sub-Board" ), choices );
    dlg.SetSelection( initial );

    if( dlg.ShowModal() != wxID_OK )
        return 0;

    int picked = dlg.GetSelection();

    if( picked < 0 || picked >= (int) uuids.size() )
        return 0;

    if( picked == initial )
        return 0;  // no-op

    m_frame->SwitchActiveSubProject( uuids[picked] );
    return 0;
}


int KICAD_MANAGER_CONTROL::ManageSubBoards( const TOOL_EVENT& aEvent )
{
    PROJECT_FILE* multi = m_frame->GetMultiBoardProject();

    if( !multi )
    {
        wxMessageBox( _( "The current session is not a multi-board project." ),
                      _( "No Multi-Board Project" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    wxFileName multiFile( multi->GetFullFilename() );

    DIALOG_MULTI_BOARD_SETUP dlg( m_frame, multi, multiFile );
    dlg.ShowModal();

    // Safety save (the dialog's Done handler also saves)
    multi->SaveToFile();

    return 0;
}


int KICAD_MANAGER_CONTROL::SyncCrossBoardNets( const TOOL_EVENT& aEvent )
{
    PROJECT_FILE* multi = m_frame->GetMultiBoardProject();

    if( !multi )
    {
        wxMessageBox( _( "The current session is not a multi-board project." ),
                      _( "No Multi-Board Project" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    // Reload from disk — eeschema writes cross_board_nets during its
    // save hook, and the SyncCrossBoardNets action runs in the manager
    // frame which doesn't share the eeschema kiface's in-memory copy.
    multi->LoadFromFile();

    if( multi->GetCrossBoardNets().empty() )
    {
        wxMessageBox( _( "No cross-board nets are defined yet.\n\n"
                         "Open the multi-board schematic, wire pins between "
                         "module blocks, then save — the nets will be extracted "
                         "automatically. If you already saved but still see this "
                         "message, try saving the multi-board schematic again "
                         "after the most recent Zeo update." ),
                      _( "Nothing to Sync" ), wxOK | wxICON_INFORMATION, m_frame );
        return 0;
    }

    MB_CROSS_BOARD_SYNC_RESULT result = ApplyCrossBoardNetsToSubProjectPCBs( *multi );

    // Persist any net-name changes the resolver adopted from local PCB nets.
    multi->SaveToFile();

    wxMessageBox( result.summary, _( "Cross-Board Net Sync" ),
                  wxOK | wxICON_INFORMATION, m_frame );

    return 0;
}


int KICAD_MANAGER_CONTROL::NewFromRepository( const TOOL_EVENT& aEvent )
{
    DIALOG_GIT_REPOSITORY dlg( m_frame, nullptr );

    dlg.SetTitle( _( "Clone Project from Git Repository" ) );

    int ret = dlg.ShowModal();

    if( ret != wxID_OK )
        return -1;

    wxString   project_name = dlg.GetRepoName();
    wxFileName pro = newProjectDirectory( &project_name, true );

    if( !pro.IsOk() )
        return -1;

    PROJECT_TREE_PANE* pane = static_cast<PROJECT_TREE_PANE*>( m_frame->GetToolCanvas() );


    GIT_CLONE_HANDLER cloneHandler( pane->m_TreeProject->GitCommon() );

    cloneHandler.SetRemote( dlg.GetFullURL() );
    cloneHandler.SetClonePath( pro.GetPath() );
    cloneHandler.SetUsername( dlg.GetUsername() );
    cloneHandler.SetPassword( dlg.GetPassword() );
    cloneHandler.SetSSHKey( dlg.GetRepoSSHPath() );

    cloneHandler.SetProgressReporter(
            std::make_unique<WX_PROGRESS_REPORTER>( m_frame, _( "Clone Repository" ), 1, PR_NO_ABORT ) );

    if( !cloneHandler.PerformClone() )
    {
        DisplayErrorMessage( m_frame, cloneHandler.GetErrorString() );
        return -1;
    }

    std::vector<wxString> projects = cloneHandler.GetProjectDirs();

    if( projects.empty() )
    {
        DisplayErrorMessage( m_frame, _( "No project files were found in the repository." ) );
        return -1;
    }

    // Currently, we pick the first project file we find in the repository.
    // TODO: Look into spare checkout to allow the user to pick a partial repository
    wxString dest = pro.GetPath() + wxFileName::GetPathSeparator() + projects.front();
    m_frame->LoadProject( dest );

    KIPLATFORM::SECRETS::StoreSecret( dlg.GetRepoURL(), dlg.GetUsername(), dlg.GetPassword() );
    Prj().GetLocalSettings().m_GitRepoUsername = dlg.GetUsername();
    Prj().GetLocalSettings().m_GitSSHKey = dlg.GetRepoSSHPath();

    if( dlg.GetRepoType() == KIGIT_COMMON::GIT_CONN_TYPE::GIT_CONN_SSH )
        Prj().GetLocalSettings().m_GitRepoType = "ssh";
    else if( dlg.GetRepoType() == KIGIT_COMMON::GIT_CONN_TYPE::GIT_CONN_HTTPS )
        Prj().GetLocalSettings().m_GitRepoType = "https";
    else
        Prj().GetLocalSettings().m_GitRepoType = "local";

    return 0;
}


int KICAD_MANAGER_CONTROL::NewJobsetFile( const TOOL_EVENT& aEvent )
{
    wxString     default_dir = wxFileName( Prj().GetProjectFullName() ).GetPathWithSep();
    wxFileDialog dlg( m_frame, _( "Create New Jobset" ), default_dir, wxEmptyString, FILEEXT::JobsetFileWildcard(),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return -1;

    wxFileName jobsetFn( dlg.GetPath() );

    // Check if the file already exists
    bool fileExists = wxFileExists( jobsetFn.GetFullPath() );

    if( fileExists )
    {
        // Remove the existing file so that a new one can be created
        if( !wxRemoveFile( jobsetFn.GetFullPath() ) )
        {
            return -1;
        }
    }

    m_frame->OpenJobsFile( jobsetFn.GetFullPath(), true );

    return 0;
}


int KICAD_MANAGER_CONTROL::openProject( const wxString& aDefaultDir )
{
    wxString wildcard = FILEEXT::AllProjectFilesWildcard() + "|"
                      + FILEEXT::ProjectFileWildcard() + "|"
                      + FILEEXT::LegacyProjectFileWildcard();

    wxFileDialog dlg( m_frame, _( "Open Existing Project" ), aDefaultDir, wxEmptyString, wildcard,
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    dlg.AddShortcut( PATHS::GetDefaultUserProjectsPath() );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return -1;

    wxFileName pro( dlg.GetPath() );

    if( !pro.IsAbsolute() )
        pro.MakeAbsolute();

    // You'd think wxFD_FILE_MUST_EXIST and the wild-cards would enforce these.  Sentry
    // indicates otherwise (at least on MSW).
    if( !pro.Exists()
        || ( pro.GetExt() != FILEEXT::ProjectFileExtension
             && pro.GetExt() != FILEEXT::LegacyProjectFileExtension ) )
    {
        return -1;
    }

    // LoadMultiBoardProject is a superset of LoadProject: it does the
    // regular load and, if the project file carries the container flag,
    // runs the multi-board post-load (Setup dialog if empty, title, etc.)
    // Routing every open through it keeps the single-board path unchanged.
    m_frame->LoadMultiBoardProject( pro );
    return 0;
}


int KICAD_MANAGER_CONTROL::OpenDemoProject( const TOOL_EVENT& aEvent )
{
    return openProject( PATHS::GetStockDemosPath() );
}


int KICAD_MANAGER_CONTROL::OpenProject( const TOOL_EVENT& aEvent )
{
    return openProject( m_frame->GetMruPath() );
}


int KICAD_MANAGER_CONTROL::OpenJobsetFile( const TOOL_EVENT& aEvent )
{
    wxString     default_dir = wxFileName( Prj().GetProjectFullName() ).GetPathWithSep();
    wxFileDialog dlg( m_frame, _( "Open Jobset" ), default_dir, wxEmptyString, FILEEXT::JobsetFileWildcard(),
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return -1;

    wxFileName jobsetFn( dlg.GetPath() );

    m_frame->OpenJobsFile( jobsetFn.GetFullPath(), true );

    return 0;
}


int KICAD_MANAGER_CONTROL::CloseProject( const TOOL_EVENT& aEvent )
{
    m_frame->CloseProject( true );
    return 0;
}


int KICAD_MANAGER_CONTROL::LoadProject( const TOOL_EVENT& aEvent )
{
    if( aEvent.Parameter<wxString*>() )
        m_frame->LoadProject( wxFileName( *aEvent.Parameter<wxString*>() ) );
    return 0;
}


int KICAD_MANAGER_CONTROL::ArchiveProject( const TOOL_EVENT& aEvent )
{
    wxFileName fileName = m_frame->GetProjectFileName();

    fileName.SetExt( FILEEXT::ArchiveFileExtension );

    wxFileDialog dlg( m_frame, _( "Archive Project Files" ), fileName.GetPath(), fileName.GetFullName(),
                      FILEEXT::ZipFileWildcard(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return 0;

    wxFileName zipFile = dlg.GetPath();

    wxString currdirname = fileName.GetPathWithSep();
    wxDir    dir( currdirname );

    if( !dir.IsOpened() ) // wxWidgets display a error message on issue.
        return 0;

    STATUSBAR_REPORTER reporter( m_frame->GetStatusBar(), 1 );
    PROJECT_ARCHIVER   archiver;

    archiver.Archive( currdirname, zipFile.GetFullPath(), reporter, true, true );
    return 0;
}


int KICAD_MANAGER_CONTROL::UnarchiveProject( const TOOL_EVENT& aEvent )
{
    m_frame->UnarchiveFiles();
    return 0;
}


int KICAD_MANAGER_CONTROL::ExploreProject( const TOOL_EVENT& aEvent )
{
    // Open project directory in host OS's file explorer
    LaunchExternal( Prj().GetProjectPath() );
    return 0;
}

int KICAD_MANAGER_CONTROL::RestoreLocalHistory( const TOOL_EVENT& aEvent )
{
    m_frame->RestoreLocalHistory();
    return 0;
}


int KICAD_MANAGER_CONTROL::ToggleLocalHistory( const TOOL_EVENT& aEvent )
{
    m_frame->ToggleLocalHistory();
    return 0;
}


int KICAD_MANAGER_CONTROL::ViewDroppedViewers( const TOOL_EVENT& aEvent )
{
    if( aEvent.Parameter<wxString*>() )
        wxExecute( *aEvent.Parameter<wxString*>(), wxEXEC_ASYNC );

    return 0;
}


int KICAD_MANAGER_CONTROL::SaveProjectAs( const TOOL_EVENT& aEvent )
{
    wxString msg;

    wxFileName currentProjectFile( Prj().GetProjectFullName() );
    wxString   currentProjectDirPath = currentProjectFile.GetPath();
    wxString   currentProjectName = Prj().GetProjectName();

    wxString default_dir = m_frame->GetMruPath();

    Prj().GetProjectFile().SaveToFile( currentProjectDirPath );
    Prj().GetLocalSettings().SaveToFile( currentProjectDirPath );

    if( default_dir == currentProjectDirPath || default_dir == currentProjectDirPath + wxFileName::GetPathSeparator() )
    {
        // Don't start within the current project
        wxFileName default_dir_fn( default_dir );
        default_dir_fn.RemoveLastDir();
        default_dir = default_dir_fn.GetPath();
    }

    wxFileDialog dlg( m_frame, _( "Save Project To" ), default_dir, wxEmptyString, wxEmptyString, wxFD_SAVE );

    dlg.AddShortcut( PATHS::GetDefaultUserProjectsPath() );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return -1;

    wxFileName newProjectDir( dlg.GetPath(), wxEmptyString );

    if( !newProjectDir.IsAbsolute() )
        newProjectDir.MakeAbsolute();

    if( wxDirExists( newProjectDir.GetFullPath() ) )
    {
        msg.Printf( _( "'%s' already exists." ), newProjectDir.GetFullPath() );
        DisplayErrorMessage( m_frame, msg );
        return -1;
    }

    if( !wxMkdir( newProjectDir.GetFullPath() ) )
    {
        DisplayErrorMessage( m_frame, wxString::Format( _( "Folder '%s' could not be created.\n\n"
                                                           "Please make sure you have sufficient permissions." ),
                                                        newProjectDir.GetPath() ) );
        return -1;
    }

    if( !newProjectDir.IsDirWritable() )
    {
        DisplayErrorMessage( m_frame, wxString::Format( _( "Insufficient permissions to write to folder '%s'." ),
                                                        newProjectDir.GetFullPath() ) );
        return -1;
    }

    const wxString& newProjectDirPath = newProjectDir.GetFullPath();
    const wxString& newProjectName = newProjectDir.GetDirs().Last();
    wxDir           currentProjectDir( currentProjectDirPath );

    PROJECT_TREE_TRAVERSER traverser( m_frame, currentProjectDirPath, currentProjectName, newProjectDirPath,
                                      newProjectName );

    currentProjectDir.Traverse( traverser );

    if( !traverser.GetErrors().empty() )
        DisplayErrorMessage( m_frame, traverser.GetErrors() );

    if( !traverser.GetNewProjectFile().FileExists() )
        m_frame->CreateNewProject( traverser.GetNewProjectFile() );

    m_frame->LoadProject( traverser.GetNewProjectFile() );

    return 0;
}


int KICAD_MANAGER_CONTROL::Refresh( const TOOL_EVENT& aEvent )
{
    m_frame->RefreshProjectTree();
    return 0;
}


int KICAD_MANAGER_CONTROL::UpdateMenu( const TOOL_EVENT& aEvent )
{
    ACTION_MENU*      actionMenu = aEvent.Parameter<ACTION_MENU*>();
    CONDITIONAL_MENU* conditionalMenu = dynamic_cast<CONDITIONAL_MENU*>( actionMenu );
    SELECTION         dummySel;

    if( conditionalMenu )
        conditionalMenu->Evaluate( dummySel );

    if( actionMenu )
        actionMenu->UpdateAll();

    return 0;
}


int KICAD_MANAGER_CONTROL::ShowTerminal( const TOOL_EVENT& aEvent )
{
    return ShowPlayer( aEvent );
}

int KICAD_MANAGER_CONTROL::ShowPlayer( const TOOL_EVENT& aEvent )
{
    FRAME_T       playerType = aEvent.Parameter<FRAME_T>();
    KIWAY_PLAYER* player;

    wxLogInfo( "ShowPlayer: requested frame type %d", (int) playerType );

    if( playerType == FRAME_SCH && !m_frame->IsProjectActive() )
    {
        DisplayInfoMessage( m_frame, _( "Create (or open) a project to edit a schematic." ), wxEmptyString );
        return -1;
    }
    else if( playerType == FRAME_PCB_EDITOR && !m_frame->IsProjectActive() )
    {
        DisplayInfoMessage( m_frame, _( "Create (or open) a project to edit a pcb." ), wxEmptyString );
        return -1;
    }

    if( m_inShowPlayer )
    {
        wxLogWarning( "ShowPlayer: reentrancy guard active, skipping frame type %d", (int) playerType );
        return -1;
    }

    REENTRANCY_GUARD guard( &m_inShowPlayer );

    try
    {
        player = m_frame->Kiway().Player( playerType, true );
    }
    catch( const IO_ERROR& err )
    {
        wxLogError( _( "Application failed to load:\n" ) + err.What() );
        return -1;
    }

    if( !player )
    {
        wxLogError( _( "Application cannot start (frame type %d)." ), (int) playerType );
        return -1;
    }

    // Re-send the shared auth pointer to the agent frame. Handles the case where the
    // frame was destroyed and recreated (e.g., force-close) after the initial startup
    // dispatch in KICAD_MANAGER_FRAME::onReady.
    if( playerType == FRAME_AGENT )
    {
        KICAD_MANAGER_FRAME* kmf = static_cast<KICAD_MANAGER_FRAME*>( m_frame );
        SESSION_MANAGER*     sm = kmf->GetSessionManager();

        if( sm && sm->GetAuth() )
        {
            std::string authPtr = std::to_string(
                    reinterpret_cast<uintptr_t>( sm->GetAuth() ) );
            m_frame->Kiway().ExpressMail( FRAME_AGENT, MAIL_AUTH_POINTER, authPtr, m_frame );
        }
    }

    if( !player->IsVisible() ) // A hidden frame might not have the document loaded.
    {
        wxString filepath;

        if( playerType == FRAME_SCH )
        {
            wxFileName kicad_schematic( m_frame->SchFileName() );
            wxFileName legacy_schematic( m_frame->SchLegacyFileName() );

            if( !legacy_schematic.FileExists() || kicad_schematic.FileExists() )
                filepath = kicad_schematic.GetFullPath();
            else
                filepath = legacy_schematic.GetFullPath();
        }
        else if( playerType == FRAME_PCB_EDITOR )
        {
            wxFileName kicad_board( m_frame->PcbFileName() );
            wxFileName legacy_board( m_frame->PcbLegacyFileName() );

            if( !legacy_board.FileExists() || kicad_board.FileExists() )
                filepath = kicad_board.GetFullPath();
            else
                filepath = legacy_board.GetFullPath();
        }

        if( !filepath.IsEmpty() )
        {
            std::vector<wxString> file_list{ filepath };

            if( !player->OpenProjectFiles( file_list ) )
            {
                player->Destroy();
                return -1;
            }
        }

        wxBusyCursor busy;
        player->Show( true );

        // Position the agent window to the right of an open schematic or PCB
        // editor.  Only resize actual editors — never the project launcher.
        if( playerType == FRAME_AGENT )
        {
            EDA_DRAW_FRAME* editorWin = nullptr;

            for( FRAME_T ft : { FRAME_SCH, FRAME_PCB_EDITOR } )
            {
                try
                {
                    KIWAY_PLAYER* ed = m_frame->Kiway().Player( ft, false );

                    if( ed && ed->IsVisible() && !ed->IsIconized() )
                    {
                        editorWin = dynamic_cast<EDA_DRAW_FRAME*>( ed );
                        break;
                    }
                }
                catch( ... )
                {
                }
            }

            if( editorWin )
            {
                wxDisplay display( wxDisplay::GetFromWindow( editorWin ) );
                wxRect    workArea = display.GetClientArea();
                int       agentW = std::min( 500, workArea.width / 3 );
                int       editorW = workArea.width - agentW;

                editorWin->SetSize( workArea.x, workArea.y, editorW, workArea.height );
                player->SetSize( workArea.x + editorW, workArea.y, agentW, workArea.height );

                // Defer the canvas repaint — the resize hasn't
                // taken effect on screen yet, so DoRePaint() would
                // bail out if we called ForceRefresh() synchronously.
                if( editorWin->GetCanvas() )
                {
                    editorWin->CallAfter( [editorWin]()
                    {
                        if( editorWin->GetCanvas() )
                            editorWin->GetCanvas()->Refresh();
                    } );
                }
            }
        }
    }

    // Needed on Windows, other platforms do not use it, but it creates no issue
    if( player->IsIconized() )
        player->Iconize( false );

    player->Raise();

    // Raising the window does not set the focus on Linux.  This should work on
    // any platform.
    if( wxWindow::FindFocus() != player )
        player->SetFocus();

    // Save window state to disk now.  Don't wait around for a crash.
    if( Pgm().GetCommonSettings()->m_Session.remember_open_files && !player->GetCurrentFileName().IsEmpty()
        && Prj().GetLocalSettings().ShouldAutoSave() )
    {
        wxFileName rfn( player->GetCurrentFileName() );
        rfn.MakeRelativeTo( Prj().GetProjectPath() );

        WINDOW_SETTINGS windowSettings;
        player->SaveWindowSettings( &windowSettings );

        Prj().GetLocalSettings().SaveFileState( rfn.GetFullPath(), &windowSettings, true );
        Prj().GetLocalSettings().SaveToFile( Prj().GetProjectPath() );
    }

    return 0;
}


int KICAD_MANAGER_CONTROL::Execute( const TOOL_EVENT& aEvent )
{
    wxString execFile;
    wxString param;

    if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::viewGerbers ) )
        execFile = GERBVIEW_EXE;
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::showAiAssistant ) )
        execFile = KICAD_AGENT_EXE;
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::convertImage ) )
        execFile = BITMAPCONVERTER_EXE;
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::showCalculator ) )
        execFile = PCB_CALCULATOR_EXE;
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::editDrawingSheet ) )
        execFile = PL_EDITOR_EXE;
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::openTextEditor ) )
        execFile = Pgm().GetTextEditor();
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::editOtherSch ) )
        execFile = EESCHEMA_EXE;
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::editOtherPCB ) )
        execFile = PCBNEW_EXE;
    else
        wxFAIL_MSG( "Execute(): unexpected request" );

    if( execFile.IsEmpty() )
        return 0;

    if( aEvent.Parameter<wxString*>() )
        param = *aEvent.Parameter<wxString*>();
    else if( aEvent.IsAction( &KICAD_MANAGER_ACTIONS::viewGerbers ) && m_frame->IsProjectActive() )
        param = m_frame->Prj().GetProjectPath();

    COMMON_CONTROL* commonControl = m_toolMgr->GetTool<COMMON_CONTROL>();
    return commonControl->Execute( execFile, param );
}


int KICAD_MANAGER_CONTROL::ShowPluginManager( const TOOL_EVENT& aEvent )
{
    if( KIPLATFORM::POLICY::GetPolicyBool( POLICY_KEY_PCM ) == KIPLATFORM::POLICY::PBOOL::DISABLED )
    {
        // policy disables the plugin manager
        return 0;
    }

    // For some reason, after a click or a double click the bitmap button calling
    // PCM keeps the focus althougt the focus was not set to this button.
    // This hack force removing the focus from this button
    m_frame->SetFocus();
    wxSafeYield();

    if( !m_frame->GetPcm() )
        m_frame->CreatePCM();

    DIALOG_PCM pcm( m_frame, m_frame->GetPcm() );
    pcm.ShowModal();

    const std::unordered_set<PCM_PACKAGE_TYPE>& changed = pcm.GetChangedPackageTypes();

    if( changed.count( PCM_PACKAGE_TYPE::PT_PLUGIN ) || changed.count( PCM_PACKAGE_TYPE::PT_FAB ) )
    {
        std::string payload = "";
        m_frame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_RELOAD_PLUGINS, payload );
    }

    KICAD_SETTINGS* settings = GetAppSettings<KICAD_SETTINGS>( "kicad" );

    if( changed.count( PCM_PACKAGE_TYPE::PT_LIBRARY ) && ( settings->m_PcmLibAutoAdd || settings->m_PcmLibAutoRemove ) )
    {
        KIWAY& kiway = m_frame->Kiway();

        // Reset state containing global lib tables
        if( KIFACE* kiface = kiway.KiFACE( KIWAY::FACE_SCH, false ) )
            kiface->Reset();

        if( KIFACE* kiface = kiway.KiFACE( KIWAY::FACE_PCB, false ) )
            kiface->Reset();

        // Reload lib tables
        std::string payload = "";

        kiway.ExpressMail( FRAME_FOOTPRINT_EDITOR, MAIL_RELOAD_LIB, payload );
        kiway.ExpressMail( FRAME_FOOTPRINT_VIEWER, MAIL_RELOAD_LIB, payload );
        kiway.ExpressMail( FRAME_CVPCB, MAIL_RELOAD_LIB, payload );
        kiway.ExpressMail( FRAME_SCH_SYMBOL_EDITOR, MAIL_RELOAD_LIB, payload );
        kiway.ExpressMail( FRAME_SCH_VIEWER, MAIL_RELOAD_LIB, payload );
    }

    if( changed.count( PCM_PACKAGE_TYPE::PT_COLORTHEME ) )
        Pgm().GetSettingsManager().ReloadColorSettings();

    return 0;
}


void KICAD_MANAGER_CONTROL::setTransitions()
{
    Go( &KICAD_MANAGER_CONTROL::NewProject, KICAD_MANAGER_ACTIONS::newProject.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::NewFromRepository, KICAD_MANAGER_ACTIONS::newFromRepository.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ManageSubBoards, KICAD_MANAGER_ACTIONS::manageSubBoards.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::SyncCrossBoardNets,
        KICAD_MANAGER_ACTIONS::syncCrossBoardNets.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::SwitchSubBoard, KICAD_MANAGER_ACTIONS::switchSubBoard.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::SpawnPeerSchematic,
        KICAD_MANAGER_ACTIONS::spawnPeerSchematic.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::OpenSubProjectSchematicById,
        KICAD_MANAGER_ACTIONS::openSubProjectSchematicById.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::OpenSubProjectPcbById,
        KICAD_MANAGER_ACTIONS::openSubProjectPcbById.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::EditMultiBoardSchematic,
        KICAD_MANAGER_ACTIONS::editMultiBoardSchematic.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::OpenAssemblyViewer,
        KICAD_MANAGER_ACTIONS::openAssemblyViewer.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::NewJobsetFile, KICAD_MANAGER_ACTIONS::newJobsetFile.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::OpenDemoProject, KICAD_MANAGER_ACTIONS::openDemoProject.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::OpenProject, KICAD_MANAGER_ACTIONS::openProject.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::OpenJobsetFile, KICAD_MANAGER_ACTIONS::openJobsetFile.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::CloseProject, KICAD_MANAGER_ACTIONS::closeProject.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::SaveProjectAs, ACTIONS::saveAs.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::LoadProject, KICAD_MANAGER_ACTIONS::loadProject.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ViewDroppedViewers, KICAD_MANAGER_ACTIONS::viewDroppedGerbers.MakeEvent() );

    Go( &KICAD_MANAGER_CONTROL::ArchiveProject, KICAD_MANAGER_ACTIONS::archiveProject.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::UnarchiveProject, KICAD_MANAGER_ACTIONS::unarchiveProject.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ExploreProject, KICAD_MANAGER_ACTIONS::openProjectDirectory.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::RestoreLocalHistory, KICAD_MANAGER_ACTIONS::restoreLocalHistory.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ToggleLocalHistory, KICAD_MANAGER_ACTIONS::showLocalHistory.MakeEvent() );

    Go( &KICAD_MANAGER_CONTROL::Refresh, ACTIONS::zoomRedraw.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::UpdateMenu, ACTIONS::updateMenu.MakeEvent() );

    Go( &KICAD_MANAGER_CONTROL::ShowPlayer, KICAD_MANAGER_ACTIONS::editSchematic.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ShowPlayer, KICAD_MANAGER_ACTIONS::editSymbols.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ShowPlayer, KICAD_MANAGER_ACTIONS::editPCB.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ShowPlayer, KICAD_MANAGER_ACTIONS::editFootprints.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ShowPlayer, KICAD_MANAGER_ACTIONS::showAiAssistant.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ShowPlayer, KICAD_MANAGER_ACTIONS::showVersionControl.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::Execute, KICAD_MANAGER_ACTIONS::viewGerbers.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::Execute, KICAD_MANAGER_ACTIONS::convertImage.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::Execute, KICAD_MANAGER_ACTIONS::showCalculator.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::Execute, KICAD_MANAGER_ACTIONS::editDrawingSheet.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::Execute, KICAD_MANAGER_ACTIONS::openTextEditor.MakeEvent() );

    Go( &KICAD_MANAGER_CONTROL::Execute, KICAD_MANAGER_ACTIONS::editOtherSch.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::Execute, KICAD_MANAGER_ACTIONS::editOtherPCB.MakeEvent() );
    Go( &KICAD_MANAGER_CONTROL::ShowTerminal, KICAD_MANAGER_ACTIONS::showTerminal.MakeEvent() );

    Go( &KICAD_MANAGER_CONTROL::ShowPluginManager, KICAD_MANAGER_ACTIONS::showPluginManager.MakeEvent() );
}
