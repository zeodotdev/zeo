/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ripup_checker.h"
#include <pcb_track.h>
#include <cmath>


RIPUP_CHECKER::RIPUP_CHECKER( const AUTOROUTE_CONTROL& aControl )
    : m_control( aControl )
{
}


RIPUP_RESULT RIPUP_CHECKER::CheckRipup( OBSTACLE_ROOM* aRoom, double aNewRouteCost,
                                         double aAlternativeCost )
{
    RIPUP_RESULT result;

    if( !aRoom || !CanRipup( aRoom ) )
        return result;

    // Calculate the ripup cost
    double ripupCost = CalculateRipupCost( aRoom );

    // The benefit is how much shorter the new route would be
    double benefit = aAlternativeCost - aNewRouteCost;

    // Apply the ripup cost multiplier to make ripup less attractive
    double adjustedRipupCost = ripupCost * m_ripupCostMultiplier;

    // Ripup is beneficial if the benefit exceeds the adjusted cost
    if( benefit > adjustedRipupCost )
    {
        result.should_ripup = true;
        result.ripup_cost = ripupCost;
        result.reroute_benefit = benefit;

        // Add the room's item as a candidate
        RIPUP_CANDIDATE candidate;
        candidate.item = aRoom->GetItem();
        candidate.room = aRoom;
        candidate.ripup_cost = ripupCost;
        candidate.net_code = aRoom->GetNetCode();

        PCB_VIA* via = dynamic_cast<PCB_VIA*>( candidate.item );
        candidate.is_via = ( via != nullptr );

        result.candidates.push_back( candidate );
    }

    return result;
}


double RIPUP_CHECKER::CalculateRipupCost( OBSTACLE_ROOM* aRoom )
{
    if( !aRoom )
        return std::numeric_limits<double>::max();

    BOARD_ITEM* item = aRoom->GetItem();
    if( !item )
        return std::numeric_limits<double>::max();

    // Calculate cost based on item type
    PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( item );
    if( track )
    {
        PCB_VIA* via = dynamic_cast<PCB_VIA*>( track );
        if( via )
            return CalculateViaCost( via );
        else
            return CalculateTraceCost( track );
    }

    // Default high cost for unknown items
    return std::numeric_limits<double>::max();
}


double RIPUP_CHECKER::CalculateTraceCost( PCB_TRACK* aTrack )
{
    if( !aTrack )
        return 0.0;

    // Base cost is the length of the trace
    double length = aTrack->GetLength();

    // Apply trace cost multiplier from control
    double cost = length * m_control.trace_cost;

    // Add a fixed overhead for each segment
    cost += m_control.clearance;  // Use clearance as a baseline cost

    return cost;
}


double RIPUP_CHECKER::CalculateViaCost( PCB_VIA* aVia )
{
    if( !aVia )
        return 0.0;

    // Via cost is the via penalty
    return m_control.via_cost;
}


bool RIPUP_CHECKER::CanRipup( OBSTACLE_ROOM* aRoom ) const
{
    if( !aRoom )
        return false;

    BOARD_ITEM* item = aRoom->GetItem();
    if( !item )
        return false;

    // Can't rip up items that are already marked
    if( m_markedItems.find( item ) != m_markedItems.end() )
        return false;

    // Can't rip up locked items
    if( item->IsLocked() )
        return false;

    // Check if ripup is allowed for this net
    int netCode = aRoom->GetNetCode();

    // Don't rip up power/ground nets (they're typically more important)
    // This is a simplification - a full implementation would check net class
    // For now, we'll allow ripup of all nets

    // Don't exceed maximum ripup passes
    if( m_ripupPass >= m_maxRipupPasses )
        return false;

    return true;
}


void RIPUP_CHECKER::MarkForRipup( const std::vector<RIPUP_CANDIDATE>& aCandidates )
{
    for( const auto& candidate : aCandidates )
    {
        if( candidate.item )
        {
            m_markedItems.insert( candidate.item );
            m_rippedNets.insert( candidate.net_code );
        }
    }
}


void RIPUP_CHECKER::ClearMarks()
{
    m_markedItems.clear();
    // Note: Don't clear m_rippedNets - we track which nets have been affected
}
