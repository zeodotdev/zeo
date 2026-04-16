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
                             const MULTI_BOARD_PROJECT& aMultiBoard )
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


std::vector<CROSS_BOARD_NET> ExtractCrossBoardNets( SCH_SCREEN& aMbsScreen,
                                                    const MULTI_BOARD_PROJECT& aMultiBoard )
{
    POS_UNION_FIND              uf;
    std::vector<PinRecord>      modulePins;
    std::vector<SCH_LABEL_BASE*> labels;

    // Pass 1: walk the screen, collect module pins, unite wire endpoints,
    // and stash labels for net naming.
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
                uf.Unite( line->GetStartPoint(), line->GetEndPoint() );
        }
        else if( item->Type() == SCH_LABEL_T
                 || item->Type() == SCH_GLOBAL_LABEL_T
                 || item->Type() == SCH_HIER_LABEL_T )
        {
            labels.push_back( static_cast<SCH_LABEL_BASE*>( item ) );
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
    std::vector<CROSS_BOARD_NET> nets;

    for( auto& [root, group] : groups )
    {
        if( group.size() < 2 )
            continue;

        CROSS_BOARD_NET net;
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
            CROSS_BOARD_NET_ENDPOINT endpoint;
            endpoint.subProjectUuid = subProjectUuidForBlock( *rec.block, aMultiBoard );
            endpoint.componentRef   = rec.pin->GetComponentRef();
            endpoint.pinNumber      = rec.pin->GetPinNumber();
            endpoint.pinName        = rec.pin->GetText();
            net.endpoints.push_back( endpoint );
        }

        nets.push_back( std::move( net ) );
    }

    // Stable ordering by net name for deterministic output.
    std::sort( nets.begin(), nets.end(),
               []( const CROSS_BOARD_NET& a, const CROSS_BOARD_NET& b )
               {
                   return a.name < b.name;
               } );

    return nets;
}
