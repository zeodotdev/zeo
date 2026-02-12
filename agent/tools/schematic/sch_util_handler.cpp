#include "sch_util_handler.h"
#include <sstream>


bool SCH_UTIL_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_annotate";
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

    return "Executing " + aToolName;
}


bool SCH_UTIL_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_annotate";
}


std::string SCH_UTIL_HANDLER::GetIPCCommand( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string code;

    if( aToolName == "sch_annotate" )
        code = GenerateAnnotateCode( aInput );

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
