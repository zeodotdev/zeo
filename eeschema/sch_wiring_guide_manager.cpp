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

    wxLogMessage( "WIRING_GUIDE: ScanSymbolsForWiring called" );

    // First pass: build ref to KIID mapping
    int symbolCount = 0;
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        wxString ref = symbol->GetRef( &m_frame->GetCurrentSheet() );
        m_refToKiid[ref] = symbol->m_Uuid;
        symbolCount++;
    }
    wxLogMessage( "WIRING_GUIDE: Found %d symbols on screen", symbolCount );

    // Second pass: parse Agent_Wiring fields
    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        ParseSymbolWiring( symbol );
    }

    wxLogMessage( "WIRING_GUIDE: Parsed %zu guides from Agent_Wiring fields", m_guides.size() );

    // Resolve target positions and check completion states
    int resolved = 0;
    for( auto& guide : m_guides )
    {
        if( ResolveTargetPosition( guide ) )
            resolved++;
    }
    wxLogMessage( "WIRING_GUIDE: Resolved %d/%zu target positions", resolved, m_guides.size() );

    RefreshGuideStates();
}


void SCH_WIRING_GUIDE_MANAGER::ParseSymbolWiring( SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return;

    wxString symbolRef = aSymbol->GetRef( &m_frame->GetCurrentSheet() );

    // Log all fields on the symbol to help debug
    const std::vector<SCH_FIELD>& fields = aSymbol->GetFields();
    wxLogMessage( "WIRING_GUIDE: ParseSymbolWiring checking %s with %zu fields",
                  symbolRef, fields.size() );
    for( size_t i = 0; i < fields.size(); ++i )
    {
        wxLogMessage( "WIRING_GUIDE:   Field[%zu]: name='%s' value='%s'",
                      i, fields[i].GetName(), fields[i].GetText() );
    }

    // Get the Agent_Wiring field
    const SCH_FIELD* field = aSymbol->GetField( SCH_AGENT_WIRING::FIELD_NAME );
    if( !field )
    {
        wxLogMessage( "WIRING_GUIDE: %s has no Agent_Wiring field (looked for '%s')",
                      symbolRef, SCH_AGENT_WIRING::FIELD_NAME );
        return;
    }

    wxString fieldValue = field->GetText();
    if( fieldValue.IsEmpty() )
    {
        wxLogMessage( "WIRING_GUIDE: %s has empty Agent_Wiring field", symbolRef );
        return;
    }

    wxLogMessage( "WIRING_GUIDE: %s has Agent_Wiring: %s", symbolRef, fieldValue );

    // Parse the wiring entries
    std::vector<SCH_AGENT_WIRING::WIRING_ENTRY> entries =
        SCH_AGENT_WIRING::ParseAgentWiring( fieldValue );

    wxLogMessage( "WIRING_GUIDE: Parsed %zu entries from %s", entries.size(), symbolRef );

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
            wxLogMessage( "WIRING_GUIDE: Added guide %s:%s -> %s",
                          symbolRef, entry.pin, entry.target );
            m_guides.push_back( guide );
        }
        else
        {
            wxLogMessage( "WIRING_GUIDE: Failed to get pin position for %s:%s",
                          symbolRef, entry.pin );
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
            wxLogMessage( "WIRING_GUIDE: Parsed pin ref: symbol='%s' pin='%s'",
                          symbolRef, pinName );

            SCH_SYMBOL* targetSymbol = FindSymbolByRef( symbolRef );
            if( targetSymbol )
            {
                wxLogMessage( "WIRING_GUIDE: Found target symbol %s", symbolRef );
                if( GetPinPosition( targetSymbol, pinName, aGuide.targetPos ) )
                {
                    wxLogMessage( "WIRING_GUIDE: Resolved pin %s position (%d, %d)",
                                  pinName, aGuide.targetPos.x, aGuide.targetPos.y );
                    aGuide.targetResolved = true;
                    return true;
                }
                else
                {
                    wxLogMessage( "WIRING_GUIDE: FAILED to find pin '%s' on symbol %s",
                                  pinName, symbolRef );
                    // Log available pins for debugging
                    std::vector<SCH_PIN*> pins = targetSymbol->GetPins( &m_frame->GetCurrentSheet() );
                    for( SCH_PIN* pin : pins )
                    {
                        wxLogMessage( "WIRING_GUIDE:   - Available pin: num='%s' name='%s'",
                                      pin->GetNumber(), pin->GetName() );
                    }
                }
            }
            else
            {
                wxLogMessage( "WIRING_GUIDE: FAILED to find symbol '%s'", symbolRef );
                // Log available refs for debugging
                wxLogMessage( "WIRING_GUIDE: Available refs in map:" );
                for( const auto& pair : m_refToKiid )
                {
                    wxLogMessage( "WIRING_GUIDE:   - '%s'", pair.first );
                }
            }
        }
        else
        {
            wxLogMessage( "WIRING_GUIDE: FAILED to parse pin reference '%s'",
                          aGuide.targetRef );
        }
    }
    else
    {
        // Target is a net name like "VCC" or "GND"
        wxLogMessage( "WIRING_GUIDE: Target '%s' is a net name, searching for power symbol",
                      aGuide.targetRef );

        // Try to find a power symbol with this net on the current screen
        SCH_SCREEN* screen = m_frame->GetScreen();
        if( !screen )
            return false;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            // Check if this is a power symbol with matching net
            if( symbol->GetLibSymbolRef() )
            {
                if( symbol->GetLibSymbolRef()->IsPower() )
                {
                    wxString powerNet = symbol->GetValue( false, &m_frame->GetCurrentSheet(), false );

                    if( powerNet.IsSameAs( aGuide.targetRef, false ) )
                    {
                        // Get the power symbol's pin position
                        std::vector<SCH_PIN*> pins = symbol->GetPins( &m_frame->GetCurrentSheet() );
                        if( !pins.empty() )
                        {
                            aGuide.targetPos = pins[0]->GetPosition();
                            aGuide.targetResolved = true;
                            wxLogMessage( "WIRING_GUIDE: Resolved power net '%s' at (%d, %d)",
                                          aGuide.targetRef, aGuide.targetPos.x, aGuide.targetPos.y );
                            return true;
                        }
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
                wxLogMessage( "WIRING_GUIDE: Resolved net '%s' via label at (%d, %d)",
                              aGuide.targetRef, aGuide.targetPos.x, aGuide.targetPos.y );
                return true;
            }
        }

        wxLogMessage( "WIRING_GUIDE: FAILED to resolve net '%s'", aGuide.targetRef );
    }

    return false;
}


