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
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef VIEWER_3D_ASSEMBLY_H
#define VIEWER_3D_ASSEMBLY_H

#include <kiid.h>
#include <plugins/3dapi/xv3d_types.h>
#include <wx/gdicmn.h>
#include <wx/string.h>

#include <memory>
#include <vector>

class BOARD;
class BOARD_ADAPTER;
class BOARD_ITEM;
class CAMERA;
class EDA_3D_CANVAS;
class MATE_GIZMO;
class PROJECT;
class REPORTER;
class RENDER_3D_OPENGL;
class S3D_CACHE;


/**
 * Represents a single sub-project's PCB rendered in the 3D assembly
 * view. Keyed by the container project's `SUB_PROJECT_INFO::uuid`,
 * not by a `BOARD_INFO::uuid` (the legacy single-project multi-PCB
 * model).
 */
struct BOARD_3D_INSTANCE
{
    KIID                    uuid;             ///< Unique ID for this instance
    KIID                    subProjectUuid;   ///< UUID of the source sub-project in the container
    wxString                pcbFilePath;      ///< Absolute path to the loaded `.kicad_pcb`
    std::unique_ptr<BOARD>  board;            ///< Loaded board (owned by the manager); may be null
    wxString                displayName;      ///< Display name for the board

    /// The sub-project's PROJECT loaded via SETTINGS_MANAGER::LoadProject
    /// (non-active). Owned by SETTINGS_MANAGER, NOT this struct — null
    /// when the sub-project couldn't be loaded. Populated by
    /// `LoadProjectBoards` so each instance has its own KIPRJMOD context
    /// for 3D-model resolution; otherwise sub-board models referencing
    /// `${KIPRJMOD}/...` would be looked up against the *container's*
    /// directory and fail (the user-visible bug: virtual / project-local
    /// 3D models silently disappear in multi-board view).
    PROJECT*                subProject = nullptr;

    // Transform
    SFVEC3F                 position;         ///< Position in mm (X, Y, Z)
    SFVEC3F                 rotation;         ///< Rotation in degrees (X, Y, Z)

    // Visibility
    bool                    visible;          ///< Whether this board is visible
    bool                    transparent;      ///< Whether to render with transparency
    float                   opacity;          ///< Opacity level (0.0 - 1.0)

    // Movable, not copyable: BOARD ownership is unique per instance.
    // Special members are out-of-line so unique_ptr<BOARD> can be
    // instantiated against the complete BOARD type in the .cpp file.
    BOARD_3D_INSTANCE();
    ~BOARD_3D_INSTANCE();
    BOARD_3D_INSTANCE( BOARD_3D_INSTANCE&& ) noexcept;
    BOARD_3D_INSTANCE& operator=( BOARD_3D_INSTANCE&& ) noexcept;
    BOARD_3D_INSTANCE( const BOARD_3D_INSTANCE& ) = delete;
    BOARD_3D_INSTANCE& operator=( const BOARD_3D_INSTANCE& ) = delete;

    /**
     * Get the transformation matrix for this instance.
     */
    glm::mat4 GetTransformMatrix() const;

    /**
     * Set position in mm.
     */
    void SetPosition( float aX, float aY, float aZ )
    {
        position = SFVEC3F( aX, aY, aZ );
    }

    /**
     * Set rotation in degrees.
     */
    void SetRotation( float aX, float aY, float aZ )
    {
        rotation = SFVEC3F( aX, aY, aZ );
    }
};


/**
 * One connector mate pair derived from cross-board nets, OR declared
 * by the user in M6.D-phase-2. Two footprints on two distinct board
 * instances connected by `pinCount` shared net endpoints.
 *
 * The (instanceA, footprintRefA) / (instanceB, footprintRefB) tuples
 * are stored in canonical order (lower instance UUID first; ties
 * broken by footprint ref) so the same physical mate keys to the same
 * MATE_PAIR regardless of which net's endpoint encountered it first.
 *
 * Phase-2 fields (forcedPrimary, alignmentOnly, nonElectrical, custom*)
 * are zero / empty for auto-derived pairs. They track decorations
 * applied by user-declared CUSTOM_MATE overrides.
 */
struct MATE_PAIR
{
    KIID        instanceA;
    KIID        instanceB;
    wxString    footprintRefA;
    wxString    footprintRefB;
    int         pinCount;        ///< number of cross-board net endpoints linking these two footprints

