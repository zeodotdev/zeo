#include "open_editor_handler.h"
#include <filesystem>
#include <wx/filename.h>
#include <wx/log.h>


PathValidationResult ValidatePathInProject( const std::string& aFilePath,
                                            const std::string& aProjectPath )
{
    if( aProjectPath.empty() )
        return PathValidationResult::Success( aFilePath );

    try
    {
        std::filesystem::path fsPath( aFilePath );
        std::filesystem::path projectPath( aProjectPath );

        if( fsPath.is_relative() )
            fsPath = projectPath / fsPath;

        auto canonicalProject = std::filesystem::canonical( projectPath );
        auto canonicalFile = std::filesystem::weakly_canonical( fsPath );

        auto projectStr = canonicalProject.string();
        auto fileStr = canonicalFile.string();

        if( !projectStr.empty() && projectStr.back() != '/' )
            projectStr += '/';

        if( fileStr.find( projectStr ) != 0 && fileStr != canonicalProject.string() )
        {
            return PathValidationResult( "File path must be within project directory: " +
                                         aProjectPath + " (resolved path: " + fileStr + ")" );
        }

        return PathValidationResult::Success( canonicalFile.string() );
    }
    catch( const std::filesystem::filesystem_error& e )
    {
        return PathValidationResult( "Invalid file path: " + std::string( e.what() ) );
    }
}


OpenEditorResult OPEN_EDITOR_HANDLER::Evaluate( const nlohmann::json& aInput,
                                                 const std::string& aProjectPath,
                                                 const std::string& aProjectName,
                                                 const std::vector<std::string>& aAllowedPaths,
                                                 bool aEditorShown,
                                                 const std::string& aCurrentFile )
{
    OpenEditorResult result;

    std::string editorType = aInput.value( "editor_type", "" );
    result.frameType = ( editorType == "sch" ) ? FRAME_SCH : FRAME_PCB_EDITOR;
    result.editorLabel = ( editorType == "sch" ) ? "Schematic" : "PCB";
    result.isSch = ( editorType == "sch" );

    std::string filePath = aInput.value( "file_path", "" );
    wxString resolvedFilePath;

    wxLogInfo( "OPEN_EDITOR_HANDLER::Evaluate: editor_type='%s', file_path='%s', projectPath='%s'",
               wxString::FromUTF8( editorType ), wxString::FromUTF8( filePath ),
               wxString::FromUTF8( aProjectPath ) );

    // --- Validate / resolve file path ---
    if( !filePath.empty() )
    {
        if( aAllowedPaths.empty() )
        {
            result.action = OpenEditorResult::ERROR;
            result.errorMessage = "Error: no project or editor is open";
            return result;
        }

        bool pathValid = false;
        PathValidationResult pathResult;

        for( const auto& allowed : aAllowedPaths )
        {
            pathResult = ValidatePathInProject( filePath, allowed );
            if( pathResult.valid )
            {
                pathValid = true;
                break;
            }
        }

        if( !pathValid )
        {
            wxLogWarning( "OPEN_EDITOR_HANDLER: Path validation failed: %s", pathResult.error );
            result.action = OpenEditorResult::ERROR;
            result.errorMessage = "Error: " + pathResult.error;
            return result;
        }

        resolvedFilePath = wxString::FromUTF8( pathResult.resolvedPath );
        wxLogInfo( "OPEN_EDITOR_HANDLER: Validated file_path -> '%s'", resolvedFilePath );
    }
    else
    {
        // No file path — auto-detect from project
        if( !aProjectName.empty() && !aProjectPath.empty() )
        {
            wxString defaultFile;
            if( editorType == "sch" )
                defaultFile = wxString::FromUTF8( aProjectPath + aProjectName ) + ".kicad_sch";
            else
                defaultFile = wxString::FromUTF8( aProjectPath + aProjectName ) + ".kicad_pcb";

            if( wxFileExists( defaultFile ) )
            {
                resolvedFilePath = defaultFile;
                wxLogInfo( "OPEN_EDITOR_HANDLER: Auto-detected project file: %s", defaultFile );
            }
            else
            {
                wxLogWarning( "OPEN_EDITOR_HANDLER: Default file does not exist: %s", defaultFile );
            }
        }
    }

    result.filePath = resolvedFilePath;

    // --- Determine action based on editor state ---
    if( aEditorShown )
    {
        if( !resolvedFilePath.IsEmpty() )
        {
            wxFileName currentFn( wxString::FromUTF8( aCurrentFile ) );
            wxFileName requestedFn( resolvedFilePath );
            currentFn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG );
            requestedFn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG );

            if( aCurrentFile.empty() || currentFn.GetFullPath() != requestedFn.GetFullPath() )
            {
                // Different file or untitled — close and reopen
                wxLogInfo( "OPEN_EDITOR_HANDLER: %s - will reload with '%s'",
                           aCurrentFile.empty() ? "Editor has untitled document" : "Different file open",
                           requestedFn.GetFullPath() );

                result.action = OpenEditorResult::RELOAD_WITH_FILE;
                result.resultMessage = result.editorLabel.ToStdString() +
                    " editor reloaded with file: " + resolvedFilePath.ToStdString();
            }
            else
            {
                // Same file — just focus
                wxLogInfo( "OPEN_EDITOR_HANDLER: File '%s' already open, focusing",
                           currentFn.GetFullPath() );

                result.action = OpenEditorResult::FOCUS_EXISTING;
                result.resultMessage = result.editorLabel.ToStdString() +
                    " editor already has file open: " + resolvedFilePath.ToStdString();
            }
        }
        else
        {
            // No file path, editor already open — just focus
            result.action = OpenEditorResult::FOCUS_EXISTING;
            result.resultMessage = result.editorLabel.ToStdString() + " editor is already open";
        }
    }
    else
    {
        // Editor not open — request approval
        result.action = OpenEditorResult::NEEDS_APPROVAL;
    }

    return result;
}
