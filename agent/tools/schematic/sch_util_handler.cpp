#include "sch_util_handler.h"
#include <sstream>


bool SCH_UTIL_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_annotate" ||
           aToolName == "sch_get_nets";
}


std::string SCH_UTIL_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    // All utility tools require IPC execution - should not be called directly
    return "Error: " + aToolName + " requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_UTIL_HANDLER::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    if( aToolName == "sch_annotate" )
    {
        std::string scope = aInput.value( "scope", "unannotated_only" );
        if( scope == "all" )
            return "Annotating all symbols";
        return "Annotating unannotated symbols";
    }
    else if( aToolName == "sch_get_nets" )
    {
        return "Getting net list";
    }

    return "Executing " + aToolName;
}


bool SCH_UTIL_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_annotate" ||
           aToolName == "sch_get_nets";
}


std::string SCH_UTIL_HANDLER::GetIPCCommand( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string code;

    if( aToolName == "sch_annotate" )
        code = GenerateAnnotateCode( aInput );
    else if( aToolName == "sch_get_nets" )
        code = GenerateGetNetsCode( aInput );

    return "run_shell sch " + code;
}


std::string SCH_UTIL_HANDLER::GenerateAnnotateCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    // Parse input parameters
    std::string scope = aInput.value( "scope", "all" );
    std::string sortBy = aInput.value( "sort_by", "x_position" );
    bool resetExisting = aInput.value( "reset_existing", false );

    // Map scope parameter to kipy API values
    std::string kipyScope = "all";
    if( scope == "unannotated_only" )
        kipyScope = "all";  // annotate all, but don't reset existing
    else if( scope == "all" )
        kipyScope = "all";
    else if( scope == "current_sheet" )
        kipyScope = "current_sheet";
    else if( scope == "selection" )
        kipyScope = "selection";

    // Map sort_by to kipy order parameter
    std::string kipyOrder = "x_y";
    if( sortBy == "x_position" )
        kipyOrder = "x_y";
    else if( sortBy == "y_position" )
        kipyOrder = "y_x";

    code << "import json\n";
    code << "\n";
    code << "# Refresh document to handle close/reopen cycles\n";
    code << "if hasattr(sch, 'refresh_document'):\n";
    code << "    if not sch.refresh_document():\n";
    code << "        raise RuntimeError('Schematic editor not open or document not available')\n";
    code << "\n";
    code << "try:\n";
    code << "    # Get symbols before annotation for comparison\n";
    code << "    symbols_before = {}\n";
    code << "    for sym in sch.symbols.get_all():\n";
    code << "        ref = getattr(sym, 'reference', '?')\n";
    code << "        sym_id = str(sym.id.value) if hasattr(sym, 'id') and hasattr(sym.id, 'value') else str(getattr(sym, 'id', ''))\n";
    code << "        symbols_before[sym_id] = ref\n";
    code << "\n";
    code << "    # Call the annotation API via sch.erc.annotate()\n";
    code << "    # kipy API: annotate(scope, order, algorithm, start_number, reset_existing, recursive)\n";
    code << "    # Note: KiCad adds 1 to start_number internally, so pass 0 to start at 1\n";
    code << "    response = sch.erc.annotate(\n";
    code << "        scope='" << kipyScope << "',\n";
    code << "        order='" << kipyOrder << "',\n";
    code << "        algorithm='incremental',\n";
    code << "        start_number=0,\n";
    code << "        reset_existing=" << ( resetExisting || scope == "all" ? "True" : "False" ) << ",\n";
    code << "        recursive=True\n";
    code << "    )\n";
    code << "\n";
    code << "    # Get symbols after annotation to show changes\n";
    code << "    annotated = []\n";
    code << "    for sym in sch.symbols.get_all():\n";
    code << "        sym_id = str(sym.id.value) if hasattr(sym, 'id') and hasattr(sym.id, 'value') else str(getattr(sym, 'id', ''))\n";
    code << "        new_ref = getattr(sym, 'reference', '?')\n";
    code << "        old_ref = symbols_before.get(sym_id, '?')\n";
    code << "        if old_ref != new_ref:\n";
    code << "            annotated.append({'uuid': sym_id, 'old_ref': old_ref, 'new_ref': new_ref})\n";
    code << "\n";
    code << "    # Get count from response if available\n";
    code << "    count = getattr(response, 'symbols_annotated', len(annotated))\n";
    code << "\n";
    code << "    result = {\n";
    code << "        'status': 'success',\n";
    code << "        'scope': '" << scope << "',\n";
    code << "        'sort_by': '" << sortBy << "',\n";
    code << "        'symbols_annotated': count,\n";
    code << "        'changes': annotated[:50]  # Limit to first 50 changes\n";
    code << "    }\n";
    code << "    if len(annotated) > 50:\n";
    code << "        result['note'] = f'Showing first 50 of {len(annotated)} changes'\n";
    code << "    print(json.dumps(result, indent=2))\n";
    code << "\n";
    code << "except Exception as e:\n";
    code << "    import traceback\n";
    code << "    print(json.dumps({'status': 'error', 'message': str(e), 'traceback': traceback.format_exc()}))\n";

    return code.str();
}


