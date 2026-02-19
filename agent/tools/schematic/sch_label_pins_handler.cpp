#include "sch_label_pins_handler.h"
#include <sstream>


bool SCH_LABEL_PINS_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_label_pins";
}


std::string SCH_LABEL_PINS_HANDLER::Execute( const std::string& aToolName,
                                              const nlohmann::json& aInput )
{
    return "Error: sch_label_pins requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_LABEL_PINS_HANDLER::GetDescription( const std::string& aToolName,
                                                     const nlohmann::json& aInput ) const
{
    std::string ref = aInput.value( "ref", "?" );
    int count = 0;

    if( aInput.contains( "labels" ) && aInput["labels"].is_object() )
        count = static_cast<int>( aInput["labels"].size() );

    if( count > 0 )
        return "Labeling " + std::to_string( count ) + " pins on " + ref;

    return "Labeling pins on " + ref;
}


bool SCH_LABEL_PINS_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_label_pins";
}


std::string SCH_LABEL_PINS_HANDLER::GetIPCCommand( const std::string& aToolName,
                                                    const nlohmann::json& aInput ) const
{
    return "run_shell sch " + GenerateLabelPinsCode( aInput );
}


std::string SCH_LABEL_PINS_HANDLER::EscapePythonString( const std::string& aStr ) const
{
    std::string result;
    result.reserve( aStr.size() + 10 );

    for( char c : aStr )
    {
        switch( c )
        {
        case '\\': result += "\\\\"; break;
        case '\'': result += "\\'"; break;
        case '\"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:   result += c; break;
        }
    }

    return result;
}


