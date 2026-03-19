#include "netclass_generator.h"
#include "tools/tool_registry.h"
#include <zeo/agent_auth.h>
#include <zeo/zeo_constants.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>
#include <wx/app.h>
#include <thread>

using json = nlohmann::json;


// ---------------------------------------------------------------------------
// Execute (synchronous — used by MCP tool execution)
// ---------------------------------------------------------------------------
std::string NETCLASS_GENERATOR::Execute( const std::string& aToolName,
                                          const nlohmann::json& aInput )
{
    const auto& reg = TOOL_REGISTRY::Instance();

    if( !reg.IsPcbEditorOpen() )
        return R"({"error": "PCB editor must be open to generate net classes."})";

    AGENT_AUTH* auth = reg.GetAuth();

    if( !auth || auth->GetAccessToken().empty() )
        return R"({"error": "Please sign in to Zeo to use AI features."})";

    bool apply = aInput.value( "apply", true );
    return DoGenerate( apply );
}


std::string NETCLASS_GENERATOR::GetDescription( const std::string& aToolName,
                                                  const nlohmann::json& aInput ) const
{
    return "Generating net classes from design analysis";
}


// ---------------------------------------------------------------------------
// ExecuteAsync
// ---------------------------------------------------------------------------
void NETCLASS_GENERATOR::ExecuteAsync( const std::string& aToolName,
                                        const nlohmann::json& aInput,
                                        const std::string& aToolUseId,
                                        wxEvtHandler* aEventHandler )
{
    const auto& reg = TOOL_REGISTRY::Instance();

    if( !reg.IsPcbEditorOpen() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "PCB editor must be open to generate net classes."})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    AGENT_AUTH* auth = reg.GetAuth();
    if( !auth || auth->GetAccessToken().empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "Please sign in to Zeo to use AI features."})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    bool apply = aInput.value( "apply", true );

    wxLogInfo( "NETCLASS_GENERATOR: Starting async (apply=%s)", apply ? "true" : "false" );

    // ── Main-thread IPC: gather net names and setup data ──────────────
    // These call ExecuteToolSync → SendRequest → ExpressMail which require
    // the main thread.  Do them here before spawning the background thread.
    wxLogInfo( "NETCLASS_GENERATOR: Gathering net names (main thread)..." );
    json netData = GatherNetNames();

    if( netData.contains( "error" ) )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = netData.dump();
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    json netNames = json::array();
    if( netData.contains( "nets" ) && netData["nets"].is_array() )
    {
        for( const auto& net : netData["nets"] )
        {
            if( net.contains( "name" ) && !net["name"].get<std::string>().empty() )
                netNames.push_back( net["name"].get<std::string>() );
        }
    }

    if( netNames.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "No nets found in the PCB. Add components and create a netlist first."})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    wxLogInfo( "NETCLASS_GENERATOR: Found %zu nets", netNames.size() );

    wxLogInfo( "NETCLASS_GENERATOR: Gathering setup data (main thread)..." );
    json setupData = GatherSetupData();

    if( setupData.contains( "error" ) )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = setupData.dump();
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    // ── Background thread: LLM call (the slow part) ──────────────────
    // Only the HTTP request to the LLM endpoint runs on the background thread.
    // The apply step uses IPC and must run on the main thread via CallAfter.
    std::thread( [=, this]()
    {
        // Build request from pre-gathered data and call LLM endpoint
        std::string llmResponse = BuildAndCallLLM( netNames, setupData );

        // Post back to main thread for IPC-dependent apply + result posting
        wxTheApp->CallAfter( [=, this]()
        {
            std::string resultStr = FinishGenerate( llmResponse, netNames, apply );

            bool success = false;
            try
            {
                auto resultJson = json::parse( resultStr );
                success = !resultJson.contains( "error" );
            }
            catch( ... ) {}

            ToolExecutionResult result;
            result.tool_use_id = aToolUseId;
            result.tool_name = "generate_net_classes";
            result.result = resultStr;
            result.success = success;
            PostToolResult( aEventHandler, result );
        } );
    } ).detach();
}


