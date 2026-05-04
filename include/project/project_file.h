/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * @author Jon Evans <jon@craftyjon.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KICAD_PROJECT_FILE_H
#define KICAD_PROJECT_FILE_H

#include <common.h> // needed for wxstring hash template
#include <kiid.h>
#include <math/vector3.h>
#include <project/board_project_settings.h>
#include <project/project_file_observer.h>   // T3: MULTI_BOARD_FIELD enum + observer interface
#include <settings/json_settings.h>
#include <settings/nested_settings.h>

#include <memory>
#include <set>

class BOARD_DESIGN_SETTINGS;
class ERC_SETTINGS;
class NET_SETTINGS;
class COMPONENT_CLASS_SETTINGS;
class TUNING_PROFILES;
class LAYER_PAIR_SETTINGS;
class SCHEMATIC_SETTINGS;
class TEMPLATES;

/**
 * For files like sheets and boards, a pair of that object KIID and display name
 * Display name is typically blank for the project root sheet
 */
typedef std::pair<KIID, wxString> FILE_INFO_PAIR;

/**
 * Information about a top-level schematic sheet
 */
struct TOP_LEVEL_SHEET_INFO
{
    KIID     uuid;          ///< Unique identifier for the sheet
    wxString name;          ///< Display name for the sheet
    wxString filename;      ///< Relative path to the sheet file

    TOP_LEVEL_SHEET_INFO() = default;

    TOP_LEVEL_SHEET_INFO( const KIID& aUuid, const wxString& aName, const wxString& aFilename )
        : uuid( aUuid ), name( aName ), filename( aFilename )
    {}

    bool operator==( const TOP_LEVEL_SHEET_INFO& aOther ) const
    {
        return uuid == aOther.uuid && name == aOther.name && filename == aOther.filename;
    }

    bool operator!=( const TOP_LEVEL_SHEET_INFO& aOther ) const
    {
        return !( *this == aOther );
    }
};

/**
 * Information about a board in a multi-board project.
 * Each board has its own design settings and can be active or inactive.
 */
struct KICOMMON_API BOARD_INFO
{
    KIID     uuid;              ///< Unique identifier for this board within the project
    wxString filename;          ///< Relative path to the .kicad_pcb file
    wxString displayName;       ///< User-friendly name for the board
    bool     isActive = false;  ///< True if this is the currently active board

    BOARD_INFO() = default;

    BOARD_INFO( const KIID& aUuid, const wxString& aFilename,
                const wxString& aDisplayName = wxEmptyString, bool aActive = false )
        : uuid( aUuid ), filename( aFilename ), displayName( aDisplayName ), isActive( aActive )
    {}

    bool operator==( const BOARD_INFO& aOther ) const
    {
        return uuid == aOther.uuid;
    }

    bool operator!=( const BOARD_INFO& aOther ) const
    {
        return !( *this == aOther );
    }
};

/**
 * Represents a connection between pads on two different boards.
 * Used for cross-board connectors (e.g., board-to-board headers, flex cables).
 */
struct KICOMMON_API CROSS_BOARD_CONNECTION
{
    KIID board1Uuid;    ///< UUID of the first board
    KIID pad1Uuid;      ///< UUID of the pad on board 1
    KIID board2Uuid;    ///< UUID of the second board
    KIID pad2Uuid;      ///< UUID of the pad on board 2

    CROSS_BOARD_CONNECTION() = default;

    CROSS_BOARD_CONNECTION( const KIID& aBoard1, const KIID& aPad1,
                            const KIID& aBoard2, const KIID& aPad2 )
        : board1Uuid( aBoard1 ), pad1Uuid( aPad1 ),
          board2Uuid( aBoard2 ), pad2Uuid( aPad2 )
    {}

    bool operator==( const CROSS_BOARD_CONNECTION& aOther ) const
    {
        return board1Uuid == aOther.board1Uuid && pad1Uuid == aOther.pad1Uuid &&
               board2Uuid == aOther.board2Uuid && pad2Uuid == aOther.pad2Uuid;
    }
};

/**
 * Assignment of a component reference to one or more boards.
 * Components can exist on multiple boards (e.g., connectors that bridge boards).
 */
struct KICOMMON_API COMPONENT_BOARD_ASSIGNMENT
{
    wxString           reference;   ///< Component reference designator (e.g., "U1", "J5")
    std::vector<KIID>  boardUuids;  ///< List of board UUIDs this component is assigned to

    COMPONENT_BOARD_ASSIGNMENT() = default;

    COMPONENT_BOARD_ASSIGNMENT( const wxString& aRef, const std::vector<KIID>& aBoards )
        : reference( aRef ), boardUuids( aBoards )
    {}

    COMPONENT_BOARD_ASSIGNMENT( const wxString& aRef, const KIID& aSingleBoard )
        : reference( aRef ), boardUuids( { aSingleBoard } )
    {}

    bool IsMultiBoard() const { return boardUuids.size() > 1; }

    bool IsAssignedTo( const KIID& aBoardUuid ) const
    {
        return std::find( boardUuids.begin(), boardUuids.end(), aBoardUuid ) != boardUuids.end();
    }

    bool operator==( const COMPONENT_BOARD_ASSIGNMENT& aOther ) const
    {
        return reference == aOther.reference && boardUuids == aOther.boardUuids;
    }
};

/**
 * Describes a sub-project that is a member of an Altium-style multi-board
 * project. Each sub-project is a standalone `.kicad_pro` (with its own
 * `.kicad_sch` and `.kicad_pcb`) referenced by relative path from the
 * multi-board container project.
 */
