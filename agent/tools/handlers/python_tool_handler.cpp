#include "python_tool_handler.h"

#include <fstream>
#include <sstream>

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>


// ---------------------------------------------------------------------------
// File loading helpers
// ---------------------------------------------------------------------------

std::string PYTHON_TOOL_HANDLER::FindPythonDir()
{
    // 1. Check environment variable (for development and testing)
    const char* envDir = std::getenv( "AGENT_PYTHON_DIR" );

    if( envDir && envDir[0] )
    {
        wxLogInfo( "PYTHON_TOOL_HANDLER: Using AGENT_PYTHON_DIR=%s", envDir );
        return std::string( envDir );
    }

    wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );

#ifdef __WXMAC__
    // 2a. macOS: locate in app bundle
    //     Executable: Zeo.app/Contents/MacOS/kicad
    //     Scripts:    Zeo.app/Contents/SharedSupport/agent/python/
    wxFileName pythonDir( exePath.GetPath(), "" );
    pythonDir.RemoveLastDir();                  // remove MacOS
    pythonDir.AppendDir( "SharedSupport" );
    pythonDir.AppendDir( "agent" );
    pythonDir.AppendDir( "python" );
#else
    // 2b. Linux: scripts installed to ${prefix}/share/kicad/agent/python/
    //     Executable: /usr/bin/kicad
    //     Scripts:    /usr/share/kicad/agent/python/
    wxFileName pythonDir( exePath.GetPath(), "" );
    pythonDir.RemoveLastDir();                  // remove bin
    pythonDir.AppendDir( "share" );
    pythonDir.AppendDir( "kicad" );
    pythonDir.AppendDir( "agent" );
    pythonDir.AppendDir( "python" );
#endif

    std::string result = pythonDir.GetPath().ToStdString();
    wxLogInfo( "PYTHON_TOOL_HANDLER: Python scripts dir: %s", result );
    return result;
}


