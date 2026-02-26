#include "tool_registry.h"
#include "tool_handler.h"
#include "handlers/python_tool_handler.h"
#include "handlers/pcb_autoroute_handler.h"
#include "handlers/screenshot_handler.h"
#include "handlers/check_status_handler.h"
#include "handlers/create_project_handler.h"
#include "handlers/datasheet_handler.h"
#include "handlers/symbol_generator.h"
#include "handlers/component_search_handler.h"
#include "handlers/footprint_generator.h"
#include "handlers/netclass_generator.h"

#include <frame_type.h>
#include <wx/log.h>


TOOL_REGISTRY& TOOL_REGISTRY::Instance()
{
    static TOOL_REGISTRY instance;
    return instance;
}


void TOOL_REGISTRY::Register( std::unique_ptr<TOOL_HANDLER> aHandler )
{
    TOOL_HANDLER* raw = aHandler.get();

    for( const auto& name : raw->GetToolNames() )
        m_toolMap[name] = raw;

    m_handlers.push_back( std::move( aHandler ) );
}


TOOL_REGISTRY::TOOL_REGISTRY()
{
    Register( std::make_unique<PYTHON_TOOL_HANDLER>() );
    Register( std::make_unique<PCB_AUTOROUTE_HANDLER>() );
    Register( std::make_unique<SCREENSHOT_HANDLER>() );
    Register( std::make_unique<CHECK_STATUS_HANDLER>() );
    Register( std::make_unique<CREATE_PROJECT_HANDLER>() );
    Register( std::make_unique<DATASHEET_HANDLER>() );
    Register( std::make_unique<SYMBOL_GENERATOR>() );
    Register( std::make_unique<COMPONENT_SEARCH_HANDLER>() );
    Register( std::make_unique<FOOTPRINT_GENERATOR>() );
    Register( std::make_unique<NETCLASS_GENERATOR>() );

    wxLogInfo( "TOOL_REGISTRY: %zu tools registered", m_toolMap.size() );
}


TOOL_HANDLER* TOOL_REGISTRY::FindHandler( const std::string& aToolName ) const
{
    auto it = m_toolMap.find( aToolName );
    return ( it != m_toolMap.end() ) ? it->second : nullptr;
}


bool TOOL_REGISTRY::HasHandler( const std::string& aToolName ) const
{
    return m_toolMap.count( aToolName ) > 0;
}


std::string TOOL_REGISTRY::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    TOOL_HANDLER* handler = FindHandler( aToolName );

    if( handler )
        return handler->Execute( aToolName, aInput );

    return "Error: No handler found for tool '" + aToolName + "'";
}


std::string TOOL_REGISTRY::GetDescription( const std::string& aToolName,
                                            const nlohmann::json& aInput ) const
{
    TOOL_HANDLER* handler = FindHandler( aToolName );

    if( handler )
        return handler->GetDescription( aToolName, aInput );

    // Fallback descriptions for non-registered tools (run_terminal, open_editor, etc.)
    if( aToolName == "run_terminal" )
    {
        std::string cmd = aInput.value( "command", "" );

        if( cmd.length() > 50 )
            cmd = cmd.substr( 0, 47 ) + "...";

        return "Running: " + cmd;
    }
    else if( aToolName == "open_editor" )
    {
        std::string editorType = aInput.value( "editor_type", "" );
        std::string label = ( editorType == "sch" ) ? "Schematic" : "PCB";
        return "Open " + label + " Editor";
    }
    return "Executing " + aToolName;
}


bool TOOL_REGISTRY::RequiresIPC( const std::string& aToolName ) const
{
    TOOL_HANDLER* handler = FindHandler( aToolName );
    return handler ? handler->RequiresIPC( aToolName ) : false;
}


std::string TOOL_REGISTRY::GetIPCCommand( const std::string& aToolName,
                                           const nlohmann::json& aInput ) const
{
    TOOL_HANDLER* handler = FindHandler( aToolName );
    return handler ? handler->GetIPCCommand( aToolName, aInput ) : "";
}


bool TOOL_REGISTRY::IsAsync( const std::string& aToolName ) const
{
    TOOL_HANDLER* handler = FindHandler( aToolName );
    return handler ? handler->IsAsync( aToolName ) : false;
}


void TOOL_REGISTRY::ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                                   const std::string& aToolUseId, wxEvtHandler* aEventHandler )
{
    TOOL_HANDLER* handler = FindHandler( aToolName );

    if( handler )
        handler->ExecuteAsync( aToolName, aInput, aToolUseId, aEventHandler );
}


std::string TOOL_REGISTRY::ExecuteToolSync( const std::string& aToolName,
                                             const nlohmann::json& aInput )
{
    wxLogInfo( "TOOL_REGISTRY::ExecuteToolSync called for tool: %s", aToolName.c_str() );

    // Registered tools (sch_*, pcb_*, screenshot, etc.)
    if( HasHandler( aToolName ) )
    {
        if( RequiresIPC( aToolName ) )
        {
            std::string command = GetIPCCommand( aToolName, aInput );
            return m_sendRequestFn( FRAME_TERMINAL, command );
        }

        return Execute( aToolName, aInput );
    }

    // Built-in run_terminal tool
    if( aToolName == "run_terminal" )
    {
        std::string command = aInput.value( "command", "" );

        if( command.empty() )
            return "Error: run_terminal requires 'command' parameter";

        return m_sendRequestFn( FRAME_TERMINAL, "run_terminal " + command );
    }

    return "Error: Unknown tool '" + aToolName + "'";
}