struct KICOMMON_API SUB_PROJECT_INFO
{
    KIID     uuid;           ///< Stable identifier within the container project
    wxString name;           ///< Short name / directory basename, e.g. "fc"
    wxString relativePath;   ///< Path to `.kicad_pro` relative to container dir
    wxString displayName;    ///< Human-friendly name (falls back to name)
    wxString role;           ///< Reserved; defaults to "standard"

    SUB_PROJECT_INFO() : role( wxT( "standard" ) ) {}

    bool operator==( const SUB_PROJECT_INFO& aOther ) const
    {
        return uuid == aOther.uuid && name == aOther.name
               && relativePath == aOther.relativePath
               && displayName == aOther.displayName && role == aOther.role;
    }
};

/**
 * One endpoint of a cross-board net on the multi-board schematic: identifies
 * a specific connector pad on a specific sub-project.
 */
struct KICOMMON_API MB_CROSS_BOARD_NET_ENDPOINT
{
    KIID     subProjectUuid;  ///< Which sub-project
    wxString componentRef;    ///< Connector reference, e.g. "J1"
    wxString pinNumber;       ///< Pad number on the connector
    wxString pinName;         ///< Pad display name / net name

    bool operator==( const MB_CROSS_BOARD_NET_ENDPOINT& aOther ) const
    {
        return subProjectUuid == aOther.subProjectUuid
               && componentRef == aOther.componentRef
               && pinNumber == aOther.pinNumber && pinName == aOther.pinName;
    }
};

/**
 * A net that physically connects pads on two or more sub-project boards.
 * Derived from MBS topology on save; consumed by cross-board PCB sync and
 * cross-board ERC.
 */
struct KICOMMON_API MB_CROSS_BOARD_NET
{
    KIID     uuid;                                    ///< Stable identifier
    wxString name;                                    ///< Net name (label or autogenerated)
    std::vector<MB_CROSS_BOARD_NET_ENDPOINT> endpoints;

    bool operator==( const MB_CROSS_BOARD_NET& aOther ) const
    {
        return uuid == aOther.uuid && name == aOther.name && endpoints == aOther.endpoints;
    }
};

/**
 * Role a CUSTOM_MATE plays in the M6.D mate solver.
 *
 * Custom mates layer on top of the auto-derived (from `MB_CROSS_BOARD_NET`)
 * mates produced by the 3D viewer's `BuildMateGraph` pass:
 *
 *   - PRIMARY  : forces this pair as the primary mate on its board edge,
 *                overriding the auto pin-count heuristic;
 *   - SECONDARY: alignment-check only — does not place, just contributes
 *                a residual to the DRC report;
 *   - DISABLED : drops a matching auto-derived mate so it is not used at
 *                all (handy when auto picks the wrong primary).
 */
enum class CUSTOM_MATE_ROLE
{
    PRIMARY,
    SECONDARY,
    DISABLED
};


/**
 * Class of physical thing being mated. Drives placement heuristics:
 * CONNECTOR uses pad-centroid + dominant-side flip; non-CONNECTOR types
 * skip electrical-side detection and rely on the optional offset.
 */
enum class CUSTOM_MATE_TYPE
{
    CONNECTOR,      ///< Default; references electrical connector pads.
    MOUNTING_HOLE,  ///< No electrical net — through-hole / standoff mate.
    ALIGNMENT       ///< Generic mechanical alignment (fiducial, corner, post).
};


/**
 * One end of a CUSTOM_MATE: identifies a footprint on a sub-project.
 * Footprints are addressed by reference because their KIID can change
 * across sub-board re-saves; reference is what the user sees and edits.
 */
struct KICOMMON_API CUSTOM_MATE_END
{
    KIID     subProjectUuid;  ///< Which sub-project the footprint lives on
    wxString footprintRef;    ///< Footprint reference (e.g. "J1", "MH1")

    bool operator==( const CUSTOM_MATE_END& aOther ) const
    {
        return subProjectUuid == aOther.subProjectUuid && footprintRef == aOther.footprintRef;
    }
};


/**
 * User-declared mate that overrides or augments the auto-derived mate
 * graph (M6.D-phase-2). Lives in container `.kicad_pro` under
 * `multi_board.assembly_3d.mates[]` — survives sub-project re-edits and
 * is project-scoped, not schematic-scoped (mating is an assembly fact,
 * not an electrical fact).
 */
struct KICOMMON_API CUSTOM_MATE
{
    KIID                uuid;
    CUSTOM_MATE_ROLE    role;
    CUSTOM_MATE_TYPE    type;
    CUSTOM_MATE_END     endA;
    CUSTOM_MATE_END     endB;

    /// True when an offset override is present. Without it the solver
    /// applies the auto-computed pose; with it, the solver applies
    /// (auto-pose) ∘ (offset translation, then offset rotation).
    bool                hasOffset;
    VECTOR3D            offsetTranslation;   ///< mm (X, Y, Z)
    VECTOR3D            offsetRotation;      ///< degrees (X, Y, Z)

    CUSTOM_MATE() :
            role( CUSTOM_MATE_ROLE::PRIMARY ),
            type( CUSTOM_MATE_TYPE::CONNECTOR ),
            hasOffset( false ),
            offsetTranslation( 0.0, 0.0, 0.0 ),
            offsetRotation( 0.0, 0.0, 0.0 )
    {}

    bool operator==( const CUSTOM_MATE& aOther ) const
    {
        return uuid == aOther.uuid && role == aOther.role && type == aOther.type
               && endA == aOther.endA && endB == aOther.endB
               && hasOffset == aOther.hasOffset
               && offsetTranslation == aOther.offsetTranslation
               && offsetRotation == aOther.offsetRotation;
    }
};


