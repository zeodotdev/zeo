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

#ifndef SCH_AGENT_WIRING_H
#define SCH_AGENT_WIRING_H

#include <wx/string.h>
#include <vector>

/**
 * @file sch_agent_wiring.h
 * @brief Utilities for parsing and manipulating the Agent_Wiring symbol field.
 *
 * The Agent_Wiring field stores recommended pin connections in the format:
 *   "1→VCC; 2→U1:PA0; 3→NET_SPI_CLK"
 *
 * Where each entry is:
 *   <pin>→<target>
 *
 * Target can be:
 *   - Net name: "VCC", "GND", "SPI_CLK"
 *   - Pin reference: "U1:PA0", "C1:1"
 */

namespace SCH_AGENT_WIRING
{

/**
 * A single wiring recommendation entry.
 */
struct WIRING_ENTRY
{
    wxString pin;       ///< Pin number or name on the source symbol (e.g., "1", "PA0")
    wxString target;    ///< Target: net name or "ref:pin" (e.g., "VCC", "U1:PA0")

    bool operator==( const WIRING_ENTRY& aOther ) const
    {
        return pin == aOther.pin && target == aOther.target;
    }
};

/**
 * The field name used to store agent wiring recommendations.
 */
inline const wxString FIELD_NAME = wxT( "Agent_Wiring" );

/**
 * The arrow character used to separate pin from target.
 */
inline const wxString ARROW = wxT( "→" );

/**
 * The separator between entries.
 */
inline const wxString SEPARATOR = wxT( "; " );

/**
 * Parse an Agent_Wiring field value into structured entries.
 *
 * @param aFieldValue The raw field value (e.g., "1→VCC; 2→U1:PA0")
 * @return Vector of parsed WIRING_ENTRY structs
 */
std::vector<WIRING_ENTRY> ParseAgentWiring( const wxString& aFieldValue );

/**
 * Serialize wiring entries back to field format.
 *
 * @param aEntries Vector of WIRING_ENTRY structs
 * @return Serialized field value (e.g., "1→VCC; 2→U1:PA0")
 */
wxString SerializeAgentWiring( const std::vector<WIRING_ENTRY>& aEntries );

/**
 * Remove a specific pin entry from the wiring field.
 *
 * @param aFieldValue The current field value
 * @param aPin The pin to remove (e.g., "1" or "PA0")
 * @return Updated field value with the entry removed
 */
wxString RemoveWiringEntry( const wxString& aFieldValue, const wxString& aPin );

/**
 * Add or update a wiring entry.
 *
 * @param aFieldValue The current field value
 * @param aPin The pin to add/update
 * @param aTarget The target for the pin
 * @return Updated field value
 */
wxString AddOrUpdateWiringEntry( const wxString& aFieldValue, const wxString& aPin,
                                  const wxString& aTarget );

/**
 * Check if a target string is a pin reference (contains ':').
 *
 * @param aTarget The target string to check
 * @return true if target is a pin reference (e.g., "U1:PA0"), false if net name
 */
bool IsPinReference( const wxString& aTarget );

/**
 * Parse a pin reference into symbol ref and pin.
 *
 * @param aTarget The target string (e.g., "U1:PA0")
 * @param aSymbolRef Output: symbol reference (e.g., "U1")
 * @param aPin Output: pin name/number (e.g., "PA0")
 * @return true if successfully parsed, false if not a pin reference
 */
bool ParsePinReference( const wxString& aTarget, wxString& aSymbolRef, wxString& aPin );

/**
 * Build a wiring field value from a list of pin-to-target pairs.
 *
 * @param aPinTargets Vector of {pin, target} pairs
 * @return Serialized field value
 */
wxString BuildAgentWiringField( const std::vector<std::pair<wxString, wxString>>& aPinTargets );

} // namespace SCH_AGENT_WIRING

#endif // SCH_AGENT_WIRING_H
