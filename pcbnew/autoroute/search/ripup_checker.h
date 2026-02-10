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

#ifndef RIPUP_CHECKER_H
#define RIPUP_CHECKER_H

#include "../expansion/expansion_room.h"
#include "../autoroute_control.h"
#include <vector>
#include <set>
#include <map>

// Forward declarations
class BOARD_ITEM;
class PCB_TRACK;
class PCB_VIA;


/**
 * Information about a potential ripup candidate.
 */
struct RIPUP_CANDIDATE
{
    BOARD_ITEM*     item = nullptr;          ///< The item to rip up (trace or via)
    OBSTACLE_ROOM*  room = nullptr;          ///< The obstacle room for this item
    double          ripup_cost = 0.0;        ///< Cost of ripping up this item
    int             net_code = 0;            ///< Net code of the item
    bool            is_via = false;          ///< True if this is a via

    bool operator<( const RIPUP_CANDIDATE& other ) const
    {
        return ripup_cost < other.ripup_cost;
    }
};


/**
 * Result of ripup analysis.
 */
struct RIPUP_RESULT
{
    bool            should_ripup = false;    ///< True if ripup is beneficial
    double          ripup_cost = 0.0;        ///< Total cost of the ripup
    double          reroute_benefit = 0.0;   ///< Benefit from the new route
    std::vector<RIPUP_CANDIDATE> candidates; ///< Items to rip up

    /**
     * Get the net benefit (benefit - cost).
     */
    double GetNetBenefit() const { return reroute_benefit - ripup_cost; }
};


/**
 * Checks whether ripping up existing traces would improve routing.
 *
 * This is based on FreeRouting's check_ripup() method. It analyzes
 * whether removing existing traces would allow a better overall
 * routing solution.
 */
class RIPUP_CHECKER
{
public:
    RIPUP_CHECKER( const AUTOROUTE_CONTROL& aControl );

    /**
     * Check if ripping up an obstacle room would be beneficial.
     *
     * @param aRoom The obstacle room to potentially rip up.
     * @param aNewRouteCost The cost of routing through this room.
     * @param aAlternativeCost The cost of routing around this room.
     * @return Ripup result with decision and affected items.
     */
    RIPUP_RESULT CheckRipup( OBSTACLE_ROOM* aRoom, double aNewRouteCost,
                              double aAlternativeCost );

    /**
     * Calculate the ripup cost for an obstacle room.
     *
     * The cost considers:
     * - Length of traces being removed
     * - Number of connections affected
     * - Priority of the net being ripped up
     *
     * @param aRoom The obstacle room.
     * @return The ripup cost.
     */
    double CalculateRipupCost( OBSTACLE_ROOM* aRoom );

    /**
     * Check if a room can be ripped up.
     *
     * Some items cannot be ripped up:
     * - Items on protected nets
     * - Locked items
     * - Items already marked for ripup
     *
     * @param aRoom The obstacle room.
     * @return True if the room can be ripped up.
     */
    bool CanRipup( OBSTACLE_ROOM* aRoom ) const;

    /**
     * Mark items for ripup.
     * These items will be removed before rerouting.
     */
    void MarkForRipup( const std::vector<RIPUP_CANDIDATE>& aCandidates );

    /**
     * Get all items marked for ripup.
     */
    const std::set<BOARD_ITEM*>& GetMarkedItems() const { return m_markedItems; }

    /**
     * Clear all ripup marks.
     */
    void ClearMarks();

    /**
     * Set the maximum number of ripup passes allowed.
     */
    void SetMaxRipupPasses( int aPasses ) { m_maxRipupPasses = aPasses; }

    /**
     * Get the current ripup pass number.
     */
    int GetRipupPass() const { return m_ripupPass; }

    /**
     * Increment the ripup pass counter.
     */
    void IncrementPass() { m_ripupPass++; }

    /**
     * Check if more ripup passes are allowed.
     */
    bool CanDoMorePasses() const { return m_ripupPass < m_maxRipupPasses; }

    /**
     * Set the ripup cost multiplier (higher = more reluctant to rip up).
     */
    void SetRipupCostMultiplier( double aMultiplier ) { m_ripupCostMultiplier = aMultiplier; }

    /**
     * Get nets that have been ripped up.
     */
    const std::set<int>& GetRippedNets() const { return m_rippedNets; }

private:
    /**
     * Calculate cost for a single trace segment.
     */
    double CalculateTraceCost( PCB_TRACK* aTrack );

    /**
     * Calculate cost for a via.
     */
    double CalculateViaCost( PCB_VIA* aVia );

    const AUTOROUTE_CONTROL& m_control;
    std::set<BOARD_ITEM*>    m_markedItems;
    std::set<int>            m_rippedNets;  ///< Nets that have been ripped up
    int                      m_maxRipupPasses = 5;
    int                      m_ripupPass = 0;
    double                   m_ripupCostMultiplier = 1.5;
};


#endif // RIPUP_CHECKER_H