std::string PYTHON_TOOL_HANDLER::ReadFile( const std::string& aPath )
{
    std::ifstream file( aPath );

    if( !file.is_open() )
    {
        wxLogWarning( "PYTHON_TOOL_HANDLER: Failed to read script: %s", aPath );
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}


// ---------------------------------------------------------------------------
// IPC command builder (replaces python_utils.h)
// ---------------------------------------------------------------------------

std::string PYTHON_TOOL_HANDLER::BuildIPCCommand( const std::string& aApp,
                                                    const nlohmann::json& aInput,
                                                    const std::string& aScript )
{
    // Build TOOL_ARGS preamble: json::dump() handles JSON escaping;
    // we additionally escape single-quotes and backslashes for the
    // Python single-quoted string literal.
    std::string jsonStr = aInput.dump();
    std::string escaped;
    escaped.reserve( jsonStr.size() + 16 );

    for( char c : jsonStr )
    {
        if( c == '\'' )
            escaped += "\\'";
        else if( c == '\\' )
            escaped += "\\\\";
        else
            escaped += c;
    }

    return "run_shell " + aApp + " "
           + "import json\nTOOL_ARGS = json.loads('" + escaped + "')\n"
           + aScript;
}


// ---------------------------------------------------------------------------
// Description helpers for complex tools
// ---------------------------------------------------------------------------

static std::string DescribeSchFindSymbol( const nlohmann::json& a )
{
    std::string libId;

    if( a.is_object() && a.contains( "lib_id" ) )
        libId = a.value( "lib_id", "" );

    if( libId.empty() )
        return "Getting library symbol info";

    bool hasWildcard = libId.find( '*' ) != std::string::npos
                       || libId.find( '?' ) != std::string::npos;
    bool hasRegex = libId.find_first_of( "[]{}()|\\+^$" ) != std::string::npos;

    if( hasRegex )
        return "Searching symbols matching regex: " + libId;
    else if( hasWildcard )
        return "Searching symbols matching: " + libId;

    return "Getting symbol info: " + libId;
}


static std::string DescribeSchSimulation( const nlohmann::json& a )
{
    std::string command = a.value( "command", "" );
    bool        save = a.value( "save_to_schematic", false );

    std::string simType;

    if( !command.empty() )
    {
        size_t sp = command.find( ' ' );
        simType = ( sp != std::string::npos ) ? command.substr( 0, sp ) : command;
    }

    std::string desc = "Running simulation";

    if( !simType.empty() )
        desc += ": " + simType;

    if( save )
        desc += " (saving to schematic)";

    return desc;
}


static std::string DescribeSchConnectNet( const nlohmann::json& a )
{
    if( a.contains( "pins" ) && a["pins"].is_array() )
    {
        size_t count = a["pins"].size();

        if( count <= 3 )
        {
            std::string pinList;

            for( size_t i = 0; i < count; ++i )
            {
                if( i > 0 )
                    pinList += ", ";

                pinList += a["pins"][i].get<std::string>();
            }

            return "Connecting " + pinList;
        }

        return "Connecting " + std::to_string( count ) + " pins";
    }

    return "Connecting pins";
}


static std::string DescribeSchLabelPins( const nlohmann::json& a )
{
    std::string ref = a.value( "ref", "?" );
    int         count = 0;

    if( a.contains( "labels" ) && a["labels"].is_object() )
        count = static_cast<int>( a["labels"].size() );

    if( count > 0 )
        return "Labeling " + std::to_string( count ) + " pins on " + ref;

    return "Labeling pins on " + ref;
}


static std::string DescribeSchAdd( const nlohmann::json& a )
{
    if( a.contains( "elements" ) && a["elements"].is_array() )
    {
        size_t count = a["elements"].size();

        if( count == 1 )
        {
            auto        elem = a["elements"][0];
            std::string elementType = elem.value( "element_type", "element" );
            std::string libId = elem.value( "lib_id", "" );

            if( elementType == "symbol" && !libId.empty() )
            {
                size_t      colonPos = libId.find( ':' );
                std::string symbolName =
                        ( colonPos != std::string::npos ) ? libId.substr( colonPos + 1 ) : libId;
                return "Adding " + symbolName;
            }

            return "Adding " + elementType;
        }

        return "Adding " + std::to_string( count ) + " elements";
    }

    return "Adding elements";
}


static std::string DescribeSchUpdate( const nlohmann::json& a )
{
    if( a.contains( "updates" ) && a["updates"].is_array() )
    {
        size_t count = a["updates"].size();

        if( count == 1 )
        {
            std::string target = a["updates"][0].value( "target", "" );

            if( !target.empty() )
                return "Updating " + target;
        }

        return "Updating " + std::to_string( count ) + " elements";
    }

    return "Updating elements";
}


static std::string DescribeSchDelete( const nlohmann::json& a )
{
    bool        chainDelete = a.value( "chain_delete", false );
    std::string prefix = chainDelete ? "Chain-deleting net for " : "Deleting ";

    if( a.contains( "targets" ) && a["targets"].is_array() )
    {
        size_t count = a["targets"].size();

        if( count == 1 )
        {
            const auto& t = a["targets"][0];

            if( t.is_string() )
                return prefix + t.get<std::string>();
            else if( t.is_object() )
                return prefix + t.value( "type", std::string( "element" ) )
                       + ( t.contains( "text" ) ? " '" + t["text"].get<std::string>() + "'"
                                                : "" );
        }

        return prefix + std::to_string( count ) + " elements";
    }

    return prefix + "elements";
}


static std::string DescribePcbAdd( const nlohmann::json& a )
{
    if( a.contains( "elements" ) && a["elements"].is_array() )
    {
        size_t count = a["elements"].size();

        if( count == 1 )
        {
            std::string elemType = a["elements"][0].value( "element_type", "element" );
            return "Adding " + elemType;
        }

        return "Adding " + std::to_string( count ) + " elements";
    }

    return "Adding elements";
}


static std::string DescribePcbUpdate( const nlohmann::json& a )
{
    if( a.contains( "updates" ) && a["updates"].is_array() )
    {
        size_t count = a["updates"].size();

        if( count == 1 )
        {
            std::string target = a["updates"][0].value( "target", "" );
            return "Updating " + ( target.empty() ? "element" : target );
        }

        return "Updating " + std::to_string( count ) + " elements";
    }

    return "Updating elements";
}


static std::string DescribePcbDelete( const nlohmann::json& a )
{
    if( a.contains( "targets" ) && a["targets"].is_array() )
    {
        size_t count = a["targets"].size();

        if( count == 1 )
            return "Deleting " + a["targets"][0].get<std::string>();

        return "Deleting " + std::to_string( count ) + " elements";
    }

    return "Deleting elements";
}



static std::string DescribePcbSetup( const nlohmann::json& a )
{
    std::string action = a.value( "action", "get" );

    if( action == "get" )
        return "Reading PCB board settings";

    std::vector<std::string> sections;

    if( a.contains( "physical_stackup" ) )    sections.push_back( "physical stackup" );
    if( a.contains( "board_finish" ) )        sections.push_back( "board finish" );
    if( a.contains( "solder_mask_paste" ) )   sections.push_back( "solder mask/paste" );
    if( a.contains( "zone_hatch_offsets" ) )  sections.push_back( "zone hatch offsets" );
    if( a.contains( "board_editor_layers" ) ) sections.push_back( "board editor layers" );
    if( a.contains( "design_rules" ) )        sections.push_back( "design rules" );
    if( a.contains( "text_and_graphics" ) )   sections.push_back( "text/graphics defaults" );
    if( a.contains( "dimension_defaults" ) )  sections.push_back( "dimension defaults" );
    if( a.contains( "zone_defaults" ) )       sections.push_back( "zone defaults" );
    if( a.contains( "predefined_sizes" ) )    sections.push_back( "predefined sizes" );
    if( a.contains( "teardrops" ) )           sections.push_back( "teardrops" );
    if( a.contains( "length_tuning_patterns" ) ) sections.push_back( "length tuning patterns" );
    if( a.contains( "tuning_profiles" ) )     sections.push_back( "tuning profiles" );
    if( a.contains( "component_classes" ) )   sections.push_back( "component classes" );
    if( a.contains( "custom_rules" ) )        sections.push_back( "custom DRC rules" );
    if( a.contains( "grid" ) )                sections.push_back( "grid" );
    if( a.contains( "drc_severities" ) )      sections.push_back( "DRC severities" );
    if( a.contains( "net_classes" ) )         sections.push_back( "net classes" );
    if( a.contains( "text_variables" ) )      sections.push_back( "text variables" );
    if( a.contains( "title_block" ) )         sections.push_back( "title block" );
    if( a.contains( "origins" ) )             sections.push_back( "origins" );

    if( sections.empty() )
        return "Updating PCB settings";

    std::string desc = "Updating ";

    for( size_t i = 0; i < sections.size(); ++i )
    {
        if( i > 0 )
            desc += ( i == sections.size() - 1 ) ? " and " : ", ";

        desc += sections[i];
    }

    return desc;
}


// ---------------------------------------------------------------------------
// Registration & script loading
// ---------------------------------------------------------------------------

void PYTHON_TOOL_HANDLER::Register( const std::string& aToolName, const std::string& aApp,
                                     const std::string& aScriptPath, DescribeFn aDescribe )
{
    std::string fullPath = m_pythonDir + "/" + aScriptPath;
    std::string content = ReadFile( fullPath );

    if( content.empty() )
    {
        wxLogWarning( "PYTHON_TOOL_HANDLER: Skipping tool '%s' — script not found: %s",
                      aToolName, fullPath );
        return;
    }

    m_tools[aToolName] = { aApp, std::move( content ), std::move( aDescribe ) };
}


PYTHON_TOOL_HANDLER::PYTHON_TOOL_HANDLER()
{
    m_pythonDir = FindPythonDir();

    // Load shared utility scripts (prepended to tool scripts at runtime)
    m_preamble = ReadFile( m_pythonDir + "/common/preamble.py" );
    m_bbox     = ReadFile( m_pythonDir + "/common/bbox.py" );

    if( m_preamble.empty() )
        wxLogWarning( "PYTHON_TOOL_HANDLER: preamble.py not found — scripts may fail" );

    // --- Schematic tools ---

    Register( "sch_run_erc", "sch", "schematic/sch_run_erc.py", []( const nlohmann::json& a ) {
        std::string format = a.value( "output_format", "summary" );
        if( format == "detailed" )  return std::string( "Running detailed ERC analysis" );
        if( format == "by_type" )   return std::string( "Running ERC (grouped by type)" );
        return std::string( "Running ERC check" );
    } );

    Register( "sch_annotate", "sch", "schematic/sch_annotate.py", []( const nlohmann::json& a ) {
        std::string scope = a.value( "scope", "unannotated_only" );
        if( scope == "all" )  return std::string( "Annotating all symbols" );
        return std::string( "Annotating symbols" );
    } );

    Register( "sch_run_simulation", "sch", "schematic/sch_run_simulation.py", DescribeSchSimulation );
    Register( "sch_find_symbol",    "sch", "schematic/sch_find_symbol.py",    DescribeSchFindSymbol );

    Register( "sch_get_summary", "sch", "schematic/sch_get_summary.py", []( const nlohmann::json& ) {
        return std::string( "Getting schematic summary" );
    } );

    Register( "sch_inspect", "sch", "schematic/sch_inspect.py", []( const nlohmann::json& a ) {
        return "Inspecting schematic " + a.value( "section", "all" );
    } );

    Register( "sch_get_pins", "sch", "schematic/sch_get_pins.py", []( const nlohmann::json& a ) {
        std::string ref = a.value( "ref", "" );
        return "Getting pins for " + ( ref.empty() ? std::string( "symbol" ) : ref );
    } );

    Register( "sch_symbols", "sch", "schematic/sch_symbols.py", []( const nlohmann::json& a ) {
        std::string filter = a.value( "filter", "*" );
        if( filter == "*" || filter.empty() )
            return std::string( "Querying all symbols" );
        return "Querying symbols: " + filter;
    } );

    Register( "sch_get_nets", "sch", "schematic/sch_get_nets.py", []( const nlohmann::json& a ) {
        std::string f = a.value( "filter", "" );
        return f.empty() ? std::string( "Getting schematic nets" ) : "Getting nets matching " + f;
    } );

    Register( "sch_connect_net", "sch", "schematic/sch_connect_net.py", DescribeSchConnectNet );
    Register( "sch_label_pins",  "sch", "schematic/sch_label_pins.py",  DescribeSchLabelPins );

    Register( "sch_setup", "sch", "schematic/sch_setup.py", []( const nlohmann::json& a ) {
        std::string action = a.value( "action", "get" );
        if( action == "get" )  return std::string( "Reading schematic setup settings" );
        return std::string( "Updating schematic setup settings" );
    } );

    Register( "sch_add",          "sch", "schematic/sch_add.py",          DescribeSchAdd );
    Register( "sch_update",       "sch", "schematic/sch_update.py",       DescribeSchUpdate );
    Register( "sch_delete",       "sch", "schematic/sch_delete.py",       DescribeSchDelete );

    Register( "sch_switch_sheet", "sch", "schematic/sch_switch_sheet.py", []( const nlohmann::json& a ) {
        std::string sheetPath = a.value( "sheet_path", "" );
        if( !sheetPath.empty() )  return "Switching to sheet: " + sheetPath;
        return std::string( "Listing available sheets" );
    } );

    Register( "sch_add_sheet", "sch", "schematic/sch_add_sheet.py", []( const nlohmann::json& a ) {
        return "Adding sheet: " + a.value( "sheet_name", "sheet" );
    } );

    Register( "sch_place_companions", "sch", "schematic/sch_place_companions.py",
              []( const nlohmann::json& a ) {
                  std::string icRef = a.value( "ic_ref", "?" );
                  int         count = 0;
                  if( a.contains( "companions" ) && a["companions"].is_array() )
                      count = static_cast<int>( a["companions"].size() );
                  if( count > 0 )
                      return "Placing " + std::to_string( count ) + " companions for " + icRef;
                  return "Placing companions for " + icRef;
              } );

    // --- PCB tools ---

    Register( "pcb_get_summary", "pcb", "pcb/pcb_get_summary.py", []( const nlohmann::json& ) {
        return std::string( "Getting PCB summary" );
    } );

    Register( "pcb_inspect", "pcb", "pcb/pcb_inspect.py", []( const nlohmann::json& a ) {
        return "Inspecting PCB " + a.value( "section", "all" );
    } );

    Register( "pcb_run_drc", "pcb", "pcb/pcb_run_drc.py", []( const nlohmann::json& ) {
        return std::string( "Running DRC check" );
    } );

    Register( "pcb_set_outline", "pcb", "pcb/pcb_set_outline.py", []( const nlohmann::json& a ) {
        return "Setting board outline (" + a.value( "shape", "rectangle" ) + ")";
    } );

    Register( "pcb_sync_schematic", "pcb", "pcb/pcb_sync_schematic.py", []( const nlohmann::json& ) {
        return std::string( "Updating PCB from schematic" );
    } );

    Register( "pcb_place", "pcb", "pcb/pcb_place.py", []( const nlohmann::json& a ) {
        if( a.contains( "placements" ) && a["placements"].is_array() )
            return "Placing " + std::to_string( a["placements"].size() ) + " footprint(s)";
        return std::string( "Placing footprints" );
    } );

    Register( "pcb_place_companions", "pcb", "pcb/pcb_place_companions.py",
              []( const nlohmann::json& a ) {
                  std::string icRef = a.value( "ic_ref", "?" );
                  int count = 0;
                  if( a.contains( "companions" ) && a["companions"].is_array() )
                      count = static_cast<int>( a["companions"].size() );
                  return "Placing " + std::to_string( count ) + " companion(s) near " + icRef;
              } );

    Register( "pcb_add",    "pcb", "pcb/pcb_add.py",    DescribePcbAdd );
    Register( "pcb_update", "pcb", "pcb/pcb_update.py",  DescribePcbUpdate );
    Register( "pcb_delete", "pcb", "pcb/pcb_delete.py",  DescribePcbDelete );

    Register( "pcb_get_pads", "pcb", "pcb/pcb_get_pads.py", []( const nlohmann::json& a ) {
        std::string ref = a.value( "ref", "" );
        return "Getting pads for " + ( ref.empty() ? std::string( "footprint" ) : ref );
    } );

    Register( "pcb_get_footprint", "pcb", "pcb/pcb_get_footprint.py", []( const nlohmann::json& a ) {
        std::string ref = a.value( "ref", "" );
        return "Getting footprint " + ( ref.empty() ? std::string( "info" ) : ref );
    } );

    Register( "pcb_get_nets", "pcb", "pcb/pcb_get_nets.py", []( const nlohmann::json& ) {
        return std::string( "Getting net list" );
    } );

    Register( "pcb_export", "pcb", "pcb/pcb_export.py", []( const nlohmann::json& a ) {
        return "Exporting " + a.value( "format", "gerber" );
    } );

    Register( "pcb_setup", "pcb", "pcb/pcb_setup.py", DescribePcbSetup );

    wxLogInfo( "PYTHON_TOOL_HANDLER: Loaded %zu tools from %s",
               m_tools.size(), m_pythonDir );
}


// ---------------------------------------------------------------------------
// TOOL_HANDLER interface
// ---------------------------------------------------------------------------

std::vector<std::string> PYTHON_TOOL_HANDLER::GetToolNames() const
{
    std::vector<std::string> names;
    names.reserve( m_tools.size() );

    for( const auto& [name, entry] : m_tools )
        names.push_back( name );

    return names;
}


std::string PYTHON_TOOL_HANDLER::Execute( const std::string& aToolName,
                                           const nlohmann::json& /*aInput*/ )
{
    return "Error: " + aToolName + " requires IPC execution. Use GetIPCCommand() instead.";
}


std::string PYTHON_TOOL_HANDLER::GetDescription( const std::string& aToolName,
                                                   const nlohmann::json& aInput ) const
{
    auto it = m_tools.find( aToolName );

    if( it != m_tools.end() && it->second.describe )
        return it->second.describe( aInput );

    return "Executing " + aToolName;
}


bool PYTHON_TOOL_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return m_tools.count( aToolName ) > 0;
}


std::string PYTHON_TOOL_HANDLER::GetIPCCommand( const std::string& aToolName,
                                                  const nlohmann::json& aInput ) const
{
    auto it = m_tools.find( aToolName );

    if( it == m_tools.end() )
        return "";

    // Assemble: preamble + optional bbox + tool script
    std::string script = m_preamble + "\n";

    // sch_add, sch_update, and pcb placement tools need bounding-box utilities
    if( aToolName == "sch_add" || aToolName == "sch_update"
        || aToolName == "pcb_place" || aToolName == "pcb_place_companions" )
        script += m_bbox + "\n";

    script += it->second.script;

    return BuildIPCCommand( it->second.app, aInput, script );
}