/**
 * Persisted per-instance state for the M6 3D assembly viewer
 * (M6.G). Lives on the container `.kicad_pro` under
 * `multi_board.assembly_3d.instances[]`, keyed by sub-project UUID
 * so renames of relative paths or display names don't lose layout.
 *
 * Scope: pose (translation + rotation), visibility, and transparency.
 * Custom mate offsets stay on `CUSTOM_MATE.offsetTranslation/Rotation`.
 */
struct KICOMMON_API ASSEMBLY_INSTANCE_STATE
{
    KIID     subProjectUuid;                      ///< Keys this state to a sub-project
    VECTOR3D position;                            ///< mm (X, Y, Z)
    VECTOR3D rotation;                            ///< degrees (X, Y, Z)
    bool     visible;                             ///< Render this instance
    bool     transparent;                         ///< Render with transparency
    double   opacity;                             ///< 0..1; ignored when !transparent

    ASSEMBLY_INSTANCE_STATE() :
            position( 0.0, 0.0, 0.0 ),
            rotation( 0.0, 0.0, 0.0 ),
            visible( true ),
            transparent( false ),
            opacity( 1.0 )
    {}

    bool operator==( const ASSEMBLY_INSTANCE_STATE& aOther ) const
    {
        return subProjectUuid == aOther.subProjectUuid && position == aOther.position
               && rotation == aOther.rotation && visible == aOther.visible
               && transparent == aOther.transparent && opacity == aOther.opacity;
    }
};


/**
 * For storing PcbNew MRU paths of various types
 */
enum LAST_PATH_TYPE : unsigned int
{
    LAST_PATH_FIRST = 0,
    LAST_PATH_NETLIST = LAST_PATH_FIRST,
    LAST_PATH_IDF,
    LAST_PATH_VRML,
    LAST_PATH_SPECCTRADSN,
    LAST_PATH_PLOT,

    LAST_PATH_SIZE
};

/**
 * The backing store for a PROJECT, in JSON format.
 *
 * There is either zero or one PROJECT_FILE for every PROJECT
 * (you can have a dummy PROJECT that has no file)
 */
class KICOMMON_API PROJECT_FILE : public JSON_SETTINGS
{
public:
    /**
     * Construct the project file for a project
     * @param aFullPath is the full disk path to the project
     */
    PROJECT_FILE( const wxString& aFullPath );

    /// Out-of-line so the unique_ptr<PROJECT_FILE_WATCHER> destructor
    /// can run against the complete watcher type, which is only
    /// available in `project_file.cpp`. Otherwise the implicit-default
    /// destructor here would require the watcher's declaration in
    /// every translation unit including this header.
    virtual ~PROJECT_FILE();

    virtual bool MigrateFromLegacy( wxConfigBase* aCfg ) override;

    bool LoadFromFile( const wxString& aDirectory = "" ) override;

    bool SaveToFile( const wxString& aDirectory = "", bool aForce = false ) override;

    bool SaveAs( const wxString& aDirectory, const wxString& aFile );

    void SetProject( PROJECT* aProject )
    {
        m_project = aProject;
    }

    /// Back-pointer to the owning PROJECT (set by SETTINGS_MANAGER::loadProjectFile).
    /// Returns nullptr for free-standing PROJECT_FILEs that aren't registered with
    /// SETTINGS_MANAGER (e.g. the temporary instances used by the MBSCH save hook
    /// or the cross-board net extractor). Callers that need the absolute path of
    /// the .kicad_pro on disk should prefer this over GetFullFilename(): for
    /// SETTINGS_MANAGER-owned PROJECT_FILEs, m_filename is the basename only
    /// (set in settings_manager.cpp::loadProjectFile), so GetFullFilename()
    /// returns "name.kicad_pro" without a directory.
    PROJECT* GetProject() const { return m_project; }

    std::vector<FILE_INFO_PAIR>& GetSheets()
    {
        return m_sheets;
    }

    std::vector<FILE_INFO_PAIR>& GetBoards()
    {
        return m_boards;
    }

    /**
     * Get the list of board information for multi-board projects.
     * @return Reference to the vector of BOARD_INFO structures
     */
    std::vector<BOARD_INFO>& GetBoardInfos()
    {
        return m_boardInfos;
    }

    const std::vector<BOARD_INFO>& GetBoardInfos() const
    {
        return m_boardInfos;
    }

    /**
     * Get board info by UUID.
     * @param aUuid The UUID of the board to find
     * @return Pointer to BOARD_INFO if found, nullptr otherwise
     */
    BOARD_INFO* GetBoardInfo( const KIID& aUuid );

    const BOARD_INFO* GetBoardInfo( const KIID& aUuid ) const;

    /**
     * Get the currently active board info.
     * @return Pointer to the active BOARD_INFO, or nullptr if none active
     */
    BOARD_INFO* GetActiveBoardInfo();

    /**
     * Set the active board by UUID.
     * @param aUuid The UUID of the board to make active
     * @return true if the board was found and set active
     */
    bool SetActiveBoard( const KIID& aUuid );

    /**
     * Add a new board to the project.
     * @param aInfo The board information to add
     */
    void AddBoard( const BOARD_INFO& aInfo );

    /**
     * Remove a board from the project by UUID.
     * @param aUuid The UUID of the board to remove
     * @return true if the board was found and removed
     */
    bool RemoveBoard( const KIID& aUuid );

    /**
     * Get cross-board connections (connector mappings).
     * @return Reference to the vector of cross-board connections
     */
    std::vector<CROSS_BOARD_CONNECTION>& GetCrossBoardConnections()
    {
        return m_crossBoardConnections;
    }

    const std::vector<CROSS_BOARD_CONNECTION>& GetCrossBoardConnections() const
    {
        return m_crossBoardConnections;
    }

