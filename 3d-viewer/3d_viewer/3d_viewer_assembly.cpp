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

// kiglad must precede any other include that pulls in system gl.h,
// otherwise glad refuses to load. render_3d_opengl.h transitively
// includes kiglad, but other headers below (board.h, wx, etc.) may
// drag system gl.h in first on macOS if we don't pre-empt them.
#include <kicad_gl/kiglad.h>

#include "3d_viewer_assembly.h"

#include "3d_canvas/board_adapter.h"
#include "3d_canvas/eda_3d_canvas.h"
#include "3d_rendering/opengl/render_3d_opengl.h"
#include <gal/3d/camera.h>
#include <board.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <multi_board/sub_project_board_loader.h>
#include <pad.h>
#include <project.h>
#include <project/multi_board_scan.h>
#include <project/project_file.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>


// ========== BOARD_3D_INSTANCE ==========

BOARD_3D_INSTANCE::BOARD_3D_INSTANCE() :
        position( 0, 0, 0 ),
        rotation( 0, 0, 0 ),
        visible( true ),
        transparent( false ),
        opacity( 1.0f )
{
}


// Out-of-line so unique_ptr<BOARD>'s special members instantiate against
// the complete BOARD type included above.
BOARD_3D_INSTANCE::~BOARD_3D_INSTANCE() = default;
BOARD_3D_INSTANCE::BOARD_3D_INSTANCE( BOARD_3D_INSTANCE&& ) noexcept = default;
BOARD_3D_INSTANCE& BOARD_3D_INSTANCE::operator=( BOARD_3D_INSTANCE&& ) noexcept = default;


