/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2011 Wayne Stambaugh <stambaughw@gmail.com>
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
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <fmt.h>
#include <wx/log.h>
#include <kiface_base.h>
#include <kiway.h>
#include <kiway_express.h>
#include <eda_dde.h>
#include <connection_graph.h>
#include <sch_sheet.h>
#include <sch_symbol.h>
#include <sch_reference_list.h>
#include <string_utils.h>
#include <netlist_exporters/netlist_exporter_kicad.h>
#include <project/project_file.h>
#include <project/net_settings.h>
#include <project_sch.h>
#include <richio.h>
#include <tools/sch_actions.h>
#include <tools/sch_editor_control.h>
#include <advanced_config.h>

#include <pgm_base.h>
#include <libraries/symbol_library_adapter.h>
#include <widgets/sch_design_block_pane.h>
#include <agent_change_tracker.h>
#include <settings/settings_manager.h>
#include <wx/log.h>
#include "sch_plotter.h"
#include <nlohmann/json.hpp>
#include <sch_text.h>
#include <nlohmann/json.hpp>
#include <sch_text.h>
#include <trace_helpers.h>
#include <id.h>
#include <diff_manager.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr_parser.h>


SCH_ITEM* SCH_EDITOR_CONTROL::FindSymbolAndItem( const wxString* aPath, const wxString* aReference,
                                                 bool aSearchHierarchy, SCH_SEARCH_T aSearchType,
                                                 const wxString& aSearchText )
{
    SCH_SHEET_PATH* sheetWithSymbolFound = nullptr;
    SCH_SYMBOL*     symbol = nullptr;
    SCH_PIN*        pin = nullptr;
    SCH_SHEET_LIST  sheetList;
    SCH_ITEM*       foundItem = nullptr;

    if( !aSearchHierarchy )
        sheetList.push_back( m_frame->GetCurrentSheet() );
    else
        sheetList = m_frame->Schematic().Hierarchy();

    for( SCH_SHEET_PATH& sheet : sheetList )
    {
        SCH_SCREEN* screen = sheet.LastScreen();

        for( EDA_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* candidate = static_cast<SCH_SYMBOL*>( item );

            // Search by path if specified, otherwise search by reference
            bool found = false;

            if( aPath )
            {
                wxString path = sheet.PathAsString() + candidate->m_Uuid.AsString();
                found = ( *aPath == path );
            }
            else
            {
                found = ( aReference && aReference->CmpNoCase( candidate->GetRef( &sheet ) ) == 0 );
            }

            if( found )
            {
                symbol = candidate;
                sheetWithSymbolFound = &sheet;


                // The 'break;' here would break out of the inner loop, but it's syntactically misplaced
                // and 'item' is not necessarily the 'candidate' symbol.
                // The original code continues with pin/symbol logic.
                // break; // Removed to maintain original logic flow and avoid syntax error.

                if( aSearchType == HIGHLIGHT_PIN )
                {
                    pin = symbol->GetPin( aSearchText );

                    // Ensure we have found the right unit in case of multi-units symbol
                    if( pin )
                    {
                        int unit = pin->GetLibPin()->GetUnit();

                        if( unit != 0 && unit != symbol->GetUnit() )
                        {
                            pin = nullptr;
                            continue;
                        }

                        // Get pin position in true schematic coordinate
                        foundItem = pin;
                        break;
                    }
                }
                else
                {
                    foundItem = symbol;
                    break;
                }
            }
        }

        if( foundItem )
            break;
    }

    CROSS_PROBING_SETTINGS& crossProbingSettings = m_frame->eeconfig()->m_CrossProbing;

    if( symbol )
    {
        if( *sheetWithSymbolFound != m_frame->GetCurrentSheet() )
        {
            m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, sheetWithSymbolFound );
        }

        if( crossProbingSettings.center_on_items )
        {
            if( crossProbingSettings.zoom_to_fit )
            {
                BOX2I bbox = symbol->GetBoundingBox();

                m_toolMgr->GetTool<SCH_SELECTION_TOOL>()->ZoomFitCrossProbeBBox( bbox );
            }

            if( pin )
                m_frame->FocusOnItem( pin );
            else
                m_frame->FocusOnItem( symbol );
        }
    }

    /* Print diag */
    wxString msg;
    wxString displayRef;

    if( aReference )
        displayRef = *aReference;
    else if( aPath )
        displayRef = *aPath;

    if( symbol )
    {
        if( aSearchType == HIGHLIGHT_PIN )
        {
            if( foundItem )
                msg.Printf( _( "%s pin %s found" ), displayRef, aSearchText );
            else
                msg.Printf( _( "%s found but pin %s not found" ), displayRef, aSearchText );
        }
        else
        {
            msg.Printf( _( "%s found" ), displayRef );
        }
    }
    else
    {
        msg.Printf( _( "%s not found" ), displayRef );
    }

    m_frame->SetStatusText( msg );
    m_frame->GetCanvas()->Refresh();

    return foundItem;
}


/* Execute a remote command sent via a socket on port KICAD_PCB_PORT_SERVICE_NUMBER
 *
 * Commands are:
 *
 * $PART: "reference"                  Put cursor on symbol.
 * $PART: "reference" $REF: "ref"      Put cursor on symbol reference.
 * $PART: "reference" $VAL: "value"    Put cursor on symbol value.
 * $PART: "reference" $PAD: "pin name" Put cursor on the symbol pin.
 * $NET: "netname"                     Highlight a specified net
 * $CLEAR: "HIGHLIGHTED"               Clear symbols highlight
 *
 * $CONFIG     Show the Manage Symbol Libraries dialog
 * $ERC        Show the ERC dialog
 */
