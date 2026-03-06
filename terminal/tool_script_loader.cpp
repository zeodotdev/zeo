#include "tool_script_loader.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>


// ---------------------------------------------------------------------------
// File loading helpers (mirrors PYTHON_TOOL_HANDLER)
// ---------------------------------------------------------------------------

std::string TOOL_SCRIPT_LOADER::FindPythonDir()
{
    // 1. Check environment variable (for development and testing)
    const char* envDir = std::getenv( "AGENT_PYTHON_DIR" );

    if( envDir && envDir[0] )
    {
        wxLogDebug( "TOOL_SCRIPT_LOADER: Using AGENT_PYTHON_DIR=%s", envDir );
        return std::string( envDir );
    }

    // 2. Locate relative to the running executable
    //    macOS:   Zeo.app/Contents/MacOS/kicad -> Contents/SharedSupport/agent/python/
    //    Windows: bin/kicad.exe -> bin/agent/python/
    wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
    wxFileName pythonDir( exePath.GetPath(), "" );
#ifdef __WXMSW__
    pythonDir.AppendDir( "agent" );
    pythonDir.AppendDir( "python" );
#else
    pythonDir.RemoveLastDir();                  // remove MacOS
    pythonDir.AppendDir( "SharedSupport" );
    pythonDir.AppendDir( "agent" );
    pythonDir.AppendDir( "python" );
#endif

    std::string result = pythonDir.GetPath().ToStdString();
    wxLogDebug( "TOOL_SCRIPT_LOADER: Python scripts dir: %s", result );
    return result;
}