glm::mat4 BOARD_3D_INSTANCE::GetTransformMatrix() const
{
    glm::mat4 transform = glm::mat4( 1.0f );

    // Apply translation
    transform = glm::translate( transform, glm::vec3( position.x, position.y, position.z ) );

    // Apply rotations (Z, Y, X order - Euler angles)
    transform = glm::rotate( transform, glm::radians( rotation.z ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
    transform = glm::rotate( transform, glm::radians( rotation.y ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    transform = glm::rotate( transform, glm::radians( rotation.x ), glm::vec3( 1.0f, 0.0f, 0.0f ) );

    return transform;
}


// ========== ASSEMBLY_3D_MANAGER ==========

namespace
{

/**
 * Find the FOOTPRINT on @p aBoard whose reference matches @p aRef.
 * Returns nullptr when no such footprint exists.
 */
FOOTPRINT* findFootprintByRef( BOARD* aBoard, const wxString& aRef )
{
    if( !aBoard )
        return nullptr;

    return aBoard->FindFootprintByReference( aRef );
}


/**
 * Find the PAD on @p aFootprint addressed by pad number @p aPinNumber.
 * Pad numbers are strings ("1", "A12", "GND") per KiCad convention.
 */
PAD* findPadByNumber( FOOTPRINT* aFootprint, const wxString& aPinNumber )
{
    if( !aFootprint )
        return nullptr;

    for( PAD* pad : aFootprint->Pads() )
    {
        if( pad->GetNumber() == aPinNumber )
            return pad;
    }

    return nullptr;
}

} // namespace


ASSEMBLY_3D_MANAGER::ASSEMBLY_3D_MANAGER() :
        m_project( nullptr )
{
}


ASSEMBLY_3D_MANAGER::~ASSEMBLY_3D_MANAGER()
{
    if( m_project )
        m_project->RemoveDestroyHook( this );
}


void ASSEMBLY_3D_MANAGER::LoadProjectBoards( PROJECT* aProject )
{
    Clear();
    m_project = aProject;

    if( m_project )
        m_project->AddDestroyHook( this, [this]() { m_project = nullptr; } );

    if( !aProject )
        return;

    PROJECT_FILE& projectFile = aProject->GetProjectFile();

    // Container-model only. Single-project multi-PCB (legacy
    // BOARD_INFO) is not addressed here — those projects don't use the
    // assembly viewer.
    if( !projectFile.IsMultiBoardContainer() )
        return;

    for( const SUB_PROJECT_INFO& sub : projectFile.GetSubProjects() )
    {
        BOARD_3D_INSTANCE instance;
        instance.uuid = KIID();
        instance.subProjectUuid = sub.uuid;
        instance.displayName = sub.displayName.IsEmpty() ? sub.name : sub.displayName;

        wxFileName proFile = projectFile.ResolveSubProjectPath( sub );
        instance.pcbFilePath = MultiBoardMainPcb( proFile ).GetFullPath();

        // Best-effort load. A null `board` is tolerated downstream:
        // layout / mating / collision checks all skip instances that
        // failed to load. Logging is via the loader's wxLogTrace.
        instance.board = LoadSubProjectBoard( *aProject, sub );

        m_boardInstances.push_back( std::move( instance ) );
    }

    // Default flat layout; user can switch via PANEL_3D_ASSEMBLY.
    ArrangeBoards( BOARD_LAYOUT_MODE::FLAT, 20.0f );
}


void ASSEMBLY_3D_MANAGER::Clear()
{
    if( m_project )
        m_project->RemoveDestroyHook( this );

    // Tear down renderers BEFORE the instance BOARDs are destroyed —
    // each renderer holds a BOARD_ADAPTER that references its BOARD.
    m_instanceRenderers.clear();
    m_instanceAdapters.clear();

    m_boardInstances.clear();
    m_lastCollisions.clear();
    m_project = nullptr;

    // Camera cache is frame-owned; clear the pointer so a stale CAMERA*
    // can't outlive its frame. Next InitRenderers re-caches.
    m_camera = nullptr;
    m_cameraFitPending = false;
}


KIID ASSEMBLY_3D_MANAGER::AddBoardInstance( std::unique_ptr<BOARD> aBoard,
                                             const wxString& aDisplayName,
                                             const KIID& aSubProjectUuid )
{
    BOARD_3D_INSTANCE instance;
    instance.uuid = KIID();
    instance.subProjectUuid = aSubProjectUuid;
    instance.board = std::move( aBoard );
    instance.displayName = aDisplayName;

    KIID instanceUuid = instance.uuid;
    m_boardInstances.push_back( std::move( instance ) );
    return instanceUuid;
}


bool ASSEMBLY_3D_MANAGER::RemoveBoardInstance( const KIID& aInstanceUuid )
{
    auto it = std::find_if( m_boardInstances.begin(), m_boardInstances.end(),
            [&aInstanceUuid]( const BOARD_3D_INSTANCE& inst )
            {
                return inst.uuid == aInstanceUuid;
            } );

    if( it != m_boardInstances.end() )
    {
        m_boardInstances.erase( it );
        return true;
    }
    return false;
}


BOARD_3D_INSTANCE* ASSEMBLY_3D_MANAGER::GetBoardInstance( const KIID& aInstanceUuid )
{
    for( auto& inst : m_boardInstances )
    {
        if( inst.uuid == aInstanceUuid )
            return &inst;
    }
    return nullptr;
}


const BOARD_3D_INSTANCE* ASSEMBLY_3D_MANAGER::GetBoardInstance( const KIID& aInstanceUuid ) const
{
    for( const auto& inst : m_boardInstances )
    {
        if( inst.uuid == aInstanceUuid )
            return &inst;
    }
    return nullptr;
}


void ASSEMBLY_3D_MANAGER::SetBoardPosition( const KIID& aInstanceUuid, const SFVEC3F& aPosition )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
        inst->position = aPosition;
}


void ASSEMBLY_3D_MANAGER::SetBoardRotation( const KIID& aInstanceUuid, const SFVEC3F& aRotation )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
        inst->rotation = aRotation;
}


void ASSEMBLY_3D_MANAGER::ArrangeBoards( BOARD_LAYOUT_MODE aMode, float aSpacingMm )
{
    if( m_boardInstances.empty() )
        return;

    switch( aMode )
    {
    case BOARD_LAYOUT_MODE::FLAT:
    {
        // Arrange boards side by side along X axis
        float xOffset = 0.0f;
        for( auto& inst : m_boardInstances )
        {
            inst.position = SFVEC3F( xOffset, 0.0f, 0.0f );
            inst.rotation = SFVEC3F( 0.0f, 0.0f, 0.0f );

            float boardWidth = 100.0f;  // Default 100mm if board didn't load
            if( inst.board )
            {
                BOX2I bbox = inst.board->GetBoardEdgesBoundingBox();
                boardWidth = static_cast<float>( bbox.GetWidth() ) / 1000000.0f;
            }

            xOffset += boardWidth + aSpacingMm;
        }
        break;
    }

    case BOARD_LAYOUT_MODE::STACKED:
    {
        // Stack boards along Z axis
        float zOffset = 0.0f;
        for( auto& inst : m_boardInstances )
        {
            inst.position = SFVEC3F( 0.0f, 0.0f, zOffset );
            inst.rotation = SFVEC3F( 0.0f, 0.0f, 0.0f );

            float boardThickness = GetBoardThickness( inst.board.get() );
            zOffset += boardThickness + aSpacingMm;
        }
        break;
    }

    case BOARD_LAYOUT_MODE::CUSTOM:
        // Don't change positions in custom mode
        break;
    }
}


void ASSEMBLY_3D_MANAGER::ResetPositions()
{
    for( auto& inst : m_boardInstances )
    {
        inst.position = SFVEC3F( 0.0f, 0.0f, 0.0f );
        inst.rotation = SFVEC3F( 0.0f, 0.0f, 0.0f );
    }

    ArrangeBoards( BOARD_LAYOUT_MODE::FLAT, 20.0f );
}


void ASSEMBLY_3D_MANAGER::SetBoardVisible( const KIID& aInstanceUuid, bool aVisible )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
        inst->visible = aVisible;
}


void ASSEMBLY_3D_MANAGER::SetBoardTransparent( const KIID& aInstanceUuid, bool aTransparent,
                                                float aOpacity )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
    {
        inst->transparent = aTransparent;
        inst->opacity = aOpacity;
    }
}