void SCH_EDIT_FRAME::ExecuteRemoteCommand( const char* cmdline )
{
    SCH_EDITOR_CONTROL* editor = m_toolManager->GetTool<SCH_EDITOR_CONTROL>();
    char                line[1024];

    strncpy( line, cmdline, sizeof( line ) - 1 );
    line[sizeof( line ) - 1] = '\0';

    char* idcmd = strtok( line, " \n\r" );
    char* text = strtok( nullptr, "\"\n\r" );

    if( idcmd == nullptr )
        return;

    CROSS_PROBING_SETTINGS& crossProbingSettings = eeconfig()->m_CrossProbing;

    if( strcmp( idcmd, "$CONFIG" ) == 0 )
    {
        GetToolManager()->RunAction( ACTIONS::showSymbolLibTable );
        return;
    }
    else if( strcmp( idcmd, "$ERC" ) == 0 )
    {
        GetToolManager()->RunAction( SCH_ACTIONS::runERC );
        return;
    }
    else if( strcmp( idcmd, "$NET:" ) == 0 )
    {
        if( !crossProbingSettings.auto_highlight )
            return;

        wxString netName = From_UTF8( text );

        if( auto sg = Schematic().ConnectionGraph()->FindFirstSubgraphByName( netName ) )
            m_highlightedConn = sg->GetDriverConnection()->Name();
        else
            m_highlightedConn = wxEmptyString;

        GetToolManager()->RunAction( SCH_ACTIONS::updateNetHighlighting );
        RefreshNetNavigator();

        SetStatusText( _( "Highlighted net:" ) + wxS( " " ) + UnescapeString( netName ) );
        return;
    }
    else if( strcmp( idcmd, "$CLEAR:" ) == 0 )
    {
        // Cross-probing is now done through selection so we no longer need a clear command
        return;
    }

    if( !crossProbingSettings.on_selection )
        return;

    if( text == nullptr )
        return;

    if( strcmp( idcmd, "$PART:" ) != 0 )
        return;

    wxString part_ref = From_UTF8( text );

    /* look for a complement */
    idcmd = strtok( nullptr, " \n\r" );

    if( idcmd == nullptr ) // Highlight symbol only (from CvPcb or Pcbnew)
    {
        // Highlight symbol part_ref, or clear Highlight, if part_ref is not existing
        editor->FindSymbolAndItem( nullptr, &part_ref, true, HIGHLIGHT_SYMBOL, wxEmptyString );
        return;
    }

    text = strtok( nullptr, "\"\n\r" );

    if( text == nullptr )
        return;

    wxString msg = From_UTF8( text );

    if( strcmp( idcmd, "$REF:" ) == 0 )
    {
        // Highlighting the reference itself isn't actually that useful, and it's harder to
        // see.  Highlight the parent and display the message.
        editor->FindSymbolAndItem( nullptr, &part_ref, true, HIGHLIGHT_SYMBOL, msg );
    }
    else if( strcmp( idcmd, "$VAL:" ) == 0 )
    {
        // Highlighting the value itself isn't actually that useful, and it's harder to see.
        // Highlight the parent and display the message.
        editor->FindSymbolAndItem( nullptr, &part_ref, true, HIGHLIGHT_SYMBOL, msg );
    }
    else if( strcmp( idcmd, "$PAD:" ) == 0 )
    {
        editor->FindSymbolAndItem( nullptr, &part_ref, true, HIGHLIGHT_PIN, msg );
    }
    else
    {
        editor->FindSymbolAndItem( nullptr, &part_ref, true, HIGHLIGHT_SYMBOL, wxEmptyString );
    }
}


void SCH_EDIT_FRAME::SendSelectItemsToPcb( const std::vector<EDA_ITEM*>& aItems, bool aForce )
{
    std::vector<wxString> parts;

    for( EDA_ITEM* item : aItems )
    {
        switch( item->Type() )
        {
        case SCH_SYMBOL_T:
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
            wxString    ref = symbol->GetField( FIELD_T::REFERENCE )->GetText();

            parts.push_back( wxT( "F" ) + EscapeString( ref, CTX_IPC ) );
            break;
        }

        case SCH_SHEET_T:
        {
            // For cross probing, we need the full path of the sheet, because
            // we search by the footprint path prefix in the PCB editor
            wxString full_path = GetCurrentSheet().PathAsString() + item->m_Uuid.AsString();

            parts.push_back( wxT( "S" ) + full_path );
            break;
        }

        case SCH_PIN_T:
        {
            SCH_PIN* pin = static_cast<SCH_PIN*>( item );
            SYMBOL*  symbol = pin->GetParentSymbol();
            wxString ref = symbol->GetRef( &GetCurrentSheet(), false );

            parts.push_back( wxT( "P" ) + EscapeString( ref, CTX_IPC ) + wxT( "/" )
                             + EscapeString( pin->GetShownNumber(), CTX_IPC ) );
            break;
        }

        default: break;
        }
    }

    if( parts.empty() )
        return;

    std::string command = "$SELECT: 0,";

    for( wxString part : parts )
    {
        command += part;
        command += ",";
    }

    command.pop_back();

    if( Kiface().IsSingle() )
    {
        SendCommand( MSG_TO_PCB, command );
    }
    else
    {
        // Typically ExpressMail is going to be s-expression packets, but since
        // we have existing interpreter of the selection packet on the other
        // side in place, we use that here.
        Kiway().ExpressMail( FRAME_PCB_EDITOR, aForce ? MAIL_SELECTION_FORCE : MAIL_SELECTION, command, this );
    }
}


void SCH_EDIT_FRAME::SendCrossProbeNetName( const wxString& aNetName )
{
    // The command is a keyword followed by a quoted string.

    std::string packet = fmt::format( "$NET: \"{}\"", TO_UTF8( aNetName ) );

    if( !packet.empty() )
    {
        if( Kiface().IsSingle() )
        {
            SendCommand( MSG_TO_PCB, packet );
        }
        else
        {
            // Typically ExpressMail is going to be s-expression packets, but since
            // we have existing interpreter of the cross probe packet on the other
            // side in place, we use that here.
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_CROSS_PROBE, packet, this );
        }
    }
}


void SCH_EDIT_FRAME::SetCrossProbeConnection( const SCH_CONNECTION* aConnection )
{
    if( !aConnection )
    {
        SendCrossProbeClearHighlight();
        return;
    }

    if( aConnection->IsNet() )
    {
        SendCrossProbeNetName( aConnection->Name() );
        return;
    }

    if( aConnection->Members().empty() )
        return;

    auto all_members = aConnection->AllMembers();

    wxString nets = all_members[0]->Name();

    if( all_members.size() == 1 )
    {
        SendCrossProbeNetName( nets );
        return;
    }

    // TODO: This could be replaced by just sending the bus name once we have bus contents
    // included as part of the netlist sent from Eeschema to Pcbnew (and thus Pcbnew can
    // natively keep track of bus membership)

    for( size_t i = 1; i < all_members.size(); i++ )
        nets << "," << all_members[i]->Name();

    std::string packet = fmt::format( "$NETS: \"{}\"", TO_UTF8( nets ) );

    if( !packet.empty() )
    {
        if( Kiface().IsSingle() )
            SendCommand( MSG_TO_PCB, packet );
        else
        {
            // Typically ExpressMail is going to be s-expression packets, but since
            // we have existing interpreter of the cross probe packet on the other
            // side in place, we use that here.
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_CROSS_PROBE, packet, this );
        }
    }
}


void SCH_EDIT_FRAME::SendCrossProbeClearHighlight()
{
    std::string packet = "$CLEAR\n";

    if( Kiface().IsSingle() )
    {
        SendCommand( MSG_TO_PCB, packet );
    }
    else
    {
        // Typically ExpressMail is going to be s-expression packets, but since
        // we have existing interpreter of the cross probe packet on the other
        // side in place, we use that here.
        Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_CROSS_PROBE, packet, this );
    }
}


