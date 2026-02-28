/*
 * Shared diff highlight utilities for schematic editors.
 *
 * Used by both the live canvas overlay (SCH_EDIT_FRAME) and the
 * VCS diff viewer (SCH_DIFF_FRAME) to ensure identical visual style.
 */

#ifndef SCH_DIFF_HIGHLIGHT_H
#define SCH_DIFF_HIGHLIGHT_H

#include <functional>
#include <set>
#include <unordered_map>
#include <vector>
#include <math/box2.h>
#include <gal/color4d.h>
#include <kiid.h>
#include <base_units.h>
#include <core/typeinfo.h>
#include <sch_item.h>
#include <sch_line.h>
#include <sch_symbol.h>
#include <sch_screen.h>


namespace SCH_DIFF
{

// Canonical diff colors — single source of truth.
inline const KIGFX::COLOR4D COLOR_ADDED(    0.15, 0.65, 0.20, 1.0 );  // green
inline const KIGFX::COLOR4D COLOR_DELETED(  0.80, 0.15, 0.15, 1.0 );  // red
inline const KIGFX::COLOR4D COLOR_MODIFIED( 0.85, 0.55, 0.05, 1.0 );  // amber/orange

// Style constants
inline const int SYMBOL_MARGIN = schIUScale.MilsToIU( 20 );  // breathing room for symbols/labels
inline const int WIRE_MARGIN   = schIUScale.MilsToIU( 15 );  // inflation for wires
inline const int BORDER_WIDTH  = schIUScale.MilsToIU( 3 );   // border stroke width
// FILL_ALPHA / BORDER_ALPHA live in diff_manager.h as DIFF_FILL_ALPHA / DIFF_BORDER_ALPHA


struct DiffBox
{
    BOX2I          bbox;
    KIGFX::COLOR4D color;
    bool           hasBorder = true;
};


/**
 * Compute a highlight box for a single schematic item.
 *
 * Applies the canonical margins and wire/symbol detection:
 *   - Symbols use GetBodyAndPinsBoundingBox() + 20-mil margin + border
 *   - Wires use GetBoundingBox() + 5-mil margin, no border
 *   - Everything else uses GetBoundingBox() + 20-mil margin + border
 */
inline DiffBox ComputeItemBox( SCH_ITEM* aItem, const KIGFX::COLOR4D& aColor )
{
    bool isWire = ( aItem->Type() == SCH_LINE_T
                    && static_cast<SCH_LINE*>( aItem )->IsWire() );
    bool isJunction = ( aItem->Type() == SCH_JUNCTION_T );
    bool noBorder = isWire || isJunction;

    BOX2I bbox;
    if( aItem->Type() == SCH_SYMBOL_T )
        bbox = static_cast<SCH_SYMBOL*>( aItem )->GetBodyAndPinsBoundingBox();
    else
        bbox = aItem->GetBoundingBox();

    bbox.Normalize();

    if( noBorder )
        bbox.Inflate( WIRE_MARGIN );
    else
        bbox.Inflate( SYMBOL_MARGIN );

    return { bbox, aColor, !noBorder };
}


/**
 * Merge overlapping borderless boxes (wires/junctions) into single bordered
 * groups.  Bordered boxes pass through unchanged.
 *
 * This produces a single continuous outlined region for connected wire runs
 * instead of many overlapping fill-only rectangles.
 */
inline std::vector<DiffBox> MergeWireBoxes( const std::vector<DiffBox>& aBoxes )
{
    std::vector<DiffBox> bordered;
    std::vector<DiffBox> borderless;

    for( const DiffBox& db : aBoxes )
    {
        if( db.hasBorder )
            bordered.push_back( db );
        else
            borderless.push_back( db );
    }

    if( borderless.empty() )
        return bordered;

    // Group overlapping borderless boxes by iterative merging.
    // Mark each box with a group index; merge groups when boxes overlap.
    std::vector<int> group( borderless.size() );
    for( size_t i = 0; i < group.size(); ++i )
        group[i] = static_cast<int>( i );

    // Simple union-find with path compression
    std::function<int(int)> find = [&]( int x ) -> int
    {
        while( group[x] != x )
        {
            group[x] = group[group[x]];
            x = group[x];
        }
        return x;
    };

    for( size_t i = 0; i < borderless.size(); ++i )
    {
        for( size_t j = i + 1; j < borderless.size(); ++j )
        {
            if( borderless[i].bbox.Intersects( borderless[j].bbox ) )
            {
                int gi = find( static_cast<int>( i ) );
                int gj = find( static_cast<int>( j ) );
                if( gi != gj )
                    group[gi] = gj;
            }
        }
    }

    // Compute union bbox per group
    std::unordered_map<int, BOX2I> groupBoxes;
    std::unordered_map<int, KIGFX::COLOR4D> groupColors;

    for( size_t i = 0; i < borderless.size(); ++i )
    {
        int g = find( static_cast<int>( i ) );
        auto it = groupBoxes.find( g );
        if( it == groupBoxes.end() )
        {
            groupBoxes[g] = borderless[i].bbox;
            groupColors[g] = borderless[i].color;
        }
        else
        {
            it->second.Merge( borderless[i].bbox );
        }
    }

    // Emit merged groups as bordered boxes
    for( auto& [g, bbox] : groupBoxes )
        bordered.push_back( { bbox, groupColors[g], true } );

    return bordered;
}


/**
 * Build highlight boxes for a set of items on a screen.
 *
 * Looks up each UUID in the screen's item list and produces a DiffBox
 * with the given color.  Wire/junction boxes are merged into continuous
 * bordered groups.
 */
inline std::vector<DiffBox> BuildHighlightBoxes( SCH_SCREEN* aScreen,
                                                  const std::set<KIID>& aUuids,
                                                  const KIGFX::COLOR4D& aColor )
{
    std::vector<DiffBox> boxes;

    if( !aScreen || aUuids.empty() )
        return boxes;

    for( SCH_ITEM* item : aScreen->Items() )
    {
        if( aUuids.count( item->m_Uuid ) )
            boxes.push_back( ComputeItemBox( item, aColor ) );
    }

    return MergeWireBoxes( boxes );
}


} // namespace SCH_DIFF

#endif // SCH_DIFF_HIGHLIGHT_H
