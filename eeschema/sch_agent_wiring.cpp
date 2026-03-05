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

#include "sch_agent_wiring.h"
#include <wx/tokenzr.h>

namespace SCH_AGENT_WIRING
{

std::vector<WIRING_ENTRY> ParseAgentWiring( const wxString& aFieldValue )
{
    std::vector<WIRING_ENTRY> entries;

    if( aFieldValue.IsEmpty() )
        return entries;

    // Split by separator ("; ")
    wxStringTokenizer tokenizer( aFieldValue, wxT( ";" ) );

    while( tokenizer.HasMoreTokens() )
    {
        wxString entry = tokenizer.GetNextToken().Trim().Trim( false );

        if( entry.IsEmpty() )
            continue;

        // Find the arrow
        int arrowPos = entry.Find( ARROW );

        if( arrowPos == wxNOT_FOUND )
        {
            // Try ASCII arrow as fallback
            arrowPos = entry.Find( wxT( "->" ) );
            if( arrowPos != wxNOT_FOUND )
            {
                WIRING_ENTRY we;
                we.pin = entry.Left( arrowPos ).Trim().Trim( false );
                we.target = entry.Mid( arrowPos + 2 ).Trim().Trim( false );

                if( !we.pin.IsEmpty() && !we.target.IsEmpty() )
                    entries.push_back( we );

                continue;
            }
        }

        if( arrowPos != wxNOT_FOUND )
        {
            WIRING_ENTRY we;
            we.pin = entry.Left( arrowPos ).Trim().Trim( false );
            we.target = entry.Mid( arrowPos + ARROW.length() ).Trim().Trim( false );

            if( !we.pin.IsEmpty() && !we.target.IsEmpty() )
                entries.push_back( we );
        }
    }

    return entries;
}


wxString SerializeAgentWiring( const std::vector<WIRING_ENTRY>& aEntries )
{
    wxString result;

    for( size_t i = 0; i < aEntries.size(); ++i )
    {
        if( i > 0 )
            result += SEPARATOR;

        result += aEntries[i].pin + ARROW + aEntries[i].target;
    }

    return result;
}


wxString RemoveWiringEntry( const wxString& aFieldValue, const wxString& aPin )
{
    std::vector<WIRING_ENTRY> entries = ParseAgentWiring( aFieldValue );

    // Remove entries matching the pin
    entries.erase(
        std::remove_if( entries.begin(), entries.end(),
            [&aPin]( const WIRING_ENTRY& e ) { return e.pin == aPin; } ),
        entries.end() );

    return SerializeAgentWiring( entries );
}


wxString AddOrUpdateWiringEntry( const wxString& aFieldValue, const wxString& aPin,
                                  const wxString& aTarget )
{
    std::vector<WIRING_ENTRY> entries = ParseAgentWiring( aFieldValue );

    // Look for existing entry with same pin
    bool found = false;
    for( auto& entry : entries )
    {
        if( entry.pin == aPin )
        {
            entry.target = aTarget;
            found = true;
            break;
        }
    }

    // Add new entry if not found
    if( !found )
    {
        WIRING_ENTRY newEntry;
        newEntry.pin = aPin;
        newEntry.target = aTarget;
        entries.push_back( newEntry );
    }

    return SerializeAgentWiring( entries );
}


bool IsPinReference( const wxString& aTarget )
{
    // A pin reference contains a colon separating symbol ref from pin
    // e.g., "U1:PA0", "R1:1"
    // Net names like "VCC", "GND", "SPI_CLK" don't have colons
    return aTarget.Contains( wxT( ":" ) );
}


bool ParsePinReference( const wxString& aTarget, wxString& aSymbolRef, wxString& aPin )
{
    int colonPos = aTarget.Find( wxT( ":" ) );

    if( colonPos == wxNOT_FOUND )
        return false;

    aSymbolRef = aTarget.Left( colonPos ).Trim().Trim( false );
    aPin = aTarget.Mid( colonPos + 1 ).Trim().Trim( false );

    return !aSymbolRef.IsEmpty() && !aPin.IsEmpty();
}


wxString BuildAgentWiringField( const std::vector<std::pair<wxString, wxString>>& aPinTargets )
{
    std::vector<WIRING_ENTRY> entries;

    for( const auto& pt : aPinTargets )
    {
        WIRING_ENTRY entry;
        entry.pin = pt.first;
        entry.target = pt.second;
        entries.push_back( entry );
    }

    return SerializeAgentWiring( entries );
}

} // namespace SCH_AGENT_WIRING