// ---------------------------------------------------------------------------
// DoGenerate — main workflow
// ---------------------------------------------------------------------------
std::string NETCLASS_GENERATOR::DoGenerate( bool aApply )
{
    // Step 1: Gather net names
    wxLogInfo( "NETCLASS_GENERATOR: Gathering net names..." );
    json netData = GatherNetNames();

    if( netData.contains( "error" ) )
    {
        wxLogInfo( "NETCLASS_GENERATOR: Failed to gather net names: %s",
                   netData["error"].get<std::string>().c_str() );
        return netData.dump();
    }

    json netNames = json::array();
    if( netData.contains( "nets" ) && netData["nets"].is_array() )
    {
        for( const auto& net : netData["nets"] )
        {
            if( net.contains( "name" ) && !net["name"].get<std::string>().empty() )
                netNames.push_back( net["name"].get<std::string>() );
        }
    }

    if( netNames.empty() )
    {
        json err;
        err["error"] = "No nets found in the PCB. Add components and create a netlist first.";
        return err.dump();
    }

    wxLogInfo( "NETCLASS_GENERATOR: Found %zu nets", netNames.size() );

    // Step 2: Gather existing setup data (net classes, assignments, components)
    wxLogInfo( "NETCLASS_GENERATOR: Gathering setup data..." );
    json setupData = GatherSetupData();

    if( setupData.contains( "error" ) )
    {
        wxLogInfo( "NETCLASS_GENERATOR: Failed to gather setup data: %s",
                   setupData["error"].get<std::string>().c_str() );
        return setupData.dump();
    }

    // Step 3: Build existing net classes array
    json existingNetclasses = json::array();
    if( setupData.contains( "net_classes" ) && setupData["net_classes"].is_array() )
    {
        for( const auto& nc : setupData["net_classes"] )
        {
            json ncObj;
            ncObj["name"] = nc.value( "name", "" );

            // Include PCB-specific fields
            auto addIfPresent = [&]( const char* key )
            {
                if( nc.contains( key ) && !nc[key].is_null() )
                    ncObj[key] = nc[key];
            };

            addIfPresent( "clearance" );
            addIfPresent( "track_width" );
            addIfPresent( "via_diameter" );
            addIfPresent( "via_drill" );
            addIfPresent( "diff_pair_width" );
            addIfPresent( "diff_pair_gap" );

            existingNetclasses.push_back( ncObj );
        }
    }

    // Step 4: Build existing assignments array
    json existingAssignments = json::array();
    if( setupData.contains( "net_class_assignments" ) && setupData["net_class_assignments"].is_array() )
    {
        for( const auto& assign : setupData["net_class_assignments"] )
        {
            json a;
            a["pattern"] = assign.value( "pattern", "" );
            a["netclass"] = assign.value( "netclass", "" );
            existingAssignments.push_back( a );
        }
    }

    // Step 5: Build design context with component data
    json designContext = BuildDesignContext( setupData );

    // Step 6: Build request body
    json requestBody;
    requestBody["net_names"] = netNames;
    requestBody["existing_netclasses"] = existingNetclasses;
    requestBody["existing_assignments"] = existingAssignments;
    requestBody["design_context"] = designContext.dump();
    requestBody["editor_type"] = "pcb";

    wxLogInfo( "NETCLASS_GENERATOR: Calling /api/llm/netclasses with %zu nets, %zu existing classes",
               netNames.size(), existingNetclasses.size() );

    // Step 7: Call endpoint
    std::string response = CallNetclassEndpoint( requestBody );

    json responseJson;
    try
    {
        responseJson = json::parse( response );
    }
    catch( const std::exception& e )
    {
        json err;
        err["error"] = std::string( "Failed to parse server response: " ) + e.what();
        return err.dump();
    }

    if( responseJson.contains( "error" ) )
    {
        wxLogInfo( "NETCLASS_GENERATOR: Server error: %s",
                   responseJson["error"].get<std::string>().c_str() );
        return responseJson.dump();
    }

    wxLogInfo( "NETCLASS_GENERATOR: Got %zu net classes, %zu assignments",
               responseJson.contains( "netclasses" ) ? responseJson["netclasses"].size() : 0,
               responseJson.contains( "assignments" ) ? responseJson["assignments"].size() : 0 );

    // Step 8: Apply if requested
    bool classesApplied = false;
    bool assignmentsApplied = false;

    if( aApply )
    {
        wxLogInfo( "NETCLASS_GENERATOR: Applying net classes..." );
        std::string applyResult = ApplyNetclasses( responseJson );

        json applyJson;
        try
        {
            applyJson = json::parse( applyResult );
        }
        catch( ... ) {}

        if( applyJson.contains( "error" ) )
        {
            json err;
            err["error"] = "Generated net classes but failed to apply: "
                           + applyJson.value( "error", "unknown" );
            err["generated"] = responseJson;
            return err.dump();
        }

        classesApplied = true;

        // Apply assignments via project-level API (through pcb_setup)
        if( responseJson.contains( "assignments" ) && responseJson["assignments"].is_array()
            && !responseJson["assignments"].empty() )
        {
            assignmentsApplied = ApplyAssignments( responseJson["assignments"] );

            if( !assignmentsApplied )
            {
                wxLogInfo( "NETCLASS_GENERATOR: Failed to apply assignments. "
                           "Returning assignments for manual application." );
            }
        }
    }

    // Step 9: Build success response
    json result;
    result["status"] = "success";
    result["classes_applied"] = classesApplied;
    result["assignments_applied"] = assignmentsApplied;
    result["netclasses"] = responseJson["netclasses"];
    result["assignments"] = responseJson["assignments"];

    int ncCount = responseJson["netclasses"].size();
    int assignCount = responseJson["assignments"].size();

    std::string msg = "Generated " + std::to_string( ncCount ) + " net class"
                      + ( ncCount != 1 ? "es" : "" ) + " with "
                      + std::to_string( assignCount ) + " assignment"
                      + ( assignCount != 1 ? "s" : "" );

    if( classesApplied )
    {
        msg += ". Net classes applied to board";

        if( assignmentsApplied )
            msg += " and assignments applied";
        else if( assignCount > 0 )
            msg += ". Assignments could not be applied automatically — "
                   "apply via Board Setup > Net Classes";
    }
    else
    {
        msg += " (preview only)";
    }

    result["message"] = msg;
    wxLogInfo( "NETCLASS_GENERATOR: %s", msg.c_str() );
    return result.dump( 2 );
}


