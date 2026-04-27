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
#include <settings/json_settings.h>
#include <settings/nested_settings.h>

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

    virtual ~PROJECT_FILE() = default;

    virtual bool MigrateFromLegacy( wxConfigBase* aCfg ) override;

    bool LoadFromFile( const wxString& aDirectory = "" ) override;

    bool SaveToFile( const wxString& aDirectory = "", bool aForce = false ) override;

    bool SaveAs( const wxString& aDirectory, const wxString& aFile );

    void SetProject( PROJECT* aProject )
    {
        m_project = aProject;
    }

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

    void SetCrossBoardNets( std::vector<MB_CROSS_BOARD_NET> aNets )
    {
        m_crossBoardNets = std::move( aNets );
    }

    /**
     * Custom (user-declared) mate overrides for the 3D assembly
     * solver. See `CUSTOM_MATE` for the layered-on-top semantics.
     */
    std::vector<CUSTOM_MATE>& GetCustomMates() { return m_customMates; }

    const std::vector<CUSTOM_MATE>& GetCustomMates() const { return m_customMates; }

    void SetCustomMates( std::vector<CUSTOM_MATE> aMates )
    {
        m_customMates = std::move( aMates );
    }

    /// Convenience accessor for the MBS schematic filename referenced by
    /// this container project. Empty when the MBS hasn't been created yet.
    const wxString& GetMbsFileName() const { return m_mbsFileName; }

    void SetMbsFileName( const wxString& aName ) { m_mbsFileName = aName; }

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

    std::shared_ptr<NET_SETTINGS>& NetSettings()
    {
        return m_NetSettings;
    }

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

    /// For container projects: filename of the multi-board schematic.
    wxString m_mbsFileName;

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

#endif