void ASSEMBLY_3D_MANAGER::ShowAllBoards()
{
    for( auto& inst : m_boardInstances )
        inst.visible = true;
}


void ASSEMBLY_3D_MANAGER::HideAllBoards()
{
    for( auto& inst : m_boardInstances )
        inst.visible = false;
}


void ASSEMBLY_3D_MANAGER::MateConnectors()
{
    if( !m_project || m_boardInstances.size() < 2 )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();

    // For each cross-board net with at least two endpoints, anchor the
    // first endpoint's instance and snap the second endpoint's instance
    // so the two pads coincide (with a connector-height gap in Z).
    // Endpoints beyond the second are ignored at this stage; multi-way
    // mating is M6.D's domain.
    for( const MB_CROSS_BOARD_NET& net : projectFile.GetCrossBoardNets() )
    {
        if( net.endpoints.size() < 2 )
            continue;

        const MB_CROSS_BOARD_NET_ENDPOINT& ep1 = net.endpoints[0];
        const MB_CROSS_BOARD_NET_ENDPOINT& ep2 = net.endpoints[1];

        BOARD_3D_INSTANCE* inst1 = nullptr;
        BOARD_3D_INSTANCE* inst2 = nullptr;

        for( auto& inst : m_boardInstances )
        {
            if( inst.subProjectUuid == ep1.subProjectUuid )
                inst1 = &inst;
            if( inst.subProjectUuid == ep2.subProjectUuid )
                inst2 = &inst;
        }

        if( !inst1 || !inst2 || !inst1->board || !inst2->board )
            continue;

        SFVEC3F offset = CalculateMatingOffset( *inst1, *inst2,
                                                ep1.componentRef, ep1.pinNumber,
                                                ep2.componentRef, ep2.pinNumber );

        // Snap inst2 relative to inst1.
        inst2->position = inst1->position + offset;
    }

    m_state.mateConnectors = true;
}


bool ASSEMBLY_3D_MANAGER::CanMateConnectors() const
{
    if( !m_project )
        return false;

    return !m_project->GetProjectFile().GetCrossBoardNets().empty();
}


