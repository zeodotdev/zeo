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
class CAMERA;
class EDA_3D_CANVAS;
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
     * Auto-align boards at connector pairs.
     * Uses the cross-board connection definitions from the project.
     */
    void MateConnectors();

    /**
     * Check if connectors can be mated.
     */
    bool CanMateConnectors() const;

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
     * Export the assembly as a STEP file.
     * @param aFilename Output filename
     * @return true if export succeeded
     */
    bool ExportAssemblySTEP( const wxString& aFilename );

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

private:
    /**
     * Calculate mating offset for a connector pair, addressed by
     * component reference + pin number on each side. Matches the
     * `MB_CROSS_BOARD_NET_ENDPOINT` addressing scheme.
     */
    SFVEC3F CalculateMatingOffset( const BOARD_3D_INSTANCE& aBoard1,
                                    const BOARD_3D_INSTANCE& aBoard2,
                                    const wxString& aConnector1Ref,
                                    const wxString& aConnector1Pin,
                                    const wxString& aConnector2Ref,
                                    const wxString& aConnector2Pin );

    /**
     * Get the board thickness in mm.
     */
    float GetBoardThickness( const BOARD* aBoard ) const;

    std::vector<BOARD_3D_INSTANCE>  m_boardInstances;
    std::vector<COLLISION_RESULT>   m_lastCollisions;
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
};

#endif // VIEWER_3D_ASSEMBLY_H