std::string SCH_UTIL_HANDLER::GenerateGetNetsCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    // Optional filter parameter
    std::string filter = aInput.value( "filter", "" );
    bool includeUnconnected = aInput.value( "include_unconnected", false );

    code << "import json\n";
    code << "\n";
    code << "# Refresh document to handle close/reopen cycles\n";
    code << "if hasattr(sch, 'refresh_document'):\n";
    code << "    if not sch.refresh_document():\n";
    code << "        raise RuntimeError('Schematic editor not open or document not available')\n";
    code << "\n";
    code << "try:\n";
    code << "    nets_data = []\n";
    code << "    filter_pattern = '" << filter << "'\n";
    code << "    include_unconnected = " << ( includeUnconnected ? "True" : "False" ) << "\n";
    code << "\n";
    code << "    # Try to get nets from the kipy API\n";
    code << "    nets = None\n";
    code << "    if hasattr(sch, 'nets'):\n";
    code << "        if hasattr(sch.nets, 'get_all'):\n";
    code << "            nets = sch.nets.get_all()\n";
    code << "        elif hasattr(sch.nets, 'items'):\n";
    code << "            nets = list(sch.nets.items())\n";
    code << "        elif callable(sch.nets):\n";
    code << "            nets = sch.nets()\n";
    code << "    elif hasattr(sch, 'get_nets'):\n";
    code << "        nets = sch.get_nets()\n";
    code << "\n";
    code << "    if nets is None:\n";
    code << "        # Fallback: Build net list from symbols and their connections\n";
    code << "        nets_dict = {}\n";
    code << "        for sym in sch.symbols.get_all():\n";
    code << "            ref = getattr(sym, 'reference', '?')\n";
    code << "            if hasattr(sym, 'pins'):\n";
    code << "                for pin in sym.pins:\n";
    code << "                    pin_name = getattr(pin, 'name', getattr(pin, 'number', '?'))\n";
    code << "                    pin_num = getattr(pin, 'number', pin_name)\n";
    code << "                    net_name = getattr(pin, 'net', getattr(pin, 'net_name', ''))\n";
    code << "                    if net_name:\n";
    code << "                        if net_name not in nets_dict:\n";
    code << "                            nets_dict[net_name] = []\n";
    code << "                        nets_dict[net_name].append({'ref': ref, 'pin': str(pin_num), 'pin_name': str(pin_name)})\n";
    code << "        \n";
    code << "        for net_name, pins in nets_dict.items():\n";
    code << "            if filter_pattern and filter_pattern not in net_name:\n";
    code << "                continue\n";
    code << "            nets_data.append({\n";
    code << "                'name': net_name,\n";
    code << "                'pins': pins,\n";
    code << "                'pin_count': len(pins)\n";
    code << "            })\n";
    code << "    else:\n";
    code << "        # Process nets from API\n";
    code << "        for net in nets:\n";
    code << "            # Handle both object and tuple formats\n";
    code << "            if isinstance(net, tuple):\n";
    code << "                net_name, net_obj = net\n";
    code << "            else:\n";
    code << "                net_name = getattr(net, 'name', str(net))\n";
    code << "                net_obj = net\n";
    code << "            \n";
    code << "            if filter_pattern and filter_pattern not in net_name:\n";
    code << "                continue\n";
    code << "            \n";
    code << "            # Get connected pins\n";
    code << "            pins = []\n";
    code << "            if hasattr(net_obj, 'pins'):\n";
    code << "                for pin in net_obj.pins:\n";
    code << "                    pin_ref = getattr(pin, 'reference', getattr(pin, 'ref', '?'))\n";
    code << "                    pin_num = getattr(pin, 'number', getattr(pin, 'pin', '?'))\n";
    code << "                    pin_name = getattr(pin, 'name', '')\n";
    code << "                    pins.append({'ref': str(pin_ref), 'pin': str(pin_num), 'pin_name': str(pin_name)})\n";
    code << "            elif hasattr(net_obj, 'connections'):\n";
    code << "                for conn in net_obj.connections:\n";
    code << "                    pins.append({'ref': str(getattr(conn, 'ref', '?')), 'pin': str(getattr(conn, 'pin', '?'))})\n";
    code << "            \n";
    code << "            # Skip unconnected nets if not requested\n";
    code << "            if not include_unconnected and len(pins) < 2:\n";
    code << "                continue\n";
    code << "            \n";
    code << "            nets_data.append({\n";
    code << "                'name': net_name,\n";
    code << "                'pins': pins,\n";
    code << "                'pin_count': len(pins)\n";
    code << "            })\n";
    code << "\n";
    code << "    # Sort by net name\n";
    code << "    nets_data.sort(key=lambda x: x['name'])\n";
    code << "\n";
    code << "    result = {\n";
    code << "        'status': 'success',\n";
    code << "        'net_count': len(nets_data),\n";
    code << "        'nets': nets_data\n";
    code << "    }\n";
    code << "    print(json.dumps(result, indent=2))\n";
    code << "\n";
    code << "except Exception as e:\n";
    code << "    import traceback\n";
    code << "    print(json.dumps({'status': 'error', 'message': str(e), 'traceback': traceback.format_exc()}))\n";

    return code.str();
}
