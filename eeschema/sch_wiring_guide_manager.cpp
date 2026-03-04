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
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "sch_wiring_guide_manager.h"
#include "sch_agent_wiring.h"
#include "sch_edit_frame.h"
#include "sch_screen.h"
#include "sch_symbol.h"
#include "sch_pin.h"
#include "sch_label.h"
#include "sch_commit.h"
#include "schematic.h"
#include "connection_graph.h"
#include <agent_change_tracker.h>
#include <set>


SCH_WIRING_GUIDE_MANAGER::SCH_WIRING_GUIDE_MANAGER( SCH_EDIT_FRAME* aFrame ) :
    m_frame( aFrame ),
    m_globalVisible( true )
{
}


SCH_WIRING_GUIDE_MANAGER::~SCH_WIRING_GUIDE_MANAGER()
{
}


void SCH_WIRING_GUIDE_MANAGER::ScanSymbolsForWiring()
{
    m_guides.clear();
    m_refToKiid.clear();

    if( !m_frame )
        return;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return;

    // First pass: build ref to KIID mapping
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        wxString ref = symbol->GetRef( &m_frame->GetCurrentSheet() );
        m_refToKiid[ref] = symbol->m_Uuid;
    }

    // Second pass: parse Agent_Wiring fields
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        ParseSymbolWiring( symbol );
    }

    // Resolve target positions and check completion states
    for( auto& guide : m_guides )
    {
        ResolveTargetPosition( guide );
    }

    RefreshGuideStates();
}


void SCH_WIRING_GUIDE_MANAGER::ParseSymbolWiring( SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return;

    wxString symbolRef = aSymbol->GetRef( &m_frame->GetCurrentSheet() );

    // Get the Agent_Wiring field
    const SCH_FIELD* field = aSymbol->GetField( SCH_AGENT_WIRING::FIELD_NAME );
    if( !field )
        return;

    wxString fieldValue = field->GetText();
    if( fieldValue.IsEmpty() )
        return;

    // Parse the wiring entries
    std::vector<SCH_AGENT_WIRING::WIRING_ENTRY> entries =
        SCH_AGENT_WIRING::ParseAgentWiring( fieldValue );

    for( const auto& entry : entries )
    {
        WIRING_GUIDE guide;
        guide.sourceSymbolId = aSymbol->m_Uuid;
        guide.sourceRef = symbolRef;
        guide.sourcePin = entry.pin;
        guide.targetRef = entry.target;
        guide.targetResolved = false;
        guide.isComplete = false;
        guide.isVisible = true;

        // Get source pin position
        if( GetPinPosition( aSymbol, entry.pin, guide.sourcePos ) )
        {
            m_guides.push_back( guide );
        }
    }
}


bool SCH_WIRING_GUIDE_MANAGER::ResolveTargetPosition( WIRING_GUIDE& aGuide )
{
    aGuide.targetResolved = false;

    if( SCH_AGENT_WIRING::IsPinReference( aGuide.targetRef ) )
    {
        // Target is a pin reference like "U1:PA0"
        wxString symbolRef, pinName;
        if( SCH_AGENT_WIRING::ParsePinReference( aGuide.targetRef, symbolRef, pinName ) )
        {
            SCH_SYMBOL* targetSymbol = FindSymbolByRef( symbolRef );
            if( targetSymbol )
            {
                if( GetPinPosition( targetSymbol, pinName, aGuide.targetPos ) )
                {
                    // Debug: log all pins of target symbol for comparison
                    wxLogMessage( "WIRING_GUIDE: Resolved %s -> %s at (%d,%d). Target symbol %s pins:",
                                  aGuide.sourceRef, aGuide.targetRef,
                                  aGuide.targetPos.x, aGuide.targetPos.y, symbolRef );
                    for( SCH_PIN* pin : targetSymbol->GetPins( &m_frame->GetCurrentSheet() ) )
                    {
                        wxLogMessage( "  pin %s (name=%s) at (%d,%d)",
                                      pin->GetNumber(), pin->GetName(),
                                      pin->GetPosition().x, pin->GetPosition().y );
                    }

                    aGuide.targetResolved = true;
                    return true;
                }
            }
        }
    }
    else
    {
        // Target is a net name like "VCC" or "GND"
        SCH_SCREEN* screen = m_frame->GetScreen();
        if( !screen )
            return false;

        // Try to find a power symbol with this net
        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( symbol->GetLibSymbolRef() && symbol->GetLibSymbolRef()->IsPower() )
            {
                wxString powerNet = symbol->GetValue( false, &m_frame->GetCurrentSheet(), false );

                if( powerNet.IsSameAs( aGuide.targetRef, false ) )
                {
                    std::vector<SCH_PIN*> pins = symbol->GetPins( &m_frame->GetCurrentSheet() );
                    if( !pins.empty() )
                    {
                        aGuide.targetPos = pins[0]->GetPosition();
                        aGuide.targetResolved = true;
                        return true;
                    }
                }
            }
        }

        // Also check for global/hierarchical labels with this net name
        for( SCH_ITEM* item : screen->Items().OfType( SCH_GLOBAL_LABEL_T ) )
        {
            SCH_GLOBALLABEL* label = static_cast<SCH_GLOBALLABEL*>( item );
            if( label->GetText().IsSameAs( aGuide.targetRef, false ) )
            {
                aGuide.targetPos = label->GetPosition();
                aGuide.targetResolved = true;
                return true;
            }
        }
    }

    return false;
}