void SCH_WIRING_GUIDE_MANAGER::RefreshGuideStates()
{
    if( !m_frame )
        return;

    wxLogMessage( "WIRING_GUIDE: RefreshGuideStates - checking %zu guides", m_guides.size() );

    int completed = 0;
    for( auto& guide : m_guides )
    {
        if( guide.targetResolved )
        {
            guide.isComplete = CheckConnectionExists( guide.sourcePos, guide.targetPos );
            if( guide.isComplete )
                completed++;
        }
        else
        {
            guide.isComplete = false;
        }
    }

    wxLogMessage( "WIRING_GUIDE: RefreshGuideStates - %d/%zu complete",
                  completed, m_guides.size() );
}


bool SCH_WIRING_GUIDE_MANAGER::CheckConnectionExists( const VECTOR2I& aStart, const VECTOR2I& aEnd )
{
    // Use the connectivity graph to check if the two positions are on the same net
    CONNECTION_GRAPH* connGraph = m_frame->Schematic().ConnectionGraph();
    if( !connGraph )
    {
        wxLogMessage( "WIRING_GUIDE: CheckConnectionExists - no connection graph" );
        return false;
    }

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return false;

    // Find items at both positions
    SCH_ITEM* startItem = nullptr;
    SCH_ITEM* endItem = nullptr;

    // Schematic uses ~10000 IU per mm, use 0.5mm tolerance (5000 IU)
    const int tolerance = 5000;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() == SCH_PIN_T )
        {
            SCH_PIN* pin = static_cast<SCH_PIN*>( item );
            VECTOR2I pinPos = pin->GetPosition();

            if( ( pinPos - aStart ).EuclideanNorm() < tolerance )
                startItem = item;
            if( ( pinPos - aEnd ).EuclideanNorm() < tolerance )
                endItem = item;
        }
        else if( item->Type() == SCH_SYMBOL_T )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
            for( SCH_PIN* pin : symbol->GetPins( &m_frame->GetCurrentSheet() ) )
            {
                VECTOR2I pinPos = pin->GetPosition();

                if( ( pinPos - aStart ).EuclideanNorm() < tolerance )
                    startItem = pin;
                if( ( pinPos - aEnd ).EuclideanNorm() < tolerance )
                    endItem = pin;
            }
        }
    }

    if( !startItem || !endItem )
    {
        wxLogMessage( "WIRING_GUIDE: CheckConnectionExists (%d,%d)->(%d,%d) - items not found (start=%p, end=%p)",
                      aStart.x, aStart.y, aEnd.x, aEnd.y, startItem, endItem );
        return false;
    }

    // Check if both items have connections and are on the same net
    SCH_CONNECTION* startConn = startItem->Connection();
    SCH_CONNECTION* endConn = endItem->Connection();

    if( startConn && endConn )
    {
        wxString startNet = startConn->Name();
        wxString endNet = endConn->Name();

        // "/<NO NET>" or empty means unconnected - don't consider these as matching
        bool startUnconnected = startNet.IsEmpty() || startNet.Contains( wxT( "NO NET" ) );
        bool endUnconnected = endNet.IsEmpty() || endNet.Contains( wxT( "NO NET" ) );

        if( startUnconnected || endUnconnected )
        {
            wxLogMessage( "WIRING_GUIDE: CheckConnectionExists - startNet='%s' endNet='%s' - one or both unconnected",
                          startNet, endNet );
            return false;
        }

        bool sameNet = startNet == endNet;
        wxLogMessage( "WIRING_GUIDE: CheckConnectionExists - startNet='%s' endNet='%s' same=%d",
                      startNet, endNet, sameNet );
        return sameNet;
    }

    wxLogMessage( "WIRING_GUIDE: CheckConnectionExists - no connections (startConn=%p, endConn=%p)",
                  startConn, endConn );
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
        wxLogMessage( "WIRING_GUIDE: GetActiveGuides - global visibility is OFF" );
        return active;
    }

    wxLogMessage( "WIRING_GUIDE: GetActiveGuides - checking %zu guides", m_guides.size() );

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
        else
        {
            wxLogMessage( "WIRING_GUIDE: Filtered out %s:%s -> %s (complete=%d, visible=%d, resolved=%d, pending=%d)",
                          guide.sourceRef, guide.sourcePin, guide.targetRef,
                          guide.isComplete, guide.isVisible, guide.targetResolved, pending );
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
    wxLogMessage( "WIRING_GUIDE: OnSchematicChanged - rescanning symbols" );
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