// ---------------------------------------------------------------------------
// BuildAndCallLLM — steps 3-7 from DoGenerate, thread-safe (no IPC)
// ---------------------------------------------------------------------------
std::string NETCLASS_GENERATOR::BuildAndCallLLM( const json& aNetNames,
                                                   const json& aSetupData )
{
    // Step 3: Build existing net classes array
    json existingNetclasses = json::array();
    if( aSetupData.contains( "net_classes" ) && aSetupData["net_classes"].is_array() )
    {
        for( const auto& nc : aSetupData["net_classes"] )
        {
            json ncObj;
            ncObj["name"] = nc.value( "name", "" );

            auto addIfPresent = [&]( const char* key )
            {
                if( nc.contains( key ) && !nc[key].is_null() )
                    ncObj[key] = nc[key];
            };

            addIfPresent( "clearance" );
            addIfPresent( "track_width" );
            addIfPresent( "via_diameter" );
            addIfPresent( "via_drill" );
            addIfPresent( "diff_pair_width" );
            addIfPresent( "diff_pair_gap" );

            existingNetclasses.push_back( ncObj );
        }
    }

    // Step 4: Build existing assignments array
    json existingAssignments = json::array();
    if( aSetupData.contains( "net_class_assignments" )
        && aSetupData["net_class_assignments"].is_array() )
    {
        for( const auto& assign : aSetupData["net_class_assignments"] )
        {
            json a;
            a["pattern"] = assign.value( "pattern", "" );
            a["netclass"] = assign.value( "netclass", "" );
            existingAssignments.push_back( a );
        }
    }

    // Step 5: Build design context with component data
    json designContext = BuildDesignContext( aSetupData );

    // Step 6: Build request body
    json requestBody;
    requestBody["net_names"] = aNetNames;
    requestBody["existing_netclasses"] = existingNetclasses;
    requestBody["existing_assignments"] = existingAssignments;
    requestBody["design_context"] = designContext.dump();
    requestBody["editor_type"] = "pcb";

    wxLogInfo( "NETCLASS_GENERATOR: Calling /api/llm/netclasses with %zu nets, %zu existing classes",
               aNetNames.size(), existingNetclasses.size() );

    // Step 7: Call endpoint (HTTP — thread-safe)
    return CallNetclassEndpoint( requestBody );
}