void SCH_WIRING_GUIDE_MANAGER::RefreshGuideStates()
{
    if( !m_frame )
        return;

    for( auto& guide : m_guides )
    {
        if( guide.targetResolved )
        {
            bool wasComplete = guide.isComplete;
            guide.isComplete = CheckConnectionExists( guide.sourcePos, guide.targetPos );

            // Log state changes for debugging
            if( wasComplete != guide.isComplete )
            {
                wxLogMessage( "WIRING_GUIDE: %s:%s -> %s changed to %s",
                              guide.sourceRef, guide.sourcePin, guide.targetRef,
                              guide.isComplete ? "COMPLETE" : "INCOMPLETE" );
            }
        }
        else
        {
            guide.isComplete = false;
        }
    }
}


bool SCH_WIRING_GUIDE_MANAGER::CheckConnectionExists( const VECTOR2I& aStart, const VECTOR2I& aEnd )
{
    // Use the connectivity graph to check if the two positions are on the same net
    CONNECTION_GRAPH* connGraph = m_frame->Schematic().ConnectionGraph();
    if( !connGraph )
        return false;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return false;

    // Find pins at both positions
    SCH_PIN* startPin = nullptr;
    SCH_PIN* endPin = nullptr;
    wxString startPinInfo, endPinInfo;

    // Schematic uses ~10000 IU per mm, use 0.5mm tolerance (5000 IU)
    const int tolerance = 5000;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() == SCH_SYMBOL_T )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
            wxString symRef = symbol->GetRef( &m_frame->GetCurrentSheet() );

            for( SCH_PIN* pin : symbol->GetPins( &m_frame->GetCurrentSheet() ) )
            {
                VECTOR2I pinPos = pin->GetPosition();

                if( ( pinPos - aStart ).EuclideanNorm() < tolerance )
                {
                    startPin = pin;
                    startPinInfo = wxString::Format( "%s:%s (name=%s)",
                                                     symRef, pin->GetNumber(), pin->GetName() );
                }
                if( ( pinPos - aEnd ).EuclideanNorm() < tolerance )
                {
                    endPin = pin;
                    endPinInfo = wxString::Format( "%s:%s (name=%s)",
                                                   symRef, pin->GetNumber(), pin->GetName() );
                }
            }
        }
    }

    if( !startPin || !endPin )
    {
        // Log MISS - one or both pins not found at expected positions
        wxLogMessage( "WIRING_GUIDE: MISS at (%d,%d)->(%d,%d) start=%s end=%s",
                      aStart.x, aStart.y, aEnd.x, aEnd.y,
                      startPinInfo.empty() ? "NOT FOUND" : startPinInfo,
                      endPinInfo.empty() ? "NOT FOUND" : endPinInfo );
        return false;
    }

    // Use the CONNECTION_GRAPH to check if both pins are in the same subgraph
    // This is more reliable than checking pin->Connection() which may have stale data
    CONNECTION_SUBGRAPH* startSubgraph = connGraph->GetSubgraphForItem( startPin );
    CONNECTION_SUBGRAPH* endSubgraph = connGraph->GetSubgraphForItem( endPin );

    if( startSubgraph && endSubgraph )
    {
        // If both pins are in the same subgraph, they're connected
        if( startSubgraph == endSubgraph )
        {
            wxLogMessage( "WIRING_GUIDE: Pins connected (same subgraph): start=%s end=%s",
                          startPinInfo, endPinInfo );
            return true;
        }

        // Different subgraphs - not connected
        wxLogMessage( "WIRING_GUIDE: Pins in different subgraphs: start=%s end=%s",
                      startPinInfo, endPinInfo );
        return false;
    }

    // One or both pins not in any subgraph - check if they're NO NET
    if( !startSubgraph && !endSubgraph )
    {
        wxLogMessage( "WIRING_GUIDE: Both pins have no subgraph (unconnected): start=%s end=%s",
                      startPinInfo, endPinInfo );
        return false;
    }

    wxLogMessage( "WIRING_GUIDE: Mixed subgraph state: start=%s (sg=%d) end=%s (sg=%d)",
                  startPinInfo, startSubgraph != nullptr, endPinInfo, endSubgraph != nullptr );
    return false;
}


