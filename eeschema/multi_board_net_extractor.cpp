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

#include "multi_board_net_extractor.h"

#include "sch_line.h"
#include "sch_module_block.h"
#include "sch_module_pin.h"
#include "sch_label.h"
#include "sch_screen.h"

#include <layer_ids.h>
#include <wx/filename.h>

#include <algorithm>
#include <map>
#include <unordered_map>


namespace
{

/**
 * Union-find over VECTOR2I positions. Positions are equivalent if joined by
 * a chain of wire segments.
 */
class POS_UNION_FIND
{
public:
    void Unite( const VECTOR2I& aA, const VECTOR2I& aB )
    {
        VECTOR2I rootA = find( aA );
        VECTOR2I rootB = find( aB );

        if( rootA != rootB )
            m_parent[rootB] = rootA;
    }

    VECTOR2I Find( const VECTOR2I& aPos ) { return find( aPos ); }

private:
    VECTOR2I find( const VECTOR2I& aPos )
    {
        auto it = m_parent.find( aPos );

        if( it == m_parent.end() )
        {
            m_parent[aPos] = aPos;
            return aPos;
        }

        if( it->second == aPos )
            return aPos;

        // Path compression.
        VECTOR2I root = find( it->second );
        m_parent[aPos] = root;
        return root;
    }

    std::map<VECTOR2I, VECTOR2I> m_parent;
};


struct PinRecord
{
    SCH_MODULE_BLOCK* block;
    SCH_MODULE_PIN*   pin;
};


/**
 * Look up a sub-project uuid by matching the block's sub_project_path
 * against the MULTI_BOARD_PROJECT's registered sub-projects.
 * Returns a nil KIID when no match is found.
 */
KIID subProjectUuidForBlock( const SCH_MODULE_BLOCK& aBlock,
                             const PROJECT_FILE& aMultiBoard )
{
    const wxString& path = aBlock.GetSubProjectPath();

    if( path.IsEmpty() )
        return KIID( 0 );

    for( const SUB_PROJECT_INFO& info : aMultiBoard.GetSubProjects() )
    {
        if( info.relativePath == path )
            return info.uuid;
    }

    return KIID( 0 );
}


} // anonymous namespace


namespace {

/**
 * True if `aP` lies on the segment [aA, aB] (including endpoints). Handles
 * orthogonal and diagonal segments; collinearity tested via 2D cross product,
 * on-segment range tested via dot-product bounds.
 */
bool isPointOnSegment( const VECTOR2I& aP, const VECTOR2I& aA, const VECTOR2I& aB )
{
    VECTOR2I ab = aB - aA;
    VECTOR2I ap = aP - aA;

    int64_t cross = (int64_t) ab.x * ap.y - (int64_t) ab.y * ap.x;

    if( cross != 0 )
        return false;

    int64_t dot    = (int64_t) ab.x * ap.x + (int64_t) ab.y * ap.y;
    int64_t lenSq  = (int64_t) ab.x * ab.x + (int64_t) ab.y * ab.y;

    return dot >= 0 && dot <= lenSq;
}

} // anonymous namespace