    // ----- M6.D-phase-2 custom-mate decorations -----

    /// Custom PRIMARY override: PickPrimaryPair returns this pair on
    /// its edge regardless of pinCount.
    bool        forcedPrimary = false;

    /// Custom SECONDARY: skipped in primary selection, but tracked as
    /// a residual so the DRC panel can report the misalignment.
    bool        alignmentOnly = false;

    /// Custom MOUNTING_HOLE / ALIGNMENT: no copper pads, skip the
    /// dominant-side detection in `PlaceChildOnParent` and rely on
    /// the optional offset for fine adjustment.
    bool        nonElectrical = false;

    /// KIID of the originating `CUSTOM_MATE` row in the container
    /// project file. Null KIID for auto-derived pairs. Lets the UI
    /// trace a graph edge back to the row the user can edit.
    KIID        customMateUuid;

    /// Optional placement offset applied after the auto-computed pose.
    bool        hasOffset = false;
    double      offsetTx = 0.0, offsetTy = 0.0, offsetTz = 0.0;   ///< mm
    double      offsetRx = 0.0, offsetRy = 0.0, offsetRz = 0.0;   ///< deg
};


/**
 * One edge in the board mate graph: every MATE_PAIR between the same
 * pair of board instances aggregates into a single MATE_EDGE. The
 * primary mate pair (highest pinCount) constrains the child board's
 * 6DOF; secondaries become alignment-check residuals.
 */
struct MATE_EDGE
{
    KIID                    instanceA;
    KIID                    instanceB;
    std::vector<MATE_PAIR>  pairs;
    int                     totalWeight;     ///< sum of all pairs' pinCount
};


/**
 * Alignment-check residual reported when a non-primary mate pair on
 * a board edge can't be perfectly satisfied by the primary pair's
 * pose (over-constrained). Translation in mm, rotation in degrees.
 */
struct MATE_RESIDUAL
{
    MATE_PAIR   pair;
    float       residualMm;       ///< euclidean distance between expected and actual mate-center
    float       residualDeg;      ///< rotational error (currently zero in phase-1)
};


/**
 * Result of a collision check between two 3D objects.
 */
struct COLLISION_RESULT
{
    KIID            board1Uuid;
    KIID            board2Uuid;
    wxString        item1Desc;      ///< Description of item on board 1
    wxString        item2Desc;      ///< Description of item on board 2
    SFVEC3F         collisionPoint; ///< Point of collision in mm
    float           penetrationMm;  ///< Depth of penetration in mm

    COLLISION_RESULT() : penetrationMm( 0.0f ) {}
};


/**
 * State of the assembly view.
 */
struct ASSEMBLY_STATE
{
    bool            showEnclosure;      ///< Whether to show enclosure model if loaded
    bool            mateConnectors;     ///< Snap connector pairs together
    bool            collisionHighlight; ///< Highlight collision areas

    ASSEMBLY_STATE() :
            showEnclosure( false ),
            mateConnectors( false ),
            collisionHighlight( false )
    {}
};


/**
 * Layout mode for multi-board display.
 */
enum class BOARD_LAYOUT_MODE
{
    FLAT,           ///< Boards arranged side by side
    STACKED,        ///< Boards stacked vertically
    CUSTOM          ///< Custom user-defined positions
};


/**
 * Manages multiple board instances in the 3D viewer for assembly visualization.
 *
 * This class handles:
 * - Loading and managing multiple boards from a project
 * - Board positioning and visibility
 * - Connector mating (auto-alignment at connector pairs)
 * - Collision detection between boards
 * - Assembly export to STEP format
 */
class ASSEMBLY_3D_MANAGER
{
public:
    ASSEMBLY_3D_MANAGER();
    ~ASSEMBLY_3D_MANAGER();

    /**
     * Load boards from a multi-board project.
     * @param aProject The project containing board definitions
     */
    void LoadProjectBoards( PROJECT* aProject );

    /**
     * Clear all board instances.
     */
    void Clear();

    // ========== Board Instance Management ==========

    /**
     * Add a board instance to the assembly.
     *
     * The manager takes ownership of @p aBoard.
     *
     * @param aBoard The board to add (manager takes ownership)
     * @param aDisplayName Display name for the board
     * @param aSubProjectUuid Sub-project this board belongs to (may be a
     *                        null KIID for ad-hoc additions outside the
     *                        container model)
     * @return The UUID of the created instance
     */
    KIID AddBoardInstance( std::unique_ptr<BOARD> aBoard, const wxString& aDisplayName,
                           const KIID& aSubProjectUuid = KIID() );

