#include "tool_registry.h"
#include "tool_handler.h"
#include "schematic/sch_tool_handler.h"
#include "schematic/sch_erc_handler.h"
#include "schematic/sch_lib_symbol_handler.h"
#include "schematic/sch_crud_handler.h"
#include "schematic/sch_connect_net_handler.h"
#include "schematic/sch_util_handler.h"
#include "schematic/sch_sim_handler.h"
#include "schematic/sch_setup_handler.h"
#include "pcb/pcb_tool_handler.h"
#include "pcb/pcb_crud_handler.h"
#include "pcb/pcb_autoroute_handler.h"
#include "pcb/pcb_setup_handler.h"
#include "screenshot/screenshot_handler.h"


TOOL_REGISTRY& TOOL_REGISTRY::Instance()
{
    static TOOL_REGISTRY instance;
    return instance;
}


TOOL_REGISTRY::TOOL_REGISTRY()
{
    // Register schematic tool handlers
    m_handlers.push_back( std::make_unique<SCH_TOOL_HANDLER>() );
    m_handlers.push_back( std::make_unique<SCH_ERC_HANDLER>() );
    m_handlers.push_back( std::make_unique<SCH_LIB_SYMBOL_HANDLER>() );
    m_handlers.push_back( std::make_unique<SCH_CRUD_HANDLER>() );
    m_handlers.push_back( std::make_unique<SCH_CONNECT_NET_HANDLER>() );
    m_handlers.push_back( std::make_unique<SCH_UTIL_HANDLER>() );
    m_handlers.push_back( std::make_unique<SCH_SIM_HANDLER>() );
    m_handlers.push_back( std::make_unique<SCH_SETUP_HANDLER>() );

    // Register PCB tool handlers (CRUD handler first since it has actual implementations)
    m_handlers.push_back( std::make_unique<PCB_CRUD_HANDLER>() );
    m_handlers.push_back( std::make_unique<PCB_TOOL_HANDLER>() );
    m_handlers.push_back( std::make_unique<PCB_AUTOROUTE_HANDLER>() );
    m_handlers.push_back( std::make_unique<PCB_SETUP_HANDLER>() );

    // Register screenshot handler
    m_handlers.push_back( std::make_unique<SCREENSHOT_HANDLER>() );
}


bool TOOL_REGISTRY::HasHandler( const std::string& aToolName ) const
{
    for( const auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return true;
    }
    return false;
}


std::string TOOL_REGISTRY::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    for( auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return handler->Execute( aToolName, aInput );
    }
    return "Error: No handler found for tool '" + aToolName + "'";
}


std::string TOOL_REGISTRY::GetDescription( const std::string& aToolName,
                                            const nlohmann::json& aInput ) const
{
    for( const auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return handler->GetDescription( aToolName, aInput );
    }
    return "Unknown tool: " + aToolName;
}


void TOOL_REGISTRY::SetProjectPath( const std::string& aPath )
{
    for( auto& handler : m_handlers )
    {
        handler->SetProjectPath( aPath );
    }
}


bool TOOL_REGISTRY::RequiresIPC( const std::string& aToolName ) const
{
    for( const auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return handler->RequiresIPC( aToolName );
    }
    return false;
}


std::string TOOL_REGISTRY::GetIPCCommand( const std::string& aToolName,
                                           const nlohmann::json& aInput ) const
{
    for( const auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return handler->GetIPCCommand( aToolName, aInput );
    }
    return "";
}


void TOOL_REGISTRY::SetSchematicEditorOpen( bool aOpen )
{
    for( auto& handler : m_handlers )
    {
        handler->SetSchematicEditorOpen( aOpen );
    }
}


void TOOL_REGISTRY::SetPcbEditorOpen( bool aOpen )
{
    for( auto& handler : m_handlers )
    {
        handler->SetPcbEditorOpen( aOpen );
    }
}


bool TOOL_REGISTRY::IsSchematicEditorOpen() const
{
    if( !m_handlers.empty() )
        return m_handlers.front()->IsSchematicEditorOpen();

    return false;
}


bool TOOL_REGISTRY::IsPcbEditorOpen() const
{
    if( !m_handlers.empty() )
        return m_handlers.front()->IsPcbEditorOpen();

    return false;
}


void TOOL_REGISTRY::SetSendRequestFn( std::function<std::string( int, const std::string& )> aFn )
{
    for( auto& handler : m_handlers )
    {
        handler->SetSendRequestFn( aFn );
    }
}


bool TOOL_REGISTRY::IsAsync( const std::string& aToolName ) const
{
    for( const auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return handler->IsAsync( aToolName );
    }
    return false;
}


void TOOL_REGISTRY::ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                                   const std::string& aToolUseId, wxEvtHandler* aEventHandler )
{
    for( auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
        {
            handler->ExecuteAsync( aToolName, aInput, aToolUseId, aEventHandler );
            return;
        }
    }
}
