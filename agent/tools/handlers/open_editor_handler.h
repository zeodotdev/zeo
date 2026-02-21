#ifndef OPEN_EDITOR_HANDLER_H
#define OPEN_EDITOR_HANDLER_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <frame_type.h>
#include <wx/string.h>


struct PathValidationResult
{
    bool        valid;
    std::string error;
    std::string resolvedPath;

    PathValidationResult() : valid( true ) {}
    PathValidationResult( const std::string& aError ) : valid( false ), error( aError ) {}

    static PathValidationResult Success( const std::string& aResolvedPath )
    {
        PathValidationResult r;
        r.resolvedPath = aResolvedPath;
        return r;
    }
};


PathValidationResult ValidatePathInProject( const std::string& aFilePath,
                                            const std::string& aProjectPath );


struct OpenEditorResult
{
    enum Action
    {
        FOCUS_EXISTING,     ///< Editor already open with correct file — just focus
        RELOAD_WITH_FILE,   ///< Editor open but needs a different file — close and reopen
        NEEDS_APPROVAL,     ///< Editor not open — show approval dialog
        ERROR               ///< Validation error — return error to LLM
    };

    Action      action;
    FRAME_T     frameType;
    wxString    editorLabel;     // "Schematic" or "PCB"
    wxString    filePath;        // Resolved file path (may be empty)
    std::string resultMessage;   // For immediate success results
    std::string errorMessage;    // For errors
    bool        isSch;           // true = schematic, false = PCB
};


class OPEN_EDITOR_HANDLER
{
public:
    /**
     * Evaluate an open_editor tool call: validate paths, auto-detect file, determine action.
     *
     * @param aInput           Tool input JSON (editor_type, file_path)
     * @param aProjectPath     Current project directory path
     * @param aProjectName     Current project name (for auto-detect)
     * @param aAllowedPaths    Paths the agent is allowed to access
     * @param aEditorShown     Whether the target editor is currently visible
     * @param aCurrentFile     File currently loaded in the editor (empty if untitled)
     */
    OpenEditorResult Evaluate( const nlohmann::json& aInput,
                                const std::string& aProjectPath,
                                const std::string& aProjectName,
                                const std::vector<std::string>& aAllowedPaths,
                                bool aEditorShown,
                                const std::string& aCurrentFile );
};

#endif // OPEN_EDITOR_HANDLER_H
