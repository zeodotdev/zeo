/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */


/**
 * @file pcbnew/cross-probing.cpp
 * @brief Cross probing functions to handle communication to and from Eeschema.
 * Handle messages between Pcbnew and Eeschema via a socket, the port numbers are
 * KICAD_PCB_PORT_SERVICE_NUMBER (currently 4242) (Eeschema to Pcbnew)
 * KICAD_SCH_PORT_SERVICE_NUMBER (currently 4243) (Pcbnew to Eeschema)
 * Note: these ports must be enabled for firewall protection
 */

#include <wx/tokenzr.h>
#include <board.h>
#include <board_design_settings.h>
#include <fmt.h>
#include <wx/log.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_track.h>
#include <pcb_group.h>
#include <zone.h>
#include <collectors.h>
#include <eda_dde.h>
#include <kiface_base.h>
#include <kiway_mail.h>
#include <string_utils.h>
#include <netlist_reader/pcb_netlist.h>
#include <netlist_reader/board_netlist_updater.h>
#include <gal/painter.h>
#include <pcb_edit_frame.h>
#include <pcbnew_settings.h>
#include <render_settings.h>
#include <richio.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <trace_helpers.h>
#include <netlist_reader/netlist_reader.h>
#include <widgets/pcb_design_block_pane.h>
#include <widgets/kistatusbar.h>
#include <project_pcb.h>
#include <footprint_library_adapter.h>
#include <wx/log.h>
#include <nlohmann/json.hpp>
#include <id.h>
#include <dialogs/dialog_settings_diff.h>
#include <diff_manager.h>
#include <agent_change_tracker.h>
#include <settings/settings_manager.h>
#include <pgm_base.h>
#include <python_scripting.h> // Fixed include path
#include "pcb_plotter.h"
#include <reporter.h>
#include <specctra_import_export/specctra.h>
#include <set>
// #include <Python.h> // Included by python_scripting.h if KICAD_SCRIPTING is defined


/* Execute a remote command sent via a socket on port KICAD_PCB_PORT_SERVICE_NUMBER
 *
 * Commands are:
 *
 * $NET: "net name"               Highlight the given net
 * $NETS: "net name 1,net name 2" Highlight all given nets
 * $CLEAR                         Clear existing highlight
 *
 * $CONFIG       Show the Manage Footprint Libraries dialog
 * $CUSTOM_RULES Show the "Custom Rules" page of the Board Setup dialog
 * $DRC          Show the DRC dialog
 */
void PCB_EDIT_FRAME::ExecuteRemoteCommand( const char* cmdline )
{
    char   line[1024];
    char*  idcmd;
    char*  text;
    int    netcode = -1;
    bool   multiHighlight = false;
    BOARD* pcb = GetBoard();

    CROSS_PROBING_SETTINGS& crossProbingSettings = GetPcbNewSettings()->m_CrossProbing;

    KIGFX::VIEW*            view = m_toolManager->GetView();
    KIGFX::RENDER_SETTINGS* renderSettings = view->GetPainter()->GetSettings();

    strncpy( line, cmdline, sizeof( line ) - 1 );
    line[sizeof( line ) - 1] = 0;

    idcmd = strtok( line, " \n\r" );
    text = strtok( nullptr, "\"\n\r" );

    if( idcmd == nullptr )
        return;

    if( strcmp( idcmd, "$CONFIG" ) == 0 )
    {
        GetToolManager()->RunAction( ACTIONS::showFootprintLibTable );
        return;
    }
    else if( strcmp( idcmd, "$CUSTOM_RULES" ) == 0 )
    {
        ShowBoardSetupDialog( _( "Custom Rules" ) );
        return;
    }
    else if( strcmp( idcmd, "$DRC" ) == 0 )
    {
        GetToolManager()->RunAction( PCB_ACTIONS::runDRC );
        return;
    }
    else if( strcmp( idcmd, "$CLEAR" ) == 0 )
    {
        if( renderSettings->IsHighlightEnabled() )
        {
            renderSettings->SetHighlight( false );
            view->UpdateAllLayersColor();
        }

        if( pcb->IsHighLightNetON() )
        {
            pcb->ResetNetHighLight();
            SetMsgPanel( pcb );
        }

        GetCanvas()->Refresh();
        return;
    }
    else if( strcmp( idcmd, "$NET:" ) == 0 )
    {
        if( !crossProbingSettings.auto_highlight )
            return;

        wxString net_name = From_UTF8( text );

        NETINFO_ITEM* netinfo = pcb->FindNet( net_name );

        if( netinfo )
        {
            netcode = netinfo->GetNetCode();

            std::vector<MSG_PANEL_ITEM> items;
            netinfo->GetMsgPanelInfo( this, items );
            SetMsgPanel( items );
        }

        // fall through to highlighting section
    }
    else if( strcmp( idcmd, "$NETS:" ) == 0 )
    {
        if( !crossProbingSettings.auto_highlight )
            return;

        wxStringTokenizer netsTok = wxStringTokenizer( From_UTF8( text ), ",", wxTOKEN_STRTOK );
        bool              first = true;

        while( netsTok.HasMoreTokens() )
        {
            NETINFO_ITEM* netinfo = pcb->FindNet( netsTok.GetNextToken().Trim( true ).Trim( false ) );

            if( netinfo )
            {
                if( first )
                {
                    // TODO: Once buses are included in netlist, show bus name
                    std::vector<MSG_PANEL_ITEM> items;
                    netinfo->GetMsgPanelInfo( this, items );
                    SetMsgPanel( items );
                    first = false;

                    pcb->SetHighLightNet( netinfo->GetNetCode() );
                    renderSettings->SetHighlight( true, netinfo->GetNetCode() );
                    multiHighlight = true;
                }
                else
                {
                    pcb->SetHighLightNet( netinfo->GetNetCode(), true );
                    renderSettings->SetHighlight( true, netinfo->GetNetCode(), true );
                }
            }
        }

        netcode = -1;

        // fall through to highlighting section
    }

    BOX2I bbox;

    if( netcode > 0 || multiHighlight )
    {
        if( !multiHighlight )
        {
            renderSettings->SetHighlight( ( netcode >= 0 ), netcode );
            pcb->SetHighLightNet( netcode );
        }
        else
        {
            // Just pick the first one for area calculation
            netcode = *pcb->GetHighLightNetCodes().begin();
        }

        pcb->HighLightON();

        auto merge_area = [netcode, &bbox]( BOARD_CONNECTED_ITEM* aItem )
        {
            if( aItem->GetNetCode() == netcode )
                bbox.Merge( aItem->GetBoundingBox() );
        };

        if( crossProbingSettings.center_on_items )
        {
            for( ZONE* zone : pcb->Zones() )
                merge_area( zone );

            for( PCB_TRACK* track : pcb->Tracks() )
                merge_area( track );

            for( FOOTPRINT* fp : pcb->Footprints() )
            {
                for( PAD* p : fp->Pads() )
                    merge_area( p );
            }
        }
    }
    else
    {
        renderSettings->SetHighlight( false );
    }

    if( crossProbingSettings.center_on_items && bbox.GetWidth() != 0 && bbox.GetHeight() != 0 )
    {
        if( crossProbingSettings.zoom_to_fit )
            GetToolManager()->GetTool<PCB_SELECTION_TOOL>()->ZoomFitCrossProbeBBox( bbox );

        FocusOnLocation( bbox.Centre() );
    }

    view->UpdateAllLayersColor();

    // Ensure the display is refreshed, because in some installs the refresh is done only
    // when the gal canvas has the focus, and that is not the case when crossprobing from
    // Eeschema:
    GetCanvas()->Refresh();
}