std::vector<COLLISION_RESULT> ASSEMBLY_3D_MANAGER::RunCollisionCheck()
{
    m_lastCollisions.clear();

    // AABB-only check on board outlines + thickness. Mesh-accurate
    // collision is M6.E.
    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        const BOARD_3D_INSTANCE& inst1 = m_boardInstances[i];
        if( !inst1.visible || !inst1.board )
            continue;

        for( size_t j = i + 1; j < m_boardInstances.size(); j++ )
        {
            const BOARD_3D_INSTANCE& inst2 = m_boardInstances[j];
            if( !inst2.visible || !inst2.board )
                continue;

            BOX2I bbox1 = inst1.board->GetBoardEdgesBoundingBox();
            BOX2I bbox2 = inst2.board->GetBoardEdgesBoundingBox();

            float z1Min = inst1.position.z;
            float z1Max = inst1.position.z + GetBoardThickness( inst1.board.get() );
            float z2Min = inst2.position.z;
            float z2Max = inst2.position.z + GetBoardThickness( inst2.board.get() );

            if( z1Max <= z2Min || z2Max <= z1Min )
                continue;

            // XY overlap (rotation not yet handled — M6.E)
            float x1Min = inst1.position.x + bbox1.GetLeft() / 1000000.0f;
            float x1Max = inst1.position.x + bbox1.GetRight() / 1000000.0f;
            float x2Min = inst2.position.x + bbox2.GetLeft() / 1000000.0f;
            float x2Max = inst2.position.x + bbox2.GetRight() / 1000000.0f;

            float y1Min = inst1.position.y + bbox1.GetTop() / 1000000.0f;
            float y1Max = inst1.position.y + bbox1.GetBottom() / 1000000.0f;
            float y2Min = inst2.position.y + bbox2.GetTop() / 1000000.0f;
            float y2Max = inst2.position.y + bbox2.GetBottom() / 1000000.0f;

            bool xOverlap = x1Max > x2Min && x2Max > x1Min;
            bool yOverlap = y1Max > y2Min && y2Max > y1Min;

            if( xOverlap && yOverlap )
            {
                COLLISION_RESULT result;
                result.board1Uuid = inst1.uuid;
                result.board2Uuid = inst2.uuid;
                result.item1Desc = inst1.displayName;
                result.item2Desc = inst2.displayName;

                float cx = ( std::max( x1Min, x2Min ) + std::min( x1Max, x2Max ) ) / 2.0f;
                float cy = ( std::max( y1Min, y2Min ) + std::min( y1Max, y2Max ) ) / 2.0f;
                float cz = ( std::max( z1Min, z2Min ) + std::min( z1Max, z2Max ) ) / 2.0f;
                result.collisionPoint = SFVEC3F( cx, cy, cz );

                float xPen = std::min( x1Max - x2Min, x2Max - x1Min );
                float yPen = std::min( y1Max - y2Min, y2Max - y1Min );
                float zPen = std::min( z1Max - z2Min, z2Max - z1Min );
                result.penetrationMm = std::min( { xPen, yPen, zPen } );

                m_lastCollisions.push_back( result );
            }
        }
    }

    return m_lastCollisions;
}


bool ASSEMBLY_3D_MANAGER::ExportAssemblySTEP( const wxString& aFilename )
{
    // M6.F deliverable — implement via OpenCASCADE STEP compound:
    // 1. For each visible board instance, drive existing per-board STEP
    //    export to an in-memory TopoDS_Shape.
    // 2. Apply the instance transform as a TopLoc_Location.
    // 3. Compose into a STEP compound and write.
    return false;
}


void ASSEMBLY_3D_MANAGER::GetAssemblyBoundingBox( SFVEC3F& aMin, SFVEC3F& aMax ) const
{
    bool first = true;

    for( const auto& inst : m_boardInstances )
    {
        if( !inst.visible || !inst.board )
            continue;

        BOX2I bbox = inst.board->GetBoardEdgesBoundingBox();
        float thickness = GetBoardThickness( inst.board.get() );

        SFVEC3F instMin(
            inst.position.x + bbox.GetLeft() / 1000000.0f,
            inst.position.y + bbox.GetTop() / 1000000.0f,
            inst.position.z
        );

        SFVEC3F instMax(
            inst.position.x + bbox.GetRight() / 1000000.0f,
            inst.position.y + bbox.GetBottom() / 1000000.0f,
            inst.position.z + thickness
        );

        if( first )
        {
            aMin = instMin;
            aMax = instMax;
            first = false;
        }
        else
        {
            aMin.x = std::min( aMin.x, instMin.x );
            aMin.y = std::min( aMin.y, instMin.y );
            aMin.z = std::min( aMin.z, instMin.z );
            aMax.x = std::max( aMax.x, instMax.x );
            aMax.y = std::max( aMax.y, instMax.y );
            aMax.z = std::max( aMax.z, instMax.z );
        }
    }
}