    /**
     * Remove a board instance.
     */
    bool RemoveBoardInstance( const KIID& aInstanceUuid );

    /**
     * Get all board instances.
     */
    const std::vector<BOARD_3D_INSTANCE>& GetBoardInstances() const { return m_boardInstances; }

    /**
     * M6.G: snapshot every instance's pose / visibility / transparency
     * into the container `.kicad_pro`'s assembly state list. Use after
     * bulk operations (`ArrangeBoards`, `ResetPositions`, layout-mode
     * change) so the persisted state matches what's on screen. Per-
     * instance setters call this internally for direct edits.
     */
    void PersistAllInstances();

    /**
     * Get a board instance by UUID.
     */
    BOARD_3D_INSTANCE* GetBoardInstance( const KIID& aInstanceUuid );
    const BOARD_3D_INSTANCE* GetBoardInstance( const KIID& aInstanceUuid ) const;

    // ========== Positioning ==========

    /**
     * Set position of a board instance.
     * @param aInstanceUuid Board instance UUID
     * @param aPosition Position in mm (X, Y, Z)
     */
    void SetBoardPosition( const KIID& aInstanceUuid, const SFVEC3F& aPosition );

    /**
     * Set rotation of a board instance.
     * @param aInstanceUuid Board instance UUID
     * @param aRotation Rotation in degrees (X, Y, Z)
     */
    void SetBoardRotation( const KIID& aInstanceUuid, const SFVEC3F& aRotation );

    /**
     * Arrange boards in a layout.
     * @param aMode Layout mode (flat, stacked, custom)
     * @param aSpacingMm Spacing between boards in mm
     */
    void ArrangeBoards( BOARD_LAYOUT_MODE aMode, float aSpacingMm = 10.0f );

    /**
     * Reset all board positions to default.
     */
    void ResetPositions();

    // ========== Visibility ==========

    /**
     * Set visibility of a board instance.
     */
    void SetBoardVisible( const KIID& aInstanceUuid, bool aVisible );

    /**
     * Set transparency of a board instance.
     */
    void SetBoardTransparent( const KIID& aInstanceUuid, bool aTransparent, float aOpacity = 0.5f );

    /**
     * Show all boards.
     */
    void ShowAllBoards();

    /**
     * Hide all boards.
     */
    void HideAllBoards();

    // ========== Connector Mating ==========

    /**
     * Auto-mate boards at connector pairs derived from the container
     * project's `MB_CROSS_BOARD_NET` set. Implements primary-mate-wins:
     * each board edge's strongest connector pair (most pins) places
     * the child board; secondary pairs become alignment-check residuals
     * available via `GetMateResiduals()`.
     *
     * Pose model in phase-1: translation + 180° X-flip when both
     * connectors sit on the same copper side. Arbitrary rotation /
     * non-electrical mates are M6.D-phase-2.
     */
    void MateConnectors();

    /**
     * Check if connectors can be mated.
     */
    bool CanMateConnectors() const;

    /**
     * Alignment-check residuals from the most recent `MateConnectors()`
     * pass. Each entry is a non-primary mate pair on some board edge
     * whose pose doesn't perfectly satisfy the primary's placement —
     * surface in DRC / collision panel (M6.E).
     */
    const std::vector<MATE_RESIDUAL>& GetMateResiduals() const { return m_lastMateResiduals; }

    // ========== M6.D-phase-2 Custom Mate API ==========

    /**
     * Read-only view of the user-declared mates persisted in the
     * container `.kicad_pro`. Empty when no project is loaded or in
     * single-board mode.
     */
    const std::vector<struct CUSTOM_MATE>& GetCustomMates() const;

    /**
     * Append a custom mate to the container's persisted mates list.
     * The caller fills the mate's fields except `uuid` (auto-assigned
     * if null). After insertion, the next `MateConnectors()` pass
     * picks it up; callers that want the new mate to take effect
     * immediately should call `MateConnectors()` themselves.
     *
     * @return UUID of the newly-stored mate, or null KIID on failure
     *         (no project loaded / not a multi-board container).
     */
    KIID AddCustomMate( const struct CUSTOM_MATE& aMate );

