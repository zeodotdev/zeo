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

#include "multi_board_mbs_refresh.h"

#include "sch_module_block.h"
#include "sch_module_pin.h"
#include "sch_screen.h"

#include <project/project_file.h>
#include <project/multi_board_scan.h>

#include <algorithm>
#include <map>
#include <set>


namespace
{

// Layout knobs — mirror the initial-generation numbers in
// MULTI_BOARD_PROJECT::EnsureMbsFile so regenerated blocks look the same.
static constexpr double GRID_MM         = 1.27;
static constexpr int    MM_TO_IU        = 10000;

static constexpr int BLOCK_WIDTH_IU  = (int) ( 40 * GRID_MM * MM_TO_IU );
static constexpr int PER_PIN_IU      = (int) ( 5 * GRID_MM * MM_TO_IU );
static constexpr int PAD_TOP_IU      = (int) ( 8 * GRID_MM * MM_TO_IU );
static constexpr int PAD_BOT_IU      = (int) ( 8 * GRID_MM * MM_TO_IU );
static constexpr int MIN_HEIGHT_IU   = (int) ( 32 * GRID_MM * MM_TO_IU );
static constexpr int BLOCK_SPACING_IU = (int) ( 12 * GRID_MM * MM_TO_IU );
static constexpr int ROW_SPACING_IU  = (int) ( 20 * GRID_MM * MM_TO_IU );
static constexpr int START_X_IU      = (int) ( 40 * GRID_MM * MM_TO_IU );


struct PerBlockPlan
{
    SCH_MODULE_BLOCK*                 existing = nullptr;
    wxString                          subProjectPath;
    wxString                          subProjectName;
    wxString                          componentRef;
    KIID                              subProjectUuid;
    std::vector<MULTI_BOARD_PAD_INFO> expectedPads;   ///< Ordered pads from the scan
};


/**
 * Distribute the given pads evenly along the left (x=0) and right (x=width)
 * edges of a block of the given height, returning absolute pin positions
 * relative to block origin (0,0).
 */
void layoutPinsOnBlock( const std::vector<MULTI_BOARD_PAD_INFO>& aPads, int aBlockHeight,
                        std::vector<std::pair<MULTI_BOARD_PAD_INFO, VECTOR2I>>& aPositions )
{
    size_t pinCount   = aPads.size();
    size_t leftCount  = ( pinCount + 1 ) / 2;

    for( size_t i = 0; i < leftCount; ++i )
    {
        int y = PAD_TOP_IU + PER_PIN_IU * (int) i;
        aPositions.emplace_back( aPads[i], VECTOR2I( 0, y ) );
    }

    for( size_t i = leftCount; i < pinCount; ++i )
    {
        int y = PAD_TOP_IU + PER_PIN_IU * (int) ( i - leftCount );
        aPositions.emplace_back( aPads[i], VECTOR2I( BLOCK_WIDTH_IU, y ) );
    }
}


int computeBlockHeight( size_t aPinCount )
{
    size_t leftCount  = ( aPinCount + 1 ) / 2;
    size_t rightCount = aPinCount - leftCount;
    size_t maxSide    = std::max( leftCount, rightCount );

    int needed = PAD_TOP_IU + PAD_BOT_IU + PER_PIN_IU * (int) std::max<size_t>( maxSide, 1 );
    return std::max( needed, MIN_HEIGHT_IU );
}


/**
 * Find the next available spot to place a new block so it doesn't overlap
 * existing blocks. Simple strategy: place at (START_X, maxBottomY + spacing)
 * where maxBottomY is the bottom edge of the lowest existing block.
 */
VECTOR2I nextFreeSlot( SCH_SCREEN& aScreen )
{
    int maxBottom = 0;
    int anyBlock  = false;

    for( SCH_ITEM* item : aScreen.Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* b = static_cast<SCH_MODULE_BLOCK*>( item );
        int               bottom = b->GetPosition().y + b->GetSize().y;

        maxBottom = std::max( maxBottom, bottom );
        anyBlock  = true;
    }

    if( !anyBlock )
        return VECTOR2I( START_X_IU, START_X_IU );

    return VECTOR2I( START_X_IU, maxBottom + ROW_SPACING_IU );
}


} // anonymous namespace