std::string FormatProbeItem( BOARD_ITEM* aItem )
{
    if( !aItem )
        return "$CLEAR: \"HIGHLIGHTED\""; // message to clear highlight state

    switch( aItem->Type() )
    {
    case PCB_FOOTPRINT_T:
    {
        FOOTPRINT* footprint = static_cast<FOOTPRINT*>( aItem );
        return fmt::format( "$PART: \"{}\"", TO_UTF8( footprint->GetReference() ) );
    }

    case PCB_PAD_T:
    {
        PAD*       pad = static_cast<PAD*>( aItem );
        FOOTPRINT* footprint = pad->GetParentFootprint();

        return fmt::format( "$PART: \"{}\" $PAD: \"{}\"", TO_UTF8( footprint->GetReference() ),
                            TO_UTF8( pad->GetNumber() ) );
    }

    case PCB_FIELD_T:
    {
        PCB_FIELD*  field = static_cast<PCB_FIELD*>( aItem );
        FOOTPRINT*  footprint = field->GetParentFootprint();
        const char* text_key;

        /* This can't be a switch since the break need to pull out
         * from the outer switch! */
        if( field->IsReference() )
            text_key = "$REF:";
        else if( field->IsValue() )
            text_key = "$VAL:";
        else
            break;

        return fmt::format( "$PART: \"{}\" {} \"{}\"", TO_UTF8( footprint->GetReference() ), text_key,
                            TO_UTF8( field->GetText() ) );
    }

    default: break;
    }

    return "";
}


template <typename ItemContainer>
void collectItemsForSyncParts( ItemContainer& aItems, std::set<wxString>& parts )
{
    for( EDA_ITEM* item : aItems )
    {
        switch( item->Type() )
        {
        case PCB_GROUP_T:
        {
            PCB_GROUP* group = static_cast<PCB_GROUP*>( item );

            collectItemsForSyncParts( group->GetItems(), parts );
            break;
        }
        case PCB_FOOTPRINT_T:
        {
            FOOTPRINT* footprint = static_cast<FOOTPRINT*>( item );
            wxString   ref = footprint->GetReference();

            parts.emplace( wxT( "F" ) + EscapeString( ref, CTX_IPC ) );
            break;
        }

        case PCB_PAD_T:
        {
            PAD*     pad = static_cast<PAD*>( item );
            wxString ref = pad->GetParentFootprint()->GetReference();

            parts.emplace( wxT( "P" ) + EscapeString( ref, CTX_IPC ) + wxT( "/" )
                           + EscapeString( pad->GetNumber(), CTX_IPC ) );
            break;
        }

        default: break;
        }
    }
}


void PCB_EDIT_FRAME::SendSelectItemsToSch( const std::deque<EDA_ITEM*>& aItems, EDA_ITEM* aFocusItem, bool aForce )
{
    std::string command = "$SELECT: ";

    if( aFocusItem )
    {
        std::deque<EDA_ITEM*> focusItems = { aFocusItem };
        std::set<wxString>    focusParts;
        collectItemsForSyncParts( focusItems, focusParts );

        if( focusParts.size() > 0 )
        {
            command += "1,";
            command += *focusParts.begin();
            command += ",";
        }
        else
        {
            command += "0,";
        }
    }
    else
    {
        command += "0,";
    }

    std::set<wxString> parts;
    collectItemsForSyncParts( aItems, parts );

    if( parts.empty() )
        return;

    for( wxString part : parts )
    {
        command += part;
        command += ",";
    }

    command.pop_back();

    if( Kiface().IsSingle() )
    {
        SendCommand( MSG_TO_SCH, command );
    }
    else
    {
        // Typically ExpressMail is going to be s-expression packets, but since
        // we have existing interpreter of the selection packet on the other
        // side in place, we use that here.
        Kiway().ExpressMail( FRAME_SCH, aForce ? MAIL_SELECTION_FORCE : MAIL_SELECTION, command, this );
    }
}


void PCB_EDIT_FRAME::SendCrossProbeNetName( const wxString& aNetName )
{
    std::string packet = fmt::format( "$NET: \"{}\"", TO_UTF8( aNetName ) );

    if( !packet.empty() )
    {
        if( Kiface().IsSingle() )
        {
            SendCommand( MSG_TO_SCH, packet );
        }
        else
        {
            // Typically ExpressMail is going to be s-expression packets, but since
            // we have existing interpreter of the cross probe packet on the other
            // side in place, we use that here.
            Kiway().ExpressMail( FRAME_SCH, MAIL_CROSS_PROBE, packet, this );
        }
    }
}


