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
#include <base_units.h>
#include <algorithm>
#include <set>


SCH_WIRING_GUIDE_MANAGER::SCH_WIRING_GUIDE_MANAGER( SCH_EDIT_FRAME* aFrame ) :
    m_frame( aFrame ),
    m_globalVisible( true ),
    m_hasActiveWiring( false )
{
}


SCH_WIRING_GUIDE_MANAGER::~SCH_WIRING_GUIDE_MANAGER()
{
}


void SCH_WIRING_GUIDE_MANAGER::ScanSymbolsForWiring()
{
    m_guides.clear();
    m_refToKiid.clear();

    // Clear any active wiring state when rescanning
    m_hasActiveWiring = false;

    if( !m_frame )
        return;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return;

    wxLogMessage( "WIRING_GUIDE: ScanSymbolsForWiring starting" );

    // First pass: build ref to KIID mapping
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        wxString ref = symbol->GetRef( &m_frame->GetCurrentSheet() );
        m_refToKiid[ref] = symbol->m_Uuid;
    }

    wxLogMessage( "WIRING_GUIDE: Found %zu symbols in ref map:", m_refToKiid.size() );
    for( const auto& pair : m_refToKiid )
    {
        wxLogMessage( "  %s -> %s", pair.first, pair.second.AsString() );
    }

    // Second pass: parse Agent_Wiring fields
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        ParseSymbolWiring( symbol );
    }

    wxLogMessage( "WIRING_GUIDE: Parsed %zu guides total", m_guides.size() );

    // Resolve target positions and check completion states
    int resolved = 0;
    for( auto& guide : m_guides )
    {
        if( ResolveTargetPosition( guide ) )
            resolved++;
    }

    wxLogMessage( "WIRING_GUIDE: Resolved %d/%zu guide targets", resolved, m_guides.size() );

    // Remove guides that couldn't be resolved (target symbol doesn't exist)
    // This handles orphaned Agent_Wiring entries after changes are rejected
    size_t beforeCleanup = m_guides.size();
    m_guides.erase(
        std::remove_if( m_guides.begin(), m_guides.end(),
                        []( const WIRING_GUIDE& g ) { return !g.targetResolved; } ),
        m_guides.end() );

    if( m_guides.size() < beforeCleanup )
    {
        wxLogMessage( "WIRING_GUIDE: Removed %zu unresolved guides (orphaned references)",
                      beforeCleanup - m_guides.size() );
    }

    RefreshGuideStates();

    // Summary
    int complete = 0, incomplete = 0;
    for( const auto& guide : m_guides )
    {
        if( guide.isComplete )
            complete++;
        else
            incomplete++;
    }
    wxLogMessage( "WIRING_GUIDE: Summary: %d complete, %d incomplete out of %zu total",
                  complete, incomplete, m_guides.size() );
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

    wxLogMessage( "WIRING_GUIDE: Parsing Agent_Wiring for %s: \"%s\"", symbolRef, fieldValue );

    // Log available pins for debugging
    wxLogMessage( "WIRING_GUIDE: Available pins on %s:", symbolRef );
    for( SCH_PIN* pin : aSymbol->GetPins( &m_frame->GetCurrentSheet() ) )
    {
        wxLogMessage( "  pin number=\"%s\" name=\"%s\" at (%d,%d)",
                      pin->GetNumber(), pin->GetName(),
                      pin->GetPosition().x, pin->GetPosition().y );
    }

    // Parse the wiring entries
    std::vector<SCH_AGENT_WIRING::WIRING_ENTRY> entries =
        SCH_AGENT_WIRING::ParseAgentWiring( fieldValue );

    wxLogMessage( "WIRING_GUIDE: Parsed %zu entries from %s", entries.size(), symbolRef );

    for( const auto& entry : entries )
    {
        wxLogMessage( "WIRING_GUIDE: Entry: pin=\"%s\" -> target=\"%s\"", entry.pin, entry.target );

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
            wxLogMessage( "WIRING_GUIDE: Source pin found at (%d,%d)", guide.sourcePos.x, guide.sourcePos.y );
            m_guides.push_back( guide );
        }
        else
        {
            wxLogMessage( "WIRING_GUIDE: FAILED to find source pin \"%s\" on %s", entry.pin, symbolRef );
        }
    }
}