    /**
     * Update an existing custom mate in place (matched by UUID).
     * @return true if the mate was found and updated.
     */
    bool UpdateCustomMate( const struct CUSTOM_MATE& aMate );

    /**
     * Remove a custom mate by UUID.
     * @return true if a mate matched and was removed.
     */
    bool RemoveCustomMate( const KIID& aMateUuid );

    /**
     * Compute the current mate graph (auto-derived from cross-board
     * nets, with custom-mate overrides layered on top) without
     * actually solving placement. Lets the panel UI inspect what
     * `MateConnectors()` would do — display source, role, status —
     * without mutating any pose.
     *
     * Side-effect free: callable from a `const` UI handler. Returns
     * by value because the graph is rebuilt on every call (cheap;
     * scales with `O(nets · custom_mates)`).
     */
    std::vector<MATE_EDGE> BuildMateGraph() const;

    // ========== M6.D-phase-2 mate visualization ==========

    /**
     * Toggle the per-frame "draw mate gizmos" pass in the 3D viewer.
     * When on, every active mate pair shows as a coloured rod with
     * spheres at the connector centres. Default: on.
     */
    void SetShowMateGizmos( bool aShow ) { m_showMateGizmos = aShow; }

    bool GetShowMateGizmos() const { return m_showMateGizmos; }

    /// Show red collision markers for unintended component overlaps
    /// (auto-populated by every position change). Default: on.
    void SetShowCollisionHighlights( bool aShow ) { m_showCollisionHighlights = aShow; }
    bool GetShowCollisionHighlights() const { return m_showCollisionHighlights; }

    /// Show "expected contact" highlights (currently aliases the mate
    /// gizmo's AUTO/CUSTOM entries — same visual, separate toggle so
    /// the user can hide mate-pair lines independently of any future
    /// component-level contact tinting). Default: on.
    void SetShowContactHighlights( bool aShow ) { m_showContactHighlights = aShow; }
    bool GetShowContactHighlights() const { return m_showContactHighlights; }

    /**
     * Debug: blue wireframe boxes for every pair that survives the
     * broad-phase AABB pre-filter — useful for diagnosing why the
     * confirmed-collision pass disagrees with what the user sees.
     */
    void SetShowBroadAabbDebug( bool aShow ) { m_showBroadAabbDebug = aShow; }
    bool GetShowBroadAabbDebug() const { return m_showBroadAabbDebug; }

    /**
     * Mesh-overlap thickness threshold (mm). Mated mesh interpene-
     * trations thicker than this register as COLLISION; thinner ones
     * register as CONTACT. Default 0.5 mm. Tunable per project from
     * the panel (different connector classes have different natural
     * mating depths).
     */
    void  SetCollisionThresholdMm( float aMm );
    float GetCollisionThresholdMm() const      { return m_collisionThresholdMm; }

    /**
     * True iff the project file has any persisted per-instance pose
     * (read by `applyPersistedInstanceStates`). The panel uses this to
     * default the layout-mode dropdown to "Custom" when reopening a
     * project the user has already arranged manually — otherwise we'd
     * keep showing "Flat (side by side)" even though that's a lie.
     */
    bool HasPersistedInstanceStates() const;

    /**
     * Highlight one mate pair in the gizmo render. The matching pair
     * draws bright + larger; all other pairs fade. Pass an empty
     * string to clear the selection.
     *
     * The id is the canonical
     * `instanceA_uuid|footprintRefA|instanceB_uuid|footprintRefB`
     * form (lower instance UUID first; ties broken by footprint ref)
     * — matching what `BuildMateGraph` keys mate pairs on. The panel
     * supplies this string for any tree row, so both auto and custom
     * pairs can be highlighted.
     */
    void SetSelectedMatePair( const wxString& aPairId );

    const wxString& GetSelectedMatePair() const { return m_selectedMatePairId; }

    /**
     * Build the canonical mate-pair id from the four addressing parts.
     * Both auto and custom pairs use the same encoding so panel rows
     * and the gizmo's ENTRY.matePairId match by string equality.
     */
    static wxString MakeMatePairId( const KIID&     aInstanceA,
                                    const wxString& aFootprintRefA,
                                    const KIID&     aInstanceB,
                                    const wxString& aFootprintRefB );

    /**
     * Shift a pair up one position within its board edge — primary
     * selection follows the head of `edge.pairs`, so this raises the
     * pair's priority. Auto-derived pairs work the same as custom
     * ones; bumps are stored in `m_pairPriorityBumps` keyed by pair
     * id (session-only, not persisted yet).
     */
    void ShiftPairUp( const wxString& aPairId );