void PCB_EDIT_FRAME::SendCrossProbeItem( BOARD_ITEM* aSyncItem )
{
    std::string packet = FormatProbeItem( aSyncItem );

    if( !packet.empty() )
    {
        if( Kiface().IsSingle() )
        {
            SendCommand( MSG_TO_SCH, packet );
        }
        else
        {
            // Typically ExpressMail is going to be s-expression packets, but since
            // we have existing interpreter of the cross probe packet on the other
            // side in place, we use that here.
            Kiway().ExpressMail( FRAME_SCH, MAIL_CROSS_PROBE, packet, this );
        }
    }
}


std::vector<BOARD_ITEM*> PCB_EDIT_FRAME::FindItemsFromSyncSelection( std::string syncStr )
{
    wxArrayString syncArray = wxStringTokenize( syncStr, "," );

    std::vector<std::pair<int, BOARD_ITEM*>> orderPairs;

    for( FOOTPRINT* footprint : GetBoard()->Footprints() )
    {
        if( footprint == nullptr )
            continue;

        wxString fpSheetPath = footprint->GetPath().AsString().BeforeLast( '/' );
        wxString fpUUID = footprint->m_Uuid.AsString();

        if( fpSheetPath.IsEmpty() )
            fpSheetPath += '/';

        if( fpUUID.empty() )
            continue;

        wxString fpRefEscaped = EscapeString( footprint->GetReference(), CTX_IPC );

        for( unsigned index = 0; index < syncArray.size(); ++index )
        {
            wxString syncEntry = syncArray[index];

            if( syncEntry.empty() )
                continue;

            wxString syncData = syncEntry.substr( 1 );

            switch( syncEntry.GetChar( 0 ).GetValue() )
            {
            case 'S': // Select sheet with subsheets: S<Sheet path>
                if( fpSheetPath.StartsWith( syncData ) )
                {
                    orderPairs.emplace_back( index, footprint );
                }
                break;
            case 'F': // Select footprint: F<Reference>
                if( syncData == fpRefEscaped )
                {
                    orderPairs.emplace_back( index, footprint );
                }
                break;
            case 'P': // Select pad: P<Footprint reference>/<Pad number>
            {
                if( syncData.StartsWith( fpRefEscaped ) )
                {
                    wxString selectPadNumberEscaped = syncData.substr( fpRefEscaped.size() + 1 ); // Skips the slash

                    wxString selectPadNumber = UnescapeString( selectPadNumberEscaped );

                    for( PAD* pad : footprint->Pads() )
                    {
                        if( selectPadNumber == pad->GetNumber() )
                        {
                            orderPairs.emplace_back( index, pad );
                        }
                    }
                }
                break;
            }
            default: break;
            }
        }
    }

    std::sort( orderPairs.begin(), orderPairs.end(),
               []( const std::pair<int, BOARD_ITEM*>& a, const std::pair<int, BOARD_ITEM*>& b ) -> bool
               {
                   return a.first < b.first;
               } );

    std::vector<BOARD_ITEM*> items;
    items.reserve( orderPairs.size() );

    for( const std::pair<int, BOARD_ITEM*>& pair : orderPairs )
        items.push_back( pair.second );

    return items;
}


