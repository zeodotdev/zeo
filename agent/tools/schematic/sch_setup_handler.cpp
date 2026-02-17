#include "sch_setup_handler.h"
#include <sstream>


bool SCH_SETUP_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_setup";
}


std::string SCH_SETUP_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    // This tool requires IPC execution - should not be called directly
    return "Error: " + aToolName + " requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_SETUP_HANDLER::GetDescription( const std::string& aToolName,
                                                const nlohmann::json& aInput ) const
{
    if( aToolName == "sch_setup" )
    {
        std::string action = aInput.value( "action", "get" );
        if( action == "get" )
            return "Reading schematic setup settings";
        else
            return "Updating schematic setup settings";
    }

    return "Executing " + aToolName;
}


bool SCH_SETUP_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_setup";
}


std::string SCH_SETUP_HANDLER::GetIPCCommand( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    std::string code;

    if( aToolName == "sch_setup" )
    {
        std::string action = aInput.value( "action", "get" );
        if( action == "get" )
            code = GenerateGetCode();
        else
            code = GenerateSetCode( aInput );
    }

    return "run_shell sch " + code;
}


std::string SCH_SETUP_HANDLER::GenerateGetCode() const
{
    std::ostringstream code;

    code << "import json\n";
    code << "\n";
    code << "# Refresh document to handle close/reopen cycles\n";
    code << "if hasattr(sch, 'refresh_document'):\n";
    code << "    if not sch.refresh_document():\n";
    code << "        raise RuntimeError('Schematic editor not open or document not available')\n";
    code << "\n";
    code << "result = {'status': 'success'}\n";
    code << "\n";
    code << "# Get page settings\n";
    code << "page = sch.page.get_settings()\n";
    code << "result['page'] = {\n";
    code << "    'size_type': page.size_type,\n";
    code << "    'size_type_name': page.size_type_name,\n";
    code << "    'portrait': page.portrait,\n";
    code << "    'width_mm': page.width_mm,\n";
    code << "    'height_mm': page.height_mm\n";
    code << "}\n";
    code << "\n";
    code << "# Get title block\n";
    code << "tb = sch.page.get_title_block()\n";
    code << "result['title_block'] = {\n";
    code << "    'title': tb.title,\n";
    code << "    'date': tb.date,\n";
    code << "    'revision': tb.revision,\n";
    code << "    'company': tb.company,\n";
    code << "    'comments': tb.comments\n";
    code << "}\n";
    code << "\n";
    code << "# Get grid settings\n";
    code << "result['grid'] = sch.page.get_grid_settings()\n";
    code << "\n";
    code << "# Get editor preferences (display settings)\n";
    code << "result['editor'] = sch.page.get_editor_preferences()\n";
    code << "\n";
    code << "# Get formatting settings (project-level from Schematic Setup)\n";
    code << "result['formatting'] = sch.page.get_formatting_settings()\n";
    code << "\n";
    code << "# Get ERC settings\n";
    code << "result['erc'] = sch.erc.get_settings()\n";
    code << "\n";
    code << "# Get annotation settings (project-level from Schematic Setup)\n";
    code << "result['annotation'] = sch.page.get_annotation_settings()\n";
    code << "\n";
    code << "# Get field name templates (project-level from Schematic Setup)\n";
    code << "result['field_name_templates'] = sch.page.get_field_name_templates()\n";
    code << "\n";
    code << "# Get pin conflict map (ERC pin-to-pin conflict settings)\n";
    code << "result['pin_conflict_map'] = sch.erc.get_pin_type_matrix()\n";
    code << "\n";
    code << "# Get net classes\n";
    code << "result['net_classes'] = sch.netclass.get_all()\n";
    code << "\n";
    code << "# Get net class assignments\n";
    code << "result['net_class_assignments'] = sch.netclass.get_assignments()\n";
    code << "\n";
    code << "# Get bus aliases\n";
    code << "result['bus_aliases'] = sch.bus_alias.get_all()\n";
    code << "\n";
    code << "# Get text variables (project-level)\n";
    code << "project = sch.get_project()\n";
    code << "text_vars = project.get_text_variables()\n";
    code << "result['text_variables'] = dict(text_vars.variables)\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2, default=str))\n";

    return code.str();
}