std::vector<MB_CROSS_BOARD_NET> ExtractCrossBoardNets( SCH_SCREEN& aMbsScreen,
                                                    const PROJECT_FILE& aMultiBoard )
{
    POS_UNION_FIND              uf;
    std::vector<PinRecord>      modulePins;
    std::vector<SCH_LABEL_BASE*> labels;
    std::vector<SCH_LINE*>      wires;

    // Pass 1: walk the screen, collect module pins + wires + labels.
    for( SCH_ITEM* item : aMbsScreen.Items() )
    {
        if( item->Type() == SCH_MODULE_BLOCK_T )
        {
            auto* block = static_cast<SCH_MODULE_BLOCK*>( item );

            for( SCH_MODULE_PIN* pin : block->GetPins() )
            {
                modulePins.push_back( { block, pin } );
                uf.Find( pin->GetPosition() );  // seed this position in UF
            }
        }
        else if( item->Type() == SCH_LINE_T )
        {
            auto* line = static_cast<SCH_LINE*>( item );

            if( line->GetLayer() == LAYER_WIRE )
            {
                wires.push_back( line );
                uf.Unite( line->GetStartPoint(), line->GetEndPoint() );
            }
        }
        else if( item->Type() == SCH_LABEL_T
                 || item->Type() == SCH_GLOBAL_LABEL_T
                 || item->Type() == SCH_HIER_LABEL_T )
        {
            labels.push_back( static_cast<SCH_LABEL_BASE*>( item ) );
        }
    }

    // Pass 1b: unite each label's position with the first wire it sits on.
    // A label placed mid-segment (not at an endpoint) otherwise wouldn't
    // share a union-find root with the wire — so the naming lookup would
    // miss. This handles labels at wire endpoints (trivial) and mid-wire
    // (segment test).
    for( SCH_LABEL_BASE* label : labels )
    {
        VECTOR2I pos = label->GetPosition();

        for( SCH_LINE* wire : wires )
        {
            if( isPointOnSegment( pos, wire->GetStartPoint(), wire->GetEndPoint() ) )
            {
                uf.Unite( pos, wire->GetStartPoint() );
                break;
            }
        }
    }

    // Pass 1c: unite each module pin's position with the first wire it
    // touches. Without this step, two pins joined only by a wire keep
    // separate union-find roots (the wire unites its own endpoints, but
    // nothing connects the wire back to the pin) and Pass 2 groups each
    // pin alone — Pass 3 then drops the singleton groups and no cross-
    // board net is extracted. Mirrors the label→wire bridge above.
    for( const PinRecord& rec : modulePins )
    {
        VECTOR2I pos = rec.pin->GetPosition();

        for( SCH_LINE* wire : wires )
        {
            if( isPointOnSegment( pos, wire->GetStartPoint(), wire->GetEndPoint() ) )
            {
                uf.Unite( pos, wire->GetStartPoint() );
                break;
            }
        }
    }

    // Pass 2: group module pins by their UF root.
    std::map<VECTOR2I, std::vector<PinRecord>> groups;

    for( const PinRecord& rec : modulePins )
    {
        VECTOR2I root = uf.Find( rec.pin->GetPosition() );
        groups[root].push_back( rec );
    }

    // Pass 3: for each group that spans 2+ pins, emit a cross-board net.
    std::vector<MB_CROSS_BOARD_NET> nets;

    for( auto& [root, group] : groups )
    {
        if( group.size() < 2 )
            continue;

        MB_CROSS_BOARD_NET net;
        net.uuid = KIID();

        // Name: first label that lands on this group's root set.
        for( SCH_LABEL_BASE* label : labels )
        {
            if( uf.Find( label->GetPosition() ) == root )
            {
                net.name = label->GetText();
                break;
            }
        }

        if( net.name.IsEmpty() )
            net.name = wxString::Format( wxT( "Net-%s" ),
                                         net.uuid.AsString().SubString( 0, 7 ) );

        for( const PinRecord& rec : group )
        {
            MB_CROSS_BOARD_NET_ENDPOINT endpoint;
            endpoint.subProjectUuid = subProjectUuidForBlock( *rec.block, aMultiBoard );

            // Prefer the block-level componentRef (authoritative in the
            // block-per-connector model). Fall back to the pin's field, then
            // to pinNumber for legacy MBS files where each connector became a
            // single pin.
            wxString componentRef = rec.block->GetComponentRef();

            if( componentRef.IsEmpty() )
                componentRef = rec.pin->GetComponentRef();

            if( componentRef.IsEmpty() )
                componentRef = rec.pin->GetPinNumber();

            endpoint.componentRef = componentRef;
            endpoint.pinNumber    = rec.pin->GetPinNumber();
            endpoint.pinName      = rec.pin->GetText();
            net.endpoints.push_back( endpoint );
        }

        nets.push_back( std::move( net ) );
    }

    // Stable ordering by net name for deterministic output.
    std::sort( nets.begin(), nets.end(),
               []( const MB_CROSS_BOARD_NET& a, const MB_CROSS_BOARD_NET& b )
               {
                   return a.name < b.name;
               } );

    return nets;
}
