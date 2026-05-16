/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "multi_board_net_extractor.h"

#include "connection_graph.h"
#include "schematic.h"
#include "sch_module_block.h"
#include "sch_module_pin.h"

#include <algorithm>
#include <map>
#include <set>

#include <wx/regex.h>


namespace
{

/**
 * Look up a sub-project uuid by matching the block's sub_project_path
 * against the container `PROJECT_FILE`'s registered sub_projects.
 * Returns a nil KIID when no match is found (caller surfaces as a
 * diagnostic).
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


namespace
{

bool isAutoGenLocalNet( const wxString& aName )
{
    if( aName.IsEmpty() )
        return true;

    if( aName.StartsWith( wxT( "Net-(" ) ) )
        return true;

    if( aName.StartsWith( wxT( "unconnected-" ) ) )
        return true;

    return false;
}


/**
 * Strip the trailing `_<digits>` that KiCad's CONNECTION_GRAPH appends
 * to disambiguate multiple subgraphs sharing a label base. E.g.
 * `5V_1` → `5V`, `GND_33` → `GND`. Names without the suffix are
 * returned unchanged. Names that look like real user labels (no
 * trailing digit suffix) are also returned unchanged.
 */
wxString stripDisambigSuffix( const wxString& aName )
{
    static wxRegEx re( wxT( "_[0-9]+$" ) );

    if( !re.IsValid() )
        return aName;

    wxString result = aName;
    re.Replace( &result, wxEmptyString );
    return result;
}


/**
 * Tiny path-compressed union-find indexed by SCH_MODULE_PIN*. We use
 * this so a single MBS wire can implicitly carry every pin that's
 * physically on the same local net on a sub-project board (e.g., when
 * J1.1, J1.2, J1.3 all carry "+5V" locally on board A, wiring J1.1 to
 * J2.1 on the MBS extends the cross-board net to all five pins).
 */
class PIN_UNION_FIND
{
public:
    void add( SCH_MODULE_PIN* aPin )
    {
        if( m_parent.find( aPin ) == m_parent.end() )
            m_parent[aPin] = aPin;
    }

    SCH_MODULE_PIN* find( SCH_MODULE_PIN* aPin )
    {
        SCH_MODULE_PIN* root = aPin;

        while( m_parent[root] != root )
            root = m_parent[root];

        // Path compression.
        while( m_parent[aPin] != root )
        {
            SCH_MODULE_PIN* next = m_parent[aPin];
            m_parent[aPin] = root;
            aPin = next;
        }

        return root;
    }

    void unite( SCH_MODULE_PIN* a, SCH_MODULE_PIN* b )
    {
        SCH_MODULE_PIN* ra = find( a );
        SCH_MODULE_PIN* rb = find( b );

        if( ra != rb )
            m_parent[ra] = rb;
    }

private:
    std::map<SCH_MODULE_PIN*, SCH_MODULE_PIN*> m_parent;
};

} // anonymous namespace