SFVEC3F ASSEMBLY_3D_MANAGER::CalculateMatingOffset( const BOARD_3D_INSTANCE& aBoard1,
                                                     const BOARD_3D_INSTANCE& aBoard2,
                                                     const wxString& aConnector1Ref,
                                                     const wxString& aConnector1Pin,
                                                     const wxString& aConnector2Ref,
                                                     const wxString& aConnector2Pin )
{
    FOOTPRINT* fp1 = findFootprintByRef( aBoard1.board.get(), aConnector1Ref );
    FOOTPRINT* fp2 = findFootprintByRef( aBoard2.board.get(), aConnector2Ref );

    if( !fp1 || !fp2 )
        return SFVEC3F( 0, 0, 0 );

    PAD* pad1 = findPadByNumber( fp1, aConnector1Pin );
    PAD* pad2 = findPadByNumber( fp2, aConnector2Pin );

    // Prefer pad-center alignment when pads were resolved; fall back to
    // footprint-anchor alignment otherwise. M6.D will replace this with
    // pad-normal-aware mating that respects board flip.
    VECTOR2I pos1 = pad1 ? pad1->GetPosition() : fp1->GetPosition();
    VECTOR2I pos2 = pad2 ? pad2->GetPosition() : fp2->GetPosition();

    float dx = ( pos1.x - pos2.x ) / 1000000.0f;
    float dy = ( pos1.y - pos2.y ) / 1000000.0f;

    // Stack with a connector-height Z gap. M6.D will replace the 5 mm
    // fallback with the connector's actual 3D-model height.
    float thickness1 = GetBoardThickness( aBoard1.board.get() );
    float connectorHeight = 5.0f;
    float dz = thickness1 + connectorHeight;

    return SFVEC3F( dx, dy, dz );
}


float ASSEMBLY_3D_MANAGER::GetBoardThickness( const BOARD* aBoard ) const
{
    if( !aBoard )
        return 1.6f;  // Default 1.6mm

    const BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();
    return static_cast<float>( bds.GetBoardThickness() ) / 1000000.0f;
}


// ========== M6.C multi-instance rendering ==========