bool SCH_WIRING_GUIDE_MANAGER::ResolveTargetPosition( WIRING_GUIDE& aGuide )
{
    aGuide.targetResolved = false;

    wxLogMessage( "WIRING_GUIDE: ResolveTargetPosition for %s:%s -> %s",
                  aGuide.sourceRef, aGuide.sourcePin, aGuide.targetRef );

    if( SCH_AGENT_WIRING::IsPinReference( aGuide.targetRef ) )
    {
        // Target is a pin reference like "U1:PA0"
        wxString symbolRef, pinName;
        if( SCH_AGENT_WIRING::ParsePinReference( aGuide.targetRef, symbolRef, pinName ) )
        {
            wxLogMessage( "WIRING_GUIDE: Target is pin ref: symbol=%s pin=%s", symbolRef, pinName );

            SCH_SYMBOL* targetSymbol = FindSymbolByRef( symbolRef );
            if( targetSymbol )
            {
                wxLogMessage( "WIRING_GUIDE: Found target symbol %s", symbolRef );

                if( GetPinPosition( targetSymbol, pinName, aGuide.targetPos ) )
                {
                    wxLogMessage( "WIRING_GUIDE: Resolved %s:%s -> %s at (%d,%d)",
                                  aGuide.sourceRef, aGuide.sourcePin, aGuide.targetRef,
                                  aGuide.targetPos.x, aGuide.targetPos.y );
                    aGuide.targetResolved = true;
                    return true;
                }
                else
                {
                    wxLogMessage( "WIRING_GUIDE: FAILED to find pin \"%s\" on %s. Available pins:",
                                  pinName, symbolRef );
                    for( SCH_PIN* pin : targetSymbol->GetPins( &m_frame->GetCurrentSheet() ) )
                    {
                        wxLogMessage( "  number=\"%s\" name=\"%s\"", pin->GetNumber(), pin->GetName() );
                    }
                }
            }
            else
            {
                wxLogMessage( "WIRING_GUIDE: FAILED to find symbol %s in ref map. Available refs:", symbolRef );
                for( const auto& pair : m_refToKiid )
                {
                    wxLogMessage( "  %s", pair.first );
                }
            }
        }
    }
    else
    {
        // Target is a net name like "VCC" or "GND"
        wxLogMessage( "WIRING_GUIDE: Target is net name: \"%s\"", aGuide.targetRef );

        SCH_SCREEN* screen = m_frame->GetScreen();
        if( !screen )
            return false;

        // Log what labels are available
        wxLogMessage( "WIRING_GUIDE: Searching for net \"%s\". Available labels:", aGuide.targetRef );
        for( SCH_ITEM* item : screen->Items().OfType( SCH_GLOBAL_LABEL_T ) )
        {
            SCH_GLOBALLABEL* label = static_cast<SCH_GLOBALLABEL*>( item );
            wxLogMessage( "  GlobalLabel: \"%s\" at (%d,%d)", label->GetText(),
                          label->GetPosition().x, label->GetPosition().y );
        }
        for( SCH_ITEM* item : screen->Items().OfType( SCH_HIER_LABEL_T ) )
        {
            SCH_HIERLABEL* label = static_cast<SCH_HIERLABEL*>( item );
            wxLogMessage( "  HierLabel: \"%s\" at (%d,%d)", label->GetText(),
                          label->GetPosition().x, label->GetPosition().y );
        }
        for( SCH_ITEM* item : screen->Items().OfType( SCH_LABEL_T ) )
        {
            SCH_LABEL* label = static_cast<SCH_LABEL*>( item );
            wxLogMessage( "  Label: \"%s\" at (%d,%d)", label->GetText(),
                          label->GetPosition().x, label->GetPosition().y );
        }

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
                        wxLogMessage( "WIRING_GUIDE: Resolved via power symbol at (%d,%d)",
                                      aGuide.targetPos.x, aGuide.targetPos.y );
                        return true;
                    }
                }
            }
        }

        // Find the NEAREST matching label to the source position
        // Collect all matching labels, then pick the closest one
        VECTOR2I bestPos;
        int64_t bestDist = INT64_MAX;
        bool found = false;
        wxString bestType;

        // Check global labels
        for( SCH_ITEM* item : screen->Items().OfType( SCH_GLOBAL_LABEL_T ) )
        {
            SCH_GLOBALLABEL* label = static_cast<SCH_GLOBALLABEL*>( item );
            if( label->GetText().IsSameAs( aGuide.targetRef, false ) )
            {
                int64_t dist = ( label->GetPosition() - aGuide.sourcePos ).SquaredEuclideanNorm();
                if( dist < bestDist )
                {
                    bestDist = dist;
                    bestPos = label->GetPosition();
                    bestType = "global label";
                    found = true;
                }
            }
        }

        // Check hierarchical labels
        for( SCH_ITEM* item : screen->Items().OfType( SCH_HIER_LABEL_T ) )
        {
            SCH_HIERLABEL* label = static_cast<SCH_HIERLABEL*>( item );
            if( label->GetText().IsSameAs( aGuide.targetRef, false ) )
            {
                int64_t dist = ( label->GetPosition() - aGuide.sourcePos ).SquaredEuclideanNorm();
                if( dist < bestDist )
                {
                    bestDist = dist;
                    bestPos = label->GetPosition();
                    bestType = "hier label";
                    found = true;
                }
            }
        }

        // Check regular local labels
        for( SCH_ITEM* item : screen->Items().OfType( SCH_LABEL_T ) )
        {
            SCH_LABEL* label = static_cast<SCH_LABEL*>( item );
            if( label->GetText().IsSameAs( aGuide.targetRef, false ) )
            {
                int64_t dist = ( label->GetPosition() - aGuide.sourcePos ).SquaredEuclideanNorm();
                if( dist < bestDist )
                {
                    bestDist = dist;
                    bestPos = label->GetPosition();
                    bestType = "local label";
                    found = true;
                }
            }
        }

        if( found )
        {
            aGuide.targetPos = bestPos;
            aGuide.targetResolved = true;
            wxLogMessage( "WIRING_GUIDE: Resolved via %s at (%d,%d) (nearest to source)",
                          bestType, aGuide.targetPos.x, aGuide.targetPos.y );
            return true;
        }

        wxLogMessage( "WIRING_GUIDE: FAILED to resolve net \"%s\" - no matching label found",
                      aGuide.targetRef );
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

    // Find connectivity items at both positions (pins or labels)
    SCH_ITEM* startItem = nullptr;
    SCH_ITEM* endItem = nullptr;
    wxString startItemInfo, endItemInfo;

    // Schematic uses ~10000 IU per mm, use 0.5mm tolerance (5000 IU)
    const int tolerance = 5000;

    // Search for pins at both positions
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
                    startItem = pin;
                    startItemInfo = wxString::Format( "%s:%s (name=%s)",
                                                      symRef, pin->GetNumber(), pin->GetName() );
                }
                if( ( pinPos - aEnd ).EuclideanNorm() < tolerance )
                {
                    endItem = pin;
                    endItemInfo = wxString::Format( "%s:%s (name=%s)",
                                                    symRef, pin->GetNumber(), pin->GetName() );
                }
            }
        }
    }

    // If we didn't find an item at the end position, check for labels
    if( !endItem )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_GLOBAL_LABEL_T ) )
        {
            if( ( item->GetPosition() - aEnd ).EuclideanNorm() < tolerance )
            {
                endItem = item;
                endItemInfo = wxString::Format( "GlobalLabel:%s",
                                                static_cast<SCH_GLOBALLABEL*>( item )->GetText() );
                break;
            }
        }
    }
    if( !endItem )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_HIER_LABEL_T ) )
        {
            if( ( item->GetPosition() - aEnd ).EuclideanNorm() < tolerance )
            {
                endItem = item;
                endItemInfo = wxString::Format( "HierLabel:%s",
                                                static_cast<SCH_HIERLABEL*>( item )->GetText() );
                break;
            }
        }
    }
    if( !endItem )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_LABEL_T ) )
        {
            if( ( item->GetPosition() - aEnd ).EuclideanNorm() < tolerance )
            {
                endItem = item;
                endItemInfo = wxString::Format( "Label:%s",
                                                static_cast<SCH_LABEL*>( item )->GetText() );
                break;
            }
        }
    }

    // Also check for labels at start position (less common but possible)
    if( !startItem )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_GLOBAL_LABEL_T ) )
        {
            if( ( item->GetPosition() - aStart ).EuclideanNorm() < tolerance )
            {
                startItem = item;
                startItemInfo = wxString::Format( "GlobalLabel:%s",
                                                  static_cast<SCH_GLOBALLABEL*>( item )->GetText() );
                break;
            }
        }
    }
    if( !startItem )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_HIER_LABEL_T ) )
        {
            if( ( item->GetPosition() - aStart ).EuclideanNorm() < tolerance )
            {
                startItem = item;
                startItemInfo = wxString::Format( "HierLabel:%s",
                                                  static_cast<SCH_HIERLABEL*>( item )->GetText() );
                break;
            }
        }
    }
    if( !startItem )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_LABEL_T ) )
        {
            if( ( item->GetPosition() - aStart ).EuclideanNorm() < tolerance )
            {
                startItem = item;
                startItemInfo = wxString::Format( "Label:%s",
                                                  static_cast<SCH_LABEL*>( item )->GetText() );
                break;
            }
        }
    }

    if( !startItem || !endItem )
    {
        // Log MISS - one or both items not found at expected positions
        wxLogMessage( "WIRING_GUIDE: MISS at (%d,%d)->(%d,%d) start=%s end=%s",
                      aStart.x, aStart.y, aEnd.x, aEnd.y,
                      startItemInfo.empty() ? "NOT FOUND" : startItemInfo,
                      endItemInfo.empty() ? "NOT FOUND" : endItemInfo );
        return false;
    }

    // Use the CONNECTION_GRAPH to check if both items are in the same subgraph
    // This is more reliable than checking pin->Connection() which may have stale data
    CONNECTION_SUBGRAPH* startSubgraph = connGraph->GetSubgraphForItem( startItem );
    CONNECTION_SUBGRAPH* endSubgraph = connGraph->GetSubgraphForItem( endItem );

    if( startSubgraph && endSubgraph )
    {
        // If both items are in the same subgraph, they're connected
        if( startSubgraph == endSubgraph )
        {
            wxLogMessage( "WIRING_GUIDE: Items connected (same subgraph): start=%s end=%s",
                          startItemInfo, endItemInfo );
            return true;
        }

        // Different subgraphs - not connected
        wxLogMessage( "WIRING_GUIDE: Items in different subgraphs: start=%s end=%s",
                      startItemInfo, endItemInfo );
        return false;
    }

    // One or both items not in any subgraph - check if they're NO NET
    if( !startSubgraph && !endSubgraph )
    {
        wxLogMessage( "WIRING_GUIDE: Both items have no subgraph (unconnected): start=%s end=%s",
                      startItemInfo, endItemInfo );
        return false;
    }

    wxLogMessage( "WIRING_GUIDE: Mixed subgraph state: start=%s (sg=%d) end=%s (sg=%d)",
                  startItemInfo, startSubgraph != nullptr, endItemInfo, endSubgraph != nullptr );
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
    {
        wxLogMessage( "WIRING_GUIDE: GetActiveGuides: global visibility is off" );
        return active;
    }

    wxLogMessage( "WIRING_GUIDE: GetActiveGuides checking %zu guides", m_guides.size() );

    for( const auto& guide : m_guides )
    {
        // Show guides that are:
        // - Not completed (not yet wired)
        // - Visible (not hidden by user)
        // - Target resolved (we know where to draw the line)
        // Note: We no longer filter by pending status - guides should show during preview too
        if( !guide.isComplete && guide.isVisible && guide.targetResolved )
        {
            active.push_back( guide );
        }
        else
        {
            wxLogMessage( "WIRING_GUIDE: Filtered out %s:%s->%s (complete=%d visible=%d resolved=%d)",
                          guide.sourceRef, guide.sourcePin, guide.targetRef,
                          guide.isComplete, guide.isVisible, guide.targetResolved );
        }
    }

    wxLogMessage( "WIRING_GUIDE: GetActiveGuides returning %zu active guides", active.size() );
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
    m_hasActiveWiring = false;
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
                // Target is a net name like "VCC" - try power symbols first
                bool found = false;
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
                                found = true;
                                break;
                            }
                        }
                    }
                }

                // Try global labels
                if( !found )
                {
                    for( SCH_ITEM* item : screen->Items().OfType( SCH_GLOBAL_LABEL_T ) )
                    {
                        SCH_GLOBALLABEL* label = static_cast<SCH_GLOBALLABEL*>( item );
                        if( label->GetText().IsSameAs( guide.targetRef, false ) )
                        {
                            guide.targetPos = label->GetPosition();
                            found = true;
                            break;
                        }
                    }
                }

                // Try hierarchical labels
                if( !found )
                {
                    for( SCH_ITEM* item : screen->Items().OfType( SCH_HIER_LABEL_T ) )
                    {
                        SCH_HIERLABEL* label = static_cast<SCH_HIERLABEL*>( item );
                        if( label->GetText().IsSameAs( guide.targetRef, false ) )
                        {
                            guide.targetPos = label->GetPosition();
                            found = true;
                            break;
                        }
                    }
                }

                // Try local labels
                if( !found )
                {
                    for( SCH_ITEM* item : screen->Items().OfType( SCH_LABEL_T ) )
                    {
                        SCH_LABEL* label = static_cast<SCH_LABEL*>( item );
                        if( label->GetText().IsSameAs( guide.targetRef, false ) )
                        {
                            guide.targetPos = label->GetPosition();
                            break;
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


void SCH_WIRING_GUIDE_MANAGER::SetActiveWiringPosition( const VECTOR2I& aPos )
{
    m_activeWiringPos = aPos;
    m_hasActiveWiring = true;
}


void SCH_WIRING_GUIDE_MANAGER::ClearActiveWiringPosition()
{
    m_hasActiveWiring = false;
}


int SCH_WIRING_GUIDE_MANAGER::GetActiveGuideIndex( const std::vector<WIRING_GUIDE>& aActiveGuides ) const
{
    if( !m_hasActiveWiring || aActiveGuides.empty() )
        return -1;

    // Find the guide whose source or target position is closest to the active wiring position
    // Use a reasonable threshold - if nothing is close, return -1
    const int64_t threshold = schIUScale.mmToIU( 2.0 );  // 2mm tolerance
    const int64_t thresholdSq = threshold * threshold;

    int bestIndex = -1;
    int64_t bestDistSq = INT64_MAX;

    for( size_t i = 0; i < aActiveGuides.size(); ++i )
    {
        const WIRING_GUIDE& guide = aActiveGuides[i];

        // Check distance to source position
        int64_t srcDistSq = ( guide.sourcePos - m_activeWiringPos ).SquaredEuclideanNorm();
        if( srcDistSq < bestDistSq && srcDistSq <= thresholdSq )
        {
            bestDistSq = srcDistSq;
            bestIndex = static_cast<int>( i );
        }

        // Check distance to target position
        if( guide.targetResolved )
        {
            int64_t tgtDistSq = ( guide.targetPos - m_activeWiringPos ).SquaredEuclideanNorm();
            if( tgtDistSq < bestDistSq && tgtDistSq <= thresholdSq )
            {
                bestDistSq = tgtDistSq;
                bestIndex = static_cast<int>( i );
            }
        }
    }

    return bestIndex;
}
