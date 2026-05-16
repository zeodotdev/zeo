/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#pragma once

#include <kiid.h>
#include <set>
#include <functional>

// Forward declarations - avoid including heavy headers
class SCHEMATIC;
class BOARD;
class SCH_ITEM;
class BOARD_ITEM;
class AGENT_WORKSPACE;

/**
 * Callback type for notifying about user edits to items.
 * Parameters: item UUID, optional property name, old value, new value
 */
using USER_EDIT_CALLBACK = std::function<void( const KIID&, const wxString&,
                                                const wxString&, const wxString& )>;


/**
 * SCH_CONFLICT_DETECTOR listens to schematic changes and detects when
 * user edits conflict with agent's working set.
 *
 * This class implements SCHEMATIC_LISTENER (defined in schematic.h) but
 * we use a PIMPL-style approach to avoid including the heavy schematic headers
 * in this header file.
 */
class SCH_CONFLICT_DETECTOR
{
public:
    SCH_CONFLICT_DETECTOR();
    ~SCH_CONFLICT_DETECTOR();

    /**
     * Set the agent workspace to monitor for conflicts.
     */
    void SetWorkspace( AGENT_WORKSPACE* aWorkspace );

    /**
     * Set the agent's working set of item IDs.
     * User edits to these items will trigger conflict detection.
     */
    void SetAgentWorkingSet( const std::set<KIID>& aWorkingSet );

    /**
     * Add an item to the agent's working set.
     */
    void AddToWorkingSet( const KIID& aItemId );

    /**
     * Remove an item from the agent's working set.
     */
    void RemoveFromWorkingSet( const KIID& aItemId );

    /**
     * Clear the agent's working set.
     */
    void ClearWorkingSet();

    /**
     * Check if an item is in the agent's working set.
     */
    bool IsInWorkingSet( const KIID& aItemId ) const;

    /**
     * Enable or disable conflict detection.
     */
    void SetEnabled( bool aEnabled ) { m_enabled = aEnabled; }

    /**
     * Check if conflict detection is enabled.
     */
    bool IsEnabled() const { return m_enabled; }

    /**
     * Set callback for user edit notifications.
     */
    void SetUserEditCallback( USER_EDIT_CALLBACK aCallback ) { m_userEditCallback = aCallback; }

    // SCHEMATIC_LISTENER interface implementation
    void OnSchItemsAdded( SCHEMATIC& aSch, std::vector<SCH_ITEM*>& aSchItems );
    void OnSchItemsRemoved( SCHEMATIC& aSch, std::vector<SCH_ITEM*>& aSchItems );
    void OnSchItemsChanged( SCHEMATIC& aSch, std::vector<SCH_ITEM*>& aSchItems );
    void OnSchSheetChanged( SCHEMATIC& aSch );

private:
    /**
     * Check if any items in the list conflict with agent's working set.
     */
    void checkForConflicts( std::vector<SCH_ITEM*>& aItems, const wxString& aChangeType );

private:
    AGENT_WORKSPACE*        m_workspace;
    std::set<KIID>          m_agentWorkingSet;
    bool                    m_enabled;
    USER_EDIT_CALLBACK      m_userEditCallback;
};


/**
 * BOARD_CONFLICT_DETECTOR listens to board changes and detects when
 * user edits conflict with agent's working set.
 *
 * This class implements BOARD_LISTENER (defined in board.h).
 */
class BOARD_CONFLICT_DETECTOR
{
public:
    BOARD_CONFLICT_DETECTOR();
    ~BOARD_CONFLICT_DETECTOR();

    /**
     * Set the agent workspace to monitor for conflicts.
     */
    void SetWorkspace( AGENT_WORKSPACE* aWorkspace );

    /**
     * Set the agent's working set of item IDs.
     */
    void SetAgentWorkingSet( const std::set<KIID>& aWorkingSet );

    /**
     * Add an item to the agent's working set.
     */
    void AddToWorkingSet( const KIID& aItemId );

    /**
     * Remove an item from the agent's working set.
     */
    void RemoveFromWorkingSet( const KIID& aItemId );

    /**
     * Clear the agent's working set.
     */
    void ClearWorkingSet();

    /**
     * Check if an item is in the agent's working set.
     */
    bool IsInWorkingSet( const KIID& aItemId ) const;

    /**
     * Enable or disable conflict detection.
     */
    void SetEnabled( bool aEnabled ) { m_enabled = aEnabled; }

    /**
     * Check if conflict detection is enabled.
     */
    bool IsEnabled() const { return m_enabled; }

    /**
     * Set callback for user edit notifications.
     */
    void SetUserEditCallback( USER_EDIT_CALLBACK aCallback ) { m_userEditCallback = aCallback; }

    // BOARD_LISTENER interface implementation
    void OnBoardItemAdded( BOARD& aBoard, BOARD_ITEM* aBoardItem );
    void OnBoardItemsAdded( BOARD& aBoard, std::vector<BOARD_ITEM*>& aBoardItems );
    void OnBoardItemRemoved( BOARD& aBoard, BOARD_ITEM* aBoardItem );
    void OnBoardItemsRemoved( BOARD& aBoard, std::vector<BOARD_ITEM*>& aBoardItems );
    void OnBoardItemChanged( BOARD& aBoard, BOARD_ITEM* aBoardItem );
    void OnBoardItemsChanged( BOARD& aBoard, std::vector<BOARD_ITEM*>& aBoardItems );
    void OnBoardNetSettingsChanged( BOARD& aBoard );
    void OnBoardHighlightNetChanged( BOARD& aBoard );
    void OnBoardRatsnestChanged( BOARD& aBoard );
    void OnBoardCompositeUpdate( BOARD& aBoard, std::vector<BOARD_ITEM*>& aAddedItems,
                                  std::vector<BOARD_ITEM*>& aRemovedItems,
                                  std::vector<BOARD_ITEM*>& aChangedItems );

private:
    /**
     * Check if any items in the list conflict with agent's working set.
     */
    void checkForConflicts( std::vector<BOARD_ITEM*>& aItems, const wxString& aChangeType );

    /**
     * Check a single item for conflicts.
     */
    void checkForConflict( BOARD_ITEM* aItem, const wxString& aChangeType );

private:
    AGENT_WORKSPACE*        m_workspace;
    std::set<KIID>          m_agentWorkingSet;
    bool                    m_enabled;
    USER_EDIT_CALLBACK      m_userEditCallback;
};