void ASSEMBLY_3D_MANAGER::InitRenderers( EDA_3D_CANVAS* aCanvas, CAMERA& aCamera,
                                          S3D_CACHE* a3DCache )
{
    if( !aCanvas )
        return;

    // Cache the camera for RedrawAll's post-reload fit override.
    m_camera = &aCamera;

    // The canvas's main BOARD_ADAPTER has already been wired up by the
    // frame (LoadSettings → applySettings sets m_Cfg from
    // GetAppSettings<EDA_3D_VIEWER_SETTINGS>). We share that same
    // settings pointer with each per-instance adapter so layer
    // visibility, colors, and render toggles stay consistent across
    // the composite — and so the InitSettings call below has non-null
    // m_Cfg to dereference.
    EDA_3D_VIEWER_SETTINGS* sharedCfg = aCanvas->GetBoardAdapter().m_Cfg;

    if( !sharedCfg )
    {
        // applySettings hasn't run yet. Skip this pass; DoRePaint will
        // re-enter on the next paint, at which point cfg is available.
        return;
    }

    // Grow per-instance vectors to match the instance list. Existing
    // entries are preserved so already-initialized renderers stay warm
    // across repeated calls (e.g. when Redraw re-enters on camera move).
    m_instanceAdapters.resize( m_boardInstances.size() );
    m_instanceRenderers.resize( m_boardInstances.size() );

    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        BOARD_3D_INSTANCE& inst = m_boardInstances[i];

        // Skip instances whose BOARD didn't load — they have nothing to
        // render. Keeping the slot null lets the render loop skip.
        if( !inst.board )
            continue;

        if( !m_instanceAdapters[i] )
        {
            m_instanceAdapters[i] = std::make_unique<BOARD_ADAPTER>();
            m_instanceAdapters[i]->SetBoard( inst.board.get() );
            m_instanceAdapters[i]->Set3dCacheManager( a3DCache );
            m_instanceAdapters[i]->m_Cfg = sharedCfg;
            // InitSettings builds layer polygons / BVH containers and is
            // the expensive step (~100-500ms per board). We do it once
            // per load, not per frame.
            m_instanceAdapters[i]->InitSettings( nullptr, nullptr );
        }

        if( !m_instanceRenderers[i] )
        {
            m_instanceRenderers[i] = std::make_unique<RENDER_3D_OPENGL>(
                    aCanvas, *m_instanceAdapters[i], aCamera );
            m_instanceRenderers[i]->ReloadRequest();

            // Fresh renderer → its reload() will call
            // SetBoardLookAtPos with a local (sub-board) center, which
            // is wrong for a composite. Mark the camera for re-fit
            // after RedrawAll so we point at the assembly-wide center.
            m_cameraFitPending = true;
        }
    }

    // Compute the shared BIU→3D-unit factor once all per-instance
    // adapters have initialized. Scope it to the LARGEST physical
    // dimension across all instance boards in BIU, so the composite
    // fits within RANGE_SCALE_3D units — matching how single-board
    // rendering + camera defaults are tuned. Each per-instance pose
    // (built in RedrawAll) scales its board's local 3D units into
    // this shared space before applying the mm translation.
    double maxDimBIU = 0.0;

    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        if( !inst.board )
            continue;

        BOX2I bbox = inst.board->GetBoardEdgesBoundingBox();
        double dimX = static_cast<double>( bbox.GetWidth() );
        double dimY = static_cast<double>( bbox.GetHeight() );
        maxDimBIU = std::max( { maxDimBIU, dimX, dimY } );
    }

    // Include the flat-layout X offset of the rightmost instance so
    // the composite fits too. Positions are in mm; convert to BIU.
    double maxRightEdgeBIU = 0.0;

    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        if( !inst.board )
            continue;

        BOX2I  bbox = inst.board->GetBoardEdgesBoundingBox();
        double posRightBIU =
                static_cast<double>( inst.position.x ) * 1e6
                + static_cast<double>( bbox.GetWidth() );
        maxRightEdgeBIU = std::max( maxRightEdgeBIU, posRightBIU );
    }

    double assemblyExtentBIU = std::max( maxDimBIU, maxRightEdgeBIU );

    if( assemblyExtentBIU > 0.0 )
        m_sharedBiuTo3Dunits = RANGE_SCALE_3D / assemblyExtentBIU;
    else
        m_sharedBiuTo3Dunits = 1e-6;  // fallback: 1 unit = 1 mm
}