    /**
     * Shift a pair down one position within its board edge.
     */
    void ShiftPairDown( const wxString& aPairId );

    // ========== Collision Detection ==========

    /**
     * Run collision check between all visible board instances.
     * @return Vector of collision results
     */
    std::vector<COLLISION_RESULT> RunCollisionCheck();

    /**
     * Check if any collisions exist.
     */
    bool HasCollisions() const { return !m_lastCollisions.empty(); }

    /**
     * Get the last collision check results.
     */
    const std::vector<COLLISION_RESULT>& GetLastCollisions() const { return m_lastCollisions; }

    // ========== Assembly State ==========

    /**
     * Get/set assembly state.
     */
    const ASSEMBLY_STATE& GetState() const { return m_state; }
    void SetState( const ASSEMBLY_STATE& aState ) { m_state = aState; }

    void SetShowEnclosure( bool aShow ) { m_state.showEnclosure = aShow; }
    void SetMateConnectors( bool aMate ) { m_state.mateConnectors = aMate; }
    void SetCollisionHighlight( bool aHighlight ) { m_state.collisionHighlight = aHighlight; }

    // ========== Export ==========

    /**
     * Export the assembly as a STEP file. The actual OCCT-using
     * implementation lives in `pcbnew/exporters/step/assembly_step.cpp`
     * and registers itself via `SetSTEPExportCallback` from the pcbnew
     * kiface init path; in any kiface that doesn't pull pcbnew in
     * (e.g. cvpcb's footprint preview viewer) this returns false
     * instead of pulling OCCT into that kiface's link line.
     *
     * @param aFilename Output filename
     * @return true if export succeeded
     */
    bool ExportAssemblySTEP( const wxString& aFilename );

    /**
     * Plain-old-data view of one board in the export, with the
     * world-space pose to apply. Defined here so the 3d-viewer
     * library doesn't need to reach into pcbnew's exporter headers
     * (or pull OCCT) to invoke the registered callback.
     */
    struct STEPBoardEntry
    {
        BOARD*   board;        ///< pcbcommon BOARD; already in 3d-viewer's link surface
        wxString name;
        SFVEC3F  positionMm;   ///< mm
        SFVEC3F  rotationDeg;  ///< Z-Y-X Euler in degrees
    };

    using STEPExportCallback = bool ( * )( const std::vector<STEPBoardEntry>& aEntries,
                                           const wxString&                    aOutputFile );

    /**
     * Install the actual STEP export implementation. Called once from
     * pcbnew kiface init; subsequent calls overwrite. Pass nullptr to
     * disable.
     */
    static void SetSTEPExportCallback( STEPExportCallback aCallback );

    /**
     * Get the combined bounding box of all visible boards.
     */
    void GetAssemblyBoundingBox( SFVEC3F& aMin, SFVEC3F& aMax ) const;

    // ========== M6.C multi-instance rendering ==========

    /**
     * Lazily build a BOARD_ADAPTER + RENDER_3D_OPENGL pair per instance
     * whose BOARD loaded successfully. Each adapter's InitSettings()
     * runs once; geometry caches then stay warm across Redraws.
     *
     * Callable repeatedly — skips instances that already have renderers.
     * Invoked by the canvas the first time it enters its assembly-mode
     * render path and after Clear()/LoadProjectBoards().
     */
    void InitRenderers( EDA_3D_CANVAS* aCanvas, CAMERA& aCamera, S3D_CACHE* a3DCache );

    /**
     * Orchestrate a multi-instance render. For each visible instance
     * whose renderer is built, applies an assembly pose matching the
     * instance's position (translation only in M6.C-phase-1; rotation
     * lands in M6.D), sets SetSkipBufferClear(false) on the first and
     * (true) on subsequent instances, then calls each renderer's
     * Redraw. Returns true if any renderer requested another redraw.
     */
    bool RedrawAll( bool aIsMoving, REPORTER* aStatusReporter, REPORTER* aWarningReporter );

    /**
     * Propagate the canvas's current window size to every per-instance
     * renderer. RENDER_3D_OPENGL::Redraw uses m_windowSize for
     * glViewport; without this each instance would render into a
     * 0x0 viewport while the background still covers the full frame.
     * Called by EDA_3D_CANVAS::DoRePaint before every composite.
     */
    void SetInstancesWindowSize( const wxSize& aSize );

