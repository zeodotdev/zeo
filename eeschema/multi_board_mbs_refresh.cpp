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
#include <reporter.h>
#include <view/view.h>

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>


namespace
{

// Layout knobs — mirror the initial-generation numbers in
// MULTI_BOARD_PROJECT::EnsureMbsFile so regenerated blocks look the same.
static constexpr double GRID_MM          = 1.27;
static constexpr int    MM_TO_IU         = 10000;

static constexpr int BLOCK_WIDTH_IU   = (int) ( 40 * GRID_MM * MM_TO_IU );
static constexpr int PER_PIN_IU       = (int) (  5 * GRID_MM * MM_TO_IU );
static constexpr int PAD_TOP_IU       = (int) (  8 * GRID_MM * MM_TO_IU );
static constexpr int PAD_BOT_IU       = (int) (  8 * GRID_MM * MM_TO_IU );
static constexpr int MIN_HEIGHT_IU    = (int) ( 32 * GRID_MM * MM_TO_IU );
static constexpr int ROW_SPACING_IU   = (int) ( 20 * GRID_MM * MM_TO_IU );
static constexpr int START_X_IU       = (int) ( 40 * GRID_MM * MM_TO_IU );


void layoutPinsOnBlock( const std::vector<MULTI_BOARD_PAD_INFO>& aPads, int aBlockHeight,
                        std::vector<std::pair<MULTI_BOARD_PAD_INFO, VECTOR2I>>& aPositions )
{
    size_t pinCount  = aPads.size();
    size_t leftCount = ( pinCount + 1 ) / 2;

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
 * Pick the next "B<N>" reference not already used by any block on the
 * screen. Scan to the first numeric gap so freshly-annotated refs
 * stay compact when the user has manually set some.
 */
wxString nextMbsReference( SCH_SCREEN& aScreen )
{
    std::set<int> used;

    for( SCH_ITEM* item : aScreen.Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* b     = static_cast<SCH_MODULE_BLOCK*>( item );
        const wxString&   ref   = b->GetMbsReference();

        if( ref.IsEmpty() || !ref.StartsWith( wxT( "B" ) ) )
            continue;

        long n = 0;

        if( ref.Mid( 1 ).ToLong( &n ) && n > 0 )
            used.insert( (int) n );
    }

    int candidate = 1;

    while( used.count( candidate ) )
        candidate++;

    return wxString::Format( wxT( "B%d" ), candidate );
}


VECTOR2I nextFreeSlot( SCH_SCREEN& aScreen )
{
    int  maxBottom = 0;
    bool anyBlock  = false;

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


/**
 * Best-effort display label for a sub-project. Falls back to the path
 * basename if no explicit display name was set in the container.
 */
wxString subProjectDisplay( const SUB_PROJECT_INFO& aInfo )
{
    if( !aInfo.displayName.IsEmpty() )
        return aInfo.displayName;

    if( !aInfo.name.IsEmpty() )
        return aInfo.name;

    return aInfo.relativePath;
}

} // anonymous namespace


wxString MBS_CHANGE::Describe() const
{
    wxString boardLabel = subProjectDisplayName.IsEmpty() ? subProjectPath : subProjectDisplayName;

    // Block identity preferred for rows that touch an existing block:
    // show the MBS-scoped annotation (e.g. "B1") since that's the
    // unique identifier the user sees in the canvas. The sub-project
    // componentRef (J2) follows as context.
    wxString blockId = componentRef;

    if( existingBlock && !existingBlock->GetMbsReference().IsEmpty() )
        blockId = existingBlock->GetMbsReference() + wxT( " / " ) + componentRef;

    switch( kind )
    {
    case KIND::ADD_BLOCK:
        // Empty componentRef + empty pad list = "the sub-project exists in
        // the container but has no connectors yet, and nothing on the MBS
        // represents it." Emit a placeholder block so the user can see the
        // sub-project on the canvas; it'll be replaced by real connector
        // blocks on a future refresh once connectors are added.
        if( componentRef.IsEmpty() && blockAllPads.empty() )
            return wxString::Format( _( "Add placeholder block for %s "
                                        "(no connectors detected yet)" ),
                                     boardLabel );

        return wxString::Format( _( "Add block %s (%s, %zu pin(s))" ),
                                 componentRef, boardLabel, blockAllPads.size() );

    case KIND::REMOVE_BLOCK:
        return wxString::Format( _( "Remove block %s (%s) — no longer in sub-project" ),
                                 blockId, boardLabel );

    case KIND::ADD_PIN:
        return wxString::Format( _( "Add pin %s.%s (\"%s\") to %s" ),
                                 blockId, pinNumber, newLabel, boardLabel );

    case KIND::REMOVE_PIN:
        return wxString::Format( _( "Remove pin %s.%s from %s (pad vanished from PCB)" ),
                                 blockId, pinNumber, boardLabel );

    case KIND::RENAME_PIN:
        return wxString::Format( _( "Rename pin %s.%s on %s: \"%s\" → \"%s\"" ),
                                 blockId, pinNumber, boardLabel, oldLabel, newLabel );

    case KIND::PATH_DRIFT:
        return wxString::Format( _( "Update path on %s: %s → %s" ),
                                 boardLabel, oldPath, newPath );

    case KIND::UPGRADE_UUID:
        return wxString::Format( _( "Stamp sub-project UUID onto legacy block %s (%s)" ),
                                 blockId, boardLabel );
    }

    return wxEmptyString;
}


/**
 * Normalize a `.kicad_pro` relative path for diff matching: strip a
 * leading `./`, fold backslashes to forward slashes, drop a trailing
 * separator if any. Lets blocks saved with subtle path-format
 * differences still match on the path-fallback lookup.
 */
static wxString normalizeMbsRelPath( wxString aPath )
{
    aPath.Replace( wxT( "\\" ), wxT( "/" ) );

    if( aPath.StartsWith( wxT( "./" ) ) )
        aPath.Remove( 0, 2 );

    while( aPath.EndsWith( wxT( "/" ) ) )
        aPath.RemoveLast( 1 );

    return aPath;
}


/**
 * Extract the directory basename (last path component) from a
 * sub-project's `.kicad_pro` path. Used as a final-fallback match key
 * because the sub-project's own directory name is what the user
 * typically thinks of as its identity (and what SUB_PROJECT_INFO::name
 * stores).
 */
static wxString subProjectNameFromPath( const wxString& aPath )
{
    wxString p = normalizeMbsRelPath( aPath );

    // Path looks like "boards/esp_cm/esp_cm.kicad_pro"; the directory
    // basename is the second-to-last component ("esp_cm"). When the
    // path has no directory parts, fall back to the file basename.
    wxFileName fn( p );

    if( fn.GetDirCount() > 0 )
        return fn.GetDirs().Last();

    return fn.GetName();
}


std::vector<MBS_CHANGE> ComputeMbsRefreshDiff( SCH_SCREEN& aMbsScreen,
                                               const PROJECT_FILE& aMultiBoard )
{
    std::vector<MBS_CHANGE> changes;

    // --- Index existing blocks ---------------------------------------
    //
    // Primary: (sub_project_uuid, componentRef). Stable across path
    // renames.
    //
    // Fallback A: (normalized sub_project_path, componentRef). Used for
    // legacy blocks saved before sub_project_uuid was persisted, AND
    // for blocks whose stored path differs in trivial formatting from
    // the container's relativePath (leading ./, separator style).
    // Matching here queues an UPGRADE_UUID change.
    //
    // Fallback B: (sub_project name, componentRef). Final defensive
    // match — the sub-project directory basename is stable across path
    // moves and case-insensitive on macOS/Windows. Without this an
    // incidental UUID drift between the .kicad_pro and the .kicad_mbs
    // (e.g. user manually edited one of the files) causes a destructive
    // teardown of all existing blocks. Matching here also stamps the
    // current uuid + path onto the existing block.
    std::map<std::pair<KIID, wxString>, SCH_MODULE_BLOCK*>     byUuid;
    std::map<std::pair<wxString, wxString>, SCH_MODULE_BLOCK*> byPath;
    std::map<std::pair<wxString, wxString>, SCH_MODULE_BLOCK*> byName;

    // Connector-less placeholder index: keyed on sub-project UUID alone
    // (componentRef is empty by definition for a placeholder). Used to
    // (a) keep an existing placeholder matched on a refresh while the
    // sub-project still has no connectors — i.e. avoid REMOVE/ADD churn
    // — and (b) detect "newly-added sub-project with no representation
    // on the MBS" so we can emit a fresh placeholder for it. See the
    // sub-project-walk below.
    std::map<KIID, SCH_MODULE_BLOCK*> placeholderByUuid;

    for( SCH_ITEM* item : aMbsScreen.Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* b = static_cast<SCH_MODULE_BLOCK*>( item );

        if( b->GetSubProjectUuid() != niluuid )
            byUuid[{ b->GetSubProjectUuid(), b->GetComponentRef() }] = b;
        else
            byPath[{ normalizeMbsRelPath( b->GetSubProjectPath() ),
                      b->GetComponentRef() }] = b;

        // Always populate byName regardless of uuid presence — it's the
        // final-fallback for any matching attempt that fails the first
        // two paths.
        wxString existingName = subProjectNameFromPath( b->GetSubProjectPath() );

        if( !existingName.IsEmpty() )
            byName[{ existingName, b->GetComponentRef() }] = b;

        if( b->GetComponentRef().IsEmpty() && b->GetSubProjectUuid() != niluuid )
            placeholderByUuid[b->GetSubProjectUuid()] = b;

        wxLogInfo( wxT( "MBS diff: existing block uuid=%s path=%s ref=%s name=%s" ),
                   b->GetSubProjectUuid().AsString(),
                   b->GetSubProjectPath(),
                   b->GetComponentRef(),
                   existingName );
    }

    // Track which existing blocks got matched to something in the scan.
    // Anything left un-matched is an orphan → REMOVE_BLOCK candidate.
    std::set<SCH_MODULE_BLOCK*> matchedBlocks;

    // --- Walk the scan, emit ADD / UPDATE / DRIFT / UPGRADE ----------
    for( const SUB_PROJECT_INFO& info : aMultiBoard.GetSubProjects() )
    {
        wxFileName proFile = aMultiBoard.ResolveSubProjectPath( info );
        wxFileName schFile = MultiBoardMainSchematic( proFile );
        wxFileName pcbFile = MultiBoardMainPcb( proFile );

        std::vector<wxString> connectors = MultiBoardScanConnectorReferences( schFile );

        wxLogInfo( wxT( "MBS diff: container sub-project name=%s uuid=%s path=%s "
                        "resolved=%s connectors=%zu" ),
                   info.name, info.uuid.AsString(),
                   info.relativePath, proFile.GetFullPath(),
                   connectors.size() );
        std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>> padsByRef =
                MultiBoardScanConnectorPads( pcbFile );

        wxString displayName = subProjectDisplay( info );

        for( const wxString& ref : connectors )
        {
            std::vector<MULTI_BOARD_PAD_INFO> expectedPads;

            if( auto it = padsByRef.find( ref ); it != padsByRef.end() )
                expectedPads = it->second;
            else
                expectedPads.push_back( MULTI_BOARD_PAD_INFO{} );   // placeholder

            // Locate an existing block for this (subProject, ref) via
            // UUID first, normalized path second, sub-project name third.
            SCH_MODULE_BLOCK* existing = nullptr;
            bool              upgradeUuid = false;
            bool              pathDrifted = false;

            wxString normalizedInfoPath = normalizeMbsRelPath( info.relativePath );

            if( auto it = byUuid.find( { info.uuid, ref } ); it != byUuid.end() )
            {
                existing = it->second;
                pathDrifted = normalizeMbsRelPath( existing->GetSubProjectPath() )
                              != normalizedInfoPath;
            }
            else if( auto it2 = byPath.find( { normalizedInfoPath, ref } );
                     it2 != byPath.end() )
            {
                existing    = it2->second;
                upgradeUuid = true;
            }
            else if( !info.name.IsEmpty() )
            {
                // Final fallback: match by sub-project directory name.
                // Catches the case where both UUID AND path lookups fail
                // due to drift between the .kicad_pro and the .kicad_mbs
                // (e.g. user manually edited one file, or a legacy save
                // path stored a slightly different format).
                if( auto it3 = byName.find( { info.name, ref } ); it3 != byName.end() )
                {
                    existing    = it3->second;
                    upgradeUuid = ( existing->GetSubProjectUuid() != info.uuid );
                    pathDrifted = normalizeMbsRelPath( existing->GetSubProjectPath() )
                                  != normalizedInfoPath;

                    wxLogInfo( wxT( "MBS diff: matched via NAME fallback: name=%s ref=%s "
                                    "(stored uuid=%s, container uuid=%s, "
                                    "stored path=%s, container path=%s)" ),
                               info.name, ref,
                               existing->GetSubProjectUuid().AsString(),
                               info.uuid.AsString(),
                               existing->GetSubProjectPath(),
                               info.relativePath );
                }
            }

            if( !existing )
            {
                // ADD_BLOCK
                MBS_CHANGE ch;
                ch.kind                  = MBS_CHANGE::KIND::ADD_BLOCK;
                ch.subProjectUuid        = info.uuid;
                ch.subProjectPath        = info.relativePath;
                ch.subProjectDisplayName = displayName;
                ch.componentRef          = ref;
                ch.blockAllPads          = expectedPads;
                changes.push_back( std::move( ch ) );
                continue;
            }

            matchedBlocks.insert( existing );

            if( upgradeUuid )
            {
                MBS_CHANGE ch;
                ch.kind                  = MBS_CHANGE::KIND::UPGRADE_UUID;
                ch.subProjectUuid        = info.uuid;
                ch.subProjectPath        = info.relativePath;
                ch.subProjectDisplayName = displayName;
                ch.componentRef          = ref;
                ch.existingBlock         = existing;
                changes.push_back( std::move( ch ) );
            }

            if( pathDrifted )
            {
                MBS_CHANGE ch;
                ch.kind                  = MBS_CHANGE::KIND::PATH_DRIFT;
                ch.subProjectUuid        = info.uuid;
                ch.subProjectPath        = info.relativePath;
                ch.subProjectDisplayName = displayName;
                ch.componentRef          = ref;
                ch.oldPath               = existing->GetSubProjectPath();
                ch.newPath               = info.relativePath;
                ch.existingBlock         = existing;
                changes.push_back( std::move( ch ) );
            }

            // Pin-level diff: map existing pins by pad number, compare
            // against expected. Missing → ADD_PIN. Extra → REMOVE_PIN.
            // Label mismatch → RENAME_PIN.
            std::unordered_map<wxString, SCH_MODULE_PIN*> existingPinByNumber;

            for( SCH_MODULE_PIN* pin : existing->GetPins() )
                existingPinByNumber[pin->GetPinNumber()] = pin;

            std::set<wxString> expectedNumbers;

            for( const MULTI_BOARD_PAD_INFO& padInfo : expectedPads )
            {
                if( padInfo.padNumber.IsEmpty() )
                    continue;   // placeholder, not a real pad

                expectedNumbers.insert( padInfo.padNumber );

                wxString expectedLabel = MultiBoardPinLabel( ref, padInfo );
                auto     it            = existingPinByNumber.find( padInfo.padNumber );

                if( it == existingPinByNumber.end() )
                {
                    MBS_CHANGE ch;
                    ch.kind                  = MBS_CHANGE::KIND::ADD_PIN;
                    ch.subProjectUuid        = info.uuid;
                    ch.subProjectPath        = info.relativePath;
                    ch.subProjectDisplayName = displayName;
                    ch.componentRef          = ref;
                    ch.pinNumber             = padInfo.padNumber;
                    ch.newLabel              = expectedLabel;
                    ch.existingBlock         = existing;
                    ch.padInfo               = padInfo;
                    changes.push_back( std::move( ch ) );
                }
                else if( it->second->GetText() != expectedLabel )
                {
                    MBS_CHANGE ch;
                    ch.kind                  = MBS_CHANGE::KIND::RENAME_PIN;
                    ch.subProjectUuid        = info.uuid;
                    ch.subProjectPath        = info.relativePath;
                    ch.subProjectDisplayName = displayName;
                    ch.componentRef          = ref;
                    ch.pinNumber             = padInfo.padNumber;
                    ch.oldLabel              = it->second->GetText();
                    ch.newLabel              = expectedLabel;
                    ch.existingBlock         = existing;
                    ch.existingPin           = it->second;
                    changes.push_back( std::move( ch ) );
                }
            }

            for( SCH_MODULE_PIN* pin : existing->GetPins() )
            {
                if( expectedNumbers.count( pin->GetPinNumber() ) )
                    continue;

                MBS_CHANGE ch;
                ch.kind                  = MBS_CHANGE::KIND::REMOVE_PIN;
                ch.subProjectUuid        = info.uuid;
                ch.subProjectPath        = info.relativePath;
                ch.subProjectDisplayName = displayName;
                ch.componentRef          = ref;
                ch.pinNumber             = pin->GetPinNumber();
                ch.oldLabel              = pin->GetText();
                ch.existingBlock         = existing;
                ch.existingPin           = pin;
                changes.push_back( std::move( ch ) );
            }
        }

        // Placeholder block for sub-projects with no connectors yet.
        //
        // A sub-project added to the container via Manage Boards starts
        // life with no connector-class symbols (the default project
        // template doesn't include any). Without this branch the
        // connector loop never executes for it, so the diff is empty
        // and the user thinks the refresh "missed" their newly-added
        // board. We emit a placeholder ADD_BLOCK (empty componentRef,
        // no pads) so the new sub-project becomes visible on the MBS;
        // a later refresh — once the user has actually added a
        // connector to that sub-project — replaces the placeholder
        // with real connector blocks via the orphan/add path.
        //
        // If a placeholder for this sub-project already exists, mark
        // it as matched so we don't churn (REMOVE+ADD on every
        // refresh).
        if( connectors.empty() )
        {
            auto pit = placeholderByUuid.find( info.uuid );

            if( pit != placeholderByUuid.end() )
            {
                matchedBlocks.insert( pit->second );
            }
            else
            {
                MBS_CHANGE ch;
                ch.kind                  = MBS_CHANGE::KIND::ADD_BLOCK;
                ch.subProjectUuid        = info.uuid;
                ch.subProjectPath        = info.relativePath;
                ch.subProjectDisplayName = displayName;
                ch.componentRef          = wxEmptyString;
                ch.blockAllPads          = {};
                changes.push_back( std::move( ch ) );
            }
        }
    }

    // --- Orphan blocks: REMOVE_BLOCK ---------------------------------
    //
    // Any block that didn't get matched above corresponds to a
    // sub-project / connector no longer in the scan — either the
    // sub-project was removed from the container, or the user renamed
    // the connector on the sub-project PCB.
    for( SCH_ITEM* item : aMbsScreen.Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* b = static_cast<SCH_MODULE_BLOCK*>( item );

        if( matchedBlocks.count( b ) )
            continue;

        MBS_CHANGE ch;
        ch.kind                  = MBS_CHANGE::KIND::REMOVE_BLOCK;
        ch.subProjectUuid        = b->GetSubProjectUuid();
        ch.subProjectPath        = b->GetSubProjectPath();
        ch.subProjectDisplayName = b->GetDisplayName().IsEmpty() ? b->GetSubProjectPath()
                                                                 : b->GetDisplayName();
        ch.componentRef          = b->GetComponentRef();
        ch.existingBlock         = b;
        changes.push_back( std::move( ch ) );
    }

    return changes;
}


MBS_REFRESH_RESULT ApplyMbsRefreshChanges( SCH_SCREEN& aMbsScreen,
                                           const std::vector<MBS_CHANGE>& aChanges,
                                           KIGFX::VIEW* aView,
                                           REPORTER* aReporter )
{
    MBS_REFRESH_RESULT result;

    auto report = [&]( const wxString& aMsg, SEVERITY aSeverity = RPT_SEVERITY_INFO )
    {
        if( aReporter )
            aReporter->Report( aMsg, aSeverity );
    };

    // Apply in a deterministic order so deletes happen before adds —
    // prevents e.g. a REMOVE_BLOCK from deleting a block that a
    // subsequent ADD_PIN depends on when the user checks both.
    auto kindOrder = []( MBS_CHANGE::KIND k ) -> int
    {
        switch( k )
        {
        case MBS_CHANGE::KIND::UPGRADE_UUID:  return 0;
        case MBS_CHANGE::KIND::PATH_DRIFT:    return 1;
        case MBS_CHANGE::KIND::RENAME_PIN:    return 2;
        case MBS_CHANGE::KIND::REMOVE_PIN:    return 3;
        case MBS_CHANGE::KIND::REMOVE_BLOCK:  return 4;
        case MBS_CHANGE::KIND::ADD_BLOCK:     return 5;
        case MBS_CHANGE::KIND::ADD_PIN:       return 6;
        }

        return 99;
    };

    std::vector<const MBS_CHANGE*> ordered;
    ordered.reserve( aChanges.size() );

    for( const MBS_CHANGE& ch : aChanges )
    {
        if( ch.checked )
            ordered.push_back( &ch );
    }

    std::stable_sort( ordered.begin(), ordered.end(),
                      [&]( const MBS_CHANGE* a, const MBS_CHANGE* b )
                      {
                          return kindOrder( a->kind ) < kindOrder( b->kind );
                      } );

    // Track blocks that got REMOVE_BLOCK'd so we skip any pin-level
    // changes that targeted them. Pointers to freed blocks would UB.
    std::set<SCH_MODULE_BLOCK*> deletedBlocks;

    for( const MBS_CHANGE* ch : ordered )
    {
        // If the target block was just deleted upstream, skip — and
        // surface it as a warning so the user can correlate "I asked
        // for ADD_PIN on B7 but nothing happened" with "B7 was also
        // REMOVE_BLOCK'd in the same pass."
        if( ch->existingBlock && deletedBlocks.count( ch->existingBlock ) )
        {
            report( wxString::Format( _( "Skipped (parent block was removed): %s" ),
                                       ch->Describe() ),
                     RPT_SEVERITY_WARNING );
            continue;
        }

        switch( ch->kind )
        {
        case MBS_CHANGE::KIND::UPGRADE_UUID:
        {
            if( ch->existingBlock )
            {
                ch->existingBlock->SetSubProjectUuid( ch->subProjectUuid );
                result.uuidsStamped++;
                report( ch->Describe() );
            }

            break;
        }

        case MBS_CHANGE::KIND::PATH_DRIFT:
        {
            if( ch->existingBlock )
            {
                ch->existingBlock->SetSubProjectPath( ch->newPath );
                result.pathsUpdated++;
                report( ch->Describe() );
            }

            break;
        }

        case MBS_CHANGE::KIND::RENAME_PIN:
        {
            if( ch->existingPin )
            {
                ch->existingPin->SetText( ch->newLabel );

                // Pin text is drawn as part of the parent block in
                // SCH_PAINTER, so the block item itself needs an
                // invalidation to repaint the new label.
                if( aView && ch->existingBlock )
                    aView->Update( ch->existingBlock, KIGFX::REPAINT );

                result.pinsRenamed++;
                report( ch->Describe() );
            }

            break;
        }

        case MBS_CHANGE::KIND::REMOVE_PIN:
        {
            if( ch->existingBlock && ch->existingPin )
            {
                ch->existingBlock->RemovePin( ch->existingPin );

                if( aView )
                    aView->Update( ch->existingBlock, KIGFX::REPAINT );

                result.pinsRemoved++;
                report( ch->Describe(), RPT_SEVERITY_ACTION );
            }

            break;
        }

        case MBS_CHANGE::KIND::REMOVE_BLOCK:
        {
            if( ch->existingBlock )
            {
                // Remove from the view BEFORE deleting so the GAL
                // layer cache doesn't end up with a dangling pointer.
                // Append/Remove on SCH_SCREEN only mutates the RTree;
                // the view has its own tracking that must be kept in
                // sync explicitly here.
                if( aView )
                    aView->Remove( ch->existingBlock );

                aMbsScreen.Remove( ch->existingBlock );
                deletedBlocks.insert( ch->existingBlock );
                delete ch->existingBlock;
                result.blocksRemoved++;
                report( ch->Describe(), RPT_SEVERITY_ACTION );
            }

            break;
        }

        case MBS_CHANGE::KIND::ADD_BLOCK:
        {
            VECTOR2I origin = nextFreeSlot( aMbsScreen );
            auto*    block  = new SCH_MODULE_BLOCK( origin );

            block->SetSubProjectUuid( ch->subProjectUuid );
            block->SetSubProjectPath( ch->subProjectPath );
            block->SetComponentRef( ch->componentRef );

            // Auto-assign an MBS-scoped annotation. Compute after the
            // block is wired to its sub-project info but before
            // Append, so nextMbsReference sees the accurate state of
            // the screen (pre-insert) and any earlier adds in this
            // apply pass contribute to the used-ref set.
            block->SetMbsReference( nextMbsReference( aMbsScreen ) );

            wxString label = ch->subProjectDisplayName.IsEmpty() ? ch->subProjectPath
                                                                 : ch->subProjectDisplayName;

            // Placeholder blocks (empty componentRef, no pads) are
            // emitted for sub-projects without connectors so the user
            // can still see the sub-project on the MBS. Drop the
            // " / <ref>" suffix in that case — it'd render as a
            // trailing " / " with nothing after it.
            if( ch->componentRef.IsEmpty() )
                block->SetDisplayName( label );
            else
                block->SetDisplayName( label + wxT( " / " ) + ch->componentRef );

            int height = computeBlockHeight( ch->blockAllPads.size() );
            block->SetSize( VECTOR2I( BLOCK_WIDTH_IU, height ) );

            std::vector<std::pair<MULTI_BOARD_PAD_INFO, VECTOR2I>> layout;
            layoutPinsOnBlock( ch->blockAllPads, height, layout );

            for( const auto& [padInfo, localPos] : layout )
            {
                VECTOR2I absPos   = origin + localPos;
                wxString pinLabel = MultiBoardPinLabel( ch->componentRef, padInfo );

                auto* pin = new SCH_MODULE_PIN( block, absPos, pinLabel );
                pin->SetComponentRef( ch->componentRef );
                pin->SetPinNumber( padInfo.padNumber );
                pin->SetType( padInfo.electricalType );
                pin->ConstrainOnEdge( absPos, true );

                block->AddPin( pin );
                result.pinsAdded++;
            }

            aMbsScreen.Append( block );

            // Screen's Append() only inserts into the RTree; the GAL
            // view also needs to be told, otherwise the block won't
            // draw until the MBS is closed + reopened.
            if( aView )
                aView->Add( block );

            result.blocksAdded++;
            result.newlyAddedBlocks.push_back( block );
            report( ch->Describe() );
            break;
        }

        case MBS_CHANGE::KIND::ADD_PIN:
        {
            if( !ch->existingBlock )
                break;

            // Re-layout the block's existing pin set with the new pin
            // inserted at the end of the list. We keep existing pin
            // positions untouched and place the new one at the next
            // free slot on the block's pin grid.
            int      height   = ch->existingBlock->GetSize().y;
            size_t   pinCount = ch->existingBlock->GetPins().size();
            VECTOR2I origin   = ch->existingBlock->GetPosition();

            // Append at bottom-right of whichever column is shorter.
            size_t leftCount  = ( pinCount + 1 ) / 2;
            size_t rightCount = pinCount - leftCount;

            VECTOR2I localPos;

            if( leftCount <= rightCount )
                localPos = VECTOR2I( 0, PAD_TOP_IU + PER_PIN_IU * (int) leftCount );
            else
                localPos = VECTOR2I( BLOCK_WIDTH_IU,
                                     PAD_TOP_IU + PER_PIN_IU * (int) rightCount );

            VECTOR2I absPos = origin + localPos;
            wxString label  = ch->newLabel.IsEmpty()
                                     ? MultiBoardPinLabel( ch->componentRef, ch->padInfo )
                                     : ch->newLabel;

            auto* pin = new SCH_MODULE_PIN( ch->existingBlock, absPos, label );
            pin->SetComponentRef( ch->componentRef );
            pin->SetPinNumber( ch->pinNumber );
            pin->SetType( ch->padInfo.electricalType );
            pin->ConstrainOnEdge( absPos, true );

            ch->existingBlock->AddPin( pin );

            // Grow the block if the new pin pushed us past the edge.
            int newHeight = computeBlockHeight( pinCount + 1 );

            if( newHeight > height )
                ch->existingBlock->SetSize( VECTOR2I( BLOCK_WIDTH_IU, newHeight ) );

            // Both GEOMETRY (block may have grown) and REPAINT (pin
            // text render as child of block) — force a full refresh.
            if( aView )
                aView->Update( ch->existingBlock, KIGFX::GEOMETRY );

            result.pinsAdded++;
            report( ch->Describe() );
            break;
        }
        }
    }

    // Sweep zero-pin blocks. These can survive a refresh when the user
    // checks every REMOVE_PIN for a block but leaves REMOVE_BLOCK
    // unchecked, or when a sub-project lost its last connector between
    // refreshes. An MBS block exists to host pins — without any, it's a
    // bare label that can't take wires, so auto-deletion is safe.
    //
    // Exception: blocks added in THIS apply pass with an intentionally
    // empty pad list — i.e. placeholder blocks for sub-projects that
    // have no connectors yet (see ComputeMbsRefreshDiff's "placeholder"
    // branch). Those are placed precisely so the user can see the
    // newly-added sub-project on the MBS until they introduce a
    // connector; sweeping them would defeat the point.
    {
        std::set<SCH_MODULE_BLOCK*> newlyAdded( result.newlyAddedBlocks.begin(),
                                                result.newlyAddedBlocks.end() );

        std::vector<SCH_MODULE_BLOCK*> empties;

        for( SCH_ITEM* item : aMbsScreen.Items() )
        {
            if( item->Type() != SCH_MODULE_BLOCK_T )
                continue;

            SCH_MODULE_BLOCK* block = static_cast<SCH_MODULE_BLOCK*>( item );

            if( newlyAdded.count( block ) )
                continue;

            if( block->GetPins().empty() && !deletedBlocks.count( block ) )
                empties.push_back( block );
        }

        for( SCH_MODULE_BLOCK* block : empties )
        {
            wxString sweepLabel = block->GetMbsReference().IsEmpty()
                                          ? block->GetDisplayName()
                                          : block->GetMbsReference();

            if( aView )
                aView->Remove( block );

            aMbsScreen.Remove( block );
            deletedBlocks.insert( block );
            delete block;
            result.blocksRemoved++;

            report( wxString::Format(
                            _( "Swept empty block %s — no pins remained after pin "
                               "removals" ),
                            sweepLabel ),
                     RPT_SEVERITY_ACTION );
        }
    }

    result.summary = wxString::Format(
            _( "Applied: +%d block(s), -%d block(s), +%d pin(s), -%d pin(s), "
               "%d renamed, %d path(s) updated, %d UUID(s) stamped." ),
            result.blocksAdded, result.blocksRemoved, result.pinsAdded,
            result.pinsRemoved, result.pinsRenamed, result.pathsUpdated,
            result.uuidsStamped );

    return result;
}


MBS_REFRESH_RESULT RefreshMbsFromSubProjects( SCH_SCREEN& aMbsScreen,
                                              const PROJECT_FILE& aMultiBoard )
{
    std::vector<MBS_CHANGE> changes = ComputeMbsRefreshDiff( aMbsScreen, aMultiBoard );
    return ApplyMbsRefreshChanges( aMbsScreen, changes );
}