bool ASSEMBLY_3D_MANAGER::RedrawAll( bool aIsMoving, REPORTER* aStatusReporter,
                                      REPORTER* aWarningReporter )
{
    bool requestAnother = false;
    bool firstPass      = true;

    // Compute the assembly bbox center in shared-space coordinates and
    // use it as an offset on every instance pose. This centres the
    // full composite at world origin (where the camera's default
    // look-at lives) — keeps rotation pivoting around the boards, no
    // need to override SetBoardLookAtPos.
    //
    // Y is negated: BOARD_ADAPTER inverts Y when projecting BIU into
    // 3D space (line 367 of board_adapter.cpp), so a KiCad +Y renders
    // at 3D −Y. GetAssemblyBoundingBox + inst.position are in KiCad
    // (mm) space, so we flip the sign here to get render-space.
    SFVEC3F bboxMinMm, bboxMaxMm;
    GetAssemblyBoundingBox( bboxMinMm, bboxMaxMm );
    const SFVEC3F centerMm = ( bboxMinMm + bboxMaxMm ) * 0.5f;
    const float   sharedF  = static_cast<float>( m_sharedBiuTo3Dunits );
    const float   biuPerMm = 1.0e6f;
    const glm::vec3 centerShared(  centerMm.x * biuPerMm * sharedF,
                                  -centerMm.y * biuPerMm * sharedF,
                                   centerMm.z * biuPerMm * sharedF );

    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        const BOARD_3D_INSTANCE& inst = m_boardInstances[i];

        if( !inst.visible || !inst.board )
            continue;

        if( i >= m_instanceRenderers.size() || !m_instanceRenderers[i] )
            continue;

        // Per-instance pose unifies coordinate frames.
        //
        // Each BOARD_ADAPTER::InitSettings computes its own
        // `biuTo3Dunits = RANGE_SCALE_3D / maxDim` — boards of
        // different sizes live in incompatible local scales.
        //
        // We map each instance from its local 3D frame into the
        // shared frame, then translate by its world position, minus
        // the assembly-center offset so the whole composite sits at
        // the origin (matching the camera's default look-at).
        //
        //   pose = translate(instancePosShared - assemblyCenterShared)
        //        × scale(shared / local_i)
        const double localFactor = m_instanceAdapters[i]->BiuTo3dUnits();
        const double scaleFactor = ( localFactor != 0.0 )
                                       ? m_sharedBiuTo3Dunits / localFactor
                                       : 1.0;

        const glm::vec3 instShared(  inst.position.x * biuPerMm * sharedF,
                                    -inst.position.y * biuPerMm * sharedF,
                                     inst.position.z * biuPerMm * sharedF );

        glm::mat4 pose = glm::mat4( 1.0f );
        pose = glm::translate( pose, instShared - centerShared );
        pose = glm::scale( pose, glm::vec3( static_cast<float>( scaleFactor ) ) );

        m_instanceRenderers[i]->SetAssemblyPose( pose );
        m_instanceRenderers[i]->SetSkipBufferClear( !firstPass );

        bool wants = m_instanceRenderers[i]->Redraw( aIsMoving, aStatusReporter,
                                                      aWarningReporter );
        requestAnother = requestAnother || wants;

        firstPass = false;
    }

    // Nothing drew: the caller's framebuffer is untouched. That's
    // visually empty but avoids a stale frame. The canvas still
    // SwapsBuffers, so the user sees a blank viewport — expected when
    // every instance is hidden or failed to load.

    // Each per-instance reload() overwrites the camera look-at with
    // its local board center. We centered the composite at world
    // origin via the pose above, so force the look-at back to origin
    // so rotation pivots around the boards (not wherever the last
    // renderer's reload happened to set it).
    if( m_camera && m_cameraFitPending && !firstPass )
    {
        m_camera->SetBoardLookAtPos( SFVEC3F( 0.0f, 0.0f, 0.0f ) );
        m_cameraFitPending = false;
        requestAnother = true;
    }

    // All-hidden case: nothing drew, so nothing cleared. Clear to the
    // adapter's bg-bottom color so the viewport shows the normal
    // background instead of a stale frame or a black flash.
    if( firstPass )
    {
        const BOARD_ADAPTER* refAdapter = nullptr;

        for( const auto& adapter : m_instanceAdapters )
        {
            if( adapter )
            {
                refAdapter = adapter.get();
                break;
            }
        }

        if( refAdapter )
        {
            glClearColor( refAdapter->m_BgColorBot.r,
                          refAdapter->m_BgColorBot.g,
                          refAdapter->m_BgColorBot.b,
                          refAdapter->m_BgColorBot.a );
        }
        else
        {
            glClearColor( 0.25f, 0.25f, 0.35f, 1.0f );
        }

        glClearDepth( 1.0f );
        glClearStencil( 0x00 );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
    }

    return requestAnother;
}


void ASSEMBLY_3D_MANAGER::RequestReload()
{
    for( auto& renderer : m_instanceRenderers )
    {
        if( renderer )
            renderer->ReloadRequest();
    }
}


void ASSEMBLY_3D_MANAGER::SetInstancesWindowSize( const wxSize& aSize )
{
    for( auto& renderer : m_instanceRenderers )
    {
        if( renderer )
            renderer->SetCurWindowSize( aSize );
    }
}