MBS_REFRESH_RESULT RefreshMbsFromSubProjects( SCH_SCREEN& aMbsScreen,
                                              const PROJECT_FILE& aMultiBoard )
{
    MBS_REFRESH_RESULT result;

    // Index existing blocks by (sub_project_path, componentRef).
    std::map<std::pair<wxString, wxString>, SCH_MODULE_BLOCK*> existingByKey;

    for( SCH_ITEM* item : aMbsScreen.Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* b = static_cast<SCH_MODULE_BLOCK*>( item );
        existingByKey[{ b->GetSubProjectPath(), b->GetComponentRef() }] = b;
    }

    // Build plans: one per (sub_project, connector) found by scan.
    std::vector<PerBlockPlan> plans;

    for( const SUB_PROJECT_INFO& info : aMultiBoard.GetSubProjects() )
    {
        wxFileName proFile = aMultiBoard.ResolveSubProjectPath( info );
        wxFileName schFile = MultiBoardMainSchematic( proFile );
        wxFileName pcbFile = MultiBoardMainPcb( proFile );

        std::vector<wxString> connectors = MultiBoardScanConnectorReferences( schFile );
        std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>> pads =
                MultiBoardScanConnectorPads( pcbFile );

        for( const wxString& ref : connectors )
        {
            PerBlockPlan plan;
            plan.subProjectPath = info.relativePath;
            plan.subProjectName = info.displayName.IsEmpty() ? info.name : info.displayName;
            plan.componentRef   = ref;
            plan.subProjectUuid = info.uuid;

            auto it = pads.find( ref );

            if( it != pads.end() )
                plan.expectedPads = it->second;
            else
                plan.expectedPads.push_back( MULTI_BOARD_PAD_INFO{} );  // placeholder

            auto existIt = existingByKey.find( { info.relativePath, ref } );

            if( existIt != existingByKey.end() )
                plan.existing = existIt->second;

            plans.push_back( std::move( plan ) );
        }
    }

    // Apply each plan: add missing pins on existing blocks, create new blocks
    // for pairs that weren't present.
    for( const PerBlockPlan& plan : plans )
    {
        if( plan.existing )
        {
            // Collect existing pin pad numbers on this block.
            std::set<wxString> existingPadNumbers;

            for( SCH_MODULE_PIN* pin : plan.existing->GetPins() )
                existingPadNumbers.insert( pin->GetPinNumber() );

            std::vector<std::pair<MULTI_BOARD_PAD_INFO, VECTOR2I>> fullLayout;
            int blockHeight = computeBlockHeight( plan.expectedPads.size() );
            layoutPinsOnBlock( plan.expectedPads, blockHeight, fullLayout );

            for( const auto& [padInfo, localPos] : fullLayout )
            {
                if( existingPadNumbers.count( padInfo.padNumber ) )
                    continue;

                VECTOR2I absPos  = plan.existing->GetPosition() + localPos;
                wxString label   = MultiBoardPinLabel( plan.componentRef, padInfo );

                SCH_MODULE_PIN* pin = new SCH_MODULE_PIN( plan.existing, absPos, label );
                pin->SetComponentRef( plan.componentRef );
                pin->SetPinNumber( padInfo.padNumber );
                pin->ConstrainOnEdge( absPos, true );

                plan.existing->AddPin( pin );
                result.pinsAdded++;
            }
        }
        else
        {
            // Create a new block.
            VECTOR2I origin = nextFreeSlot( aMbsScreen );

            auto* block = new SCH_MODULE_BLOCK( origin );
            block->SetSubProjectUuid( plan.subProjectUuid );
            block->SetSubProjectPath( plan.subProjectPath );
            block->SetComponentRef( plan.componentRef );
            block->SetDisplayName( plan.subProjectName + wxT( " / " ) + plan.componentRef );

            int blockHeight = computeBlockHeight( plan.expectedPads.size() );
            block->SetSize( VECTOR2I( BLOCK_WIDTH_IU, blockHeight ) );

            std::vector<std::pair<MULTI_BOARD_PAD_INFO, VECTOR2I>> layout;
            layoutPinsOnBlock( plan.expectedPads, blockHeight, layout );

            for( const auto& [padInfo, localPos] : layout )
            {
                VECTOR2I absPos = origin + localPos;
                wxString label  = MultiBoardPinLabel( plan.componentRef, padInfo );

                SCH_MODULE_PIN* pin = new SCH_MODULE_PIN( block, absPos, label );
                pin->SetComponentRef( plan.componentRef );
                pin->SetPinNumber( padInfo.padNumber );
                pin->ConstrainOnEdge( absPos, true );

                block->AddPin( pin );
                result.pinsAdded++;
            }

            aMbsScreen.Append( block );
            result.blocksAdded++;
        }
    }

    result.summary = wxString::Format(
            wxT( "Added %d module block(s) and %d pin(s) from sub-project scan." ),
            result.blocksAdded, result.pinsAdded );

    return result;
}
