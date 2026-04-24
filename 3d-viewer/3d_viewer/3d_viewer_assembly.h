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
#include <wx/string.h>

#include <memory>
#include <vector>

class BOARD;
class PROJECT;


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
};

#endif // VIEWER_3D_ASSEMBLY_H