    /**
     * Add a cross-board connection between two pads.
     */
    void AddCrossBoardConnection( const CROSS_BOARD_CONNECTION& aConnection );

    /**
     * Remove a cross-board connection.
     */
    bool RemoveCrossBoardConnection( const KIID& aBoard1, const KIID& aPad1,
                                      const KIID& aBoard2, const KIID& aPad2 );

    /**
     * Get component-to-board assignments.
     * @return Reference to the vector of component assignments
     */
    std::vector<COMPONENT_BOARD_ASSIGNMENT>& GetComponentAssignments()
    {
        return m_componentAssignments;
    }

    const std::vector<COMPONENT_BOARD_ASSIGNMENT>& GetComponentAssignments() const
    {
        return m_componentAssignments;
    }

    /**
     * Get the board assignment for a specific component reference.
     * @param aReference The component reference designator
     * @return Pointer to assignment if found, nullptr otherwise
     */
    COMPONENT_BOARD_ASSIGNMENT* GetComponentAssignment( const wxString& aReference );

    /**
     * Assign a component to a board.
     * @param aReference The component reference designator
     * @param aBoardUuid The board UUID to assign to
     * @param aReplace If true, replace existing assignment; if false, add to existing
     */
    void AssignComponentToBoard( const wxString& aReference, const KIID& aBoardUuid,
                                  bool aReplace = true );

    /**
     * Unassign a component from a specific board.
     * @param aReference The component reference designator
     * @param aBoardUuid The board UUID to unassign from
     */
    void UnassignComponentFromBoard( const wxString& aReference, const KIID& aBoardUuid );

    /**
     * Check if this is a multi-board project (has more than one board).
     */
    bool IsMultiBoardProject() const { return m_boardInfos.size() > 1; }

    // ------------------------------------------------------------------
    // Multi-board CONTAINER project (Altium-style peer MBS)
    //
    // A container project references N independent sub-projects (each a
    // standalone `.kicad_pro`) joined by a multi-board schematic (MBS).
    // Distinct from the single-project multi-PCB model backed by the
    // `m_boardInfos` / `m_crossBoardConnections` fields above.
    // ------------------------------------------------------------------

    /// True when this `.kicad_pro` is the top-level of a multi-board project
    /// (i.e. its `multi_board.sub_projects[]` describes children).
    bool IsMultiBoardContainer() const { return m_isMultiBoardContainer; }

    void SetMultiBoardContainer( bool aIsContainer )
    {
        m_isMultiBoardContainer = aIsContainer;
    }

    /// Sub-project back-reference: relative path from this `.kicad_pro` to
    /// the enclosing container `.kicad_pro`. Empty when this project is
    /// standalone or is itself the container. Set by AddSubProject when
    /// registering a sub-project; consumed by container-resolution helpers
    /// to skip the legacy 6-level directory walk.
    const wxString& GetContainerProjectRelativePath() const
    {
        return m_containerProjectRelativePath;
    }

    void SetContainerProjectRelativePath( const wxString& aPath );

    std::vector<SUB_PROJECT_INFO>& GetSubProjects() { return m_subProjects; }

    const std::vector<SUB_PROJECT_INFO>& GetSubProjects() const { return m_subProjects; }

    SUB_PROJECT_INFO*       GetSubProject( const KIID& aUuid );
    const SUB_PROJECT_INFO* GetSubProject( const KIID& aUuid ) const;

    void AddSubProject( const SUB_PROJECT_INFO& aInfo );
    bool RemoveSubProject( const KIID& aUuid );

    std::vector<MB_CROSS_BOARD_NET>& GetCrossBoardNets() { return m_crossBoardNets; }

    const std::vector<MB_CROSS_BOARD_NET>& GetCrossBoardNets() const
    {
        return m_crossBoardNets;
    }

    void SetCrossBoardNets( std::vector<MB_CROSS_BOARD_NET> aNets );

    /// Container-level rule: minimum number of pins each named power
    /// net must have on each sub-project (e.g. `{"GND": 4, "VCC": 2}`).
    /// Empty by default. Consumed by the per-board cross-board power
    /// DRC test provider.
    std::map<wxString, int>& GetMinPowerPins() { return m_minPowerPins; }

    const std::map<wxString, int>& GetMinPowerPins() const { return m_minPowerPins; }

    /// Container-level rule: maximum total trace length (in nanometers)
    /// for each named cross-board net, summed across all sub-projects.
    /// Used by per-board cross-board length DRC: when a cross-board
    /// net has an entry here, the test provider sums per-board lengths
    /// and flags overruns with the total.
    std::map<wxString, int64_t>& GetMaxLengthNm() { return m_maxLengthNm; }

    const std::map<wxString, int64_t>& GetMaxLengthNm() const { return m_maxLengthNm; }

    /// Container-level rule: cross-board diff-pair declarations. Each
    /// pair `(netA, netB)` says "these two cross-board nets must be a
    /// diff pair on every sub-project they touch." Order within the
    /// pair is irrelevant for matching.
    std::vector<std::pair<wxString, wxString>>& GetCrossBoardDiffPairs()
    {
        return m_crossBoardDiffPairs;
    }

    const std::vector<std::pair<wxString, wxString>>& GetCrossBoardDiffPairs() const
    {
        return m_crossBoardDiffPairs;
    }

    /// Current-capacity rule per cross-board net.
    struct MB_CURRENT_RULE
    {
        double expectedAmps   = 0.0;  ///< Expected DC current draw on this net
        double pinRatingAmps  = 0.0;  ///< Per-connector-pin current rating
    };

    std::map<wxString, MB_CURRENT_RULE>& GetCrossBoardCurrentRules() { return m_currentRules; }