// ---------------------------------------------------------------------------
// FinishGenerate — process LLM response + apply (main thread, uses IPC)
// ---------------------------------------------------------------------------
std::string NETCLASS_GENERATOR::FinishGenerate( const std::string& aLLMResponse,
                                                  const json& aNetNames, bool aApply )
{
    json responseJson;
    try
    {
        responseJson = json::parse( aLLMResponse );
    }
    catch( const std::exception& e )
    {
        json err;
        err["error"] = std::string( "Failed to parse server response: " ) + e.what();
        return err.dump();
    }

    if( responseJson.contains( "error" ) )
    {
        wxLogInfo( "NETCLASS_GENERATOR: Server error: %s",
                   responseJson["error"].get<std::string>().c_str() );
        return responseJson.dump();
    }

    wxLogInfo( "NETCLASS_GENERATOR: Got %zu net classes, %zu assignments",
               responseJson.contains( "netclasses" ) ? responseJson["netclasses"].size() : 0,
               responseJson.contains( "assignments" ) ? responseJson["assignments"].size() : 0 );

    // Step 8: Apply if requested (IPC — must be on main thread)
    bool classesApplied = false;
    bool assignmentsApplied = false;

    if( aApply )
    {
        wxLogInfo( "NETCLASS_GENERATOR: Applying net classes..." );
        std::string applyResult = ApplyNetclasses( responseJson );

        json applyJson;
        try
        {
            applyJson = json::parse( applyResult );
        }
        catch( ... ) {}

        if( applyJson.contains( "error" ) )
        {
            json err;
            err["error"] = "Generated net classes but failed to apply: "
                           + applyJson.value( "error", "unknown" );
            err["generated"] = responseJson;
            return err.dump();
        }

        classesApplied = true;

        if( responseJson.contains( "assignments" ) && responseJson["assignments"].is_array()
            && !responseJson["assignments"].empty() )
        {
            assignmentsApplied = ApplyAssignments( responseJson["assignments"] );

            if( !assignmentsApplied )
            {
                wxLogInfo( "NETCLASS_GENERATOR: Failed to apply assignments. "
                           "Returning assignments for manual application." );
            }
        }
    }

    // Step 9: Build success response
    json result;
    result["status"] = "success";
    result["classes_applied"] = classesApplied;
    result["assignments_applied"] = assignmentsApplied;
    result["netclasses"] = responseJson["netclasses"];
    result["assignments"] = responseJson["assignments"];

    int ncCount = responseJson["netclasses"].size();
    int assignCount = responseJson["assignments"].size();

    std::string msg = "Generated " + std::to_string( ncCount ) + " net class"
                      + ( ncCount != 1 ? "es" : "" ) + " with "
                      + std::to_string( assignCount ) + " assignment"
                      + ( assignCount != 1 ? "s" : "" );

    if( classesApplied )
    {
        msg += ". Net classes applied to board";

        if( assignmentsApplied )
            msg += " and assignments applied";
        else if( assignCount > 0 )
            msg += ". Assignments could not be applied automatically — "
                   "apply via Board Setup > Net Classes";
    }
    else
    {
        msg += " (preview only)";
    }

    result["message"] = msg;
    wxLogInfo( "NETCLASS_GENERATOR: %s", msg.c_str() );
    return result.dump( 2 );
}


// ---------------------------------------------------------------------------
// GatherNetNames
// ---------------------------------------------------------------------------
json NETCLASS_GENERATOR::GatherNetNames()
{
    json input;
    input["include_pads"] = false;  // We just need names, not pad details

    std::string result = TOOL_REGISTRY::Instance().ExecuteToolSync( "pcb_get_nets", input );

    try
    {
        return json::parse( result );
    }
    catch( const std::exception& e )
    {
        json err;
        err["error"] = std::string( "Failed to parse pcb_get_nets result: " ) + e.what();
        return err;
    }
}


// ---------------------------------------------------------------------------
// GatherSetupData
// ---------------------------------------------------------------------------
json NETCLASS_GENERATOR::GatherSetupData()
{
    json input;
    input["action"] = "get";

    std::string result = TOOL_REGISTRY::Instance().ExecuteToolSync( "pcb_setup", input );

    try
    {
        return json::parse( result );
    }
    catch( const std::exception& e )
    {
        json err;
        err["error"] = std::string( "Failed to parse pcb_setup result: " ) + e.what();
        return err;
    }
}


// ---------------------------------------------------------------------------
// BuildDesignContext — group components by value+datasheet
// ---------------------------------------------------------------------------
json NETCLASS_GENERATOR::BuildDesignContext( const json& aSetupData )
{
    json context;

    if( !aSetupData.contains( "components" ) || !aSetupData["components"].is_array() )
        return context;

    // Group components by value+datasheet (same as dialog_board_setup.cpp)
    struct CompGroup
    {
        std::string              value;
        std::string              datasheet;
        std::vector<std::string> refs;
    };

    std::map<std::string, CompGroup> groups;

    for( const auto& comp : aSetupData["components"] )
    {
        std::string ref = comp.value( "ref", "" );
        std::string value = comp.value( "value", "" );
        std::string datasheet = comp.value( "datasheet", "" );

        std::string key = value + "|" + datasheet;
        auto& group = groups[key];
        group.value = value;
        group.datasheet = datasheet;
        group.refs.push_back( ref );
    }

    json components = json::array();
    for( const auto& [key, group] : groups )
    {
        json comp;
        comp["value"] = group.value;
        comp["refs"] = group.refs;

        if( !group.datasheet.empty() )
            comp["datasheet"] = group.datasheet;

        components.push_back( comp );
    }

    context["components"] = components;
    return context;
}