    /**
     * Per-instance descriptor for the multi-instance raytracer. Each
     * descriptor pairs a sub-board's BOARD_ADAPTER with the local→world
     * pose the OpenGL composite path applies (scale × pivot-rotate ×
     * translate). The raytracer's INSTANCE_OBJECT_3D wrapper uses the
     * pose to transform rays into the inner sub-board's local frame.
     */
    struct RAYTRACE_INSTANCE
    {
        BOARD_ADAPTER* adapter;
        glm::mat4      pose;
    };

    /**
     * Compose the raytrace instance list from the current visible
     * BOARD_3D_INSTANCE entries. Pose math matches RedrawAll's OpenGL
     * pose composition exactly so the raytraced scene visually agrees
     * with the OpenGL composite. Skips instances without a built
     * BOARD_ADAPTER.
     */
    void BuildRaytraceInstances( std::vector<RAYTRACE_INSTANCE>& aOut ) const;

    /**
     * Destroy every per-instance RENDER_3D_OPENGL while the GL context
     * is current. Called by EDA_3D_CANVAS::releaseOpenGL inside its
     * LockCtx block — otherwise each renderer's destructor would call
     * glDeleteTextures + freeAllLists against whatever context
     * happens to be current globally, corrupting OTHER OpenGL windows
     * (GAL editors, MBS canvas) that share the process-wide GL state.
     *
     * Leaves the BOARD_ADAPTERs intact; those own only CPU-side
     * resources (BOARD unique_ptrs, layer polygons) and don't need
     * a live context to tear down.
     */
    void ReleaseOpenGL();

    /**
     * Notify renderers that their underlying BOARD data changed and
     * geometry caches must be rebuilt on next Redraw. Called after
     * per-instance edits that affect rendering (e.g. mating snap).
     * Pose changes do NOT require reload — they're applied at render
     * time.
     */
    void RequestReload();

    /// True iff at least one instance has a live renderer built.
    bool HasRenderers() const { return !m_instanceRenderers.empty(); }

    /**
     * Set the user's "sticky" selection (MOON-1331 click selection /
     * MOON-1293 cross-probe highlight). aItem must be a BOARD_ITEM*
     * whose owning BOARD matches one of the instance adapters'
     * BOARD pointers. Routes the call to that instance's
     * RENDER_3D_OPENGL::SetCurrentSelectedItem and clears any
     * previously-selected item on the other instance renderers so
     * only one footprint is highlighted at a time.
     *
     * Pass nullptr to clear the selection on every instance
     * (e.g. click on empty canvas).
     */
    void SetSelectedItem( BOARD_ITEM* aItem );

    /**
     * MOON-1331 phase 4a — board-level selection. Distinct from the
     * footprint selection above: a click on a sub-board's substrate
     * (no fp hit) selects the WHOLE board so the translation gizmo
     * has something to attach to. A click on a footprint sets the fp
     * selection AND the board selection (its parent board).
     */
    void SetSelectedBoardInstance( const KIID& aInstanceUuid );

    /// Clear board-level selection (typically on click in empty space).
    void ClearSelectedBoardInstance();

    /// Currently-selected board instance UUID, or niluuid if none.
    KIID GetSelectedBoardInstance() const { return m_selectedBoardUuid; }

    /// Find the BOARD_3D_INSTANCE whose `board` pointer owns aItem.
    /// Returns nullptr when the item is not from any sub-board (e.g.
    /// a sibling-ITEM raycast hit on the floor / ceiling decoration).
    const BOARD_3D_INSTANCE* FindInstanceForItem( const BOARD_ITEM* aItem ) const;

private:
    /**
     * Get the board thickness in mm.
     */
    float GetBoardThickness( const BOARD* aBoard ) const;

    /**
     * M6.G: copy the given instance's pose/visibility/transparency
     * into the container `.kicad_pro`'s
     * `multi_board.assembly_3d.instances[]`. Inserts a new entry when
     * the sub-project has no persisted state yet, otherwise overwrites.
     * No-op when the manager isn't bound to a container project.
     */
    void persistInstanceState( const BOARD_3D_INSTANCE& aInst );

    /**
     * M6.G: apply persisted per-instance state from the container
     * `.kicad_pro` to every BOARD_3D_INSTANCE whose sub-project UUID
     * matches an entry. Called from `LoadProjectBoards` AFTER the
     * default `ArrangeBoards(FLAT)` so persisted overrides win.
     */
    void applyPersistedInstanceStates();