    const std::map<wxString, MB_CURRENT_RULE>& GetCrossBoardCurrentRules() const
    {
        return m_currentRules;
    }

    /// Voltage-drop rule per cross-board net. Optional override fields
    /// fall back to documented defaults when zero.
    struct MB_VOLTAGE_RULE
    {
        double expectedAmps               = 0.0;  ///< Expected DC current
        double maxDropMv                  = 0.0;  ///< Max acceptable total drop
        double traceWidthUm               = 0.0;  ///< Default 250 if 0
        double traceSheetRMOhmsPerSq      = 0.0;  ///< Default 0.5 (1oz copper) if 0
        double contactRPerPinMOhms        = 0.0;  ///< Default 20 if 0
    };

    std::map<wxString, MB_VOLTAGE_RULE>& GetCrossBoardVoltageRules() { return m_voltageRules; }

    const std::map<wxString, MB_VOLTAGE_RULE>& GetCrossBoardVoltageRules() const
    {
        return m_voltageRules;
    }

    /**
     * Custom (user-declared) mate overrides for the 3D assembly
     * solver. See `CUSTOM_MATE` for the layered-on-top semantics.
     */
    std::vector<CUSTOM_MATE>& GetCustomMates() { return m_customMates; }

    const std::vector<CUSTOM_MATE>& GetCustomMates() const { return m_customMates; }

    void SetCustomMates( std::vector<CUSTOM_MATE> aMates );

    /**
     * Persisted per-instance assembly state (M6.G). One entry per
     * sub-project that has non-default pose / visibility / transparency.
     * Container projects only.
     */
    std::vector<ASSEMBLY_INSTANCE_STATE>& GetAssemblyInstances()
    {
        return m_assemblyInstances;
    }

    const std::vector<ASSEMBLY_INSTANCE_STATE>& GetAssemblyInstances() const
    {
        return m_assemblyInstances;
    }

    void SetAssemblyInstances( std::vector<ASSEMBLY_INSTANCE_STATE> aStates );

    /// Convenience accessor for the MBS schematic filename referenced by
    /// this container project. Empty when the MBS hasn't been created yet.
    const wxString& GetMbsFileName() const { return m_mbsFileName; }

    void SetMbsFileName( const wxString& aName );

    // ------------------------------------------------------------------
    // T3 — Observable mutators for `multi_board.*` state.
    //
    // Direct read access stays via the `Get…()` accessors above. To
    // mutate, use the setters below — they fan out the change to
    // registered `PROJECT_FILE_OBSERVER`s so every window holding this
    // PROJECT_FILE picks up the edit. The legacy mutable-ref `Get…()`
    // overloads above remain temporarily so existing call sites build
    // until the Phase 5 sweep migrates them. New code MUST use the
    // setters; mutating through the legacy refs bypasses notification.
    // ------------------------------------------------------------------

    void RegisterObserver( PROJECT_FILE_OBSERVER* aObserver );
    void UnregisterObserver( PROJECT_FILE_OBSERVER* aObserver );

    // Sub-projects (vector). AddSubProject / RemoveSubProject defined
    // earlier already maintain `m_subProjects`; T3 makes them notify.

    // Cross-board nets. SetCrossBoardNets defined earlier; T3 makes it
    // notify. Direct mutation of the vector via the non-const Get…()
    // does not notify and will be removed in Phase 5.

    // Custom mates. SetCustomMates defined earlier; T3 makes it notify.

    // Assembly instances. SetAssemblyInstances defined earlier; T3
    // makes it notify.

    // MBS file name. SetMbsFileName defined earlier; T3 makes it notify.

    // Min power pins.
    void SetMinPowerPin( const wxString& aNet, int aMinPins );
    void RemoveMinPowerPin( const wxString& aNet );
    void ClearMinPowerPins();

    // Max length per cross-board net (nm).
    void SetMaxLengthNm( const wxString& aNet, int64_t aMaxNm );
    void RemoveMaxLengthNm( const wxString& aNet );
    void ClearMaxLengthNm();

    // Cross-board diff pairs. Order within (a,b) is irrelevant to
    // matching, but the stored pair preserves whatever the caller
    // provided so authoring round-trips cleanly.
    void AddCrossBoardDiffPair( const wxString& aNetA, const wxString& aNetB );
    void RemoveCrossBoardDiffPair( const wxString& aNetA, const wxString& aNetB );
    void ClearCrossBoardDiffPairs();

    // Per-net current rule.
    void SetCrossBoardCurrentRule( const wxString& aNet, const MB_CURRENT_RULE& aRule );
    void RemoveCrossBoardCurrentRule( const wxString& aNet );
    void ClearCrossBoardCurrentRules();

    // Per-net voltage-drop rule.
    void SetCrossBoardVoltageRule( const wxString& aNet, const MB_VOLTAGE_RULE& aRule );
    void RemoveCrossBoardVoltageRule( const wxString& aNet );
    void ClearCrossBoardVoltageRules();

    /// Internal: dispatch a change notification. Public so internal
    /// helpers (file watcher in Phase 4, save guard in Phase 3) can
    /// fan out without going through a setter. Honours the suspend
    /// guard — if any `PROJECT_FILE_SUSPEND_NOTIFY` is active, the
    /// notification is queued and emitted on the guard's destruction.
    void NotifyMultiBoardChanged( MULTI_BOARD_FIELD aField );