std::string TOOL_SCRIPT_LOADER::ReadFile( const std::string& aPath )
{
    std::ifstream file( aPath );

    if( !file.is_open() )
    {
        wxLogWarning( "TOOL_SCRIPT_LOADER: Failed to read file: %s", aPath );
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}


// ---------------------------------------------------------------------------
// Constructor: load manifest + scripts
// ---------------------------------------------------------------------------

TOOL_SCRIPT_LOADER::TOOL_SCRIPT_LOADER()
{
    m_pythonDir = FindPythonDir();

    // Load the tool manifest JSON
    std::string manifestPath = m_pythonDir + "/tool_manifest.json";
    m_manifestJson = ReadFile( manifestPath );

    if( m_manifestJson.empty() )
    {
        wxLogWarning( "TOOL_SCRIPT_LOADER: tool_manifest.json not found at %s", manifestPath );
        return;
    }

    wxLogDebug( "TOOL_SCRIPT_LOADER: Loaded manifest from %s (%zu bytes)",
                manifestPath, m_manifestJson.size() );

    // Load shared utility scripts (prepended to tool scripts at runtime)
    m_preamble = ReadFile( m_pythonDir + "/common/preamble.py" );
    m_bbox     = ReadFile( m_pythonDir + "/common/bbox.py" );

    if( m_preamble.empty() )
        wxLogWarning( "TOOL_SCRIPT_LOADER: preamble.py not found — scripts may fail" );

    // Parse the manifest and load each tool's script
    try
    {
        nlohmann::json manifest = nlohmann::json::parse( m_manifestJson );

        if( !manifest.is_array() )
        {
            wxLogWarning( "TOOL_SCRIPT_LOADER: manifest is not a JSON array" );
            return;
        }

        for( const auto& entry : manifest )
        {
            // nlohmann::json::value() throws type_error if key exists with null,
            // so check for null before extracting string fields.
            auto strVal = []( const nlohmann::json& obj, const char* key ) -> std::string
            {
                if( !obj.contains( key ) || obj[key].is_null() )
                    return "";
                return obj[key].get<std::string>();
            };

            std::string name = strVal( entry, "name" );
            std::string app = strVal( entry, "app" );
            std::string scriptRelPath = strVal( entry, "script" );
            bool needsBbox = entry.value( "needs_bbox", false );
            bool readOnly = entry.value( "read_only", false );

            if( name.empty() || scriptRelPath.empty() || app.empty() )
            {
                wxLogDebug( "TOOL_SCRIPT_LOADER: Skipping tool '%s' — missing name/script/app",
                            name );
                continue;
            }

            std::string fullPath = m_pythonDir + "/" + scriptRelPath;
            std::string content = ReadFile( fullPath );

            if( content.empty() )
            {
                wxLogWarning( "TOOL_SCRIPT_LOADER: Skipping tool '%s' — script not found: %s",
                              name, fullPath );
                continue;
            }

            m_tools[name] = { app, std::move( content ), needsBbox, readOnly };
            wxLogDebug( "TOOL_SCRIPT_LOADER: Loaded tool '%s' (app=%s, bbox=%d, readOnly=%d)",
                        name, app, needsBbox, readOnly );
        }

        wxLogInfo( "TOOL_SCRIPT_LOADER: Loaded %zu tools from manifest", m_tools.size() );
    }
    catch( const nlohmann::json::exception& e )
    {
        wxLogError( "TOOL_SCRIPT_LOADER: Failed to parse manifest: %s", e.what() );
    }
}


// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

bool TOOL_SCRIPT_LOADER::HasTool( const std::string& aName ) const
{
    return m_tools.count( aName ) > 0;
}


std::string TOOL_SCRIPT_LOADER::GetApp( const std::string& aName ) const
{
    auto it = m_tools.find( aName );
    return ( it != m_tools.end() ) ? it->second.app : "";
}


bool TOOL_SCRIPT_LOADER::IsReadOnly( const std::string& aName ) const
{
    auto it = m_tools.find( aName );
    return ( it != m_tools.end() ) ? it->second.readOnly : false;
}


const std::string& TOOL_SCRIPT_LOADER::GetManifestJson() const
{
    return m_manifestJson;
}


// ---------------------------------------------------------------------------
// BuildCommand: mirror PYTHON_TOOL_HANDLER::BuildIPCCommand
// ---------------------------------------------------------------------------

std::string TOOL_SCRIPT_LOADER::BuildCommand( const std::string& aName,
                                                const std::string& aArgsJson ) const
{
    auto it = m_tools.find( aName );

    if( it == m_tools.end() )
        return "";

    const ToolEntry& tool = it->second;

    // Escape the JSON string for embedding in a Python single-quoted string literal.
    // json::dump() handles JSON escaping; we additionally escape single-quotes and backslashes.
    std::string escaped;
    escaped.reserve( aArgsJson.size() + 16 );

    for( char c : aArgsJson )
    {
        if( c == '\'' )
            escaped += "\\'";
        else if( c == '\\' )
            escaped += "\\\\";
        else
            escaped += c;
    }

    // Assemble: preamble + optional bbox + tool script
    std::string script = m_preamble + "\n";

    if( tool.needsBbox )
        script += m_bbox + "\n";

    script += tool.script;

    return "run_shell " + tool.app + " "
           + "import json\nTOOL_ARGS = json.loads('" + escaped + "')\n"
           + script;
}


std::string TOOL_SCRIPT_LOADER::BuildPythonCode( const std::string& aName,
                                                  const std::string& aArgsJson ) const
{
    auto it = m_tools.find( aName );

    if( it == m_tools.end() )
        return "";

    const ToolEntry& tool = it->second;

    // Escape the JSON string for embedding in a Python single-quoted string literal.
    std::string escaped;
    escaped.reserve( aArgsJson.size() + 16 );

    for( char c : aArgsJson )
    {
        if( c == '\'' )
            escaped += "\\'";
        else if( c == '\\' )
            escaped += "\\\\";
        else
            escaped += c;
    }

    std::string code = "import json\nTOOL_ARGS = json.loads('" + escaped + "')\n"
                       + m_preamble + "\n";

    if( tool.needsBbox )
        code += m_bbox + "\n";

    code += tool.script;
    return code;
}