std::vector<MB_CROSS_BOARD_NET> ExtractCrossBoardNets( SCHEMATIC& aMbsSchematic,
                                                       const PROJECT_FILE& aMultiBoard )
{
    std::vector<MB_CROSS_BOARD_NET> nets;

    SCH_SCREEN* screen = aMbsSchematic.RootScreen();

    if( !screen )
        return nets;

    CONNECTION_GRAPH* graph = aMbsSchematic.ConnectionGraph();

    if( !graph )
        return nets;

    // ---- Phase 1: enumerate every module pin and seed the union-find ----
    // Track all pins; we'll union them below by (a) shared local net
    // name within a block and (b) MBS wire connectivity.
    PIN_UNION_FIND uf;
    std::vector<std::pair<SCH_MODULE_BLOCK*, SCH_MODULE_PIN*>> allPins;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_MODULE_BLOCK_T )
            continue;

        SCH_MODULE_BLOCK* block = static_cast<SCH_MODULE_BLOCK*>( item );

        for( SCH_MODULE_PIN* pin : block->GetPins() )
        {
            uf.add( pin );
            allPins.emplace_back( block, pin );
        }
    }

    // ---- Phase 2: union by shared local net name within each block ----
    // J1.1, J1.2, J1.3 all carrying "+5V" on board A's PCB are
    // physically the same node — collapse them now so a single MBS
    // wire to any of them pulls the others along.
    {
        std::map<SCH_MODULE_BLOCK*, std::map<wxString, SCH_MODULE_PIN*>> firstByBlockNet;

        for( const auto& [block, pin] : allPins )
        {
            wxString localNet = pin->GetText();

            if( isAutoGenLocalNet( localNet ) )
                continue;

            auto& blockNets = firstByBlockNet[block];
            auto  it        = blockNets.find( localNet );

            if( it == blockNets.end() )
                blockNets[localNet] = pin;
            else
                uf.unite( it->second, pin );
        }
    }

    // ---- Phase 3: union by MBS wire connectivity ----
    // Each MBS subgraph is a wire net; pins on the same subgraph join.
    std::set<CONNECTION_SUBGRAPH*> seenSubgraphs;
    std::map<CONNECTION_SUBGRAPH*, wxString> subgraphLabels;

    for( const auto& [block, pin] : allPins )
    {
        if( CONNECTION_SUBGRAPH* sg = graph->GetSubgraphForItem( pin ) )
        {
            seenSubgraphs.insert( sg );
            // Cache the label so we can reuse it for naming the
            // resulting cross-board net (driver-priority resolution).
            subgraphLabels[sg] = graph->GetResolvedSubgraphName( sg );
        }
    }

    for( CONNECTION_SUBGRAPH* sg : seenSubgraphs )
    {
        SCH_MODULE_PIN* anchor = nullptr;

        for( SCH_ITEM* item : sg->GetItems() )
        {
            if( item->Type() != SCH_MODULE_PIN_T )
                continue;

            SCH_MODULE_PIN* pin = static_cast<SCH_MODULE_PIN*>( item );

            if( !anchor )
                anchor = pin;
            else
                uf.unite( anchor, pin );
        }
    }

    // ---- Phase 4: walk groups, emit cross-board nets ----
    std::map<SCH_MODULE_PIN*,
             std::vector<std::pair<SCH_MODULE_BLOCK*, SCH_MODULE_PIN*>>> groups;

    for( const auto& [block, pin] : allPins )
        groups[uf.find( pin )].emplace_back( block, pin );

    for( auto& [root, members] : groups )
    {
        std::set<KIID> distinctSubProjects;

        for( const auto& [block, pin] : members )
            distinctSubProjects.insert( subProjectUuidForBlock( *block, aMultiBoard ) );

        // Cross-board requires endpoints on ≥2 distinct sub-projects.
        if( distinctSubProjects.size() < 2 )
            continue;

        // Resolve the canonical net name.
        //
        // Both pin labels and KiCad's resolved subgraph names can carry
        // an auto-disambig "_<N>" suffix when multiple subgraphs share
        // a base name on the schematic (very common when each
        // un-bridged "5V" pin gets its own subgraph). Collapse those
        // suffixes when the stripped form matches another label in the
        // group — that's KiCad uniquifying, not a real user-typed
        // distinction.
        //
        // Tally votes from both pin labels and subgraph labels, all
        // normalised through `stripDisambigSuffix`, then pick the
        // most common base. Falls back to `Net-<uuid>` only if every
        // candidate was auto-generated.
        std::map<wxString, int> nameCounts;

        for( const auto& [block, pin] : members )
        {
            wxString pinLabel = pin->GetText();

            if( !isAutoGenLocalNet( pinLabel ) )
                nameCounts[stripDisambigSuffix( pinLabel )]++;
        }

        std::set<CONNECTION_SUBGRAPH*> groupSubgraphs;

        for( const auto& [block, pin] : members )
        {
            if( CONNECTION_SUBGRAPH* sg = graph->GetSubgraphForItem( pin ) )
                groupSubgraphs.insert( sg );
        }

        for( CONNECTION_SUBGRAPH* sg : groupSubgraphs )
        {
            auto it = subgraphLabels.find( sg );

            if( it == subgraphLabels.end() || it->second.IsEmpty() )
                continue;

            // Strip "_N" only when the stripped form matches some pin
            // label we've already counted. That's the signal it's
            // KiCad's auto-disambig rather than a deliberately-typed
            // name like "BUS_3V3". User labels survive intact.
            wxString sgName  = it->second;
            wxString stripped = stripDisambigSuffix( sgName );

            if( stripped != sgName && nameCounts.count( stripped ) > 0 )
                nameCounts[stripped]++;
            else
                nameCounts[sgName]++;
        }

        wxString canonicalName;
        int      bestCount = 0;

        for( const auto& [name, count] : nameCounts )
        {
            if( count > bestCount )
            {
                bestCount     = count;
                canonicalName = name;
            }
        }

        MB_CROSS_BOARD_NET net;
        net.uuid = KIID();

        if( canonicalName.IsEmpty() )
            canonicalName = wxString::Format( wxT( "Net-%s" ),
                                              net.uuid.AsString().SubString( 0, 7 ) );

        net.name = canonicalName;

        for( const auto& [block, pin] : members )
        {
            MB_CROSS_BOARD_NET_ENDPOINT endpoint;
            endpoint.subProjectUuid = subProjectUuidForBlock( *block, aMultiBoard );

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

    std::sort( nets.begin(), nets.end(),
               []( const MB_CROSS_BOARD_NET& a, const MB_CROSS_BOARD_NET& b )
               {
                   return a.name < b.name;
               } );

    return nets;
}