    /// T3 Phase 4: start watching `aAbsPath` for external modifications.
    /// Idempotent — subsequent calls re-target the watcher. Pass empty
    /// string to disable.
    ///
    /// On a successful watcher event (after the self-write filter), the
    /// PROJECT_FILE blanket-reloads from disk and fires
    /// `MULTI_BOARD_FIELD::EXTERNAL_RELOAD` if the in-memory state was
    /// clean, or `EXTERNAL_RELOAD_DIRTY` if there were unsaved edits
    /// (no auto-overwrite — Phase 6's UX banner asks the user).
    ///
    /// Safe to call when `wxUSE_FSWATCHER` is unavailable; degrades to
    /// a no-op with a trace log.
    bool EnableFileWatcher( const wxString& aAbsPath );

    /// Stop watching, if active. Idempotent.
    void DisableFileWatcher();

    /// Internal: callback invoked by `PROJECT_FILE_WATCHER` when an
    /// external modification has been detected and confirmed not to be
    /// our own write. Public so the watcher class can call into us
    /// without friending — it's invoked only by the watcher.
    void OnExternalFileChange();

    /// T3 Phase 6: force a blanket reload of the in-memory state from
    /// disk, regardless of pending unsaved edits. Called by the
    /// "Reload" action on a conflict UX banner — the user has
    /// explicitly chosen to discard their in-memory changes in favour
    /// of the on-disk state.
    ///
    /// Fires `MULTI_BOARD_FIELD::EXTERNAL_RELOAD` after the reload so
    /// any other observer (e.g. peer windows on the same project)
    /// re-renders. Returns true on a successful reload, false if the
    /// file can't be parsed (in which case the in-memory state is
    /// left untouched and dirty).
    bool ReloadFromDiskDiscardingChanges();

    /// Resolve a sub-project's relativePath to an absolute `.kicad_pro`
    /// path, relative to this container project's directory.
    wxFileName ResolveSubProjectPath( const SUB_PROJECT_INFO& aInfo ) const;

    /// Resolve the multi-board schematic file to an absolute path.
    /// Returns an invalid wxFileName when m_mbsFileName is empty.
    wxFileName ResolveMbsPath() const;

    std::vector<TOP_LEVEL_SHEET_INFO>& GetTopLevelSheets()
    {
        return m_topLevelSheets;
    }

    const std::vector<TOP_LEVEL_SHEET_INFO>& GetTopLevelSheets() const
    {
        return m_topLevelSheets;
    }

    /// Returns this project's own NET_SETTINGS. M7.2 cross-board rule
    /// consistency uses physical replication (container → sub-projects
    /// on save) rather than a runtime overlay — see
    /// `MultiBoardPropagateNetSettings()` — so every project always
    /// returns its own copy.
    std::shared_ptr<NET_SETTINGS>& NetSettings() { return m_NetSettings; }

    std::shared_ptr<COMPONENT_CLASS_SETTINGS>& ComponentClassSettings()
    {
        return m_ComponentClassSettings;
    }

    std::shared_ptr<TUNING_PROFILES>& TuningProfileParameters() { return m_tuningProfileParameters; }

    /**
     * @return true if it should be safe to auto-save this file without user action
     */
    bool ShouldAutoSave() const { return !m_wasMigrated && !m_isFutureFormat; }

protected:
    wxString getFileExt() const override;

    wxString getLegacyFileExt() const override;

    /**
     * Below are project-level settings that have not been moved to a dedicated file
     */
public:
    /**
     * Shared params, used by more than one application
     */

    /// The list of pinned symbol libraries
    std::vector<wxString> m_PinnedSymbolLibs;

    /// The list of pinned footprint libraries
    std::vector<wxString> m_PinnedFootprintLibs;

    /// The list of pinned design block libraries
    std::vector<wxString> m_PinnedDesignBlockLibs;

    std::map<wxString, wxString> m_TextVars;

    /**
     * Eeschema params
     */

    // Schematic ERC settings: lifecycle managed by SCHEMATIC
    ERC_SETTINGS* m_ErcSettings;

    // Schematic editing and misc settings: lifecycle managed by SCHEMATIC
    SCHEMATIC_SETTINGS* m_SchematicSettings;

    // Legacy parameters LibDir and LibName, for importing old projects
    wxString m_LegacyLibDir;

    wxArrayString m_LegacyLibNames;

    /// Bus alias definitions for the schematic project
    std::map<wxString, std::vector<wxString>> m_BusAliases;

    /**
     * CvPcb params
     */

    /// List of equivalence (equ) files used in the project
    std::vector<wxString> m_EquivalenceFiles;

    /**
     * PcbNew params
     */

    /// Drawing sheet file
    wxString m_BoardDrawingSheetFile;

    /// MRU path storage
    wxString m_PcbLastPath[LAST_PATH_SIZE];

    /**
     * Board design settings for this project's board (legacy single-board mode).
     * This will be initialized by PcbNew after loading a board so that
     * BOARD_DESIGN_SETTINGS doesn't need to live in common for now.
     * Owned by the BOARD; may be null if a board isn't loaded: be careful.
     *
     * For multi-board projects, use m_MultiBoardSettings instead.
     */
    BOARD_DESIGN_SETTINGS* m_BoardSettings;

    /**
     * Board design settings for multi-board projects, keyed by board UUID.
     * Each board in a multi-board project has its own design settings stored here.
     * The BOARD_DESIGN_SETTINGS objects are owned by their respective BOARD objects.
     */
    std::map<KIID, BOARD_DESIGN_SETTINGS*> m_MultiBoardSettings;

    /**
     * Register a board's design settings for a multi-board project.
     * @param aBoardUuid The UUID of the board
     * @param aSettings Pointer to the board's design settings (owned by BOARD)
     */
    void RegisterBoardSettings( const KIID& aBoardUuid, BOARD_DESIGN_SETTINGS* aSettings );

    /**
     * Unregister a board's design settings.
     * @param aBoardUuid The UUID of the board to unregister
     */
    void UnregisterBoardSettings( const KIID& aBoardUuid );