bool findSymbolsAndPins( const SCH_SHEET_LIST& aSchematicSheetList, const SCH_SHEET_PATH& aSheetPath,
                         std::unordered_map<wxString, std::vector<SCH_REFERENCE>>&             aSyncSymMap,
                         std::unordered_map<wxString, std::unordered_map<wxString, SCH_PIN*>>& aSyncPinMap,
                         bool                                                                  aRecursive = false )
{
    if( aRecursive )
    {
        // Iterate over children
        for( const SCH_SHEET_PATH& candidate : aSchematicSheetList )
        {
            if( candidate == aSheetPath || !candidate.IsContainedWithin( aSheetPath ) )
                continue;

            findSymbolsAndPins( aSchematicSheetList, candidate, aSyncSymMap, aSyncPinMap, aRecursive );
        }
    }

    SCH_REFERENCE_LIST references;

    aSheetPath.GetSymbols( references, false, true );

    for( unsigned ii = 0; ii < references.GetCount(); ii++ )
    {
        SCH_REFERENCE& schRef = references[ii];

        if( schRef.IsSplitNeeded() )
            schRef.Split();

        SCH_SYMBOL* symbol = schRef.GetSymbol();
        wxString    refNum = schRef.GetRefNumber();
        wxString    fullRef = schRef.GetRef() + refNum;

        // Skip power symbols
        if( fullRef.StartsWith( wxS( "#" ) ) )
            continue;

        // Unannotated symbols are not supported
        if( refNum.compare( wxS( "?" ) ) == 0 )
            continue;

        // Look for whole footprint
        auto symMatchIt = aSyncSymMap.find( fullRef );

        if( symMatchIt != aSyncSymMap.end() )
        {
            symMatchIt->second.emplace_back( schRef );

            // Whole footprint was selected, no need to select pins
            continue;
        }

        // Look for pins
        auto symPinMatchIt = aSyncPinMap.find( fullRef );

        if( symPinMatchIt != aSyncPinMap.end() )
        {
            std::unordered_map<wxString, SCH_PIN*>& pinMap = symPinMatchIt->second;
            std::vector<SCH_PIN*>                   pinsOnSheet = symbol->GetPins( &aSheetPath );

            for( SCH_PIN* pin : pinsOnSheet )
            {
                int pinUnit = pin->GetLibPin()->GetUnit();

                if( pinUnit > 0 && pinUnit != schRef.GetUnit() )
                    continue;

                auto pinIt = pinMap.find( pin->GetNumber() );

                if( pinIt != pinMap.end() )
                    pinIt->second = pin;
            }
        }
    }

    return false;
}


bool sheetContainsOnlyWantedItems( const SCH_SHEET_LIST& aSchematicSheetList, const SCH_SHEET_PATH& aSheetPath,
                                   std::unordered_map<wxString, std::vector<SCH_REFERENCE>>&             aSyncSymMap,
                                   std::unordered_map<wxString, std::unordered_map<wxString, SCH_PIN*>>& aSyncPinMap,
                                   std::unordered_map<SCH_SHEET_PATH, bool>&                             aCache )
{
    auto cacheIt = aCache.find( aSheetPath );

    if( cacheIt != aCache.end() )
        return cacheIt->second;

    // Iterate over children
    for( const SCH_SHEET_PATH& candidate : aSchematicSheetList )
    {
        if( candidate == aSheetPath || !candidate.IsContainedWithin( aSheetPath ) )
            continue;

        bool childRet =
                sheetContainsOnlyWantedItems( aSchematicSheetList, candidate, aSyncSymMap, aSyncPinMap, aCache );

        if( !childRet )
        {
            aCache.emplace( aSheetPath, false );
            return false;
        }
    }

    SCH_REFERENCE_LIST references;
    aSheetPath.GetSymbols( references, false, true );

    if( references.GetCount() == 0 ) // Empty sheet, obviously do not contain wanted items
    {
        aCache.emplace( aSheetPath, false );
        return false;
    }

    for( unsigned ii = 0; ii < references.GetCount(); ii++ )
    {
        SCH_REFERENCE& schRef = references[ii];

        if( schRef.IsSplitNeeded() )
            schRef.Split();

        wxString refNum = schRef.GetRefNumber();
        wxString fullRef = schRef.GetRef() + refNum;

        // Skip power symbols
        if( fullRef.StartsWith( wxS( "#" ) ) )
            continue;

        // Unannotated symbols are not supported
        if( refNum.compare( wxS( "?" ) ) == 0 )
            continue;

        if( aSyncSymMap.find( fullRef ) == aSyncSymMap.end() )
        {
            aCache.emplace( aSheetPath, false );
            return false; // Some symbol is not wanted.
        }

        if( aSyncPinMap.find( fullRef ) != aSyncPinMap.end() )
        {
            aCache.emplace( aSheetPath, false );
            return false; // Looking for specific pins, so can't be mapped
        }
    }

    aCache.emplace( aSheetPath, true );
    return true;
}