std::string SCH_SETUP_HANDLER::GenerateSetCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "\n";
    code << "# Refresh document to handle close/reopen cycles\n";
    code << "if hasattr(sch, 'refresh_document'):\n";
    code << "    if not sch.refresh_document():\n";
    code << "        raise RuntimeError('Schematic editor not open or document not available')\n";
    code << "\n";

    // Page size type mapping
    code << "# Page size type mapping\n";
    code << "SIZE_MAP = {\n";
    code << "    'A5': 1, 'A4': 2, 'A3': 3, 'A2': 4, 'A1': 5, 'A0': 6,\n";
    code << "    'A': 7, 'B': 8, 'C': 9, 'D': 10, 'E': 11,\n";
    code << "    'GERBER': 12, 'USLetter': 13, 'USLegal': 14, 'USLedger': 15, 'USER': 16\n";
    code << "}\n";
    code << "\n";

    // Handle page settings
    if( aInput.contains( "page" ) )
    {
        const auto& page = aInput["page"];
        code << "# Set page settings\n";
        code << "sch.page.set_settings(\n";

        bool first = true;
        if( page.contains( "size" ) )
        {
            std::string size = page["size"].get<std::string>();
            code << ( first ? "" : ",\n" ) << "    size_type=SIZE_MAP.get('" << size << "', 2)";
            first = false;
        }
        if( page.contains( "portrait" ) )
        {
            code << ( first ? "" : ",\n" ) << "    portrait=" << ( page["portrait"].get<bool>() ? "True" : "False" );
            first = false;
        }
        if( page.contains( "width_mm" ) )
        {
            code << ( first ? "" : ",\n" ) << "    width_mm=" << page["width_mm"].get<double>();
            first = false;
        }
        if( page.contains( "height_mm" ) )
        {
            code << ( first ? "" : ",\n" ) << "    height_mm=" << page["height_mm"].get<double>();
            first = false;
        }
        code << "\n)\n\n";
    }

    // Handle title block settings
    if( aInput.contains( "title_block" ) )
    {
        const auto& tb = aInput["title_block"];
        code << "# Set title block\n";
        code << "sch.page.set_title_block(\n";

        bool first = true;
        if( tb.contains( "title" ) )
        {
            code << ( first ? "" : ",\n" ) << "    title='" << EscapePythonString( tb["title"].get<std::string>() ) << "'";
            first = false;
        }
        if( tb.contains( "date" ) )
        {
            code << ( first ? "" : ",\n" ) << "    date='" << EscapePythonString( tb["date"].get<std::string>() ) << "'";
            first = false;
        }
        if( tb.contains( "revision" ) )
        {
            code << ( first ? "" : ",\n" ) << "    revision='" << EscapePythonString( tb["revision"].get<std::string>() ) << "'";
            first = false;
        }
        if( tb.contains( "company" ) )
        {
            code << ( first ? "" : ",\n" ) << "    company='" << EscapePythonString( tb["company"].get<std::string>() ) << "'";
            first = false;
        }

        // Handle comments as a dict
        if( tb.contains( "comments" ) )
        {
            code << ( first ? "" : ",\n" ) << "    comments={";
            const auto& comments = tb["comments"];
            bool firstComment = true;
            for( int i = 1; i <= 9; i++ )
            {
                std::string key = "comment" + std::to_string( i );
                if( comments.contains( key ) )
                {
                    code << ( firstComment ? "" : ", " ) << i << ": '" << EscapePythonString( comments[key].get<std::string>() ) << "'";
                    firstComment = false;
                }
            }
            code << "}";
            first = false;
        }
        code << "\n)\n\n";
    }

    // Handle grid settings
    if( aInput.contains( "grid" ) )
    {
        const auto& grid = aInput["grid"];
        code << "# Set grid settings\n";
        code << "sch.page.set_grid(\n";

        bool first = true;
        if( grid.contains( "size_mm" ) )
        {
            code << ( first ? "" : ",\n" ) << "    grid_mm=" << grid["size_mm"].get<double>();
            first = false;
        }
        if( grid.contains( "size_mils" ) )
        {
            code << ( first ? "" : ",\n" ) << "    grid_mils=" << grid["size_mils"].get<double>();
            first = false;
        }
        if( grid.contains( "visible" ) )
        {
            code << ( first ? "" : ",\n" ) << "    visible=" << ( grid["visible"].get<bool>() ? "True" : "False" );
            first = false;
        }
        if( grid.contains( "snap" ) )
        {
            code << ( first ? "" : ",\n" ) << "    snap_to_grid=" << ( grid["snap"].get<bool>() ? "True" : "False" );
            first = false;
        }
        code << "\n)\n\n";
    }

    // Handle formatting settings (the new comprehensive settings)
    if( aInput.contains( "formatting" ) )
    {
        const auto& fmt = aInput["formatting"];
        code << "# Set formatting settings\n";
        code << "sch.page.set_formatting_settings(\n";

        bool first = true;

        // Text section
        if( fmt.contains( "text" ) )
        {
            const auto& text = fmt["text"];
            if( text.contains( "default_text_size_mils" ) )
            {
                code << ( first ? "" : ",\n" ) << "    default_text_size_mils=" << text["default_text_size_mils"].get<int>();
                first = false;
            }
            if( text.contains( "overbar_offset_ratio" ) )
            {
                code << ( first ? "" : ",\n" ) << "    overbar_offset_ratio=" << text["overbar_offset_ratio"].get<double>();
                first = false;
            }
            if( text.contains( "label_offset_ratio" ) )
            {
                code << ( first ? "" : ",\n" ) << "    label_offset_ratio=" << text["label_offset_ratio"].get<double>();
                first = false;
            }
            if( text.contains( "global_label_margin_ratio" ) )
            {
                code << ( first ? "" : ",\n" ) << "    global_label_margin_ratio=" << text["global_label_margin_ratio"].get<double>();
                first = false;
            }
        }

        // Symbols section
        if( fmt.contains( "symbols" ) )
        {
            const auto& sym = fmt["symbols"];
            if( sym.contains( "default_line_width_mils" ) )
            {
                code << ( first ? "" : ",\n" ) << "    default_line_width_mils=" << sym["default_line_width_mils"].get<int>();
                first = false;
            }
            if( sym.contains( "pin_symbol_size_mils" ) )
            {
                code << ( first ? "" : ",\n" ) << "    pin_symbol_size_mils=" << sym["pin_symbol_size_mils"].get<int>();
                first = false;
            }
        }

        // Connections section
        if( fmt.contains( "connections" ) )
        {
            const auto& conn = fmt["connections"];
            if( conn.contains( "junction_size_choice" ) )
            {
                code << ( first ? "" : ",\n" ) << "    junction_size_choice=" << conn["junction_size_choice"].get<int>();
                first = false;
            }
            if( conn.contains( "hop_over_size_choice" ) )
            {
                code << ( first ? "" : ",\n" ) << "    hop_over_size_choice=" << conn["hop_over_size_choice"].get<int>();
                first = false;
            }
            if( conn.contains( "connection_grid_mils" ) )
            {
                code << ( first ? "" : ",\n" ) << "    connection_grid_mils=" << conn["connection_grid_mils"].get<int>();
                first = false;
            }
        }

        // Inter-sheet references section
        if( fmt.contains( "intersheet_refs" ) )
        {
            const auto& isr = fmt["intersheet_refs"];
            if( isr.contains( "show" ) )
            {
                code << ( first ? "" : ",\n" ) << "    intersheet_refs_show=" << ( isr["show"].get<bool>() ? "True" : "False" );
                first = false;
            }
            if( isr.contains( "list_own_page" ) )
            {
                code << ( first ? "" : ",\n" ) << "    intersheet_refs_list_own_page=" << ( isr["list_own_page"].get<bool>() ? "True" : "False" );
                first = false;
            }
            if( isr.contains( "format_short" ) )
            {
                code << ( first ? "" : ",\n" ) << "    intersheet_refs_format_short=" << ( isr["format_short"].get<bool>() ? "True" : "False" );
                first = false;
            }
            if( isr.contains( "prefix" ) )
            {
                code << ( first ? "" : ",\n" ) << "    intersheet_refs_prefix='" << EscapePythonString( isr["prefix"].get<std::string>() ) << "'";
                first = false;
            }
            if( isr.contains( "suffix" ) )
            {
                code << ( first ? "" : ",\n" ) << "    intersheet_refs_suffix='" << EscapePythonString( isr["suffix"].get<std::string>() ) << "'";
                first = false;
            }
        }

        // Dashed lines section
        if( fmt.contains( "dashed_lines" ) )
        {
            const auto& dl = fmt["dashed_lines"];
            if( dl.contains( "dash_ratio" ) )
            {
                code << ( first ? "" : ",\n" ) << "    dashed_line_dash_ratio=" << dl["dash_ratio"].get<double>();
                first = false;
            }
            if( dl.contains( "gap_ratio" ) )
            {
                code << ( first ? "" : ",\n" ) << "    dashed_line_gap_ratio=" << dl["gap_ratio"].get<double>();
                first = false;
            }
        }

        // Operating-point overlay section
        if( fmt.contains( "opo" ) )
        {
            const auto& opo = fmt["opo"];
            if( opo.contains( "voltage_precision" ) )
            {
                code << ( first ? "" : ",\n" ) << "    opo_voltage_precision=" << opo["voltage_precision"].get<int>();
                first = false;
            }
            if( opo.contains( "voltage_range" ) )
            {
                code << ( first ? "" : ",\n" ) << "    opo_voltage_range='" << EscapePythonString( opo["voltage_range"].get<std::string>() ) << "'";
                first = false;
            }
            if( opo.contains( "current_precision" ) )
            {
                code << ( first ? "" : ",\n" ) << "    opo_current_precision=" << opo["current_precision"].get<int>();
                first = false;
            }
            if( opo.contains( "current_range" ) )
            {
                code << ( first ? "" : ",\n" ) << "    opo_current_range='" << EscapePythonString( opo["current_range"].get<std::string>() ) << "'";
                first = false;
            }
        }

        code << "\n)\n\n";
    }

    // Handle ERC settings
    if( aInput.contains( "erc" ) )
    {
        const auto& erc = aInput["erc"];
        if( erc.contains( "rule_severities" ) )
        {
            code << "# Set ERC rule severities\n";
            code << "sch.erc.set_settings(rule_severities={\n";
            const auto& rules = erc["rule_severities"];
            bool first = true;
            for( auto it = rules.begin(); it != rules.end(); ++it )
            {
                code << ( first ? "" : ",\n" ) << "    '" << it.key() << "': '" << it.value().get<std::string>() << "'";
                first = false;
            }
            code << "\n})\n\n";
        }
    }

    // Handle annotation settings
    if( aInput.contains( "annotation" ) )
    {
        const auto& ann = aInput["annotation"];
        code << "# Set annotation settings\n";
        code << "sch.page.set_annotation_settings(\n";

        bool first = true;

        // Units section
        if( ann.contains( "units" ) )
        {
            const auto& units = ann["units"];
            if( units.contains( "symbol_unit_notation" ) )
            {
                code << ( first ? "" : ",\n" ) << "    symbol_unit_notation='" << EscapePythonString( units["symbol_unit_notation"].get<std::string>() ) << "'";
                first = false;
            }
        }

        // Order section
        if( ann.contains( "order" ) )
        {
            const auto& order = ann["order"];
            if( order.contains( "sort_order" ) )
            {
                code << ( first ? "" : ",\n" ) << "    sort_order='" << EscapePythonString( order["sort_order"].get<std::string>() ) << "'";
                first = false;
            }
        }

        // Numbering section
        if( ann.contains( "numbering" ) )
        {
            const auto& num = ann["numbering"];
            if( num.contains( "method" ) )
            {
                code << ( first ? "" : ",\n" ) << "    numbering_method='" << EscapePythonString( num["method"].get<std::string>() ) << "'";
                first = false;
            }
            if( num.contains( "start_number" ) )
            {
                code << ( first ? "" : ",\n" ) << "    start_number=" << num["start_number"].get<int>();
                first = false;
            }
            if( num.contains( "allow_reference_reuse" ) )
            {
                code << ( first ? "" : ",\n" ) << "    allow_reference_reuse=" << ( num["allow_reference_reuse"].get<bool>() ? "True" : "False" );
                first = false;
            }
        }

        code << "\n)\n\n";
    }

    // Handle field name templates
    if( aInput.contains( "field_name_templates" ) )
    {
        const auto& templates = aInput["field_name_templates"];
        code << "# Set field name templates\n";
        code << "sch.page.set_field_name_templates([\n";

        bool first = true;
        for( const auto& tmpl : templates )
        {
            if( !tmpl.contains( "name" ) )
                continue;

            code << ( first ? "" : ",\n" );
            code << "    {'name': '" << EscapePythonString( tmpl["name"].get<std::string>() ) << "'";

            if( tmpl.contains( "visible" ) )
                code << ", 'visible': " << ( tmpl["visible"].get<bool>() ? "True" : "False" );
            else
                code << ", 'visible': False";

            if( tmpl.contains( "url" ) )
                code << ", 'url': " << ( tmpl["url"].get<bool>() ? "True" : "False" );
            else
                code << ", 'url': False";

            code << "}";
            first = false;
        }
        code << "\n])\n\n";
    }

    // Handle pin conflict map settings
    if( aInput.contains( "pin_conflict_map" ) )
    {
        const auto& pcm = aInput["pin_conflict_map"];

        // Check for reset_to_defaults
        if( pcm.contains( "reset_to_defaults" ) && pcm["reset_to_defaults"].get<bool>() )
        {
            code << "# Reset pin conflict map to defaults\n";
            code << "sch.erc.set_pin_type_matrix(reset_to_defaults=True)\n\n";
        }

        // Handle individual entry updates
        if( pcm.contains( "entries" ) )
        {
            const auto& entries = pcm["entries"];
            code << "# Set pin conflict map entries\n";
            code << "sch.erc.set_pin_type_matrix(entries={\n";

            bool first = true;
            for( const auto& entry : entries )
            {
                if( !entry.contains( "first_pin_type" ) || !entry.contains( "second_pin_type" ) || !entry.contains( "error_type" ) )
                    continue;

                std::string firstPin = entry["first_pin_type"].get<std::string>();
                std::string secondPin = entry["second_pin_type"].get<std::string>();
                std::string errorType = entry["error_type"].get<std::string>();

                code << ( first ? "" : ",\n" );
                code << "    ('" << firstPin << "', '" << secondPin << "'): '" << errorType << "'";
                first = false;
            }
            code << "\n})\n\n";
        }
    }

    // Handle net classes
    if( aInput.contains( "net_classes" ) )
    {
        const auto& nc = aInput["net_classes"];

        // Handle deletions first
        if( nc.contains( "delete" ) )
        {
            const auto& toDelete = nc["delete"];
            for( const auto& name : toDelete )
            {
                code << "# Delete net class\n";
                code << "sch.netclass.delete('" << EscapePythonString( name.get<std::string>() ) << "')\n";
            }
            code << "\n";
        }

        // Handle creates
        if( nc.contains( "create" ) )
        {
            const auto& toCreate = nc["create"];
            for( const auto& netclass : toCreate )
            {
                if( !netclass.contains( "name" ) )
                    continue;

                code << "# Create net class\n";
                code << "sch.netclass.create(\n";
                code << "    name='" << EscapePythonString( netclass["name"].get<std::string>() ) << "'";

                if( netclass.contains( "wire_width_mils" ) )
                    code << ",\n    wire_width_mils=" << netclass["wire_width_mils"].get<int>();
                if( netclass.contains( "bus_width_mils" ) )
                    code << ",\n    bus_width_mils=" << netclass["bus_width_mils"].get<int>();
                if( netclass.contains( "color" ) )
                    code << ",\n    color='" << EscapePythonString( netclass["color"].get<std::string>() ) << "'";
                if( netclass.contains( "line_style" ) )
                    code << ",\n    line_style='" << EscapePythonString( netclass["line_style"].get<std::string>() ) << "'";
                if( netclass.contains( "description" ) )
                    code << ",\n    description='" << EscapePythonString( netclass["description"].get<std::string>() ) << "'";
                if( netclass.contains( "priority" ) )
                    code << ",\n    priority=" << netclass["priority"].get<int>();

                code << "\n)\n\n";
            }
        }

        // Handle updates
        if( nc.contains( "update" ) )
        {
            const auto& toUpdate = nc["update"];
            for( const auto& netclass : toUpdate )
            {
                if( !netclass.contains( "name" ) )
                    continue;

                code << "# Update net class\n";
                code << "sch.netclass.update(\n";
                code << "    name='" << EscapePythonString( netclass["name"].get<std::string>() ) << "'";

                if( netclass.contains( "wire_width_mils" ) )
                    code << ",\n    wire_width_mils=" << netclass["wire_width_mils"].get<int>();
                if( netclass.contains( "bus_width_mils" ) )
                    code << ",\n    bus_width_mils=" << netclass["bus_width_mils"].get<int>();
                if( netclass.contains( "color" ) )
                    code << ",\n    color='" << EscapePythonString( netclass["color"].get<std::string>() ) << "'";
                if( netclass.contains( "line_style" ) )
                    code << ",\n    line_style='" << EscapePythonString( netclass["line_style"].get<std::string>() ) << "'";
                if( netclass.contains( "description" ) )
                    code << ",\n    description='" << EscapePythonString( netclass["description"].get<std::string>() ) << "'";
                if( netclass.contains( "priority" ) )
                    code << ",\n    priority=" << netclass["priority"].get<int>();

                code << "\n)\n\n";
            }
        }
    }

    // Handle net class assignments
    if( aInput.contains( "net_class_assignments" ) )
    {
        const auto& nca = aInput["net_class_assignments"];

        // Check for replace_all flag - replaces all assignments
        if( nca.contains( "replace_all" ) )
        {
            const auto& assignments = nca["replace_all"];
            code << "# Replace all net class assignments\n";
            code << "sch.netclass.set_assignments([\n";

            bool first = true;
            for( const auto& a : assignments )
            {
                if( !a.contains( "pattern" ) || !a.contains( "netclass" ) )
                    continue;

                code << ( first ? "" : ",\n" );
                code << "    {'pattern': '" << EscapePythonString( a["pattern"].get<std::string>() )
                     << "', 'netclass': '" << EscapePythonString( a["netclass"].get<std::string>() ) << "'}";
                first = false;
            }
            code << "\n])\n\n";
        }

        // Handle add assignments
        if( nca.contains( "add" ) )
        {
            const auto& toAdd = nca["add"];
            for( const auto& a : toAdd )
            {
                if( !a.contains( "pattern" ) || !a.contains( "netclass" ) )
                    continue;

                code << "# Add net class assignment\n";
                code << "sch.netclass.add_assignment('"
                     << EscapePythonString( a["pattern"].get<std::string>() ) << "', '"
                     << EscapePythonString( a["netclass"].get<std::string>() ) << "')\n";
            }
            code << "\n";
        }

        // Handle remove assignments
        if( nca.contains( "remove" ) )
        {
            const auto& toRemove = nca["remove"];
            for( const auto& pattern : toRemove )
            {
                code << "# Remove net class assignment\n";
                code << "sch.netclass.remove_assignment('" << EscapePythonString( pattern.get<std::string>() ) << "')\n";
            }
            code << "\n";
        }
    }

    // Handle bus aliases
    if( aInput.contains( "bus_aliases" ) )
    {
        const auto& ba = aInput["bus_aliases"];

        // Handle deletions first
        if( ba.contains( "delete" ) )
        {
            const auto& toDelete = ba["delete"];
            for( const auto& name : toDelete )
            {
                code << "# Delete bus alias\n";
                code << "sch.bus_alias.delete('" << EscapePythonString( name.get<std::string>() ) << "')\n";
            }
            code << "\n";
        }

        // Handle replace_all - replaces all bus aliases at once
        if( ba.contains( "replace_all" ) )
        {
            const auto& aliases = ba["replace_all"];
            code << "# Replace all bus aliases\n";
            code << "sch.bus_alias.set_all([\n";

            bool first = true;
            for( const auto& alias : aliases )
            {
                if( !alias.contains( "name" ) )
                    continue;

                code << ( first ? "" : ",\n" );
                code << "    {'name': '" << EscapePythonString( alias["name"].get<std::string>() ) << "', 'members': [";

                bool firstMember = true;
                if( alias.contains( "members" ) )
                {
                    for( const auto& member : alias["members"] )
                    {
                        code << ( firstMember ? "" : ", " );
                        code << "'" << EscapePythonString( member.get<std::string>() ) << "'";
                        firstMember = false;
                    }
                }
                code << "]}";
                first = false;
            }
            code << "\n])\n\n";
        }

        // Handle creates
        if( ba.contains( "create" ) )
        {
            const auto& toCreate = ba["create"];
            for( const auto& alias : toCreate )
            {
                if( !alias.contains( "name" ) )
                    continue;

                code << "# Create bus alias\n";
                code << "sch.bus_alias.create('"
                     << EscapePythonString( alias["name"].get<std::string>() ) << "', [";

                bool firstMember = true;
                if( alias.contains( "members" ) )
                {
                    for( const auto& member : alias["members"] )
                    {
                        code << ( firstMember ? "" : ", " );
                        code << "'" << EscapePythonString( member.get<std::string>() ) << "'";
                        firstMember = false;
                    }
                }
                code << "])\n";
            }
            code << "\n";
        }

        // Handle updates
        if( ba.contains( "update" ) )
        {
            const auto& toUpdate = ba["update"];
            for( const auto& alias : toUpdate )
            {
                if( !alias.contains( "name" ) )
                    continue;

                code << "# Update bus alias\n";
                code << "sch.bus_alias.update('"
                     << EscapePythonString( alias["name"].get<std::string>() ) << "', [";

                bool firstMember = true;
                if( alias.contains( "members" ) )
                {
                    for( const auto& member : alias["members"] )
                    {
                        code << ( firstMember ? "" : ", " );
                        code << "'" << EscapePythonString( member.get<std::string>() ) << "'";
                        firstMember = false;
                    }
                }
                code << "])\n";
            }
            code << "\n";
        }
    }

    // Handle text variables
    if( aInput.contains( "text_variables" ) )
    {
        const auto& tv = aInput["text_variables"];

        code << "# Get project for text variables\n";
        code << "project = sch.get_project()\n";
        code << "from kipy.project_types import TextVariables\n";
        code << "from kipy.proto.common.types.base_types_pb2 import MapMergeMode\n";
        code << "\n";

        // Handle replace_all - replaces all text variables
        if( tv.contains( "replace_all" ) )
        {
            const auto& vars = tv["replace_all"];
            code << "# Replace all text variables\n";
            code << "new_vars = TextVariables()\n";
            code << "new_vars.variables = {\n";

            bool first = true;
            for( auto it = vars.begin(); it != vars.end(); ++it )
            {
                code << ( first ? "" : ",\n" );
                code << "    '" << EscapePythonString( it.key() )
                     << "': '" << EscapePythonString( it.value().get<std::string>() ) << "'";
                first = false;
            }
            code << "\n}\n";
            code << "project.set_text_variables(new_vars, MapMergeMode.MMM_REPLACE)\n\n";
        }

        // Handle set - merges with existing variables
        if( tv.contains( "set" ) )
        {
            const auto& vars = tv["set"];
            code << "# Set/merge text variables\n";
            code << "merge_vars = TextVariables()\n";
            code << "merge_vars.variables = {\n";

            bool first = true;
            for( auto it = vars.begin(); it != vars.end(); ++it )
            {
                code << ( first ? "" : ",\n" );
                code << "    '" << EscapePythonString( it.key() )
                     << "': '" << EscapePythonString( it.value().get<std::string>() ) << "'";
                first = false;
            }
            code << "\n}\n";
            code << "project.set_text_variables(merge_vars, MapMergeMode.MMM_MERGE)\n\n";
        }

        // Handle delete - remove specific variables
        if( tv.contains( "delete" ) )
        {
            const auto& toDelete = tv["delete"];
            code << "# Delete text variables\n";
            code << "current_vars = project.get_text_variables()\n";
            code << "updated = dict(current_vars.variables)\n";
            for( const auto& name : toDelete )
            {
                code << "updated.pop('" << EscapePythonString( name.get<std::string>() ) << "', None)\n";
            }
            code << "new_vars = TextVariables()\n";
            code << "new_vars.variables = updated\n";
            code << "project.set_text_variables(new_vars, MapMergeMode.MMM_REPLACE)\n\n";
        }
    }

    code << "print(json.dumps({'status': 'success', 'message': 'Settings updated'}))\n";

    return code.str();
}


std::string SCH_SETUP_HANDLER::EscapePythonString( const std::string& aStr ) const
{
    std::string result;
    for( char c : aStr )
    {
        if( c == '\'' )
            result += "\\'";
        else if( c == '\\' )
            result += "\\\\";
        else if( c == '\n' )
            result += "\\n";
        else if( c == '\r' )
            result += "\\r";
        else if( c == '\t' )
            result += "\\t";
        else
            result += c;
    }
    return result;
}