void SCH_WIRING_GUIDE_MANAGER::DismissGuide( const KIID& aSymbolId, const wxString& aPin )
{
    if( !m_frame )
        return;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return;

    SCH_SYMBOL* symbol = nullptr;
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        if( item->m_Uuid == aSymbolId )
        {
            symbol = static_cast<SCH_SYMBOL*>( item );
            break;
        }
    }

    if( !symbol )
        return;

    SCH_FIELD* field = symbol->GetField( SCH_AGENT_WIRING::FIELD_NAME );
    if( !field )
        return;

    wxString currentValue = field->GetText();
    wxString newValue = SCH_AGENT_WIRING::RemoveWiringEntry( currentValue, aPin );

    // Use commit for undo support
    SCH_COMMIT commit( m_frame );
    commit.Modify( symbol, screen );

    if( newValue.IsEmpty() )
    {
        // Remove the field entirely if no entries remain
        symbol->RemoveField( SCH_AGENT_WIRING::FIELD_NAME );
    }
    else
    {
        field->SetText( newValue );
    }

    commit.Push( _( "Dismiss Wiring Recommendation" ) );

    // Rescan to update guide list
    ScanSymbolsForWiring();
}


void SCH_WIRING_GUIDE_MANAGER::DismissAllForSymbol( const KIID& aSymbolId )
{
    if( !m_frame )
        return;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return;

    SCH_SYMBOL* symbol = nullptr;
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        if( item->m_Uuid == aSymbolId )
        {
            symbol = static_cast<SCH_SYMBOL*>( item );
            break;
        }
    }

    if( !symbol )
        return;

    const SCH_FIELD* field = symbol->GetField( SCH_AGENT_WIRING::FIELD_NAME );
    if( !field )
        return;

    // Use commit for undo support
    SCH_COMMIT commit( m_frame );
    commit.Modify( symbol, screen );

    symbol->RemoveField( SCH_AGENT_WIRING::FIELD_NAME );

    commit.Push( _( "Dismiss All Wiring Recommendations" ) );

    // Rescan to update guide list
    ScanSymbolsForWiring();
}


void SCH_WIRING_GUIDE_MANAGER::SetGuideVisible( const KIID& aSymbolId, const wxString& aPin,
                                                 bool aVisible )
{
    for( auto& guide : m_guides )
    {
        if( guide.sourceSymbolId == aSymbolId && guide.sourcePin == aPin )
        {
            guide.isVisible = aVisible;
            break;
        }
    }
}


void SCH_WIRING_GUIDE_MANAGER::SetAllGuidesVisible( bool aVisible )
{
    m_globalVisible = aVisible;

    for( auto& guide : m_guides )
    {
        guide.isVisible = aVisible;
    }
}


std::vector<SCH_WIRING_GUIDE_MANAGER::WIRING_GUIDE>
SCH_WIRING_GUIDE_MANAGER::GetActiveGuides() const
{
    std::vector<WIRING_GUIDE> active;

    if( !m_globalVisible )
        return active;

    for( const auto& guide : m_guides )
    {
        bool pending = IsGuidePending( guide.sourceSymbolId );

        // Only show guides that are:
        // - Not completed (not yet wired)
        // - Visible (not hidden by user)
        // - Target resolved (we know where to draw the line)
        // - Not pending approval (symbol not in change tracker)
        if( !guide.isComplete && guide.isVisible && guide.targetResolved && !pending )
        {
            active.push_back( guide );
        }
    }

    return active;
}