    // ========== M6.D-phase-1 mate solver pipeline ==========
    //
    // BuildMateGraph() is declared in the public section above so the
    // panel UI can inspect mate state without re-running placement.

    /**
     * BFS-place every reachable instance from a chosen anchor: pick
     * the highest-degree instance as anchor (UUID tiebreak), seat it
     * at world origin with identity rotation, then for each newly
     * visited neighbour place the child via its primary mate pair
     * (highest `pinCount` on the back-edge). Secondary pairs and
     * cycle-closing edges become entries in `m_lastMateResiduals`.
     */
    void SolveMatePoses( const std::vector<MATE_EDGE>& aEdges );

    /**
     * Pick the primary mate pair for an edge: the pair with the
     * highest pinCount; ties broken by canonical (footprintRefA,
     * footprintRefB) ordering for determinism across re-opens.
     */
    const MATE_PAIR* PickPrimaryPair( const MATE_EDGE& aEdge ) const;

    /**
     * Place @p aChild relative to already-placed @p aParent using the
     * given primary mate pair. Computes the same-side-flip / opposite-
     * side rule, sets `aChild.position` and `aChild.rotation`.
     *
     * @return true on success; false if endpoints don't resolve to
     *         valid pads (caller treats as residual / dropped mate).
     */
    bool PlaceChildOnParent( const BOARD_3D_INSTANCE& aParent,
                             BOARD_3D_INSTANCE&       aChild,
                             const MATE_PAIR&         aPrimary );

    /**
     * Z gap between mated boards along the primary mate axis. Phase-1:
     * 5 mm fallback. Future: read from the larger of the two
     * connectors' 3D-model bounding boxes via `S3D_CACHE`.
     */
    float ComputeMateZGap( const BOARD_3D_INSTANCE& aA,
                           const wxString&          aFootprintRefA,
                           const BOARD_3D_INSTANCE& aB,
                           const wxString&          aFootprintRefB ) const;

    /**
     * Compute the residual of a non-primary mate pair after the
     * primary placement. Walks the parent + child poses, projects each
     * connector's pad-center to world coords, and reports the gap.
     */
    MATE_RESIDUAL ComputeMateResidual( const MATE_PAIR& aPair ) const;

    /**
     * Push fresh entries into `m_mateGizmo` from the current mate
     * graph. No return value (mutates the gizmo) so the public header
     * doesn't need to expose the gizmo's nested ENTRY type.
     */
    void rebuildMateGizmoEntries();

    /**
     * Compute the world-space (shared 3D units, post Y-invert)
     * position of a footprint's pad-centroid on the given instance.
     * Mirrors the pose math `RedrawAll` applies so the gizmo lines
     * up exactly with the rendered footprint. Returns false when the
     * footprint can't be resolved (logged as a missing mate).
     */
    bool projectFootprintCentroidToWorld( const BOARD_3D_INSTANCE& aInst,
                                          const BOARD_ADAPTER&     aAdapter,
                                          const wxString&          aFootprintRef,
                                          glm::vec3&               aOutWorld ) const;

    /// One unintended collision pair from M6.E phase-2. Stored
    /// On-model overlap highlight — one entry per colliding or near-
    /// touching footprint pair. The AABB is in board-local mm (XY +
    /// Z extent of each footprint's OBB intersection); the renderer
    /// converts to shared-world units before drawing. KIND drives
    /// colour: red for actual penetration, yellow for proximity.
    /// Mirror enum (rather than nested MATE_GIZMO::OVERLAP_KIND) so
    /// this header stays decoupled from the GL gizmo headers — only
    /// the .cpp converts at the render boundary.
    enum class OVERLAP_KIND
    {
        COLLISION,
        CONTACT,
        BROAD     ///< AABB-only, debug visualization
    };

    struct OverlapBox
    {
        SFVEC3F                      minMm;
        SFVEC3F                      maxMm;
        OVERLAP_KIND                 kind;
        KIID                         instanceA;
        KIID                         instanceB;
        wxString                     refA;
        wxString                     refB;
        /// Optional: intersecting triangle vertices in board-mm
        /// space. Stored as flat groups of three vertices per
        /// triangle, populated only by COLLISION boxes that came
        /// from a mesh-tri test (vs. AABB fallback / mated CONTACT).
        /// Renderer draws these as filled red triangles so the user
        /// sees the actual surface of penetration instead of just
        /// the AABB bounding the intersection vertices.
        std::vector<SFVEC3F>         triVertsMm;
    };