std::optional<std::tuple<SCH_SHEET_PATH, SCH_ITEM*, std::vector<SCH_ITEM*>>>
findItemsFromSyncSelection( const SCHEMATIC& aSchematic, const std::string aSyncStr, bool aFocusOnFirst )
{
    wxArrayString syncArray = wxStringTokenize( aSyncStr, wxS( "," ) );

    std::unordered_map<wxString, std::vector<SCH_REFERENCE>>             syncSymMap;
    std::unordered_map<wxString, std::unordered_map<wxString, SCH_PIN*>> syncPinMap;
    std::unordered_map<SCH_SHEET_PATH, double>                           symScores;
    std::unordered_map<SCH_SHEET_PATH, bool>                             fullyWantedCache;

    std::optional<wxString>                                    focusSymbol;
    std::optional<std::pair<wxString, wxString>>               focusPin;
    std::unordered_map<SCH_SHEET_PATH, std::vector<SCH_ITEM*>> focusItemResults;

    const SCH_SHEET_LIST allSheetsList = aSchematic.Hierarchy();

    // In orderedSheets, the current sheet comes first.
    std::vector<SCH_SHEET_PATH> orderedSheets;
    orderedSheets.reserve( allSheetsList.size() );
    orderedSheets.push_back( aSchematic.CurrentSheet() );

    for( const SCH_SHEET_PATH& sheetPath : allSheetsList )
    {
        if( sheetPath != aSchematic.CurrentSheet() )
            orderedSheets.push_back( sheetPath );
    }

    // Init sync maps from the sync string
    for( size_t i = 0; i < syncArray.size(); i++ )
    {
        wxString syncEntry = syncArray[i];

        if( syncEntry.empty() )
            continue;

        wxString syncData = syncEntry.substr( 1 );

        switch( syncEntry.GetChar( 0 ).GetValue() )
        {
        case 'F': // Select by footprint: F<Reference>
        {
            wxString symRef = UnescapeString( syncData );

            if( aFocusOnFirst && ( i == 0 ) )
                focusSymbol = symRef;

            syncSymMap[symRef] = std::vector<SCH_REFERENCE>();
            break;
        }

        case 'P': // Select by pad: P<Footprint reference>/<Pad number>
        {
            wxString symRef = UnescapeString( syncData.BeforeFirst( '/' ) );
            wxString padNum = UnescapeString( syncData.AfterFirst( '/' ) );

            if( aFocusOnFirst && ( i == 0 ) )
                focusPin = std::make_pair( symRef, padNum );

            syncPinMap[symRef][padNum] = nullptr;
            break;
        }

        default: break;
        }
    }

    // Lambda definitions
    auto flattenSyncMaps = [&syncSymMap, &syncPinMap]() -> std::vector<SCH_ITEM*>
    {
        std::vector<SCH_ITEM*> allVec;

        for( const auto& [symRef, symbols] : syncSymMap )
        {
            for( const SCH_REFERENCE& ref : symbols )
                allVec.push_back( ref.GetSymbol() );
        }

        for( const auto& [symRef, pinMap] : syncPinMap )
        {
            for( const auto& [padNum, pin] : pinMap )
            {
                if( pin )
                    allVec.push_back( pin );
            }
        }

        return allVec;
    };

    auto clearSyncMaps = [&syncSymMap, &syncPinMap]()
    {
        for( auto& [symRef, symbols] : syncSymMap )
            symbols.clear();

        for( auto& [reference, pins] : syncPinMap )
        {
            for( auto& [number, pin] : pins )
                pin = nullptr;
        }
    };

    auto syncMapsValuesEmpty = [&syncSymMap, &syncPinMap]() -> bool
    {
        for( const auto& [symRef, symbols] : syncSymMap )
        {
            if( symbols.size() > 0 )
                return false;
        }

        for( const auto& [symRef, pins] : syncPinMap )
        {
            for( const auto& [padNum, pin] : pins )
            {
                if( pin )
                    return false;
            }
        }

        return true;
    };

    auto checkFocusItems = [&]( const SCH_SHEET_PATH& aSheet )
    {
        if( focusSymbol )
        {
            auto findIt = syncSymMap.find( *focusSymbol );

            if( findIt != syncSymMap.end() )
            {
                if( findIt->second.size() > 0 )
                    focusItemResults[aSheet].push_back( findIt->second.front().GetSymbol() );
            }
        }
        else if( focusPin )
        {
            auto findIt = syncPinMap.find( focusPin->first );

            if( findIt != syncPinMap.end() )
            {
                if( findIt->second[focusPin->second] )
                    focusItemResults[aSheet].push_back( findIt->second[focusPin->second] );
            }
        }
    };

    auto makeRetForSheet = [&]( const SCH_SHEET_PATH& aSheet, SCH_ITEM* aFocusItem )
    {
        clearSyncMaps();

        // Fill sync maps
        findSymbolsAndPins( allSheetsList, aSheet, syncSymMap, syncPinMap );
        std::vector<SCH_ITEM*> itemsVector = flattenSyncMaps();

        // Add fully wanted sheets to vector
        for( SCH_ITEM* item : aSheet.LastScreen()->Items().OfType( SCH_SHEET_T ) )
        {
            KIID_PATH kiidPath = aSheet.Path();
            kiidPath.push_back( item->m_Uuid );

            std::optional<SCH_SHEET_PATH> subsheetPath = allSheetsList.GetSheetPathByKIIDPath( kiidPath );

            if( !subsheetPath )
                continue;

            if( sheetContainsOnlyWantedItems( allSheetsList, *subsheetPath, syncSymMap, syncPinMap, fullyWantedCache ) )
            {
                itemsVector.push_back( item );
            }
        }

        return std::make_tuple( aSheet, aFocusItem, itemsVector );
    };

    if( aFocusOnFirst )
    {
        for( const SCH_SHEET_PATH& sheetPath : orderedSheets )
        {
            clearSyncMaps();

            findSymbolsAndPins( allSheetsList, sheetPath, syncSymMap, syncPinMap );

            checkFocusItems( sheetPath );
        }

        if( focusItemResults.size() > 0 )
        {
            for( const SCH_SHEET_PATH& sheetPath : orderedSheets )
            {
                const std::vector<SCH_ITEM*>& items = focusItemResults[sheetPath];

                if( !items.empty() )
                    return makeRetForSheet( sheetPath, items.front() );
            }
        }
    }
    else
    {
        for( const SCH_SHEET_PATH& sheetPath : orderedSheets )
        {
            clearSyncMaps();

            findSymbolsAndPins( allSheetsList, sheetPath, syncSymMap, syncPinMap );

            if( !syncMapsValuesEmpty() )
            {
                // Something found on sheet
                return makeRetForSheet( sheetPath, nullptr );
            }
        }
    }

    return std::nullopt;
}


