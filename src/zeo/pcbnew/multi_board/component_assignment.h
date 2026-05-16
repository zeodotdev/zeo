/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef COMPONENT_ASSIGNMENT_H
#define COMPONENT_ASSIGNMENT_H

#include <kiid.h>
#include <wx/string.h>

#include <map>
#include <set>
#include <vector>

class NETLIST;
class PROJECT_FILE;


/**
 * Manages component-to-board assignments for multi-board projects.
 *
 * In a multi-board project, each schematic component can be assigned to one or more
 * physical boards. Components assigned to multiple boards are typically connectors
 * that bridge boards (e.g., board-to-board headers).
 */
class COMPONENT_ASSIGNMENT_MANAGER
{
public:
    COMPONENT_ASSIGNMENT_MANAGER();
    ~COMPONENT_ASSIGNMENT_MANAGER() = default;

    /**
     * Clear all component assignments.
     */
    void Clear();

    /**
     * Assign a component to a specific board.
     * If the component is already assigned to other boards, this adds an additional
     * assignment (making it a multi-board component).
     *
     * @param aReference Component reference designator (e.g., "U1", "R5")
     * @param aBoardUuid UUID of the target board
     */
    void AssignToBoard( const wxString& aReference, const KIID& aBoardUuid );

    /**
     * Remove a component's assignment to a specific board.
     *
     * @param aReference Component reference designator
     * @param aBoardUuid UUID of the board to unassign from
     */
    void UnassignFromBoard( const wxString& aReference, const KIID& aBoardUuid );

    /**
     * Assign a component to multiple boards at once.
     * Replaces any existing assignments for this component.
     *
     * @param aReference Component reference designator
     * @param aBoardUuids Vector of board UUIDs to assign to
     */
    void AssignToMultipleBoards( const wxString& aReference,
                                  const std::vector<KIID>& aBoardUuids );

    /**
     * Get all board UUIDs that a component is assigned to.
     *
     * @param aReference Component reference designator
     * @return Vector of board UUIDs (empty if component is not assigned)
     */
    std::vector<KIID> GetBoardsForComponent( const wxString& aReference ) const;

    /**
     * Check if a component is assigned to multiple boards.
     *
     * @param aReference Component reference designator
     * @return true if assigned to more than one board
     */
    bool IsMultiBoardComponent( const wxString& aReference ) const;

    /**
     * Check if a component is assigned to a specific board.
     *
     * @param aReference Component reference designator
     * @param aBoardUuid UUID of the board to check
     * @return true if component is assigned to this board
     */
    bool IsAssignedToBoard( const wxString& aReference, const KIID& aBoardUuid ) const;

    /**
     * Check if a component has any board assignment.
     *
     * @param aReference Component reference designator
     * @return true if component has at least one board assignment
     */
    bool HasAssignment( const wxString& aReference ) const;

    /**
     * Get all components assigned to a specific board.
     *
     * @param aBoardUuid UUID of the board
     * @return Set of component reference designators
     */
    std::set<wxString> GetComponentsForBoard( const KIID& aBoardUuid ) const;

    /**
     * Get all components that span multiple boards (connectors).
     *
     * @return Set of component reference designators
     */
    std::set<wxString> GetMultiBoardComponents() const;

    /**
     * Get all unassigned component references from a netlist.
     *
     * @param aNetlist The netlist to check against
     * @return Set of unassigned component reference designators
     */
    std::set<wxString> GetUnassignedComponents( const NETLIST& aNetlist ) const;

    /**
     * Auto-assign components to boards based on net connectivity analysis.
     *
     * Components connected to nets that only exist on one board get assigned
     * to that board. Components connected to nets spanning multiple boards
     * (via existing cross-board connections) get assigned to all relevant boards.
     *
     * @param aNetlist The netlist to analyze
     * @param aExistingBoardUuids UUIDs of boards to consider
     */
    void AutoAssignByNetConnectivity( const NETLIST& aNetlist,
                                       const std::vector<KIID>& aExistingBoardUuids );

    /**
     * Set the default board for unassigned components.
     *
     * @param aBoardUuid UUID of the default board
     */
    void SetDefaultBoard( const KIID& aBoardUuid ) { m_defaultBoardUuid = aBoardUuid; }

    /**
     * Get the default board UUID.
     */
    const KIID& GetDefaultBoard() const { return m_defaultBoardUuid; }

    /**
     * Save component assignments to the project file.
     *
     * @param aProjectFile The project file to save to
     */
    void SaveToProject( PROJECT_FILE* aProjectFile );

    /**
     * Load component assignments from the project file.
     *
     * @param aProjectFile The project file to load from
     */
    void LoadFromProject( const PROJECT_FILE* aProjectFile );

    /**
     * Get the internal assignment map (for serialization).
     */
    const std::map<wxString, std::vector<KIID>>& GetAssignments() const
    {
        return m_componentAssignments;
    }

private:
    /// Map of component reference → list of board UUIDs
    std::map<wxString, std::vector<KIID>> m_componentAssignments;

    /// Default board for unassigned components
    KIID m_defaultBoardUuid;
};

#endif // COMPONENT_ASSIGNMENT_H