std::string SCH_LABEL_PINS_HANDLER::GenerateLabelPinsCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string ref = aInput.value( "ref", "" );
    std::string labelType = aInput.value( "label_type", "local" );
    std::string hAlignOverride = aInput.value( "h_align", "" );
    std::string vAlignOverride = aInput.value( "v_align", "" );

    // Build the pin->label map as a Python dict literal
    std::ostringstream labelsDict;
    labelsDict << "{";
    bool first = true;

    if( aInput.contains( "labels" ) && aInput["labels"].is_object() )
    {
        for( auto it = aInput["labels"].begin(); it != aInput["labels"].end(); ++it )
        {
            if( !first )
                labelsDict << ", ";

            labelsDict << "'" << EscapePythonString( it.key() ) << "': '"
                       << EscapePythonString( it.value().get<std::string>() ) << "'";
            first = false;
        }
    }

    labelsDict << "}";

    code << "import json, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "from kipy.proto.common.types.enums_pb2 import HA_LEFT, HA_RIGHT, VA_TOP, VA_BOTTOM\n";
    code << "from kipy.schematic_types import LocalLabel, GlobalLabel, HierarchicalLabel\n";
    code << "\n";

    // Refresh preamble
    code << "# Refresh document to handle close/reopen cycles\n";
    code << "if hasattr(sch, 'refresh_document'):\n";
    code << "    if not sch.refresh_document():\n";
    code << "        raise RuntimeError('Schematic editor not open or document not available')\n";
    code << "\n";

    code << "ref = '" << EscapePythonString( ref ) << "'\n";
    code << "label_type = '" << EscapePythonString( labelType ) << "'\n";
    code << "pin_labels = " << labelsDict.str() << "\n";
    code << "h_align_override = " << ( hAlignOverride.empty() ? "None" : ( "'" + hAlignOverride + "'" ) ) << "\n";
    code << "v_align_override = " << ( vAlignOverride.empty() ? "None" : ( "'" + vAlignOverride + "'" ) ) << "\n";
    code << "\n";

    // Overlap detection preamble — collect existing label bounding boxes (0 margin)
    code << "# Collect existing label bounding boxes for overlap detection\n";
    code << "placed_bboxes = []\n";
    code << "try:\n";
    code << "    for _elbl in sch.labels.get_all():\n";
    code << "        try:\n";
    code << "            _ebb = sch.transform.get_bounding_box(_elbl, units='mm', include_text=False)\n";
    code << "        except:\n";
    code << "            continue\n";
    code << "        if _ebb:\n";
    code << "            placed_bboxes.append({'ref': getattr(_elbl, 'text', '?'), 'min_x': _ebb['min_x'], 'max_x': _ebb['max_x'], 'min_y': _ebb['min_y'], 'max_y': _ebb['max_y']})\n";
    code << "except:\n";
    code << "    pass\n";
    code << "\n";
    code << "def _bboxes_overlap(a, b):\n";
    code << "    return a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and a['min_y'] < b['max_y'] and a['max_y'] > b['min_y']\n";
    code << "\n";
    code << "def _point_in_bbox(px, py, bb):\n";
    code << "    \"\"\"Check if a point is inside a bounding box (for pin-on-pin exclusion).\"\"\"\n";
    code << "    return bb['min_x'] <= px <= bb['max_x'] and bb['min_y'] <= py <= bb['max_y']\n";
    code << "\n";

    code << "results = []\n";
    code << "try:\n";
    code << "    sym = sch.symbols.get_by_ref(ref)\n";
    code << "    if not sym:\n";
    code << "        raise ValueError(f'Symbol not found: {ref}')\n";
    code << "\n";

    // Rotation and mirroring transform for pin orientation
    code << "    # Orientation transform: apply symbol rotation and mirroring\n";
    code << "    _rot90 = {0: 2, 1: 3, 2: 1, 3: 0}\n";
    code << "    _rot_steps = round(getattr(sym, 'angle', 0) / 90) % 4\n";
    code << "\n";
    code << "    def transform_orientation(orient):\n";
    code << "        o = orient\n";
    code << "        for _ in range(_rot_steps):\n";
    code << "            o = _rot90.get(o, o)\n";
    code << "        # Apply mirroring: mirror_x flips left<->right, mirror_y flips up<->down\n";
    code << "        if getattr(sym, 'mirror_x', False):\n";
    code << "            if o == 0: o = 1\n";
    code << "            elif o == 1: o = 0\n";
    code << "        if getattr(sym, 'mirror_y', False):\n";
    code << "            if o == 2: o = 3\n";
    code << "            elif o == 3: o = 2\n";
    code << "        return o\n";
    code << "\n";

    // Label creation function
    code << "    def create_label(text, position, h_align, v_align):\n";
    code << "        if label_type == 'global':\n";
    code << "            lbl = GlobalLabel.create(position, text)\n";
    code << "        elif label_type == 'hierarchical':\n";
    code << "            lbl = HierarchicalLabel.create(position, text)\n";
    code << "        else:\n";
    code << "            lbl = LocalLabel.create(position, text)\n";
    code << "        lbl._proto.text.attributes.horizontal_alignment = h_align\n";
    code << "        lbl._proto.text.attributes.vertical_alignment = v_align\n";
    code << "        created = sch.crud.create_items(lbl)\n";
    code << "        return created[0] if created else lbl\n";
    code << "\n";

    // Process each pin
    code << "    for pin_id, label_text in pin_labels.items():\n";
    code << "        try:\n";
    code << "            pin_result = sch.symbols.get_transformed_pin_position(sym, pin_id)\n";
    code << "            if not pin_result:\n";
    code << "                pin_pos = sch.symbols.get_pin_position(sym, pin_id)\n";
    code << "                if not pin_pos:\n";
    code << "                    results.append({'pin': pin_id, 'label': label_text, 'error': f'Pin not found: {pin_id}'})\n";
    code << "                    continue\n";
    code << "                px = pin_pos.x / 1_000_000\n";
    code << "                py = pin_pos.y / 1_000_000\n";
    code << "                orient = None\n";
    code << "            else:\n";
    code << "                px = pin_result['position'].x / 1_000_000\n";
    code << "                py = pin_result['position'].y / 1_000_000\n";
    code << "                orient = pin_result.get('orientation', None)\n";
    code << "\n";

    // Determine escape direction and justification
    code << "            # Transform orientation and compute escape direction\n";
    code << "            if orient is not None:\n";
    code << "                orient = transform_orientation(orient)\n";
    code << "                # orient 0=PIN_RIGHT(body), 1=PIN_LEFT(body), 2=PIN_UP(body), 3=PIN_DOWN(body)\n";
    code << "                if orient == 0:    # escape left\n";
    code << "                    h_align, v_align = HA_RIGHT, VA_BOTTOM\n";
    code << "                    direction = 'left'\n";
    code << "                elif orient == 1:  # escape right\n";
    code << "                    h_align, v_align = HA_LEFT, VA_BOTTOM\n";
    code << "                    direction = 'right'\n";
    code << "                elif orient == 2:  # escape down\n";
    code << "                    h_align, v_align = HA_LEFT, VA_TOP\n";
    code << "                    direction = 'down'\n";
    code << "                elif orient == 3:  # escape up\n";
    code << "                    h_align, v_align = HA_LEFT, VA_BOTTOM\n";
    code << "                    direction = 'up'\n";
    code << "                else:\n";
    code << "                    h_align, v_align = HA_LEFT, VA_BOTTOM\n";
    code << "                    direction = 'right'\n";
    code << "            else:\n";
    code << "                # Fallback: guess from symbol center\n";
    code << "                sym_cx = sym.position.x / 1_000_000\n";
    code << "                sym_cy = sym.position.y / 1_000_000\n";
    code << "                if px > sym_cx:\n";
    code << "                    h_align, v_align = HA_LEFT, VA_BOTTOM\n";
    code << "                    direction = 'right'\n";
    code << "                else:\n";
    code << "                    h_align, v_align = HA_RIGHT, VA_BOTTOM\n";
    code << "                    direction = 'left'\n";
    code << "\n";

    // Apply alignment overrides if specified
    code << "            # Apply alignment overrides\n";
    code << "            if h_align_override is not None:\n";
    code << "                h_align = HA_LEFT if h_align_override == 'left' else HA_RIGHT\n";
    code << "            if v_align_override is not None:\n";
    code << "                v_align = VA_TOP if v_align_override == 'top' else VA_BOTTOM\n";
    code << "\n";

    // Place label at pin tip, then check for overlap
    code << "            label_pos = Vector2.from_xy_mm(px, py)\n";
    code << "            _lbl = create_label(label_text, label_pos, h_align, v_align)\n";
    code << "\n";
    code << "            # Overlap detection (skip bboxes whose pin point coincides with ours)\n";
    code << "            _rejected = False\n";
    code << "            try:\n";
    code << "                _bb = sch.transform.get_bounding_box(_lbl, units='mm', include_text=False)\n";
    code << "                if _bb:\n";
    code << "                    _new_bbox = {'min_x': _bb['min_x'], 'max_x': _bb['max_x'], 'min_y': _bb['min_y'], 'max_y': _bb['max_y']}\n";
    code << "                    for _pb in placed_bboxes:\n";
    code << "                        if _bboxes_overlap(_new_bbox, _pb):\n";
    code << "                            # Allow overlap if the existing label's bbox contains our pin tip\n";
    code << "                            # (pin-on-pin connection is intentional)\n";
    code << "                            if _point_in_bbox(px, py, _pb):\n";
    code << "                                continue\n";
    code << "                            sch.crud.remove_items([_lbl])\n";
    code << "                            results.append({'pin': pin_id, 'label': label_text, 'error': f\"Placement rejected: overlaps existing label '{_pb.get(\\\"ref\\\", \\\"?\\\")}'\" })\n";
    code << "                            _rejected = True\n";
    code << "                            break\n";
    code << "                    if not _rejected:\n";
    code << "                        placed_bboxes.append({'ref': label_text, **_new_bbox})\n";
    code << "            except:\n";
    code << "                pass\n";
    code << "            if not _rejected:\n";
    code << "                results.append({'pin': pin_id, 'label': label_text, 'position': [round(px, 2), round(py, 2)], 'direction': direction})\n";
    code << "\n";
    code << "        except Exception as e:\n";
    code << "            results.append({'pin': pin_id, 'label': label_text, 'error': str(e)})\n";
    code << "\n";

    code << "except Exception as e:\n";
    code << "    results = [{'error': str(e)}]\n";
    code << "\n";

    // Auto-sync sheet pins when placing hierarchical labels
    if( labelType == "hierarchical" )
    {
        code << "# Sync sheet pins on parent sheet to match hierarchical labels\n";
        code << "try:\n";
        code << "    sch.sheets.sync_pins()\n";
        code << "except:\n";
        code << "    pass\n";
        code << "\n";
    }

    code << "print(json.dumps({'status': 'success', 'ref': ref, 'labels_placed': len([r for r in results if 'error' not in r]), 'labels_failed': len([r for r in results if 'error' in r]), 'results': results}, indent=2))\n";

    return code.str();
}