void PCB_EDIT_FRAME::KiwayMailIn( KIWAY_MAIL_EVENT& mail )
{
    std::string& payload = mail.GetPayload();

    switch( mail.Command() )
    {
    case MAIL_PCB_GET_NETLIST:
    {
        NETLIST          netlist;
        STRING_FORMATTER sf;

        for( FOOTPRINT* footprint : GetBoard()->Footprints() )
        {
            if( footprint->GetAttributes() & FP_BOARD_ONLY )
                continue; // Don't add board-only footprints to the netlist

            COMPONENT* component = new COMPONENT( footprint->GetFPID(), footprint->GetReference(),
                                                  footprint->GetValue(), footprint->GetPath(), {} );

            for( PAD* pad : footprint->Pads() )
            {
                const wxString& netname = pad->GetShortNetname();

                if( !netname.IsEmpty() )
                {
                    component->AddNet( pad->GetNumber(), netname, pad->GetPinFunction(), pad->GetPinType() );
                }
            }

            nlohmann::ordered_map<wxString, wxString> fields;

            for( PCB_FIELD* field : footprint->GetFields() )
            {
                wxCHECK2( field, continue );

                fields[field->GetCanonicalName()] = field->GetText();
            }

            component->SetFields( fields );

            // Add DNP and Exclude from BOM properties
            std::map<wxString, wxString> properties;

            if( footprint->GetAttributes() & FP_DNP )
                properties.emplace( "dnp", "" );

            if( footprint->GetAttributes() & FP_EXCLUDE_FROM_BOM )
                properties.emplace( "exclude_from_bom", "" );

            component->SetProperties( properties );

            netlist.AddComponent( component );
        }

        netlist.Format( "pcb_netlist", &sf, 0, CTL_OMIT_FILTERS );
        payload = sf.GetString();
        break;
    }

    case MAIL_PCB_UPDATE_LINKS:
        try
        {
            NETLIST netlist;
            FetchNetlistFromSchematic( netlist, wxEmptyString );

            BOARD_NETLIST_UPDATER updater( this, GetBoard() );
            updater.SetLookupByTimestamp( false );
            updater.SetDeleteUnusedFootprints( false );
            updater.SetReplaceFootprints( false );
            updater.SetTransferGroups( false );
            updater.UpdateNetlist( netlist );

            bool dummy;
            OnNetlistChanged( updater, &dummy );
        }
        catch( const IO_ERROR& )
        {
            assert( false ); // should never happen
            return;
        }

        break;


    case MAIL_CROSS_PROBE: ExecuteRemoteCommand( payload.c_str() ); break;

    case MAIL_AGENT_REQUEST:
    {
        std::string safePayload = payload;
        // Sanitization for smart quotes and other common copy-paste artifacts
        // UTF-8 sequences:
        // Left Double Quote: \xe2\x80\x9c -> "
        // Right Double Quote: \xe2\x80\x9d -> "
        // Left Single Quote: \xe2\x80\x98 -> '
        // Right Single Quote: \xe2\x80\x99 -> '
        auto replaceAll = [&]( std::string& str, const std::string& from, const std::string& to )
        {
            size_t start_pos = 0;
            while( ( start_pos = str.find( from, start_pos ) ) != std::string::npos )
            {
                str.replace( start_pos, from.length(), to );
                start_pos += to.length();
            }
        };

        replaceAll( safePayload, "\xe2\x80\x9c", "\"" );
        replaceAll( safePayload, "\xe2\x80\x9d", "\"" );
        replaceAll( safePayload, "\xe2\x80\x98", "'" );
        replaceAll( safePayload, "\xe2\x80\x99", "'" );

        nlohmann::json j_in = nlohmann::json::parse( safePayload, nullptr, false );
        if( !j_in.is_discarded() )
        {
            if( j_in.contains( "type" ) && j_in["type"] == "python" && j_in.contains( "code" ) )
            {
#ifdef KICAD_SCRIPTING_WXPYTHON
                // Execute Python Code
                std::string code = j_in["code"];
                std::string result;

                // Record the current undo position before Python execution
                RecordAgentUndoPosition();

                // We need to capture stdout/stderr.
                // Approach:
                // 1. Redirect stdout/stderr to StringIO
                // 2. Exec code
                // 3. Get value
                // 4. Restore

                // We use a unique variable name for capture to avoid collisions
                std::string wrapper = "import sys\n"
                                      "from io import StringIO\n"
                                      "_agent_capture = StringIO()\n"
                                      "_agent_restore_out = sys.stdout\n"
                                      "_agent_restore_err = sys.stderr\n"
                                      "sys.stdout = _agent_capture\n"
                                      "sys.stderr = _agent_capture\n"
                                      "try:\n"
                                      "    exec(\"\"\""
                                      + code
                                      + "\"\"\")\n" // Triple quote for safety? user code might contain quotes.
                                        "except SystemExit as e:\n"
                                        "    print(f'Script exited (code={e.code})')\n"
                                        "except KeyboardInterrupt:\n"
                                        "    print('KeyboardInterrupt')\n"
                                        "except Exception as e:\n"
                                        "    import traceback\n"
                                        "    traceback.print_exc()\n"
                                        "finally:\n"
                                        "    sys.stdout = _agent_restore_out\n"
                                        "    sys.stderr = _agent_restore_err\n"
                                        "_agent_result = _agent_capture.getvalue()\n";

                // Check if Python is initialized and scripting is available
                if( !Py_IsInitialized() )
                {
                    result = "Error: Python interpreter not initialized.";
                    ClearAgentPendingChanges();
                }
                else if( SCRIPTING::IsWxAvailable() )
                {
                    // Acquire the GIL before calling Python functions
                    PyLOCK lock;

                    // Use PyRun_StringFlags instead of PyRun_SimpleString so we can
                    // intercept SystemExit before PyErr_Print() calls Py_Exit()
                    PyObject* main_module = PyImport_AddModule( "__main__" );
                    PyObject* main_dict = main_module ? PyModule_GetDict( main_module )
                                                      : nullptr;

                    if( main_dict )
                    {
                        PyObject* pyResult = PyRun_StringFlags( wrapper.c_str(),
                                Py_file_input, main_dict, main_dict, nullptr );

                        if( pyResult )
                        {
                            Py_DECREF( pyResult );
                        }
                        else if( PyErr_Occurred() )
                        {
                            if( PyErr_ExceptionMatches( PyExc_SystemExit ) )
                                PyErr_Clear();
                            else
                                PyErr_Print();
                        }
                    }
                    PyObject* res_obj = PyDict_GetItemString( main_dict, "_agent_result" );
                    if( res_obj )
                    {
                        const char* res_str = PyUnicode_AsUTF8( res_obj );
                        if( res_str )
                            result = res_str;
                    }

                    // Detect changes and show diff overlay if any
                    DetectAgentChanges();
                }
                else
                {
                    result = "Error: Python Scripting is not available.";
                    // Clean up snapshot since we didn't execute
                    ClearAgentPendingChanges();
                }

                Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, result );
#else
                std::string err = "Error: Scripting not enabled.";
                Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, err );
#endif
                break;  // Exit after handling Python execution
            }
            else if( j_in.contains( "type" ) && j_in["type"] == "take_snapshot" )
            {
                // Record the current undo position before kipy execution
                fprintf( stderr, "PCB_EDIT_FRAME: Received take_snapshot request\n" );
                fflush( stderr );
                RecordAgentUndoPosition();
                break;  // No response needed
            }
            else if( j_in.contains( "type" ) && j_in["type"] == "detect_changes" )
            {
                // Detect changes after kipy execution and show diff overlay
                fprintf( stderr, "PCB_EDIT_FRAME: Received detect_changes request\n" );
                fflush( stderr );
                DetectAgentChanges();
                break;  // No response needed
            }
            else if( j_in.contains( "type" ) && j_in["type"] == "export_screenshot" )
            {
                // Export the current in-memory PCB to SVG for screenshot
                std::string outputDir = j_in.value( "output_dir", "" );
                wxString    svgPath = wxString::FromUTF8( outputDir )
                                      + wxFileName::GetPathSeparator() + wxT( "pcb.svg" );

                PCB_PLOT_PARAMS plotOpts;
                plotOpts.SetFormat( PLOT_FORMAT::SVG );
                plotOpts.SetPlotFrameRef( false );
                plotOpts.SetSvgFitPageToBoard( true );
                plotOpts.SetBlackAndWhite( false );
                plotOpts.SetMirror( false );
                plotOpts.SetNegative( false );
                plotOpts.SetScale( 1.0 );
                plotOpts.SetAutoScale( false );
                plotOpts.SetDrillMarksType( DRILL_MARKS::FULL_DRILL_SHAPE );
                plotOpts.SetPlotReference( true );
                plotOpts.SetPlotValue( true );

                COLOR_SETTINGS* colors =
                        ::GetColorSettings( wxT( "_builtin_default" ) );
                plotOpts.SetColorSettings( colors );

                LSEQ layers;
                layers.push_back( F_Cu );
                layers.push_back( B_Cu );
                layers.push_back( F_SilkS );
                layers.push_back( B_SilkS );
                layers.push_back( Edge_Cuts );

                PCB_PLOTTER plotter( GetBoard(), &NULL_REPORTER::GetInstance(), plotOpts );
                bool        success =
                        plotter.Plot( svgPath, layers, LSEQ(), false, true );

                nlohmann::json resp;
                resp["type"] = "export_screenshot_response";
                resp["svg_path"] = svgPath.ToStdString();
                resp["success"] = success;

                std::string respStr = resp.dump();
                Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, respStr, this );
                break;
            }
            else if( j_in.contains( "type" ) && j_in["type"] == "export_specctra_dsn" )
            {
                // Export the current board to Specctra DSN format for autorouting
                std::string outputPath = j_in.value( "output_path", "" );

                nlohmann::json resp;
                resp["type"] = "export_specctra_dsn_response";

                if( outputPath.empty() )
                {
                    resp["success"] = false;
                    resp["error"] = "Missing output_path parameter";
                }
                else
                {
                    try
                    {
                        DSN::ExportBoardToSpecctraFile( GetBoard(),
                                                        wxString::FromUTF8( outputPath ) );
                        resp["success"] = true;
                        resp["output_path"] = outputPath;
                    }
                    catch( const IO_ERROR& e )
                    {
                        resp["success"] = false;
                        resp["error"] = e.What().ToStdString();
                    }
                    catch( const std::exception& e )
                    {
                        resp["success"] = false;
                        resp["error"] = e.what();
                    }
                }

                std::string respStr = resp.dump();
                Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, respStr, this );
                break;
            }
            else if( j_in.contains( "type" ) && j_in["type"] == "import_specctra_session" )
            {
                // Import a Specctra session file (SES) with routing results
                std::string inputPath = j_in.value( "input_path", "" );

                nlohmann::json resp;
                resp["type"] = "import_specctra_session_response";

                if( inputPath.empty() )
                {
                    resp["success"] = false;
                    resp["error"] = "Missing input_path parameter";
                }
                else
                {
                    try
                    {
                        BOARD* board = GetBoard();

                        // Remember existing track KIIDs before import (for identifying new tracks)
                        std::set<KIID> existingTrackIds;
                        for( PCB_TRACK* track : board->Tracks() )
                        {
                            existingTrackIds.insert( track->m_Uuid );
                        }

                        // Count tracks/vias before import for statistics
                        int tracksBefore = 0;
                        int viasBefore = 0;
                        for( PCB_TRACK* track : board->Tracks() )
                        {
                            if( track->Type() == PCB_VIA_T )
                                viasBefore++;
                            else
                                tracksBefore++;
                        }

                        // Remove existing tracks from view before import
                        // (DSN::ImportSpecctraSession will replace them on the board)
                        if( GetCanvas() )
                        {
                            for( PCB_TRACK* track : board->Tracks() )
                                GetCanvas()->GetView()->Remove( track );
                        }

                        // Import the session file
                        bool success = DSN::ImportSpecctraSession( board,
                                                        wxString::FromUTF8( inputPath ) );

                        if( success )
                        {
                            // Count tracks/vias after import
                            int tracksAfter = 0;
                            int viasAfter = 0;
                            for( PCB_TRACK* track : board->Tracks() )
                            {
                                if( track->Type() == PCB_VIA_T )
                                    viasAfter++;
                                else
                                    tracksAfter++;
                            }

                            resp["success"] = true;
                            resp["tracks_added"] = tracksAfter - tracksBefore;
                            resp["vias_added"] = viasAfter - viasBefore;

                            // Create undo entry for the new tracks (enables diff view)
                            // Identify new tracks (those not in the original set)
                            PICKED_ITEMS_LIST newItemsList;
                            for( PCB_TRACK* track : board->Tracks() )
                            {
                                if( existingTrackIds.find( track->m_Uuid ) == existingTrackIds.end() )
                                {
                                    // This is a new track from the import
                                    ITEM_PICKER picker( nullptr, track, UNDO_REDO::NEWITEM );
                                    newItemsList.PushItem( picker );
                                }
                            }

                            // Save the new tracks as an undo entry
                            if( newItemsList.GetCount() > 0 )
                            {
                                SaveCopyInUndoList( newItemsList, UNDO_REDO::NEWITEM );
                                wxLogInfo( "PCB: Created undo entry for %d new tracks/vias from autorouter",
                                           newItemsList.GetCount() );
                            }

                            // Mark board as modified
                            OnModify();

                            // Add new tracks to the view
                            if( GetCanvas() )
                            {
                                for( PCB_TRACK* track : board->Tracks() )
                                    GetCanvas()->GetView()->Add( track );

                                GetCanvas()->Refresh();
                            }
                        }
                        else
                        {
                            resp["success"] = false;
                            resp["error"] = "ImportSpecctraSession returned false";
                        }
                    }
                    catch( const IO_ERROR& e )
                    {
                        resp["success"] = false;
                        resp["error"] = e.What().ToStdString();
                    }
                    catch( const std::exception& e )
                    {
                        resp["success"] = false;
                        resp["error"] = e.what();
                    }
                }

                std::string respStr = resp.dump();
                Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, respStr, this );
                break;
            }
            else if( j_in.contains( "type" ) && j_in["type"] == "propose_settings" )
            {
                std::vector<SETTING_CHANGE> changes;
                if( j_in.contains( "changes" ) )
                {
                    for( const auto& c : j_in["changes"] )
                    {
                        SETTING_CHANGE change;
                        change.m_settingKey = From_UTF8( c.value( "key", "" ).c_str() );
                        change.m_oldValue = From_UTF8( c.value( "old", "" ).c_str() );
                        change.m_newValue = From_UTF8( c.value( "new", "" ).c_str() );
                        changes.push_back( change );
                    }
                }

                DIALOG_SETTINGS_DIFF dlg( this, changes );
                int                  ret = dlg.ShowModal();

                nlohmann::json response;
                response["type"] = "propose_settings_response";
                response["status"] = ( ret == wxID_OK ) ? "approved" : "denied";

                std::string payload_out = response.dump();
                Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, payload_out );
                break;
            }
            std::string request = payload;
            // Trim whitespace just in case
            request.erase( 0, request.find_first_not_of( " \n\r\t" ) );
            request.erase( request.find_last_not_of( " \n\r\t" ) + 1 );

            nlohmann::json response;

            if( request.rfind( "echo", 0 ) == 0 )
            {
                response["echo"] = request.length() > 5 ? request.substr( 5 ) : "";
            }
            else if( request.rfind( "get_board_info", 0 ) == 0 || request.rfind( "GET_BOARD_INFO", 0 ) == 0 )
            {
                BOARD*                 board = GetBoard();
                BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

                response["design_rules"] = { { "min_track_width", bds.m_TrackMinWidth },
                                             { "min_clearance", bds.m_MinClearance },
                                             { "min_via_diameter", bds.m_ViasMinSize } };

                response["layers"] = nlohmann::json::array();
                for( PCB_LAYER_ID layer : board->GetEnabledLayers().Seq() )
                {
                    response["layers"].push_back( { { "id", layer },
                                                    { "name", board->GetLayerName( layer ).ToStdString() },
                                                    { "type", IsCopperLayer( layer ) ? "copper" : "technical" } } );
                }
            }
            else if( request.rfind( "get_pcb_components", 0 ) == 0 ) // Starts with
            {
                BOARD* board = GetBoard();
                response["components"] = nlohmann::json::array();
                for( FOOTPRINT* fp : board->Footprints() )
                {
                    response["components"].push_back( { { "reference", fp->GetReference().ToStdString() },
                                                        { "value", fp->GetValue().ToStdString() },
                                                        { "uuid", fp->m_Uuid.AsString().ToStdString() } } );
                }
            }
            else if( request == "test_diff" )
            {
                BOX2I bbox = GetBoard()->GetBoundingBox();
                if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
                    bbox = BOX2I( VECTOR2I( 0, 0 ), VECTOR2I( 100000000, 100000000 ) ); // Default 100mm box if empty

                DIFF_CALLBACKS callbacks;
                callbacks.onUndo = []()
                {
                    wxLogMessage( "Diff: Undo Clicked" );
                };
                callbacks.onRedo = []()
                {
                    wxLogMessage( "Diff: Redo Clicked" );
                };

                DIFF_MANAGER::GetInstance().RegisterOverlay( this->GetCanvas()->GetView(), callbacks );
                DIFF_MANAGER::GetInstance().ShowDiff( bbox );
                response["status"] = "diff_shown";
            }
            else if( request.rfind( "get_component_details", 0 ) == 0 || request.rfind( "get_pcb_component", 0 ) == 0 )
            {
                // Parse "get_component_details Ref" or "get_pcb_component Ref"
                // Find first space to determine offset
                size_t      spacePos = request.find( ' ' );
                std::string ref = "";
                if( spacePos != std::string::npos )
                {
                    ref = request.substr( spacePos + 1 );
                    // Trim
                    ref.erase( 0, ref.find_first_not_of( " \"\t\r\n" ) );
                    ref.erase( ref.find_last_not_of( " \"\t\r\n" ) + 1 );
                }

                BOARD*     board = GetBoard();
                FOOTPRINT* target = nullptr;
                for( FOOTPRINT* fp : board->Footprints() )
                {
                    if( fp->GetReference() == ref )
                    {
                        target = fp;
                        break;
                    }
                }

                if( target )
                {
                    response["reference"] = target->GetReference().ToStdString();
                    response["value"] = target->GetValue().ToStdString();
                    response["layer"] = board->GetLayerName( target->GetLayer() ).ToStdString();
                    response["position"] = { { "x", target->GetPosition().x }, { "y", target->GetPosition().y } };
                }
                else
                {
                    response["error"] = "Component not found: '" + ref + "'";
                }
            }
            else if( request.rfind( "get_pcb_nets", 0 ) == 0 )
            {
                BOARD* board = GetBoard();
                response["nets"] = nlohmann::json::array();
                for( const auto& net : board->GetNetInfo().NetsByName() )
                {
                    response["nets"].push_back(
                            { { "name", net.first.ToStdString() }, { "code", net.second->GetNetCode() } } );
                }
            }
            else if( request.rfind( "get_net_details", 0 ) == 0 )
            {
                std::string netName = request.substr( 15 );
                netName.erase( 0, netName.find_first_not_of( " \"\t\r\n" ) );
                netName.erase( netName.find_last_not_of( " \"\t\r\n" ) + 1 );

                BOARD*        board = GetBoard();
                NETINFO_ITEM* netInfo = board->GetNetInfo().GetNetItem( netName );

                if( netInfo )
                {
                    response["net_name"] = netName;
                    response["net_code"] = netInfo->GetNetCode();
                    response["track_count"] = 0;
                    // Ideally iterate tracks filtered by net, but for now just summary or filtered list
                    // To avoid massive payloads, we might just return count or key items?
                    // Let's return tracks for this net.
                    response["tracks"] = nlohmann::json::array();
                    int trackCount = 0;
                    for( PCB_TRACK* track : board->Tracks() )
                    {
                        if( track->GetNetCode() == netInfo->GetNetCode() )
                        {
                            if( trackCount++ > 100 )
                                break; // Limit return size
                            nlohmann::json trk;
                            trk["type"] = track->Type() == PCB_VIA_T ? "via" : "track";
                            trk["start"] = { { "x", track->GetStart().x }, { "y", track->GetStart().y } };
                            trk["end"] = { { "x", track->GetEnd().x }, { "y", track->GetEnd().y } };
                            trk["layer"] = board->GetLayerName( track->GetLayer() ).ToStdString();
                            response["tracks"].push_back( trk );
                        }
                    }
                    response["track_count_total"] = trackCount;
                }
                else
                {
                    response["error"] = "Net not found: " + netName;
                }
            }

            else
            {
                response["error"] = "Unknown command: '" + request + "'";
                wxLogMessage( "PCB received unknown Agent Request: '%s'", request.c_str() );
            }

            // wxLogMessage( "PCB processing Agent Request: %s", request.c_str() ); // Removed alert

            std::string responseStr = response.dump();
            Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, responseStr, this );
            Kiway().ExpressMail( FRAME_TERMINAL, MAIL_AGENT_RESPONSE, responseStr, this );
            break;
        }
    }
    break;

    case MAIL_SHOW_DIFF:
    {
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            if( j.contains( "x" ) && j.contains( "y" ) && j.contains( "w" ) && j.contains( "h" ) )
            {
                BOX2I bbox( VECTOR2I( j["x"], j["y"] ), VECTOR2I( j["w"], j["h"] ) );

                DIFF_CALLBACKS callbacks;
                callbacks.onUndo = [this]()
                {
                    wxCommandEvent evt( wxEVT_COMMAND_MENU_SELECTED, wxID_UNDO );
                    this->ProcessEvent( evt );
                };
                callbacks.onRedo = [this]()
                {
                    wxCommandEvent evt( wxEVT_COMMAND_MENU_SELECTED, wxID_REDO );
                    this->ProcessEvent( evt );
                };
                callbacks.onApprove = [this]()
                {
                    ClearAgentPendingChanges();
                    // Notify agent frame that diff was handled via overlay
                    std::string payload = "pcb";
                    Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_DIFF_CLEARED, payload );
                };
                callbacks.onReject = [this]()
                {
                    RevertAgentChanges();
                    // Notify agent frame that diff was handled via overlay
                    std::string payload = "pcb";
                    Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_DIFF_CLEARED, payload );
                };
                callbacks.onRefresh = [this]()
                {
                    if( GetCanvas() )
                        GetCanvas()->Refresh();
                };

                DIFF_MANAGER::GetInstance().RegisterOverlay( this->GetCanvas()->GetView(), callbacks );
                DIFF_MANAGER::GetInstance().ShowDiff( bbox );
            }
        }
        catch( ... )
        {
        }
        break;
    }

    case MAIL_AGENT_HAS_CHANGES:
    {
        // Query if there are pending agent changes
        payload = HasAgentPendingChanges() ? "true" : "false";
        break;
    }

    case MAIL_AGENT_APPROVE:
    {
        // Approve pending agent changes
        if( m_showingAgentBefore )
            ShowAgentChangesAfter();
        ClearAgentPendingChanges();
        break;
    }

    case MAIL_AGENT_REJECT:
    {
        // Reject pending agent changes
        if( m_showingAgentBefore )
            ShowAgentChangesAfter();
        RevertAgentChanges();
        break;
    }

    // DISABLED: Users view changes via Changes tab
    // case MAIL_AGENT_VIEW_CHANGES:
    // {
    //     // Zoom to the agent changes bounding box
    //     BOX2I changedBBox = ComputeTrackedItemsBBox();
    //
    //     wxLogInfo( "PCB MAIL_AGENT_VIEW_CHANGES: hasChanges=%d, bbox=(%d,%d,%d,%d)",
    //                m_hasAgentPendingChanges,
    //                changedBBox.GetX(), changedBBox.GetY(),
    //                changedBBox.GetWidth(), changedBBox.GetHeight() );
    //
    //     if( m_hasAgentPendingChanges && changedBBox.GetWidth() > 0 )
    //     {
    //         // Bring editor to front
    //         Raise();
    //
    //         // Zoom to the changed area with some padding
    //         BOX2I zoomBox = changedBBox;
    //         zoomBox.Inflate( zoomBox.GetWidth() / 4, zoomBox.GetHeight() / 4 );
    //         // Convert BOX2I to BOX2D for SetViewport
    //         BOX2D viewport( VECTOR2D( zoomBox.GetPosition() ), VECTOR2D( zoomBox.GetSize() ) );
    //         GetCanvas()->GetView()->SetViewport( viewport );
    //         GetCanvas()->Refresh();
    //     }
    //     break;
    // }

    case MAIL_SELECTION:
        if( !GetPcbNewSettings()->m_CrossProbing.on_selection )
            break;

        KI_FALLTHROUGH;

    case MAIL_SELECTION_FORCE:
    {
        // $SELECT: <mode 0 - only footprints, 1 - with connections>,<spec1>,<spec2>,<spec3>
        std::string prefix = "$SELECT: ";

        if( !payload.compare( 0, prefix.size(), prefix ) )
        {
            std::string del = ",";
            std::string paramStr = payload.substr( prefix.size() );
            size_t      modeEnd = paramStr.find( del );
            bool        selectConnections = false;

            try
            {
                if( std::stoi( paramStr.substr( 0, modeEnd ) ) == 1 )
                    selectConnections = true;
            }
            catch( std::invalid_argument& )
            {
                wxFAIL;
            }

            std::vector<BOARD_ITEM*> items = FindItemsFromSyncSelection( paramStr.substr( modeEnd + 1 ) );

            m_ProbingSchToPcb = true; // recursion guard

            if( selectConnections )
                GetToolManager()->RunAction( PCB_ACTIONS::syncSelectionWithNets, &items );
            else
                GetToolManager()->RunAction( PCB_ACTIONS::syncSelection, &items );

            // Update 3D viewer highlighting
            Update3DView( false, GetPcbNewSettings()->m_Display.m_Live3DRefresh );

            m_ProbingSchToPcb = false;

            if( GetPcbNewSettings()->m_CrossProbing.flash_selection )
            {
                wxLogTrace( traceCrossProbeFlash, "MAIL_SELECTION(_FORCE) PCB: flash enabled, items=%zu",
                            items.size() );
                if( items.empty() )
                {
                    wxLogTrace( traceCrossProbeFlash, "MAIL_SELECTION(_FORCE) PCB: nothing to flash" );
                }
                else
                {
                    std::vector<BOARD_ITEM*> boardItems;
                    std::copy( items.begin(), items.end(), std::back_inserter( boardItems ) );
                    StartCrossProbeFlash( boardItems );
                }
            }
            else
            {
                wxLogTrace( traceCrossProbeFlash, "MAIL_SELECTION(_FORCE) PCB: flash disabled" );
            }
        }

        break;
    }

    case MAIL_PCB_UPDATE: m_toolManager->RunAction( ACTIONS::updatePcbFromSchematic ); break;

    case MAIL_IMPORT_FILE:
    {
        // Extract file format type and path (plugin type, path and properties keys, values separated with \n)
        std::stringstream ss( payload );
        char              delim = '\n';

        std::string formatStr;
        wxCHECK( std::getline( ss, formatStr, delim ), /* void */ );

        std::string fnameStr;
        wxCHECK( std::getline( ss, fnameStr, delim ), /* void */ );
        wxASSERT( !fnameStr.empty() );

        int importFormat;

        try
        {
            importFormat = std::stoi( formatStr );
        }
        catch( std::invalid_argument& )
        {
            wxFAIL;
            importFormat = -1;
        }

        std::map<std::string, UTF8> props;

        std::string key, value;
        do
        {
            if( !std::getline( ss, key, delim ) )
                break;

            if( !std::getline( ss, value, delim ) )
                break;

            props.emplace( key, value );

        } while( true );

        if( importFormat >= 0 )
            importFile( fnameStr, importFormat, props.empty() ? nullptr : &props );

        break;
    }


    case MAIL_RELOAD_PLUGINS: GetToolManager()->RunAction( ACTIONS::pluginsReload ); break;

    case MAIL_RELOAD_LIB:
    {
        // Re-read the project fp-lib-table from disk so newly-added library entries
        // (e.g. from the agent footprint generator) become known in memory.
        Pgm().GetLibraryManager().LoadProjectTables( { LIBRARY_TABLE_TYPE::FOOTPRINT } );

        FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &Prj() );

        // If payload contains a specific library nickname, reload it so newly-written
        // footprints on disk become visible to the footprint browser / board editor.
        if( !payload.empty() )
        {
            wxString libName = wxString::FromUTF8( payload );
            adapter->LoadOne( libName );
            wxLogInfo( "MAIL_RELOAD_LIB: Reloaded footprint library '%s'", libName );
        }

        m_designBlocksPane->RefreshLibs();

        // Show any footprint library load errors in the status bar
        if( KISTATUSBAR* statusBar = dynamic_cast<KISTATUSBAR*>( GetStatusBar() ) )
        {
            wxString errors = adapter->GetLibraryLoadErrors();

            if( !errors.IsEmpty() )
                statusBar->SetLoadWarningMessages( errors );
        }

        break;
    }

    // Concurrent editing support - agent transaction management
    case MAIL_AGENT_BEGIN_TRANSACTION:
    {
        // Agent is starting a transaction - track that we're in an agent operation
        m_agentTransactionActive = true;
        if( AGENT_CHANGE_TRACKER* tracker = GetAgentChangeTracker() )
            tracker->ClearTrackedItems();
        break;
    }

    case MAIL_AGENT_END_TRANSACTION:
    {
        // Agent is ending a transaction
        m_agentTransactionActive = false;
        // Don't clear working set if we have pending changes - it's needed for auto-dismiss
        if( !m_hasAgentPendingChanges )
        {
            if( AGENT_CHANGE_TRACKER* tracker = GetAgentChangeTracker() )
                tracker->ClearTrackedItems();
        }
        break;
    }

    case MAIL_AGENT_WORKING_SET:
    {
        // Agent is updating its working set of items
        // Payload is JSON array of KIID strings
        // Now uses AGENT_CHANGE_TRACKER instead of a raw set
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            AGENT_CHANGE_TRACKER* tracker = GetAgentChangeTracker();
            if( tracker )
            {
                tracker->ClearTrackedItems();
                if( j.is_array() )
                {
                    for( const auto& item : j )
                    {
                        if( item.is_string() )
                        {
                            KIID kiid( item.get<std::string>() );
                            tracker->TrackItem( kiid );  // PCB has no sheet path
                        }
                    }
                }
            }
        }
        catch( ... )
        {
            // Invalid JSON, ignore
        }
        break;
    }

    case MAIL_CONFLICT_RESOLVED:
    {
        // Agent has resolved a conflict - payload contains resolution info
        // For now just log it; future implementation will apply resolution
        wxLogTrace( "AGENT", "Received conflict resolution for PCB" );
        break;
    }

    //=========================================================================
    // File edit session management
    //=========================================================================

    case MAIL_AGENT_FILE_EDIT_BEGIN:
    {
        // PCB file edit session not yet implemented
        wxLogInfo( "PCB: Agent file edit begin" );
        break;
    }

    case MAIL_AGENT_FILE_EDIT_COMPLETE:
    {
        // PCB file edit session not yet implemented
        wxLogInfo( "PCB: Agent file edit complete" );
        break;
    }

    case MAIL_AGENT_FILE_EDIT_ABORT:
    {
        // PCB file edit session not yet implemented
        wxLogInfo( "PCB: Agent file edit aborted" );
        break;
    }

    case MAIL_AGENT_REFRESH_DIFF:
    {
        // Refresh the diff overlay - items may have moved
        DIFF_MANAGER::GetInstance().RefreshOverlay( GetCanvas()->GetView() );
        break;
    }

    // many many others.
    default:;
    }
}