// ---------------------------------------------------------------------------
// CallNetclassEndpoint
// ---------------------------------------------------------------------------
std::string NETCLASS_GENERATOR::CallNetclassEndpoint( const json& aRequestBody )
{
    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();

    if( !auth )
    {
        json err;
        err["error"] = "Not authenticated";
        return err.dump();
    }

    std::string accessToken = auth->GetAccessToken();
    if( accessToken.empty() )
    {
        json err;
        err["error"] = "Access token expired. Please sign in again.";
        return err.dump();
    }

    std::string url = ZEO_BASE_URL + "/api/llm/netclasses";

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetHeader( "Content-Type", "application/json" );
        curl.SetHeader( "Authorization", "Bearer " + accessToken );
        curl.SetPostFields( aRequestBody.dump() );
        curl_easy_setopt( curl.GetCurl(), CURLOPT_TIMEOUT, 120L );
        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();

        if( httpCode == 200 )
        {
            wxLogInfo( "NETCLASS_GENERATOR: Server responded OK" );
            return curl.GetBuffer();
        }
        else if( httpCode == 401 )
        {
            json err;
            err["error"] = "Authentication expired. Please sign in to Zeo again.";
            return err.dump();
        }
        else
        {
            wxLogInfo( "NETCLASS_GENERATOR: Server error HTTP %ld: %s",
                       httpCode, curl.GetBuffer().c_str() );
            json err;
            err["error"] = "Server error (HTTP " + std::to_string( httpCode ) + ")";
            return err.dump();
        }
    }
    catch( const std::exception& e )
    {
        wxLogInfo( "NETCLASS_GENERATOR: Network error: %s", e.what() );
        json err;
        err["error"] = std::string( "Network error: " ) + e.what();
        return err.dump();
    }
}


// ---------------------------------------------------------------------------
// ApplyNetclasses — set net class definitions via pcb_setup
// ---------------------------------------------------------------------------
std::string NETCLASS_GENERATOR::ApplyNetclasses( const json& aResponse )
{
    json setInput;
    setInput["action"] = "set";

    // Convert server response format to pcb_setup format
    if( aResponse.contains( "netclasses" ) && aResponse["netclasses"].is_array() )
    {
        json netClasses = json::array();

        for( const auto& nc : aResponse["netclasses"] )
        {
            json ncObj;
            ncObj["name"] = nc.value( "name", "" );

            // Convert mm values from server to nm for pcb_setup
            auto addMmToNm = [&]( const char* srcKey, const char* dstKey )
            {
                if( nc.contains( srcKey ) && !nc[srcKey].is_null() )
                    ncObj[dstKey] = static_cast<int>( nc[srcKey].get<double>() * 1e6 );
            };

            addMmToNm( "track_width_mm", "track_width_nm" );
            addMmToNm( "clearance_mm", "clearance_nm" );
            addMmToNm( "via_diameter_mm", "via_diameter_nm" );
            addMmToNm( "via_drill_mm", "via_drill_nm" );
            addMmToNm( "diff_pair_width_mm", "diff_pair_width_nm" );
            addMmToNm( "diff_pair_gap_mm", "diff_pair_gap_nm" );

            netClasses.push_back( ncObj );
        }

        setInput["net_classes"] = netClasses;
    }

    wxLogInfo( "NETCLASS_GENERATOR: Applying %zu net classes via pcb_setup",
               setInput.contains( "net_classes" ) ? setInput["net_classes"].size() : 0 );

    return TOOL_REGISTRY::Instance().ExecuteToolSync( "pcb_setup", setInput );
}


// ---------------------------------------------------------------------------
// ApplyAssignments — set assignments via pcb_setup (project-level API)
// ---------------------------------------------------------------------------
bool NETCLASS_GENERATOR::ApplyAssignments( const json& aAssignments )
{
    json setInput;
    setInput["action"] = "set";

    json assignments = json::array();

    for( const auto& assign : aAssignments )
    {
        json a;
        a["pattern"] = assign.value( "pattern", "" );
        a["netclass"] = assign.value( "netclass", "" );
        assignments.push_back( a );
    }

    setInput["net_class_assignments"] = assignments;

    wxLogInfo( "NETCLASS_GENERATOR: Applying %zu assignments via pcb_setup", assignments.size() );

    std::string result = TOOL_REGISTRY::Instance().ExecuteToolSync( "pcb_setup", setInput );

    try
    {
        auto resultJson = json::parse( result );
        return !resultJson.contains( "error" );
    }
    catch( ... )
    {
        return false;
    }
}