    std::vector<BOARD_3D_INSTANCE>  m_boardInstances;
    std::vector<COLLISION_RESULT>   m_lastCollisions;
    std::vector<OverlapBox>         m_lastOverlapBoxes;
    std::vector<MATE_RESIDUAL>      m_lastMateResiduals;
    ASSEMBLY_STATE                  m_state;
    PROJECT*                        m_project;

    // M6.C: per-instance render resources. Indices align with
    // m_boardInstances. Entries are owned by the manager and destroyed
    // via the out-of-line destructor in the .cpp so forward-declared
    // BOARD_ADAPTER / RENDER_3D_OPENGL can be used here.
    std::vector<std::unique_ptr<BOARD_ADAPTER>>     m_instanceAdapters;
    std::vector<std::unique_ptr<RENDER_3D_OPENGL>>  m_instanceRenderers;

    /// Non-owning. Cached from InitRenderers so RedrawAll can override
    /// the per-instance reload's SetBoardLookAtPos calls with an
    /// assembly-wide framing on first paint.
    CAMERA*                         m_camera = nullptr;

    /// One-shot: re-fit the camera to the assembly bbox after the next
    /// full Redraw cycle. Set when renderers are freshly built (so the
    /// per-instance reloads each call SetBoardLookAtPos); cleared after
    /// we override with the assembly center.
    bool                            m_cameraFitPending = false;

    /// Shared BIU→3D-unit factor. Each BOARD_ADAPTER::InitSettings
    /// computes its own per-board factor to fit that one board in
    /// ±4 3D units; multi-board composite needs every instance to
    /// share a consistent world scale. RedrawAll uses this to build a
    /// per-instance pose = translate(worldPos_shared) *
    /// scale(shared/local_i) which unifies the frames.
    /// Initialized by InitRenderers once all per-instance adapters
    /// have built their local factors.
    double                          m_sharedBiuTo3Dunits = 1.0;

    // ========== M6.D-phase-2 mate visualization ==========

    /// Lazily built in InitRenderers (needs a live GL context). Drawn
    /// after every per-instance Redraw in `RedrawAll` when
    /// `m_showMateGizmos` is true.
    std::unique_ptr<MATE_GIZMO>     m_mateGizmo;

    /// User-controllable toggle for the mate-gizmo render pass.
    /// Default on so the visualization is discoverable.
    bool                            m_showMateGizmos          = true;
    bool                            m_showCollisionHighlights = true;
    bool                            m_showContactHighlights   = true;
    /// Debug: render broad-phase AABB-overlap boxes (blue wireframe)
    /// alongside the confirmed COLLISION/CONTACT boxes so the user
    /// can compare what the AABB pre-filter is finding vs what the
    /// mesh-level narrow phase confirms. Default off.
    bool                            m_showBroadAabbDebug      = false;

    /// User-tunable mesh-overlap thickness (in mm) above which a
    /// mated mesh interpenetration is flagged as a COLLISION rather
    /// than CONTACT. Different connectors have different "natural"
    /// mating depth — board-to-board headers may need 1-2 mm,
    /// FFC sleeves only ~0.5 mm. Settable per project.
    float                           m_collisionThresholdMm    = 0.5f;

    /// Currently-selected mate pair identifier (canonical encoded
    /// string from `MakeMatePairId`). Empty string = no selection
    /// → all gizmos render at full intensity.
    wxString                        m_selectedMatePairId;

    /// MOON-1331 phase 4a — currently-selected sub-board (UUID of the
    /// BOARD_3D_INSTANCE). niluuid when no board is selected.
    /// Independent of the per-instance footprint highlight stored on
    /// the OpenGL renderers; both can be set simultaneously when the
    /// user clicks a footprint (selects fp + parent board).
    KIID                            m_selectedBoardUuid;

    /// Per-pair priority offset applied during BuildMateGraph sort.
    /// Negative = higher priority (appears earlier in edge.pairs;
    /// head of list is the primary). Adjusted by ShiftPairUp/Down.
    /// Session-only — not persisted across project re-opens yet.
    std::map<wxString, int>         m_pairPriorityBumps;
};

#endif // VIEWER_3D_ASSEMBLY_H