void SCH_EDIT_FRAME::KiwayMailIn( KIWAY_EXPRESS& mail )
{
    std::string& payload = mail.GetPayload();

    switch( mail.Command() )
    {
    case MAIL_ADD_LOCAL_LIB:
    {
        std::stringstream ss( payload );
        std::string       file;

        LIBRARY_MANAGER&              manager = Pgm().GetLibraryManager();
        SYMBOL_LIBRARY_ADAPTER*       adapter = PROJECT_SCH::SymbolLibAdapter( &Prj() );
        std::optional<LIBRARY_TABLE*> optTable =
                manager.Table( LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_SCOPE::PROJECT );

        wxCHECK_RET( optTable.has_value(), "Could not load symbol lib table." );
        LIBRARY_TABLE* table = optTable.value();

        while( std::getline( ss, file, '\n' ) )
        {
            if( file.empty() )
                continue;

            wxFileName             fn( file );
            IO_RELEASER<SCH_IO>    pi;
            SCH_IO_MGR::SCH_FILE_T type = SCH_IO_MGR::GuessPluginTypeFromLibPath( fn.GetFullPath() );
            bool                   success = true;

            if( type == SCH_IO_MGR::SCH_FILE_UNKNOWN )
            {
                wxLogTrace( "KIWAY", "Unknown file type: %s", fn.GetFullPath() );
                continue;
            }

            pi.reset( SCH_IO_MGR::FindPlugin( type ) );

            if( !table->HasRow( fn.GetName() ) )
            {
                LIBRARY_TABLE_ROW& row = table->InsertRow();
                row.SetNickname( fn.GetName() );
                row.SetURI( fn.GetFullPath() );
                row.SetType( SCH_IO_MGR::ShowType( type ) );

                table->Save().map_error(
                        [&]( const LIBRARY_ERROR& aError )
                        {
                            wxLogError( wxT( "Error saving project library table:\n\n" ) + aError.message );
                            success = false;
                        } );

                if( success )
                {
                    manager.LoadProjectTables( { LIBRARY_TABLE_TYPE::SYMBOL } );
                    adapter->LoadOne( fn.GetName() );
                }
            }
        }

        Kiway().ExpressMail( FRAME_CVPCB, MAIL_RELOAD_LIB, payload );
        Kiway().ExpressMail( FRAME_SCH_SYMBOL_EDITOR, MAIL_RELOAD_LIB, payload );
        Kiway().ExpressMail( FRAME_SCH_VIEWER, MAIL_RELOAD_LIB, payload );

        break;
    }

    case MAIL_CROSS_PROBE: ExecuteRemoteCommand( payload.c_str() ); break;

    case MAIL_AGENT_REQUEST:
    {
        std::string request = payload;
        // Trim whitespace
        request.erase( 0, request.find_first_not_of( " \n\r\t" ) );
        request.erase( request.find_last_not_of( " \n\r\t" ) + 1 );

        // Check for JSON commands first (agent diff view support)
        try
        {
            nlohmann::json j_in = nlohmann::json::parse( request, nullptr, false );
            if( !j_in.is_discarded() )
            {
                if( j_in.contains( "type" ) && j_in["type"] == "take_snapshot" )
                {
                    // Record the current undo position before agent execution
                    RecordAgentUndoPosition();
                    break;  // No response needed
                }
                else if( j_in.contains( "type" ) && j_in["type"] == "detect_changes" )
                {
                    // Detect changes after agent execution and show diff overlay
                    DetectAgentChanges();
                    break;  // No response needed
                }
                else if( j_in.contains( "type" ) && j_in["type"] == "export_screenshot" )
                {
                    // Export the current in-memory schematic to SVG for screenshot
                    std::string outputDir = j_in.value( "output_dir", "" );

                    SCH_RENDER_SETTINGS renderSettings;
                    COLOR_SETTINGS* cs = ::GetColorSettings( wxT( "_builtin_default" ) );
                    renderSettings.LoadColors( cs );
                    renderSettings.SetDefaultPenWidth(
                            Schematic().Settings().m_DefaultLineWidth );
                    renderSettings.m_LabelSizeRatio =
                            Schematic().Settings().m_LabelSizeRatio;
                    renderSettings.m_TextOffsetRatio =
                            Schematic().Settings().m_TextOffsetRatio;
                    renderSettings.m_PinSymbolSize =
                            Schematic().Settings().m_PinSymbolSize;
                    renderSettings.SetDashLengthRatio(
                            Schematic().Settings().m_DashedLineDashRatio );
                    renderSettings.SetGapLengthRatio(
                            Schematic().Settings().m_DashedLineGapRatio );

                    SCH_PLOT_OPTS plotOpts;
                    plotOpts.m_plotAll = false;
                    plotOpts.m_plotDrawingSheet = false;
                    plotOpts.m_useBackgroundColor = false;
                    plotOpts.m_blackAndWhite = false;
                    plotOpts.m_pageSizeSelect = PAGE_SIZE_AUTO;
                    plotOpts.m_theme = wxT( "_builtin_default" );
                    plotOpts.m_outputDirectory = wxString::FromUTF8( outputDir );

                    SCH_PLOTTER plotter( this );
                    plotter.Plot( PLOT_FORMAT::SVG, plotOpts, &renderSettings, nullptr );

                    nlohmann::json resp;
                    resp["type"] = "export_screenshot_response";
                    resp["svg_path"] = plotter.GetLastOutputFilePath().ToStdString();
                    resp["success"] = !plotter.GetLastOutputFilePath().IsEmpty();

                    std::string respStr = resp.dump();
                    Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, respStr,
                                         this );
                    break;
                }
            }
        }
        catch( ... )
        {
            // Not valid JSON, fall through to string-based handlers
        }

        nlohmann::json response;

        if( request.rfind( "echo", 0 ) == 0 )
        {
            response["echo"] = request.length() > 5 ? request.substr( 5 ) : "";
        }
        else if( request.rfind( "get_sch_sheets", 0 ) == 0 )
        {
            response["sheets"] = nlohmann::json::array();
            SCH_SHEET_LIST sheets = Schematic().Hierarchy();
            for( const SCH_SHEET_PATH& sheet : sheets )
            {
                response["sheets"].push_back( { { "path", sheet.PathHumanReadable().ToStdString() },
                                                { "name", sheet.Last()->GetName().ToStdString() } } );
            }
        }
        else if( request.rfind( "get_sch_components", 0 ) == 0 )
        {
            // Optional arg: SheetPath
            std::string sheetFilter = request.substr( 18 );
            sheetFilter.erase( 0, sheetFilter.find_first_not_of( " \"\t\r\n" ) );
            sheetFilter.erase( sheetFilter.find_last_not_of( " \"\t\r\n" ) + 1 );

            response["components"] = nlohmann::json::array();
            SCH_SHEET_LIST sheets = Schematic().Hierarchy();

            for( const SCH_SHEET_PATH& sheet : sheets )
            {
                // Filter? Getting path string match might be tricky, checking simplified check
                if( !sheetFilter.empty() && sheet.PathHumanReadable() != sheetFilter )
                    continue;

                for( SCH_ITEM* item : sheet.LastScreen()->Items() )
                {
                    if( item->Type() == SCH_SYMBOL_T )
                    {
                        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
                        response["components"].push_back(
                                { { "reference", sym->GetRef( &sheet ).ToStdString() },
                                  { "value", sym->GetValue( true, &sheet, true ).ToStdString() },
                                  { "sheet", sheet.PathHumanReadable().ToStdString() } } );
                    }
                }
            }
        }
        else if( request.rfind( "get_sch_symbol_details", 0 ) == 0 )
        {
            std::string ref = request.substr( 22 );
            ref.erase( 0, ref.find_first_not_of( " \"\t\r\n" ) );
            ref.erase( ref.find_last_not_of( " \"\t\r\n" ) + 1 );

            SCH_SHEET_LIST sheets = Schematic().Hierarchy();
            bool           found = false;

            for( const SCH_SHEET_PATH& sheet : sheets )
            {
                for( SCH_ITEM* item : sheet.LastScreen()->Items() )
                {
                    if( item->Type() == SCH_SYMBOL_T )
                    {
                        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
                        if( sym->GetRef( &sheet ) == ref )
                        {
                            response["reference"] = ref;
                            response["value"] = sym->GetValue( true, &sheet, true ).ToStdString();
                            response["footprint"] = sym->GetFootprintFieldText( true, &sheet, true ).ToStdString();
                            response["position"] = { { "x", sym->GetPosition().x }, { "y", sym->GetPosition().y } };
                            // Add pins?
                            response["pins"] = nlohmann::json::array();
                            std::vector<SCH_PIN*> pins = sym->GetPins( &sheet );
                            for( SCH_PIN* pin : pins )
                            {
                                response["pins"].push_back( { { "number", pin->GetNumber().ToStdString() },
                                                              { "name", pin->GetName().ToStdString() } } );
                            }
                            found = true;
                            break;
                        }
                    }
                }
                if( found )
                    break;
            }
            if( !found )
                response["error"] = "Symbol not found: " + ref;
        }
        else if( request == "get_connection_graph" )
        {
            response["nets"] = nlohmann::json::array();
            for( const auto& net : Schematic().ConnectionGraph()->GetNetMap() )
            {
                nlohmann::json netJson;
                netJson["name"] = net.first.Name.ToStdString();
                netJson["pins"] = nlohmann::json::array();

                for( CONNECTION_SUBGRAPH* subgraph : net.second )
                {
                    for( SCH_ITEM* item : subgraph->GetItems() )
                    {
                        if( item->Type() == SCH_PIN_T )
                        {
                            SCH_PIN* pin = static_cast<SCH_PIN*>( item );
                            netJson["pins"].push_back(
                                    pin->GetParentSymbol()->GetRef( &subgraph->GetSheet() ).ToStdString() + "."
                                    + pin->GetShownNumber().ToStdString() );
                        }
                    }
                }
                response["nets"].push_back( netJson );
            }
        }

        std::string responseStr = response.dump();
        Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, responseStr, this );
        Kiway().ExpressMail( FRAME_TERMINAL, MAIL_AGENT_RESPONSE, responseStr, this );
        break;
    }

    case MAIL_SHOW_DIFF:
    {
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            if( j.contains( "x" ) && j.contains( "y" ) && j.contains( "w" ) && j.contains( "h" ) )
            {
                BOX2I          bbox( VECTOR2I( j["x"], j["y"] ), VECTOR2I( j["w"], j["h"] ) );
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
                    std::string payload = "sch";
                    Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_DIFF_CLEARED, payload );
                };
                callbacks.onReject = [this]()
                {
                    RevertAgentChanges();
                    // Notify agent frame that diff was handled via overlay
                    std::string payload = "sch";
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
        // Return JSON with has_changes flag and list of affected sheets for multi-sheet support
        nlohmann::json response;
        response["has_changes"] = HasAgentPendingChanges();

        // Get all affected sheets from the tracker
        nlohmann::json sheetsArray = nlohmann::json::array();
        if( HasAgentPendingChanges() && m_agentChangeTracker )
        {
            std::set<wxString> affectedSheets = m_agentChangeTracker->GetAffectedSheets();
            for( const wxString& sheetPath : affectedSheets )
            {
                sheetsArray.push_back( sheetPath.ToStdString() );
            }
        }
        response["affected_sheets"] = sheetsArray;

        // Keep legacy single sheet_path for backwards compatibility
        if( HasAgentPendingChanges() && m_agentChangedSheetPath.size() > 0 )
        {
            response["sheet_path"] = m_agentChangedSheetPath.PathHumanReadable( false ).ToStdString();
        }
        else
        {
            response["sheet_path"] = "";
        }

        payload = response.dump();
        break;
    }

    case MAIL_AGENT_APPROVE:
    {
        // Approve pending agent changes
        // Payload can be JSON with optional "sheet_path" for per-sheet approval
        // If sheet_path is empty or missing, approve all sheets
        wxString targetSheet;
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            if( j.contains( "sheet_path" ) && !j["sheet_path"].get<std::string>().empty() )
            {
                targetSheet = wxString::FromUTF8( j["sheet_path"].get<std::string>() );
            }
        }
        catch( ... )
        {
            // Legacy format or parse error - approve all
        }

        if( m_showingAgentBefore )
            ShowAgentChangesAfter();

        if( targetSheet.IsEmpty() )
        {
            // Approve all sheets
            ClearAgentPendingChanges();
        }
        else
        {
            // Approve only the specified sheet
            ApproveAgentChangesOnSheet( targetSheet );
        }
        break;
    }

    case MAIL_AGENT_REJECT:
    {
        // Reject pending agent changes
        // Payload can be JSON with optional "sheet_path" for per-sheet rejection
        wxString targetSheet;
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            if( j.contains( "sheet_path" ) && !j["sheet_path"].get<std::string>().empty() )
            {
                targetSheet = wxString::FromUTF8( j["sheet_path"].get<std::string>() );
            }
        }
        catch( ... )
        {
            // Legacy format or parse error - reject all
        }

        if( m_showingAgentBefore )
            ShowAgentChangesAfter();

        if( targetSheet.IsEmpty() )
        {
            // Reject all sheets
            RevertAgentChanges();
        }
        else
        {
            // Reject only the specified sheet
            RejectAgentChangesOnSheet( targetSheet );
        }
        break;
    }

    // DISABLED: Users view changes via Changes tab
    // case MAIL_AGENT_VIEW_CHANGES:
    // {
    //     // Navigate to the sheet with agent changes and zoom to the bounding box
    //     // Payload can be JSON with optional "sheet_path" to specify which sheet to view
    //     wxString targetSheetPath;
    //     try
    //     {
    //         nlohmann::json j = nlohmann::json::parse( payload );
    //         if( j.contains( "sheet_path" ) && !j["sheet_path"].get<std::string>().empty() )
    //         {
    //             targetSheetPath = wxString::FromUTF8( j["sheet_path"].get<std::string>() );
    //         }
    //     }
    //     catch( ... )
    //     {
    //         // Legacy format or parse error - no specific sheet
    //     }
    //
    //     BOX2I changedBBox = ComputeTrackedItemsBBox();
    //
    //     wxLogInfo( "MAIL_AGENT_VIEW_CHANGES: hasChanges=%d, bbox=(%d,%d,%d,%d)",
    //                m_hasAgentPendingChanges,
    //                changedBBox.GetX(), changedBBox.GetY(),
    //                changedBBox.GetWidth(), changedBBox.GetHeight() );
    //
    //     if( m_hasAgentPendingChanges && changedBBox.GetWidth() > 0 )
    //     {
    //         // Bring editor to front
    //         Raise();
    //         // Find the target sheet to navigate to
    //         SCH_SHEET_PATH targetSheet;
    //         bool needSheetChange = false;
    //
    //         if( !targetSheetPath.IsEmpty() )
    //         {
    //             // Navigate to the specified sheet from payload
    //             SCH_SHEET_LIST sheets = Schematic().Hierarchy();
    //             for( const SCH_SHEET_PATH& path : sheets )
    //             {
    //                 if( path.PathHumanReadable( false ) == targetSheetPath )
    //                 {
    //                     targetSheet = path;
    //                     needSheetChange = ( path != GetCurrentSheet() );
    //                     break;
    //                 }
    //             }
    //         }
    //         else if( m_agentChangedSheetPath != GetCurrentSheet() && m_agentChangedSheetPath.size() > 0 )
    //         {
    //             // Fallback to the legacy m_agentChangedSheetPath
    //             targetSheet = m_agentChangedSheetPath;
    //             needSheetChange = true;
    //         }
    //
    //         if( needSheetChange && targetSheet.size() > 0 )
    //         {
    //             GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet,
    //                                                           &targetSheet );
    //
    //             // Recompute bbox after sheet change (for the new sheet)
    //             changedBBox = ComputeTrackedItemsBBox();
    //
    //             // After sheet change, re-register the diff overlay on the current view
    //             // Capture sheet path for per-sheet approve/reject via overlay buttons
    //             wxString currentSheetPath = GetCurrentSheet().PathHumanReadable( false );
    //             wxString capturedSheetPath = currentSheetPath;
    //             DIFF_CALLBACKS callbacks;
    //             callbacks.onApprove = [this, capturedSheetPath]() {
    //                 if( m_showingAgentBefore )
    //                     ShowAgentChangesAfter();
    //                 // Only approve changes on this sheet, not all sheets
    //                 ApproveAgentChangesOnSheet( capturedSheetPath );
    //                 // Notify agent frame with sheet path so it can update its UI
    //                 nlohmann::json j;
    //                 j["editor"] = "sch";
    //                 j["sheet_path"] = capturedSheetPath.ToStdString();
    //                 std::string payload = j.dump();
    //                 Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_DIFF_CLEARED, payload );
    //             };
    //             callbacks.onReject = [this, capturedSheetPath]() {
    //                 if( m_showingAgentBefore )
    //                     ShowAgentChangesAfter();
    //                 // Only reject changes on this sheet, not all sheets
    //                 RejectAgentChangesOnSheet( capturedSheetPath );
    //                 // Notify agent frame with sheet path so it can update its UI
    //                 nlohmann::json j;
    //                 j["editor"] = "sch";
    //                 j["sheet_path"] = capturedSheetPath.ToStdString();
    //                 std::string payload = j.dump();
    //                 Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_DIFF_CLEARED, payload );
    //             };
    //             callbacks.onUndo = [this]() { ShowAgentChangesBefore(); };
    //             callbacks.onRedo = [this]() { ShowAgentChangesAfter(); };
    //             callbacks.onRefresh = [this]() {
    //                 if( GetCanvas() )
    //                     GetCanvas()->Refresh();
    //             };
    //
    //             BBOX_COMPUTE_CALLBACK bboxCallback = [this]() -> BOX2I {
    //                 return ComputeTrackedItemsBBox();
    //             };
    //
    //             DIFF_MANAGER::GetInstance().RegisterOverlay(
    //                 GetCanvas()->GetView(),
    //                 m_agentChangeTracker.get(),
    //                 currentSheetPath,
    //                 callbacks,
    //                 bboxCallback );
    //         }
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
        if( !eeconfig()->m_CrossProbing.on_selection )
            break;

        KI_FALLTHROUGH;

    case MAIL_SELECTION_FORCE:
    {
        // $SELECT: 0,<spec1>,<spec2>,<spec3>
        // Try to select specified items.

        // $SELECT: 1,<spec1>,<spec2>,<spec3>
        // Select and focus on <spec1> item, select other specified items that are on the
        // same sheet.

        std::string prefix = "$SELECT: ";

        std::string paramStr = payload.substr( prefix.size() );

        // Empty/broken command: we need at least 2 chars for sync string.
        if( paramStr.size() < 2 )
            break;

        std::string syncStr = paramStr.substr( 2 );

        bool focusOnFirst = ( paramStr[0] == '1' );

        std::optional<std::tuple<SCH_SHEET_PATH, SCH_ITEM*, std::vector<SCH_ITEM*>>> findRet =
                findItemsFromSyncSelection( Schematic(), syncStr, focusOnFirst );

        if( findRet )
        {
            auto& [sheetPath, focusItem, items] = *findRet;

            m_syncingPcbToSchSelection = true; // recursion guard

            GetToolManager()->GetTool<SCH_SELECTION_TOOL>()->SyncSelection( sheetPath, focusItem, items );

            m_syncingPcbToSchSelection = false;

            if( eeconfig()->m_CrossProbing.flash_selection )
            {
                wxLogTrace( traceCrossProbeFlash, "MAIL_SELECTION(_FORCE): flash enabled, items=%zu", items.size() );
                if( items.empty() )
                {
                    wxLogTrace( traceCrossProbeFlash, "MAIL_SELECTION(_FORCE): nothing to flash" );
                }
                else
                {
                    std::vector<SCH_ITEM*> itemPtrs;
                    std::copy( items.begin(), items.end(), std::back_inserter( itemPtrs ) );

                    StartCrossProbeFlash( itemPtrs );
                }
            }
            else
            {
                wxLogTrace( traceCrossProbeFlash, "MAIL_SELECTION(_FORCE): flash disabled" );
            }
        }

        break;
    }

    case MAIL_SCH_GET_NETLIST:
    {
        if( !payload.empty() )
        {
            wxString annotationMessage( payload );

            // Ensure schematic is OK for netlist creation (especially that it is fully annotated):
            if( !ReadyToNetlist( annotationMessage ) )
                return;
        }

        if( ADVANCED_CFG::GetCfg().m_IncrementalConnectivity )
            RecalculateConnections( nullptr, GLOBAL_CLEANUP );

        NETLIST_EXPORTER_KICAD exporter( &Schematic() );
        STRING_FORMATTER       formatter;

        exporter.Format( &formatter, GNL_ALL | GNL_OPT_KICAD );

        payload = formatter.GetString();
        break;
    }

    case MAIL_SCH_GET_ITEM:
    {
        KIID           uuid( payload );
        SCH_SHEET_PATH path;

        if( SCH_ITEM* item = m_schematic->ResolveItem( uuid, &path, true ) )
        {
            if( item->Type() == SCH_SHEET_T )
                payload = static_cast<SCH_SHEET*>( item )->GetShownName( false );
            else if( item->Type() == SCH_SYMBOL_T )
                payload = static_cast<SCH_SYMBOL*>( item )->GetRef( &path, true );
            else
                payload = item->GetFriendlyName();
        }

        break;
    }

    case MAIL_ASSIGN_FOOTPRINTS:
        try
        {
            SCH_EDITOR_CONTROL* controlTool = m_toolManager->GetTool<SCH_EDITOR_CONTROL>();
            controlTool->AssignFootprints( payload );
        }
        catch( const IO_ERROR& )
        {
        }
        break;

    case MAIL_SCH_REFRESH:
    {
        // If a file path is provided, reload that specific screen from disk
        if( !payload.empty() )
        {
            wxString filePath = wxString::FromUTF8( payload );

            // Find the screen matching this file path
            SCH_SCREENS screens( Schematic().Root() );

            for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
            {
                if( screen->GetFileName() == filePath )
                {
                    // Found the screen - reload its content from disk
                    try
                    {
                        FILE_LINE_READER reader( filePath );
                        SCH_IO_KICAD_SEXPR_PARSER parser( &reader );

                        // Clear existing items from the screen
                        screen->Clear( true );

                        // Find the sheet that owns this screen to pass to parser
                        SCH_SHEET* owningSheet = nullptr;
                        SCH_SHEET_LIST sheets = Schematic().Hierarchy();

                        for( const SCH_SHEET_PATH& sheetPath : sheets )
                        {
                            if( sheetPath.LastScreen() == screen )
                            {
                                owningSheet = sheetPath.Last();
                                break;
                            }
                        }

                        if( owningSheet )
                        {
                            parser.ParseSchematic( owningSheet );
                        }

                        wxLogInfo( "MAIL_SCH_REFRESH: Reloaded screen from %s", filePath );
                    }
                    catch( const IO_ERROR& e )
                    {
                        wxLogError( "MAIL_SCH_REFRESH: Failed to reload %s: %s",
                                    filePath, e.What() );
                    }
                    break;
                }
            }
        }

        TestDanglingEnds();

        // Use HardRedraw() to properly refresh the canvas after reloading from disk.
        // HardRedraw() clears item caches and calls DisplaySheet() + ForceRefresh().
        // This does NOT reset zoom/pan - that's handled separately in DisplayCurrentSheet().
        HardRedraw();
        break;
    }

    case MAIL_IMPORT_FILE:
    {
        // Extract file format type and path (plugin type, path and properties keys, values
        // separated with \n)
        std::stringstream ss( payload );
        char              delim = '\n';

        std::string formatStr;
        wxCHECK( std::getline( ss, formatStr, delim ), /* void */ );

        std::string fnameStr;
        wxCHECK( std::getline( ss, fnameStr, delim ), /* void */ );

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

        do
        {
            std::string key, value;

            if( !std::getline( ss, key, delim ) )
                break;

            std::getline( ss, value, delim ); // We may want an empty string as value

            props.emplace( key, value );

        } while( true );

        if( importFormat >= 0 )
            importFile( fnameStr, importFormat, props.empty() ? nullptr : &props );

        break;
    }

    case MAIL_SCH_SAVE:
        if( SaveProject() )
            payload = "success";

        break;

    case MAIL_SCH_UPDATE: m_toolManager->RunAction( ACTIONS::updateSchematicFromPcb ); break;

    case MAIL_RELOAD_LIB:
        m_designBlocksPane->RefreshLibs();
        SyncView();
        break;

    //=========================================================================
    // Concurrent editing support - transaction management
    //=========================================================================

    case MAIL_AGENT_BEGIN_TRANSACTION:
    {
        // Agent is starting/continuing a transaction - enable conflict detection
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            wxString sheetUuid = j.value( "sheet_uuid", "" );

            // Register this frame as listening for conflicts
            m_agentTransactionActive = true;

            // If we already have a valid target sheet from a previous tool call in
            // this conversation turn, keep using it. This prevents the target from
            // changing when the user navigates between sheets during agent execution.
            if( m_agentTargetSheetUuid != NilUuid() )
            {
                // Reusing existing target sheet
            }
            else if( !sheetUuid.IsEmpty() )
            {
                // Explicit sheet UUID provided
                m_agentTargetSheetUuid = KIID( sheetUuid.ToStdString() );
            }
            else
            {
                // First tool call - capture the current sheet as the target
                // This allows the agent to work on the sheet the user is currently viewing
                // while the user can navigate away to other sheets
                const SCH_SHEET_PATH& currentPath = GetCurrentSheet();
                if( currentPath.size() > 0 )
                {
                    m_agentTargetSheetUuid = currentPath.Last()->m_Uuid;
                }
                else
                {
                    m_agentTargetSheetUuid = NilUuid();
                }
            }
        }
        catch( ... )
        {
            wxLogWarning( "Failed to parse MAIL_AGENT_BEGIN_TRANSACTION payload" );
        }
        break;
    }

    case MAIL_AGENT_END_TRANSACTION:
    {
        // Agent is ending transaction
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            bool commit = j.value( "commit", false );

            wxLogDebug( "MAIL_AGENT_END_TRANSACTION: commit=%d", commit );

            m_agentTransactionActive = false;
        }
        catch( ... )
        {
            wxLogWarning( "Failed to parse MAIL_AGENT_END_TRANSACTION payload" );
        }
        break;
    }

    case MAIL_AGENT_RESET_TARGET_SHEET:
    {
        // Reset target sheet for new conversation turn - sent when user sends a new message
        m_agentTargetSheetUuid = NilUuid();
        m_agentTransactionActive = false;
        break;
    }

    case MAIL_AGENT_WORKING_SET:
    {
        // Update the agent's working set of items for conflict detection
        // Now uses the AGENT_CHANGE_TRACKER instead of a raw set
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );

            AGENT_CHANGE_TRACKER* tracker = GetAgentChangeTracker();
            if( tracker )
            {
                tracker->ClearTrackedItems();

                wxString sheetPath = j.value( "sheet_path", "" );
                if( sheetPath.empty() )
                    sheetPath = GetCurrentSheet().PathAsString();

                if( j.contains( "items" ) && j["items"].is_array() )
                {
                    for( const auto& item : j["items"] )
                    {
                        if( item.is_string() )
                        {
                            tracker->TrackItem( KIID( item.get<std::string>() ), sheetPath );
                        }
                    }
                }

                wxLogDebug( "MAIL_AGENT_WORKING_SET: %zu items", tracker->GetTrackedItemCount() );
            }
        }
        catch( ... )
        {
            wxLogWarning( "Failed to parse MAIL_AGENT_WORKING_SET payload" );
        }
        break;
    }

    case MAIL_AGENT_TARGET_SHEET:
    {
        // Set the target sheet for agent operations
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            wxString sheetUuid = j.value( "sheet_uuid", "" );

            m_agentTargetSheetUuid = KIID( sheetUuid.ToStdString() );

            wxLogDebug( "MAIL_AGENT_TARGET_SHEET: %s", sheetUuid );
        }
        catch( ... )
        {
            wxLogWarning( "Failed to parse MAIL_AGENT_TARGET_SHEET payload" );
        }
        break;
    }

    case MAIL_CONFLICT_RESOLVED:
    {
        // A conflict was resolved by the user
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            wxString itemUuid = j.value( "item_uuid", "" );
            wxString resolution = j.value( "resolution", "" );

            wxLogDebug( "MAIL_CONFLICT_RESOLVED: item=%s resolution=%s", itemUuid, resolution );

            // Remove the item from the agent tracker if being tracked
            if( m_agentChangeTracker && m_agentChangeTracker->IsTracked( KIID( itemUuid.ToStdString() ) ) )
            {
                m_agentChangeTracker->UntrackItem( KIID( itemUuid.ToStdString() ) );
            }
        }
        catch( ... )
        {
            wxLogWarning( "Failed to parse MAIL_CONFLICT_RESOLVED payload" );
        }
        break;
    }

    //=========================================================================
    // File edit session management
    //=========================================================================

    case MAIL_AGENT_FILE_EDIT_BEGIN:
    {
        OnAgentFileEditBegin( payload );
        break;
    }

    case MAIL_AGENT_FILE_EDIT_COMPLETE:
    {
        OnAgentFileEditComplete( payload );
        break;
    }

    case MAIL_AGENT_FILE_EDIT_ABORT:
    {
        OnAgentFileEditAbort();
        break;
    }

    case MAIL_AGENT_REFRESH_DIFF:
    {
        // Refresh the diff overlay - items may have moved
        DIFF_MANAGER::GetInstance().RefreshOverlay( GetCanvas()->GetView() );
        break;
    }

    case MAIL_AGENT_TRACKING_MODE:
    {
        try
        {
            nlohmann::json j = nlohmann::json::parse( payload );
            bool enabled = j.value( "tracking", false );

            if( GetCanvas() && GetCanvas()->GetView() )
            {
                DIFF_MANAGER::GetInstance().SetTrackingMode( GetCanvas()->GetView(), enabled );
                GetCanvas()->Refresh();
            }
        }
        catch( const std::exception& e )
        {
            wxLogWarning( "Failed to parse MAIL_AGENT_TRACKING_MODE payload: %s", e.what() );
        }
        break;
    }

    default:;
    }
}
