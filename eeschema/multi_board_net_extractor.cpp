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

#include "connection_graph.h"
#include "schematic.h"
#include "sch_module_block.h"
#include "sch_module_pin.h"

#include <algorithm>
#include <set>


namespace
{

/**
 * Look up a sub-project uuid by matching the block's sub_project_path
 * against the MULTI_BOARD_PROJECT's registered sub-projects. Returns a
 * nil KIID when no match is found (caller surfaces as a diagnostic).
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

}   // anonymous namespace


std::vector<MB_CROSS_BOARD_NET> ExtractCrossBoardNets( SCHEMATIC& aMbsSchematic,
                                                       const PROJECT_FILE& aMultiBoard )
{
    std::vector<MB_CROSS_BOARD_NET> nets;

    // Query the CONNECTION_GRAPH. Each subgraph is a connected net; its
    // items include every SCH_MODULE_PIN the user wired to it (directly
    // or via an intermediate label). The graph's native naming resolves
    // the subgraph's net name from attached labels with the standard
    // driver-priority rules, so a label "BATTERY" on the wire becomes
    // the net name with zero custom naming logic on our end.
    CONNECTION_GRAPH* graph = aMbsSchematic.ConnectionGraph();

    if( !graph )
        return nets;

    const std::vector<CONNECTION_SUBGRAPH*>& subgraphs = graph->GetAllSubgraphs( wxEmptyString );
    std::set<CONNECTION_SUBGRAPH*> seen;

    // GetAllSubgraphs with empty name is actually a no-op in some KiCad
    // versions; walk the graph's full subgraph set instead.
    (void) subgraphs;

    // The authoritative subgraph list is visible via GetResolvedSubgraphName
    // per driver, but we need to iterate all of them. Use a small helper:
    // walk every SCH_MODULE_BLOCK in the root screen, fetch the subgraph
    // containing each pin, dedup by subgraph pointer. Every cross-board
    // net must contain at least one module pin by definition, so this
    // enumeration is complete for our purposes.
    SCH_SCREEN* screen = aMbsSchematic.RootScreen();

    if( !screen )
        return nets;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_MODULE_BLOCK_T )
            continue;

        SCH_MODULE_BLOCK* block = static_cast<SCH_MODULE_BLOCK*>( item );

        for( SCH_MODULE_PIN* pin : block->GetPins() )
        {
            if( CONNECTION_SUBGRAPH* sg = graph->GetSubgraphForItem( pin ) )
                seen.insert( sg );
        }
    }

    for( CONNECTION_SUBGRAPH* sg : seen )
    {
        // Collect all module pins in this subgraph, grouped by their
        // owning block. A subgraph qualifies as "cross-board" only
        // when its module pins span at least two distinct sub-project
        // UUIDs — pins that happen to share a local net on one block
        // don't need cross-board propagation.
        std::vector<std::pair<SCH_MODULE_BLOCK*, SCH_MODULE_PIN*>> modulePins;

        for( SCH_ITEM* item : sg->GetItems() )
        {
            if( item->Type() == SCH_MODULE_PIN_T )
            {
                SCH_MODULE_PIN*   pin   = static_cast<SCH_MODULE_PIN*>( item );
                SCH_MODULE_BLOCK* block = pin->GetParent();

                if( block )
                    modulePins.emplace_back( block, pin );
            }
        }

        if( modulePins.size() < 2 )
            continue;

        std::set<KIID> distinctSubProjects;

        for( const auto& [block, pin] : modulePins )
            distinctSubProjects.insert( subProjectUuidForBlock( *block, aMultiBoard ) );

        // Need ≥2 distinct sub-projects for a *cross*-board net; if all
        // pins are on one sub-project it's an intra-board connection
        // that the sub-project's own schematic already handles.
        if( distinctSubProjects.size() < 2 )
            continue;

        MB_CROSS_BOARD_NET net;
        net.uuid = KIID();
        net.name = graph->GetResolvedSubgraphName( sg );

        if( net.name.IsEmpty() )
            net.name = wxString::Format( wxT( "Net-%s" ),
                                         net.uuid.AsString().SubString( 0, 7 ) );

        for( const auto& [block, pin] : modulePins )
        {
            MB_CROSS_BOARD_NET_ENDPOINT endpoint;
            endpoint.subProjectUuid = subProjectUuidForBlock( *block, aMultiBoard );

            // Prefer block-level componentRef (the MBS authoritative
            // value); fall back to the pin's metadata.
            wxString componentRef = block->GetComponentRef();

            if( componentRef.IsEmpty() )
                componentRef = pin->GetComponentRef();

            if( componentRef.IsEmpty() )
                componentRef = pin->GetPinNumber();

            endpoint.componentRef = componentRef;
            endpoint.pinNumber    = pin->GetPinNumber();
            endpoint.pinName      = pin->GetText();
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