    /**
     * Get the design settings for a specific board.
     * @param aBoardUuid The UUID of the board
     * @return Pointer to the board's design settings, or nullptr if not found
     */
    BOARD_DESIGN_SETTINGS* GetBoardSettings( const KIID& aBoardUuid ) const;

    /**
     * Net settings for this project (owned here)
     *
     * @note If we go multi-board in the future, we have to decide whether to use a global
     *       NET_SETTINGS or have one per board.  Right now I think global makes more sense
     *       (one set of schematics, one netlist partitioned into multiple boards)
     */
    std::shared_ptr<NET_SETTINGS> m_NetSettings;



    /**
     * Component class settings for the project (owned here)
     */
    std::shared_ptr<COMPONENT_CLASS_SETTINGS> m_ComponentClassSettings;

    /**
     * Tuning profile parameters for this project
     */
    std::shared_ptr<TUNING_PROFILES> m_tuningProfileParameters;

    std::vector<LAYER_PRESET>     m_LayerPresets;   /// List of stored layer presets
    std::vector<VIEWPORT>         m_Viewports;      /// List of stored viewports (pos + zoom)
    std::vector<VIEWPORT3D>       m_Viewports3D;    /// List of stored 3D viewports (view matrixes)
    std::vector<LAYER_PAIR_INFO>  m_LayerPairInfos; /// Layer pair list for the board

    struct IP2581_BOM             m_IP2581Bom;      /// IPC-2581 BOM settings

private:
    /**
     * Schema version 2: Bump for KiCad 9 layer numbering changes.
     *
     * Migrate layer presets to use new enum values for copper layers.
     */
    bool migrateSchema1To2();

    /**
     * Schema version 3: move layer presets to use named render layers.
     */
    bool migrateSchema2To3();

    /// An list of schematic sheets in this project
    std::vector<FILE_INFO_PAIR> m_sheets;

    /// A list of top-level schematic sheets in this project
    std::vector<TOP_LEVEL_SHEET_INFO> m_topLevelSheets;

    /// A list of board files in this project (legacy, for backwards compatibility)
    std::vector<FILE_INFO_PAIR> m_boards;

    /// Rich board information for multi-board projects
    std::vector<BOARD_INFO> m_boardInfos;

    /// Cross-board connections (connector pad mappings between boards)
    std::vector<CROSS_BOARD_CONNECTION> m_crossBoardConnections;

    /// Component-to-board assignments
    std::vector<COMPONENT_BOARD_ASSIGNMENT> m_componentAssignments;

    /// True iff this `.kicad_pro` is the top-level of a multi-board project.
    bool m_isMultiBoardContainer = false;

    /// For container projects: list of sub-projects joined by the MBS.
    std::vector<SUB_PROJECT_INFO> m_subProjects;

    /// For container projects: cross-board nets extracted from the MBS.
    std::vector<MB_CROSS_BOARD_NET> m_crossBoardNets;

    /// For container projects: user-declared mates that override or
    /// augment the auto-derived assembly mate graph (M6.D-phase-2).
    std::vector<CUSTOM_MATE> m_customMates;

    /// For container projects: persisted per-instance assembly state
    /// (pose + visibility + transparency). M6.G.
    std::vector<ASSEMBLY_INSTANCE_STATE> m_assemblyInstances;

    /// For container projects: filename of the multi-board schematic.
    wxString m_mbsFileName;

    /// For container projects: minimum pin count rule per power net
    /// name. Persisted as `multi_board.min_power_pins` (JSON object).
    std::map<wxString, int> m_minPowerPins;

    /// For container projects: max total trace length (nm) per cross-
    /// board net name, summed across all sub-projects. Persisted as
    /// `multi_board.max_length_nm` (JSON object).
    std::map<wxString, int64_t> m_maxLengthNm;

    /// For container projects: cross-board diff-pair declarations.
    /// Persisted as `multi_board.cross_board_diff_pairs`
    /// (JSON array of `{"a":"NET_DP", "b":"NET_DN"}` objects).
    std::vector<std::pair<wxString, wxString>> m_crossBoardDiffPairs;

    /// For container projects: per-net current rules. Persisted as
    /// `multi_board.current_rules`.
    std::map<wxString, MB_CURRENT_RULE> m_currentRules;

    /// For container projects: per-net voltage-drop rules. Persisted as
    /// `multi_board.voltage_rules`.
    std::map<wxString, MB_VOLTAGE_RULE> m_voltageRules;

    /// M5.2: sub-project back-reference — relative path from this
    /// `.kicad_pro` to the enclosing container `.kicad_pro`. Empty
    /// for stand-alone projects and for the container itself. Lets
    /// container-aware code skip the legacy 6-level directory walk.
    /// Persisted as `multi_board.container_project_relative_path`.
    wxString m_containerProjectRelativePath;

    /// `PROJECT_FILE_SUSPEND_NOTIFY` reaches in to bump `m_notifySuspendDepth`
    /// and drain `m_pendingNotifications` directly. We don't expose these
    /// as public state because the only legitimate mutator is the RAII
    /// guard — exposing them invites callers to mismatch begin/end.
    friend class PROJECT_FILE_SUSPEND_NOTIFY;

    /// T3: registered `PROJECT_FILE_OBSERVER`s. Order matters — observers
    /// are dispatched in registration order, mirroring how Kiway peers
    /// expect deterministic ordering. Pointers, not unique_ptr — observers
    /// own their lifetime and de-register before destruction (typically
    /// via the `SCOPED_PROJECT_FILE_OBSERVER` RAII helper).
    std::vector<PROJECT_FILE_OBSERVER*> m_observers;