void SCH_WIRING_GUIDE_MANAGER::GetProgress( int& aTotal, int& aCompleted ) const
{
    aTotal = static_cast<int>( m_guides.size() );
    aCompleted = 0;

    for( const auto& guide : m_guides )
    {
        if( guide.isComplete )
            aCompleted++;
    }
}


bool SCH_WIRING_GUIDE_MANAGER::IsGuidePending( const KIID& aSymbolId ) const
{
    AGENT_CHANGE_TRACKER* tracker = m_frame->GetAgentChangeTracker();
    if( tracker )
    {
        return tracker->IsTracked( aSymbolId );
    }
    return false;
}


void SCH_WIRING_GUIDE_MANAGER::OnSchematicChanged()
{
    wxLogMessage( "WIRING_GUIDE: OnSchematicChanged called" );

    // Log wire count on the current screen for debugging
    if( m_frame && m_frame->GetScreen() )
    {
        int wireCount = 0;
        for( SCH_ITEM* item : m_frame->GetScreen()->Items() )
        {
            if( item->Type() == SCH_LINE_T )
                wireCount++;
        }
        wxLogMessage( "WIRING_GUIDE: Current screen has %d wires", wireCount );
    }

    // Re-scan symbols to update positions and resolve any new/moved targets
    ScanSymbolsForWiring();
}


void SCH_WIRING_GUIDE_MANAGER::Clear()
{
    m_guides.clear();
    m_refToKiid.clear();
}


int SCH_WIRING_GUIDE_MANAGER::GetIncompleteCount() const
{
    int count = 0;
    for( const auto& guide : m_guides )
    {
        if( !guide.isComplete )
            count++;
    }
    return count;
}


void SCH_WIRING_GUIDE_MANAGER::RefreshGuidePositions()
{
    if( !m_frame )
        return;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return;

    // Build a quick lookup from KIID to symbol pointer
    std::map<KIID, SCH_SYMBOL*> kiidToSymbol;
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        kiidToSymbol[symbol->m_Uuid] = symbol;
    }

    // Update positions for each guide
    for( auto& guide : m_guides )
    {
        // Update source position
        auto srcIt = kiidToSymbol.find( guide.sourceSymbolId );
        if( srcIt != kiidToSymbol.end() )
        {
            GetPinPosition( srcIt->second, guide.sourcePin, guide.sourcePos );
        }

        // Update target position
        if( guide.targetResolved )
        {
            if( SCH_AGENT_WIRING::IsPinReference( guide.targetRef ) )
            {
                // Target is a pin reference like "U1:PA0"
                wxString symbolRef, pinName;
                if( SCH_AGENT_WIRING::ParsePinReference( guide.targetRef, symbolRef, pinName ) )
                {
                    SCH_SYMBOL* targetSymbol = FindSymbolByRef( symbolRef );
                    if( targetSymbol )
                    {
                        GetPinPosition( targetSymbol, pinName, guide.targetPos );
                    }
                }
            }
            else
            {
                // Target is a net name like "VCC" - find power symbol
                for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
                {
                    SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
                    if( symbol->GetLibSymbolRef() && symbol->GetLibSymbolRef()->IsPower() )
                    {
                        wxString powerNet = symbol->GetValue( false, &m_frame->GetCurrentSheet(), false );
                        if( powerNet.IsSameAs( guide.targetRef, false ) )
                        {
                            std::vector<SCH_PIN*> pins = symbol->GetPins( &m_frame->GetCurrentSheet() );
                            if( !pins.empty() )
                            {
                                guide.targetPos = pins[0]->GetPosition();
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}


SCH_SYMBOL* SCH_WIRING_GUIDE_MANAGER::FindSymbolByRef( const wxString& aRef )
{
    auto it = m_refToKiid.find( aRef );
    if( it == m_refToKiid.end() )
        return nullptr;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return nullptr;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        if( item->m_Uuid == it->second )
        {
            return static_cast<SCH_SYMBOL*>( item );
        }
    }

    return nullptr;
}


bool SCH_WIRING_GUIDE_MANAGER::GetPinPosition( SCH_SYMBOL* aSymbol, const wxString& aPin,
                                                VECTOR2I& aPos )
{
    if( !aSymbol )
        return false;

    std::vector<SCH_PIN*> pins = aSymbol->GetPins( &m_frame->GetCurrentSheet() );

    for( SCH_PIN* pin : pins )
    {
        // Match by pin number or name
        if( pin->GetNumber() == aPin || pin->GetName() == aPin )
        {
            aPos = pin->GetPosition();
            return true;
        }
    }

    return false;
}
