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
#include <commit.h>
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

    // The benefit is how much shorter the new route would be
    double benefit = aAlternativeCost - aNewRouteCost;

    // First, try shoving the obstacle (push-and-shove)
    // This is preferred over ripup when possible
    BOARD_ITEM* item = aRoom->GetItem();
    PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( item );

    if( track && !dynamic_cast<PCB_VIA*>( track ) )
    {
        // Calculate shove direction (perpendicular to track)
        VECTOR2I trackDir( track->GetEnd().x - track->GetStart().x,
                            track->GetEnd().y - track->GetStart().y );
        VECTOR2I shoveDir = CalculateShoveDirection( aRoom, trackDir );

        // Try shoving by clearance + trace width
        int shoveDistance = m_control.clearance + m_control.GetTraceWidth( 0 );
        SHOVE_PROPOSAL shove = TryShove( aRoom, shoveDir, shoveDistance );

        if( shove.is_valid && shove.shove_cost < benefit )
        {
            result.should_shove = true;
            result.ripup_cost = shove.shove_cost;
            result.reroute_benefit = benefit;
            result.shoves.push_back( shove );
            return result;
        }

        // Try shoving in the opposite direction
        VECTOR2I oppDir( -shoveDir.x, -shoveDir.y );
        shove = TryShove( aRoom, oppDir, shoveDistance );

        if( shove.is_valid && shove.shove_cost < benefit )
        {
            result.should_shove = true;
            result.ripup_cost = shove.shove_cost;
            result.reroute_benefit = benefit;
            result.shoves.push_back( shove );
            return result;
        }
    }

    // If shove didn't work, try ripup
    double ripupCost = CalculateRipupCost( aRoom );

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
        candidate.item = item;
        candidate.room = aRoom;
        candidate.ripup_cost = ripupCost;
        candidate.net_code = aRoom->GetNetCode();

        PCB_VIA* via = dynamic_cast<PCB_VIA*>( item );
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


SHOVE_PROPOSAL RIPUP_CHECKER::TryShove( OBSTACLE_ROOM* aRoom, const VECTOR2I& aShoveDirection,
                                          int aShoveDistance )
{
    SHOVE_PROPOSAL result;

    if( !aRoom || !CanRipup( aRoom ) )
        return result;

    BOARD_ITEM* item = aRoom->GetItem();
    if( !item )
        return result;

    PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( item );
    if( !track )
        return result;  // Only tracks can be shoved (not vias)

    PCB_VIA* via = dynamic_cast<PCB_VIA*>( track );
    if( via )
        return result;  // Vias cannot be shoved

    result.item = track;
    result.original_start = track->GetStart();
    result.original_end = track->GetEnd();

    // Calculate shove offset
    VECTOR2I offset( 0, 0 );
    double dirLen = std::sqrt( double( aShoveDirection.x ) * aShoveDirection.x +
                                double( aShoveDirection.y ) * aShoveDirection.y );
    if( dirLen > 0 )
    {
        offset.x = static_cast<int>( aShoveDirection.x * aShoveDistance / dirLen );
        offset.y = static_cast<int>( aShoveDirection.y * aShoveDistance / dirLen );
    }

    // Calculate new positions
    result.new_start = result.original_start + offset;
    result.new_end = result.original_end + offset;

    // Validate the new position (basic check - in a real implementation,
    // this would check against all obstacles on the layer)
    // For now, just mark as valid - the actual validation happens during insertion
    result.is_valid = true;

    // Calculate shove cost (proportional to distance moved)
    result.shove_cost = aShoveDistance * m_control.trace_cost;

    return result;
}


VECTOR2I RIPUP_CHECKER::CalculateShoveDirection( OBSTACLE_ROOM* aRoom,
                                                   const VECTOR2I& aRoutingDirection )
{
    // Calculate the perpendicular direction to the routing direction
    // This is the direction we want to push the obstacle
    VECTOR2I perp( -aRoutingDirection.y, aRoutingDirection.x );

    // Normalize to unit-ish vector (keep integer)
    double len = std::sqrt( double( perp.x ) * perp.x + double( perp.y ) * perp.y );
    if( len > 0 )
    {
        // Scale to a reasonable magnitude (use clearance as base unit)
        double scale = m_control.clearance / len;
        perp.x = static_cast<int>( perp.x * scale );
        perp.y = static_cast<int>( perp.y * scale );
    }

    return perp;
}


void RIPUP_CHECKER::ApplyShoves( const std::vector<SHOVE_PROPOSAL>& aShoves, COMMIT* aCommit )
{
    for( const auto& shove : aShoves )
    {
        if( !shove.is_valid || !shove.item )
            continue;

        PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( shove.item );
        if( !track )
            continue;

        // Record the modification
        if( aCommit )
            aCommit->Modify( track );

        // Move the track
        track->SetStart( shove.new_start );
        track->SetEnd( shove.new_end );
    }
}
