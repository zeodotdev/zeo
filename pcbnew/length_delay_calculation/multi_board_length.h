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
 */

#ifndef PCBNEW_MULTI_BOARD_LENGTH_H
#define PCBNEW_MULTI_BOARD_LENGTH_H

#include <vector>
#include <wx/string.h>

class BOARD;
class NETINFO_ITEM;


/*
    Cross-board length calculation primitives.

    These wrap the per-board LENGTH_DELAY_CALCULATION engine with the
    multi-board container scan + sibling-board lazy-load logic that
    drc_test_provider_cross_board_length already prototyped, lifted into
    a reusable form so non-DRC consumers (PCB_TRACK msg panel, tuning
    overlay, net inspector, properties manager) can show the same
    "this board / cross-board total" split.

    Naming: "this-board" = trace length on the requesting BOARD only.
            "cross-board total" = this-board + sum of sibling boards
            participating in the matching cross-board net (per the
            container's multi_board.cross_board_nets[]).

    All lengths are in nanometers (board IU). Returns 0 contributions
    for nets that don't exist on a given board — never throws.

    Caching: the cross-board lookup re-loads sibling .kicad_pcb files
    from disk every call. For per-call use this is fine (one DRC pass,
    one msg-panel render). Surfaces that fire repeatedly (PNS meander
    drag, net inspector live updates) should cache results themselves
    keyed by (container path, net name, board mod-time stamps).
*/


/**
 * Sum trace + via + pad-to-die length for the named net on a single
 * BOARD. Walks tracks and footprint pads on the board, hands them to
 * BOARD::GetLengthCalculation()->CalculateLength() with the standard
 * via-layer / track-merge / trace-in-pad optimisations enabled.
 *
 * Returns 0 if the board has no NETINFO_ITEM matching the name, the
 * net code is invalid, or no items exist on the net. Never throws.
 */
int64_t SumNetLengthOnBoard( BOARD& aBoard, const wxString& aNetName );


/**
 * Result of a cross-board length sum: this board's contribution,
 * the total across this board and every sibling sub-project that
 * participates in the matching cross-board net, and the names of
 * those siblings (deduplicated, in iteration order).
 */
struct CROSS_BOARD_NET_LENGTH
{
    /// True when the requesting board's project is a multi-board
    /// sub-project AND a matching entry exists in the container's
    /// cross_board_nets[]. False otherwise — in which case
    /// thisBoardNm holds the per-board length and totalNm equals it.
    bool isCrossBoard = false;

    /// Length contribution from the requesting BOARD, in nanometers.
    int64_t thisBoardNm = 0;

    /// Total length across the requesting board and every sibling
    /// sub-project participating in the cross-board net, in
    /// nanometers. Equals thisBoardNm when isCrossBoard is false.
    int64_t totalNm = 0;

    /// Display names of sibling sub-projects whose contribution
    /// was added to totalNm. Ordered, deduplicated.
    std::vector<wxString> siblingNames;
};


/**
 * Resolve the cross-board net total for a net on the requesting board.
 *
 * Looks up the requesting board's project, asks
 * MultiBoardBuildContainerView() for the sub-project's view of the
 * container, finds the cross_board_nets[] entry whose name matches the
 * given net (canonical-name match — the container holds the canonical
 * form), then loads each sibling sub-project's .kicad_pcb on demand and
 * sums the per-board length contributions.
 *
 * On a single-board project (no container, or the net isn't a
 * cross-board net), returns isCrossBoard=false with thisBoardNm and
 * totalNm both equal to the per-board length — callers can use the
 * same code path on both single- and multi-board projects.
 */
CROSS_BOARD_NET_LENGTH ComputeCrossBoardNetLength( BOARD& aThisBoard, const wxString& aNetName );


/**
 * Convenience overload that takes a NETINFO_ITEM directly and reads
 * the canonical net name from it. Returns an empty result when aNet
 * is null. Equivalent to ComputeCrossBoardNetLength(aBoard,
 * aNet->GetNetname()).
 */
CROSS_BOARD_NET_LENGTH ComputeCrossBoardNetLength( BOARD& aThisBoard, const NETINFO_ITEM* aNet );


#endif // PCBNEW_MULTI_BOARD_LENGTH_H