    /// Defensive back-pointer table for `SCOPED_PROJECT_FILE_OBSERVER`s
    /// holding a raw `m_projectFile` pointer at us. We can outlive their
    /// owning frames (wx pending-delete + idle-time disposal can fire
    /// AFTER a project unload from SETTINGS_MANAGER), and a SCOPED dtor
    /// touching a freed `PROJECT_FILE` would crash on
    /// `UnregisterObserver`. Our dtor walks this list and nulls each
    /// scoped's `m_projectFile` so its own dtor skips the deref.
    /// `friend SCOPED_PROJECT_FILE_OBSERVER` keeps this internal — only
    /// the SCOPED helper edits it.
    friend class SCOPED_PROJECT_FILE_OBSERVER;
    std::vector<SCOPED_PROJECT_FILE_OBSERVER*> m_scopedObservers;

    /// T3: depth counter for `PROJECT_FILE_SUSPEND_NOTIFY`. While > 0,
    /// `NotifyMultiBoardChanged` queues into `m_pendingNotifications`
    /// instead of dispatching immediately. The outermost guard drains
    /// the set on destruction. Re-entrancy is fine; depth is bumped /
    /// decremented in lockstep.
    int m_notifySuspendDepth = 0;

    /// T3: coalesced pending notifications. A `std::set` so repeated
    /// edits to the same field collapse to one event.
    std::set<MULTI_BOARD_FIELD> m_pendingNotifications;

    /// T3 Phase 3: reentrancy guard for `SaveToFile`. While true, a
    /// recursive `SaveToFile` (e.g. triggered from a wxEvent fired
    /// during JSON serialisation) just sets `m_saveAgainPending` and
    /// returns; the in-flight save loops once more before returning.
    bool m_saveInProgress = false;

    /// T3 Phase 3: "save was requested while another save was in
    /// flight" flag. Drained at the end of `SaveToFile` to coalesce
    /// rapid back-to-back saves into one disk write.
    bool m_saveAgainPending = false;

    /// T3 Phase 4: tracks whether the in-memory state has been mutated
    /// since the last successful save. Set by `NotifyMultiBoardChanged`,
    /// cleared by `SaveToFile`. Read by `OnExternalFileChange` to decide
    /// between blanket-reload and EXTERNAL_RELOAD_DIRTY signalling.
    /// Coarse-grained — flips on any multi_board.* mutation rather than
    /// per-field. Good enough for the conflict UX: if the user edited
    /// anything we should ask before overwriting their work.
    bool m_multiBoardDirty = false;

    /// T3 Phase 4: file watcher, owned via opaque pointer because
    /// `wxFileSystemWatcher` is `#if wxUSE_FSWATCHER`-guarded and we
    /// don't want to leak that conditional into every translation unit
    /// that includes `project_file.h`. nullptr when watching is
    /// disabled or the platform lacks an event loop.
    std::unique_ptr<class PROJECT_FILE_WATCHER> m_watcher;

    /// A link to the owning PROJECT
    PROJECT* m_project;

    bool m_wasMigrated;
};

// Specializations to allow directly reading/writing FILE_INFO_PAIRs from JSON

void to_json( nlohmann::json& aJson, const FILE_INFO_PAIR& aPair );

void from_json( const nlohmann::json& aJson, FILE_INFO_PAIR& aPair );

// Specializations to allow directly reading/writing TOP_LEVEL_SHEET_INFO from JSON

void to_json( nlohmann::json& aJson, const TOP_LEVEL_SHEET_INFO& aInfo );

void from_json( const nlohmann::json& aJson, TOP_LEVEL_SHEET_INFO& aInfo );

// Specializations for BOARD_INFO

void to_json( nlohmann::json& aJson, const BOARD_INFO& aInfo );

void from_json( const nlohmann::json& aJson, BOARD_INFO& aInfo );

// Specializations for CROSS_BOARD_CONNECTION

void to_json( nlohmann::json& aJson, const CROSS_BOARD_CONNECTION& aConnection );

void from_json( const nlohmann::json& aJson, CROSS_BOARD_CONNECTION& aConnection );

// Specializations for COMPONENT_BOARD_ASSIGNMENT

void to_json( nlohmann::json& aJson, const COMPONENT_BOARD_ASSIGNMENT& aAssignment );

void from_json( const nlohmann::json& aJson, COMPONENT_BOARD_ASSIGNMENT& aAssignment );

// Specializations for SUB_PROJECT_INFO

void to_json( nlohmann::json& aJson, const SUB_PROJECT_INFO& aInfo );

void from_json( const nlohmann::json& aJson, SUB_PROJECT_INFO& aInfo );

// Specializations for MB_CROSS_BOARD_NET_ENDPOINT

void to_json( nlohmann::json& aJson, const MB_CROSS_BOARD_NET_ENDPOINT& aEp );

void from_json( const nlohmann::json& aJson, MB_CROSS_BOARD_NET_ENDPOINT& aEp );

// Specializations for MB_CROSS_BOARD_NET

void to_json( nlohmann::json& aJson, const MB_CROSS_BOARD_NET& aNet );

void from_json( const nlohmann::json& aJson, MB_CROSS_BOARD_NET& aNet );

// Specializations for CUSTOM_MATE

void to_json( nlohmann::json& aJson, const CUSTOM_MATE& aMate );

void from_json( const nlohmann::json& aJson, CUSTOM_MATE& aMate );

// Specializations for ASSEMBLY_INSTANCE_STATE

void to_json( nlohmann::json& aJson, const ASSEMBLY_INSTANCE_STATE& aState );

void from_json( const nlohmann::json& aJson, ASSEMBLY_INSTANCE_STATE& aState );

#endif
