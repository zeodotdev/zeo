/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

// kiglad must precede any other include that pulls in system gl.h,
// otherwise glad refuses to load. render_3d_opengl.h transitively
// includes kiglad, but other headers below (board.h, wx, etc.) may
// drag system gl.h in first on macOS if we don't pre-empt them.
#include <kicad_gl/kiglad.h>

#include "3d_viewer_assembly.h"

#include "3d_canvas/board_adapter.h"
#include "3d_viewer/eda_3d_viewer_settings.h"
#include "3d_canvas/eda_3d_canvas.h"
#include "3d_rendering/opengl/mate_gizmo.h"
#include "3d_rendering/opengl/render_3d_opengl.h"
#include <gal/3d/camera.h>
#include <board.h>
#include <board_design_settings.h>
#include <board_stackup_manager/board_stackup.h>
#include <footprint.h>
#include <multi_board/sub_project_board_loader.h>
#include <pad.h>
#include <pgm_base.h>
#include <project.h>
#include <project/multi_board_scan.h>
#include <project/project_file.h>
#include <project_pcb.h>
#include <settings/settings_manager.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <tuple>


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


// Copy the user-visible substructs from the canvas-wide settings into a
// per-board cfg. EDA_3D_VIEWER_SETTINGS itself isn't copyable (its
// JSON_SETTINGS base owns vector<unique_ptr<JSON_PARAM>> with
// addresses bound to the OWNER's member fields), but each substruct
// is plain-data and assigns cleanly. Callers don't Save() through the
// per-instance cfg — it's render-state only.
static void copyRenderState( EDA_3D_VIEWER_SETTINGS&       aDst,
                             const EDA_3D_VIEWER_SETTINGS& aSrc )
{
    aDst.m_AuiPanels        = aSrc.m_AuiPanels;
    aDst.m_Render           = aSrc.m_Render;
    aDst.m_Camera           = aSrc.m_Camera;
    aDst.m_UseStackupColors = aSrc.m_UseStackupColors;
    aDst.m_LayerPresets     = aSrc.m_LayerPresets;
    aDst.m_CurrentPreset    = aSrc.m_CurrentPreset;
}


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
 * True when a sub-board's PCB has nothing meaningful to render — no
 * footprints AND no board outline (Edge.Cuts) geometry. The 3D
 * pipeline falls back to a unit-cube placeholder for these, which is
 * confusing in a multi-board view; hide them by default and let the
 * user toggle them on if they want to see the placeholder.
 */
bool isPlaceholderBoard( const BOARD* aBoard )
{
    if( !aBoard )
        return true;

    if( !aBoard->Footprints().empty() )
        return false;

    BOX2I outlineBox = aBoard->GetBoardEdgesBoundingBox();

    if( outlineBox.GetWidth() > 0 && outlineBox.GetHeight() > 0 )
        return false;

    return true;
}


// ===== M6.E phase-3 mesh-level collision helpers =====

/// Single triangle in world-mm space, with cached AABB for the
/// pair-test broad phase.
struct WorldTri
{
    glm::vec3 v0, v1, v2;
    glm::vec3 aabbMin, aabbMax;
};


/// Möller-Trumbore segment-vs-triangle test. Returns true iff the
/// segment p0→p1 (parameter t ∈ [0, 1]) hits the triangle U0,U1,U2.
/// Used as the per-edge test in our triangle-triangle intersection
/// routine — six of these calls (3 edges of each triangle vs. the
/// other) cover all non-degenerate intersection cases.
inline bool segmentHitsTriangle( const glm::vec3& p0, const glm::vec3& p1,
                                  const glm::vec3& U0, const glm::vec3& U1,
                                  const glm::vec3& U2 )
{
    const glm::vec3 dir = p1 - p0;
    const glm::vec3 E1  = U1 - U0;
    const glm::vec3 E2  = U2 - U0;
    const glm::vec3 P   = glm::cross( dir, E2 );
    const float     det = glm::dot( E1, P );

    if( std::abs( det ) < 1e-9f )
        return false;   // segment parallel to triangle's plane

    const float     invDet = 1.0f / det;
    const glm::vec3 T      = p0 - U0;
    const float     u      = glm::dot( T, P ) * invDet;

    if( u < 0.0f || u > 1.0f )
        return false;

    const glm::vec3 Q = glm::cross( T, E1 );
    const float     v = glm::dot( dir, Q ) * invDet;

    if( v < 0.0f || u + v > 1.0f )
        return false;

    const float t = glm::dot( E2, Q ) * invDet;

    // Allow tiny numerical slack at the segment endpoints so a
    // triangle pair that exactly touches at a shared vertex / edge
    // still registers as a hit.
    return t >= -1e-7f && t <= 1.0f + 1e-7f;
}


/// True when triangle (V0,V1,V2) and triangle (U0,U1,U2) intersect
/// in 3D. Uses the symmetric "edges of one vs the other's plane"
/// approach — six segment-triangle tests per pair. Misses the
/// degenerate "fully coplanar with overlap and no edge crossing"
/// case, which doesn't occur in practice for arbitrary 3D-model
/// connector meshes.
inline bool trianglesIntersect( const glm::vec3& V0, const glm::vec3& V1, const glm::vec3& V2,
                                 const glm::vec3& U0, const glm::vec3& U1, const glm::vec3& U2 )
{
    return segmentHitsTriangle( V0, V1, U0, U1, U2 )
        || segmentHitsTriangle( V1, V2, U0, U1, U2 )
        || segmentHitsTriangle( V2, V0, U0, U1, U2 )
        || segmentHitsTriangle( U0, U1, V0, V1, V2 )
        || segmentHitsTriangle( U1, U2, V0, V1, V2 )
        || segmentHitsTriangle( U2, U0, V0, V1, V2 );
}


/// Squared distance from a point to a triangle in 3D — closed-form
/// from Eberly's "Distance Between Point and Triangle in 3D" (the
/// 7-region barycentric construction). We only care about the
/// distance, not the closest point, so the return is squared so
/// callers can compare against a margin² without a sqrt in the inner
/// loop.
inline float pointTriangleDistSq( const glm::vec3& p,
                                   const glm::vec3& a, const glm::vec3& b, const glm::vec3& c )
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;

    const float d1 = glm::dot( ab, ap );
    const float d2 = glm::dot( ac, ap );

    if( d1 <= 0.0f && d2 <= 0.0f )
        return glm::dot( ap, ap );   // closest is vertex a

    const glm::vec3 bp = p - b;
    const float     d3 = glm::dot( ab, bp );
    const float     d4 = glm::dot( ac, bp );

    if( d3 >= 0.0f && d4 <= d3 )
        return glm::dot( bp, bp );   // closest is vertex b

    const float vc = d1 * d4 - d3 * d2;

    if( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f )
    {
        const float     v_   = d1 / ( d1 - d3 );
        const glm::vec3 q    = a + v_ * ab;
        const glm::vec3 diff = p - q;
        return glm::dot( diff, diff );
    }

    const glm::vec3 cp = p - c;
    const float     d5 = glm::dot( ab, cp );
    const float     d6 = glm::dot( ac, cp );

    if( d6 >= 0.0f && d5 <= d6 )
        return glm::dot( cp, cp );   // closest is vertex c

    const float vb = d5 * d2 - d1 * d6;

    if( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f )
    {
        const float     w_   = d2 / ( d2 - d6 );
        const glm::vec3 q    = a + w_ * ac;
        const glm::vec3 diff = p - q;
        return glm::dot( diff, diff );
    }

    const float va = d3 * d6 - d5 * d4;

    if( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f )
    {
        const float     w_   = ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) );
        const glm::vec3 q    = b + w_ * ( c - b );
        const glm::vec3 diff = p - q;
        return glm::dot( diff, diff );
    }

    // Closest point is inside the triangle face → project onto its plane.
    const float denom = 1.0f / ( va + vb + vc );
    const float v_    = vb * denom;
    const float w_    = vc * denom;
    const glm::vec3 q    = a + ab * v_ + ac * w_;
    const glm::vec3 diff = p - q;
    return glm::dot( diff, diff );
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

    // Apply user-configured local env vars (kicad_common.json
    // `environment.vars`) before the per-instance S3D_CACHE resolvers
    // expand any model paths. PCB_EDIT_FRAME does this at frame init
    // via PythonSyncEnvironmentVariables, but in a fresh session the
    // assembly 3D viewer can be the first frame to resolve
    // `${KIPRJMOD}/...` paths — and PGM_BASE::loadCommonSettings
    // explicitly skips KIPRJMOD on app startup, so the user's saved
    // value (e.g. a stock 3D-models directory) only takes effect once
    // some pcbnew sibling has run. Without this call the multi-board
    // 3D viewer's first open silently drops every model whose path
    // depends on the user-configured KIPRJMOD fallback. (Per
    // SETTINGS_MANAGER::LoadProject only the active-project path is
    // pushed into the env when sub-projects are loaded with
    // aSetActive=false, which doesn't match the user's stock-models
    // intent for `${KIPRJMOD}`.)
    if( PgmOrNull() )
        Pgm().SetLocalEnvVariables();

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

        // Load the sub-project (non-active) so its KIPRJMOD context is
        // available for 3D-model path resolution. Without this, every
        // sub-board's models that reference `${KIPRJMOD}` resolve
        // against the CONTAINER's directory and fail to load. The
        // PROJECT is owned by SETTINGS_MANAGER, not us, so we just
        // hold a pointer.
        if( instance.board && proFile.FileExists() )
        {
            SETTINGS_MANAGER& mgr     = Pgm().GetSettingsManager();
            const wxString    proPath = proFile.GetFullPath();

            // SETTINGS_MANAGER::LoadProject is a no-op if already
            // loaded (e.g. user has the sub-project open in pcbnew).
            mgr.LoadProject( proPath, /*aSetActive=*/false );
            instance.subProject = mgr.GetProject( proPath );

            if( instance.subProject )
            {
                // Reference-only so we don't reset design-settings paths
                // (the BOARD already has them from its file load) — we
                // just need GetProject() to return something with the
                // right GetProjectPath() so KIPRJMOD resolution works.
                instance.board->SetProject( instance.subProject,
                                            /*aReferenceOnly=*/true );
            }
        }

        m_boardInstances.push_back( std::move( instance ) );
    }

    // Default flat layout; user can switch via PANEL_3D_ASSEMBLY.
    ArrangeBoards( BOARD_LAYOUT_MODE::FLAT, 20.0f );

    // M6.G: persisted per-instance state on the container `.kicad_pro`
    // overrides the FLAT default for any sub-project the user has
    // already arranged.
    applyPersistedInstanceStates();

    // Per-project collision threshold (mm). Same persistence channel.
    if( m_project )
    {
        const PROJECT_FILE& pf = m_project->GetProjectFile();

        if( pf.IsMultiBoardContainer() )
            m_collisionThresholdMm = static_cast<float>( pf.GetCollisionThresholdMm() );
    }

    // Hide sub-boards that have no PCB content yet (no footprints and
    // no Edge.Cuts outline). Without this the 3D pipeline draws a
    // unit-cube placeholder for them, which is confusing in an
    // assembly view. The user can still toggle them on via the panel
    // if they want to see the placeholder.
    for( BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        if( inst.board && isPlaceholderBoard( inst.board.get() ) )
            inst.visible = false;
    }
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
    m_lastOverlapBoxes.clear();
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


const BOARD_3D_INSTANCE* ASSEMBLY_3D_MANAGER::FindInstanceForItem( const BOARD_ITEM* aItem ) const
{
    if( !aItem )
        return nullptr;

    const BOARD* itemBoard = aItem->GetBoard();

    if( !itemBoard )
        return nullptr;

    for( const auto& inst : m_boardInstances )
    {
        if( inst.board.get() == itemBoard )
            return &inst;
    }

    return nullptr;
}


void ASSEMBLY_3D_MANAGER::SetSelectedItem( BOARD_ITEM* aItem )
{
    // Resolve which sub-board owns the item (if any). Then push the
    // selection into THAT instance's RENDER_3D_OPENGL and clear the
    // selection on every other instance so only one footprint is
    // highlighted at a time across the whole assembly.
    const BOARD* targetBoard = aItem ? aItem->GetBoard() : nullptr;

    KIID matchingInstanceUuid;

    for( size_t i = 0; i < m_instanceRenderers.size(); ++i )
    {
        if( !m_instanceRenderers[i] )
            continue;

        const BOARD* instBoard = ( i < m_boardInstances.size() )
                                      ? m_boardInstances[i].board.get()
                                      : nullptr;

        if( instBoard && instBoard == targetBoard )
        {
            m_instanceRenderers[i]->SetCurrentSelectedItem( aItem );
            matchingInstanceUuid = m_boardInstances[i].uuid;
        }
        else
        {
            m_instanceRenderers[i]->SetCurrentSelectedItem( nullptr );
        }
    }

    // Track parent-board selection too so the manipulation gizmo
    // (MOON-1331 phase 4a) attaches to the right sub-board.
    if( matchingInstanceUuid != niluuid )
        m_selectedBoardUuid = matchingInstanceUuid;
    else if( !aItem )
        m_selectedBoardUuid = KIID( 0 );   // explicit clear on empty-space click
}


void ASSEMBLY_3D_MANAGER::SetSelectedBoardInstance( const KIID& aInstanceUuid )
{
    m_selectedBoardUuid = aInstanceUuid;
}


void ASSEMBLY_3D_MANAGER::ClearSelectedBoardInstance()
{
    m_selectedBoardUuid = KIID( 0 );
}


void ASSEMBLY_3D_MANAGER::SetBoardPosition( const KIID& aInstanceUuid, const SFVEC3F& aPosition )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
    {
        inst->position = aPosition;
        persistInstanceState( *inst );
    }
}


void ASSEMBLY_3D_MANAGER::SetBoardRotation( const KIID& aInstanceUuid, const SFVEC3F& aRotation )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
    {
        inst->rotation = aRotation;
        persistInstanceState( *inst );
    }
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
    PersistAllInstances();
}


void ASSEMBLY_3D_MANAGER::PersistAllInstances()
{
    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
        persistInstanceState( inst );
}


void ASSEMBLY_3D_MANAGER::SetBoardVisible( const KIID& aInstanceUuid, bool aVisible )
{
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
    {
        const bool wasVisible = inst->visible;
        inst->visible = aVisible;
        persistInstanceState( *inst );

        // Visibility flip changes the assembly bounding box (boards
        // entering/leaving the visible set move the centerShared
        // offset). The camera was framed for the old composite
        // extent; an enlarged composite can fall outside the view
        // frustum (MOON-1369: "everything disappears when re-enabling
        // a board"). Re-fit on every visibility flip so the canvas
        // always shows the current visible set.
        if( wasVisible != aVisible )
            m_cameraFitPending = true;
    }
}


void ASSEMBLY_3D_MANAGER::SetBoardTransparent( const KIID& aInstanceUuid, bool aTransparent,
                                                float aOpacity )
{
    // Transparency UI was removed (the per-instance alpha approach
    // cleared the canvas instead of fading boards). The state is still
    // accepted + persisted so existing project files round-trip
    // cleanly, but it has no visible effect until a working
    // implementation lands.
    if( BOARD_3D_INSTANCE* inst = GetBoardInstance( aInstanceUuid ) )
    {
        inst->transparent = aTransparent;
        inst->opacity = aOpacity;
        persistInstanceState( *inst );
    }
}


void ASSEMBLY_3D_MANAGER::ShowAllBoards()
{
    bool anyChange = false;

    for( auto& inst : m_boardInstances )
    {
        if( !inst.visible )
        {
            inst.visible = true;
            persistInstanceState( inst );
            anyChange = true;
        }
    }

    if( anyChange )
        m_cameraFitPending = true;
}


void ASSEMBLY_3D_MANAGER::HideAllBoards()
{
    bool anyChange = false;

    for( auto& inst : m_boardInstances )
    {
        if( inst.visible )
        {
            inst.visible = false;
            persistInstanceState( inst );
            anyChange = true;
        }
    }

    if( anyChange )
        m_cameraFitPending = true;
}


void ASSEMBLY_3D_MANAGER::MateConnectors()
{
    wxLogMessage( wxT( "[MATE] MateConnectors() entry: project=%p instances=%zu" ),
                  static_cast<void*>( m_project ), m_boardInstances.size() );

    if( !m_project || m_boardInstances.size() < 2 )
    {
        wxLogMessage( wxT( "[MATE] early return: no project or <2 instances" ) );
        return;
    }

    m_lastMateResiduals.clear();
    m_lastIcpPadPins.clear();

    // M6.D-phase-1 pipeline: aggregate cross-board nets into a board
    // mate graph, then BFS-place every reachable instance from the
    // highest-degree anchor with primary-mate-wins per board edge.
    // Secondary mate pairs and graph-cycle back-edges become entries
    // in m_lastMateResiduals (consumed by M6.E DRC panel).
    std::vector<MATE_EDGE> edges = BuildMateGraph();

    wxLogMessage( wxT( "[MATE] BuildMateGraph: %zu edges, "
                       "cross_board_nets=%zu, custom_mates=%zu" ),
                  edges.size(),
                  m_project->GetProjectFile().GetCrossBoardNets().size(),
                  m_project->GetProjectFile().GetCustomMates().size() );

    if( edges.empty() )
    {
        wxLogMessage( wxT( "[MATE] no edges — boards stay at current positions" ) );
        return;
    }

    SolveMatePoses( edges );

    wxLogMessage( wxT( "[MATE] SolveMatePoses done — instance positions:" ) );

    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        wxLogMessage( wxT( "[MATE]   inst '%s' uuid=%s pos=(%.2f,%.2f,%.2f) rot=(%.1f,%.1f,%.1f)" ),
                      inst.displayName, inst.uuid.AsString(),
                      inst.position.x, inst.position.y, inst.position.z,
                      inst.rotation.x, inst.rotation.y, inst.rotation.z );
    }

    // SolveMatePoses mutates BOARD_3D_INSTANCE::position / rotation
    // directly via pointers; without persisting here the snapped poses
    // would only live in memory and revert to the FLAT default the
    // next time the 3D viewer reopens (LoadProjectBoards →
    // ArrangeBoards). Persist every visible instance so the
    // .kicad_pro picks up the new poses through the SetAssemblyInstances
    // notify chain and saves them on the next project save.
    PersistAllInstances();

    m_state.mateConnectors = true;
}


std::vector<MATE_EDGE> ASSEMBLY_3D_MANAGER::BuildMateGraph() const
{
    std::vector<MATE_EDGE> edges;

    if( !m_project )
        return edges;

    const PROJECT_FILE& projectFile = m_project->GetProjectFile();

    // Index board instances by their sub-project UUID for O(1) lookup
    // when resolving net endpoints. Multiple instances per sub-project
    // (cloned boards) are not supported in phase-1 — first-wins.
    std::map<KIID, const BOARD_3D_INSTANCE*> instBySubProj;

    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        if( !inst.board )
            continue;

        instBySubProj.emplace( inst.subProjectUuid, &inst );
    }

    // Mate-pair key: (instanceA UUID, footprintRefA, instanceB UUID,
    // footprintRefB) in canonical order so the same physical pair
    // aggregates regardless of which net's endpoint visited first.
    using PairKey = std::tuple<KIID, wxString, KIID, wxString>;
    std::map<PairKey, MATE_PAIR> pairMap;

    // For each (cross-board net, footprint-pair) combination, gather
    // the unique pin numbers on each side of the connector, then
    // match them 1-to-1 by sorted stored position. Multi-endpoint
    // power nets (GND, VCC, etc.) commonly have N pads on one
    // connector and M on the other and the netlist gives no
    // information to identify which physical GND-A pin should mate to
    // which GND-B pin — but those pads have a natural 1↔1 mapping by
    // position (left-A ↔ left-B, right-A ↔ right-B). Sorting by
    // (X, Y) on each side and pairing in order gives:
    //   • the visualizer draws min(N, M) parallel lines rather than
    //     one diagonal or N×M rats-nest;
    //   • Kabsch sees one constraint per matched pair, all consistent;
    //   • pinCount keeps accumulating per cross-board edge so primary-
    //     pair-by-pin-weight ordering is unchanged.
    //
    // Pin position lookup table: (instance UUID, footprint ref, pin
    // number) → stored BIU position. Cached for the matching pass so
    // we don't refetch the FOOTPRINT/PAD per pair.
    using PinKey = std::tuple<KIID, wxString, wxString>;
    std::map<PinKey, VECTOR2I> pinPosCache;

    auto resolvePinPos = [&]( const KIID& aInstUuid,
                               const wxString& aRef,
                               const wxString& aPin,
                               VECTOR2I& aOutPos ) -> bool
    {
        PinKey k{ aInstUuid, aRef, aPin };
        auto   cached = pinPosCache.find( k );

        if( cached != pinPosCache.end() )
        {
            aOutPos = cached->second;
            return true;
        }

        auto instIt = instBySubProj.end();

        for( auto it = instBySubProj.begin(); it != instBySubProj.end(); ++it )
        {
            if( it->second->uuid == aInstUuid )
            {
                instIt = it;
                break;
            }
        }

        if( instIt == instBySubProj.end() )
            return false;

        FOOTPRINT* fp = findFootprintByRef( instIt->second->board.get(), aRef );

        if( !fp )
            return false;

        PAD* pad = fp->FindPadByNumber( aPin );

        if( !pad )
            return false;

        aOutPos = pad->GetPosition();
        pinPosCache.emplace( k, aOutPos );
        return true;
    };

    // Per-(net-group, pair-key) candidate gathering. Cross-board nets
    // are aggregated by NAME rather than UUID so that power rails like
    // GND — which the user often models as several distinct nets each
    // named "GND" with 1 pin per side — collapse into one virtual
    // group with N+M pads. Sort-based matching across the group then
    // produces parallel lines (left-A↔left-B, right-A↔right-B) instead
    // of whichever per-net pin assignment the schematic happened to
    // emit (which often reverses across boards and X-crosses the
    // gizmo). Empty net names fall back to uuid so unnamed nets stay
    // distinct.
    struct NetPairCandidates
    {
        std::set<wxString> pinsA;     // distinct pins on the canonical-A side
        std::set<wxString> pinsB;     // distinct pins on the canonical-B side
    };

    auto netGroupKey = []( const MB_CROSS_BOARD_NET& aNet ) -> wxString
    {
        return aNet.name.IsEmpty()
                       ? wxT( "uuid:" ) + aNet.uuid.AsString()
                       : wxT( "name:" ) + aNet.name;
    };

    std::map<std::tuple<wxString, PairKey>, NetPairCandidates> candidates;

    for( const MB_CROSS_BOARD_NET& net : projectFile.GetCrossBoardNets() )
    {
        if( net.endpoints.size() < 2 )
            continue;

        const wxString groupKey = netGroupKey( net );

        for( size_t i = 0; i < net.endpoints.size(); i++ )
        {
            for( size_t j = i + 1; j < net.endpoints.size(); j++ )
            {
                const MB_CROSS_BOARD_NET_ENDPOINT& epA = net.endpoints[i];
                const MB_CROSS_BOARD_NET_ENDPOINT& epB = net.endpoints[j];

                if( epA.subProjectUuid == epB.subProjectUuid )
                    continue;   // same board — not a cross-board mate

                auto itA = instBySubProj.find( epA.subProjectUuid );
                auto itB = instBySubProj.find( epB.subProjectUuid );

                if( itA == instBySubProj.end() || itB == instBySubProj.end() )
                    continue;   // sub-board didn't load; mate dropped

                // Canonical ordering: lower instance UUID first; ties
                // broken by footprint ref. Stable across re-opens.
                const BOARD_3D_INSTANCE* iA = itA->second;
                const BOARD_3D_INSTANCE* iB = itB->second;
                wxString refA = epA.componentRef;
                wxString refB = epB.componentRef;
                wxString pinA = epA.pinNumber;
                wxString pinB = epB.pinNumber;

                bool swap = ( iB->uuid < iA->uuid )
                            || ( iA->uuid == iB->uuid && refB < refA );

                if( swap )
                {
                    std::swap( iA, iB );
                    std::swap( refA, refB );
                    std::swap( pinA, pinB );
                }

                PairKey key{ iA->uuid, refA, iB->uuid, refB };
                auto    [it, inserted] = pairMap.emplace( key, MATE_PAIR{} );

                if( inserted )
                {
                    it->second.instanceA     = iA->uuid;
                    it->second.instanceB     = iB->uuid;
                    it->second.footprintRefA = refA;
                    it->second.footprintRefB = refB;
                    it->second.pinCount      = 0;
                }

                it->second.pinCount++;

                auto& netCand = candidates[std::make_tuple( groupKey, key )];
                netCand.pinsA.insert( pinA );
                netCand.pinsB.insert( pinB );
            }
        }
    }

    // Resolve the candidates into padPins entries. For each
    // (net-group, pair-key), sort each side's pin set by stored (X, Y)
    // lex and pair in order — geometric 1-to-1 matching that handles
    // multi-pad nets (and aggregated same-named nets like GND) without
    // producing crossed gizmo lines.
    for( auto& [netPairKey, cand] : candidates )
    {
        const auto& [groupName, pairKey] = netPairKey;
        auto pairIt = pairMap.find( pairKey );

        if( pairIt == pairMap.end() )
            continue;

        const KIID&      instA = std::get<0>( pairKey );
        const wxString&  refA  = std::get<1>( pairKey );
        const KIID&      instB = std::get<2>( pairKey );
        const wxString&  refB  = std::get<3>( pairKey );

        // Resolve pin positions; drop any pin we can't look up (e.g.,
        // schematic references a pin that doesn't exist on the
        // resolved footprint — bad symbol pinout).
        std::vector<std::pair<wxString, VECTOR2I>> aPinsResolved;
        std::vector<std::pair<wxString, VECTOR2I>> bPinsResolved;

        for( const wxString& p : cand.pinsA )
        {
            VECTOR2I pos;
            if( resolvePinPos( instA, refA, p, pos ) )
                aPinsResolved.emplace_back( p, pos );
        }

        for( const wxString& p : cand.pinsB )
        {
            VECTOR2I pos;
            if( resolvePinPos( instB, refB, p, pos ) )
                bPinsResolved.emplace_back( p, pos );
        }

        auto byXY = []( const std::pair<wxString, VECTOR2I>& a,
                         const std::pair<wxString, VECTOR2I>& b )
        {
            return std::tie( a.second.x, a.second.y )
                   < std::tie( b.second.x, b.second.y );
        };

        std::sort( aPinsResolved.begin(), aPinsResolved.end(), byXY );
        std::sort( bPinsResolved.begin(), bPinsResolved.end(), byXY );

        const size_t n = std::min( aPinsResolved.size(), bPinsResolved.size() );

        for( size_t k = 0; k < n; k++ )
        {
            pairIt->second.padPins.emplace_back( aPinsResolved[k].first,
                                                 bPinsResolved[k].first );
            pairIt->second.padPinGroups.push_back( groupName );
        }
    }

    // ----- World-space ICP rematch within electrical groups -----
    //
    // Sort-based padPins above pair pads by stored (X, Y), which is
    // the correct first guess but disagrees with the post-mate world
    // alignment when same-named nets (e.g. several "GND") have N pads
    // per side and the mating pose flips the connector's geometry.
    // For each pair, project all pad-pins of both sides to world XY
    // under the CURRENT instance poses, then for each electrical group
    // do greedy nearest-neighbour matching A↔B. This produces the same
    // correspondence MateConnectors would land at (Kabsch settles to
    // the world-NN pairing in stable cases) but works on first viewer
    // open before MateConnectors has been invoked — so the gizmo's pad
    // lines are correct from the start.
    //
    // The cache from a recent MateConnectors run takes precedence over
    // this fresh rematch (PlaceChildOnParent's Kabsch+RANSAC has more
    // robust outlier handling than greedy NN), so a cached result wins
    // and the world-space rematch only fills in for pairs without a
    // cache entry.
    for( auto& [pkey, mp] : pairMap )
    {
        auto cached = m_lastIcpPadPins.find( pkey );

        if( cached != m_lastIcpPadPins.end()
            && cached->second.size() == mp.padPins.size() )
        {
            // Use the cached PlaceChildOnParent result. Same group-
            // remap logic as before so padPinGroups stays parallel.
            std::map<wxString, wxString> groupByPinB;

            for( size_t i = 0; i < mp.padPins.size(); i++ )
                groupByPinB[mp.padPins[i].second] = mp.padPinGroups[i];

            std::vector<wxString> newGroups;
            newGroups.reserve( cached->second.size() );

            for( const auto& [pa, pb] : cached->second )
            {
                auto gIt = groupByPinB.find( pb );
                newGroups.push_back( gIt != groupByPinB.end() ? gIt->second
                                                              : wxString() );
            }

            mp.padPins      = cached->second;
            mp.padPinGroups = std::move( newGroups );
            continue;
        }

        // No cache — do world-space rematch from current poses.
        if( mp.padPins.size() < 2 )
            continue;

        // Lookup parent + child instance + adapter via UUID. Anonymous
        // / un-loaded instances are skipped — their adapters might not
        // be initialized yet, in which case we leave the sort defaults
        // in place.
        auto findInstAndAdapter = [&]( const KIID& aUuid,
                                       const BOARD_3D_INSTANCE*& aOutInst,
                                       const BOARD_ADAPTER*& aOutAdapter )
        {
            aOutInst = nullptr;
            aOutAdapter = nullptr;

            for( size_t i = 0; i < m_boardInstances.size(); i++ )
            {
                if( m_boardInstances[i].uuid == aUuid )
                {
                    aOutInst = &m_boardInstances[i];

                    if( i < m_instanceAdapters.size() )
                        aOutAdapter = m_instanceAdapters[i].get();

                    return;
                }
            }
        };

        const BOARD_3D_INSTANCE* instA = nullptr;
        const BOARD_3D_INSTANCE* instB = nullptr;
        const BOARD_ADAPTER*     adapterA = nullptr;
        const BOARD_ADAPTER*     adapterB = nullptr;

        findInstAndAdapter( mp.instanceA, instA, adapterA );
        findInstAndAdapter( mp.instanceB, instB, adapterB );

        if( !instA || !instB || !adapterA || !adapterB )
            continue;

        FOOTPRINT* fpA = findFootprintByRef( instA->board.get(),
                                              mp.footprintRefA );
        FOOTPRINT* fpB = findFootprintByRef( instB->board.get(),
                                              mp.footprintRefB );

        if( !fpA || !fpB )
            continue;

        // Project a stored BIU point into shared world XY using the
        // same pose math projectFootprintCentroidToWorld applies.
        auto projectXY = [&]( const BOARD_3D_INSTANCE& aInst,
                              const BOARD_ADAPTER&     aAdapter,
                              const VECTOR2I&          aStoredBIU ) -> glm::vec2
        {
            const double localFactor = aAdapter.BiuTo3dUnits();
            const double scaleFactor = ( localFactor != 0.0 )
                                                ? m_sharedBiuTo3Dunits / localFactor
                                                : 1.0;
            const float  sharedF     = static_cast<float>( m_sharedBiuTo3Dunits );
            const float  biuPerMm    = 1.0e6f;

            const glm::vec3 local3D( static_cast<float>( aStoredBIU.x ) * localFactor,
                                     -static_cast<float>( aStoredBIU.y ) * localFactor,
                                     0.0f );
            const glm::vec3 localShared = local3D * static_cast<float>( scaleFactor );

            const SFVEC3F   lc = aAdapter.GetBoardCenter();
            const glm::vec3 localCenterShared( lc.x * static_cast<float>( scaleFactor ),
                                               lc.y * static_cast<float>( scaleFactor ),
                                               lc.z * static_cast<float>( scaleFactor ) );

            glm::mat4 R( 1.0f );
            R = glm::rotate( R, glm::radians( aInst.rotation.z ),
                             glm::vec3( 0, 0, 1 ) );
            R = glm::rotate( R, glm::radians( aInst.rotation.y ),
                             glm::vec3( 0, 1, 0 ) );
            R = glm::rotate( R, glm::radians( aInst.rotation.x ),
                             glm::vec3( 1, 0, 0 ) );

            const glm::vec3 shifted  = localShared - localCenterShared;
            const glm::vec4 rotated  = R * glm::vec4( shifted, 1.0f );
            const glm::vec3 replaced = glm::vec3( rotated ) + localCenterShared;

            const glm::vec3 instShared(  aInst.position.x * biuPerMm * sharedF,
                                        -aInst.position.y * biuPerMm * sharedF,
                                         aInst.position.z * biuPerMm * sharedF );

            return glm::vec2( replaced.x + instShared.x,
                              replaced.y + instShared.y );
        };

        // Group padPin indexes by electrical group key. Single-entry
        // groups need no rematching (unique signal correspondence).
        std::map<wxString, std::vector<size_t>> idxByGroup;

        for( size_t i = 0; i < mp.padPins.size(); i++ )
        {
            const wxString& g = ( i < mp.padPinGroups.size() )
                                        ? mp.padPinGroups[i]
                                        : wxString();

            if( !g.IsEmpty() )
                idxByGroup[g].push_back( i );
        }

        for( auto& [grp, idxs] : idxByGroup )
        {
            if( idxs.size() < 2 )
                continue;

            // Snapshot the original A pin / B pin / B world XY so
            // greedy NN can swap freely.
            const size_t           N = idxs.size();
            std::vector<wxString>  originalPinB( N );
            std::vector<glm::vec2> aWorld( N );
            std::vector<glm::vec2> bWorld( N );

            for( size_t k = 0; k < N; k++ )
            {
                const auto& pp = mp.padPins[idxs[k]];
                originalPinB[k] = pp.second;

                PAD* padA = fpA->FindPadByNumber( pp.first );
                PAD* padB = fpB->FindPadByNumber( pp.second );

                if( !padA || !padB )
                {
                    aWorld[k] = glm::vec2( 0.0f );
                    bWorld[k] = glm::vec2( 1e9f, 1e9f );    // sentinel
                    continue;
                }

                aWorld[k] = projectXY( *instA, *adapterA, padA->GetPosition() );
                bWorld[k] = projectXY( *instB, *adapterB, padB->GetPosition() );
            }

            // Greedy NN: each A pad picks its closest unused B pad.
            std::vector<bool> used( N, false );

            for( size_t k = 0; k < N; k++ )
            {
                size_t bestJ = SIZE_MAX;
                float  bestD = std::numeric_limits<float>::max();

                for( size_t j = 0; j < N; j++ )
                {
                    if( used[j] )
                        continue;

                    const float d = glm::length( aWorld[k] - bWorld[j] );

                    if( d < bestD )
                    {
                        bestD = d;
                        bestJ = j;
                    }
                }

                if( bestJ == SIZE_MAX )
                    continue;

                used[bestJ] = true;

                // Write the rematched pinB back into padPins. pinA
                // stays put — we only swap the B side.
                mp.padPins[idxs[k]].second = originalPinB[bestJ];
            }
        }
    }

    // M6.D-phase-2: layer user-declared CUSTOM_MATE overrides on top
    // of the auto-derived pair set. Apply order matters — DISABLED
    // erases first, then PRIMARY / SECONDARY decorate / create. Two
    // PRIMARY overrides on the same canonical pair → last-wins; two
    // PRIMARY on the same edge but different pairs → both keep
    // forcedPrimary=true and `PickPrimaryPair` picks via UUID
    // tiebreak (CONFLICT case the panel can warn on).
    std::map<PairKey, std::vector<KIID>> primaryOverridesByKey;

    for( const CUSTOM_MATE& cm : projectFile.GetCustomMates() )
    {
        auto itA = instBySubProj.find( cm.endA.subProjectUuid );
        auto itB = instBySubProj.find( cm.endB.subProjectUuid );

        if( itA == instBySubProj.end() || itB == instBySubProj.end() )
            continue;   // sub-board not loaded; surface as MISSING in UI

        // Validate footprint exists on its board so we don't seat a
        // child against a stale ref. CONNECTOR-type custom mates also
        // need the footprint to resolve to a real PAD list later in
        // PlaceChildOnParent — same lookup, same gate.
        if( !findFootprintByRef( itA->second->board.get(), cm.endA.footprintRef ) )
            continue;

        if( !findFootprintByRef( itB->second->board.get(), cm.endB.footprintRef ) )
            continue;

        // Canonical ordering matches the auto-derive loop above so a
        // user-declared mate keys to the same pair as the auto one.
        KIID     instA = itA->second->uuid;
        KIID     instB = itB->second->uuid;
        wxString refA  = cm.endA.footprintRef;
        wxString refB  = cm.endB.footprintRef;

        bool swap = ( instB < instA ) || ( instA == instB && refB < refA );

        if( swap )
        {
            std::swap( instA, instB );
            std::swap( refA, refB );
        }

        PairKey key{ instA, refA, instB, refB };

        switch( cm.role )
        {
        case CUSTOM_MATE_ROLE::DISABLED:
        {
            // Don't erase — the panel needs the row to remain so the
            // user can flip the disable back off. Just mark it. If
            // there's no auto pair (the override applies to a footprint
            // pair the netlist doesn't connect), insert one so the
            // disabled state is visible regardless.
            auto [it, inserted] = pairMap.emplace( key, MATE_PAIR{} );

            if( inserted )
            {
                it->second.instanceA     = instA;
                it->second.instanceB     = instB;
                it->second.footprintRefA = refA;
                it->second.footprintRefB = refB;
                it->second.pinCount      = 0;
            }

            it->second.disabled       = true;
            it->second.customMateUuid = cm.uuid;
            break;
        }

        case CUSTOM_MATE_ROLE::PRIMARY:
        case CUSTOM_MATE_ROLE::SECONDARY:
        {
            auto    [it, inserted] = pairMap.emplace( key, MATE_PAIR{} );

            if( inserted )
            {
                it->second.instanceA     = instA;
                it->second.instanceB     = instB;
                it->second.footprintRefA = refA;
                it->second.footprintRefB = refB;
                // pinCount=1 lets the pair survive in the edge graph
                // even when it's the only inhabitant of a fresh edge
                // (mounting hole between two boards with no shared
                // electrical net).
                it->second.pinCount      = 1;
            }

            it->second.customMateUuid = cm.uuid;
            it->second.nonElectrical  = ( cm.type != CUSTOM_MATE_TYPE::CONNECTOR );
            it->second.hasOffset      = cm.hasOffset;
            it->second.offsetTx       = cm.offsetTranslation.x;
            it->second.offsetTy       = cm.offsetTranslation.y;
            it->second.offsetTz       = cm.offsetTranslation.z;
            it->second.offsetRx       = cm.offsetRotation.x;
            it->second.offsetRy       = cm.offsetRotation.y;
            it->second.offsetRz       = cm.offsetRotation.z;

            if( cm.role == CUSTOM_MATE_ROLE::PRIMARY )
            {
                it->second.forcedPrimary = true;
                it->second.alignmentOnly = false;
                primaryOverridesByKey[key].push_back( cm.uuid );
            }
            else
            {
                it->second.alignmentOnly = true;
                // SECONDARY does NOT force primary — the auto-derived
                // pair (or another forcedPrimary on the same edge)
                // still wins primary selection.
            }
            break;
        }
        }
    }

    // Aggregate connector mate pairs into board edges.
    using EdgeKey = std::tuple<KIID, KIID>;
    std::map<EdgeKey, MATE_EDGE> edgeMap;

    for( const auto& [k, p] : pairMap )
    {
        EdgeKey ek{ p.instanceA, p.instanceB };
        auto    [it, inserted] = edgeMap.emplace( ek, MATE_EDGE{} );

        if( inserted )
        {
            it->second.instanceA   = p.instanceA;
            it->second.instanceB   = p.instanceB;
            it->second.totalWeight = 0;
        }

        it->second.pairs.push_back( p );
        it->second.totalWeight += p.pinCount;
    }

    edges.reserve( edgeMap.size() );

    for( auto& [k, e] : edgeMap )
        edges.push_back( std::move( e ) );

    // Sort each edge's pairs so the head of the list is the primary
    // candidate. Order is governed by:
    //   1. priority bump (lower = earlier; user-controlled via
    //      ShiftPairUp/Down — defaults to 0)
    //   2. alignmentOnly pairs ALWAYS sink to the bottom — secondary
    //      mates are never primary regardless of priority bump
    //   3. forcedPrimary first (legacy CUSTOM PRIMARY override)
    //   4. higher pinCount earlier (more electrical evidence)
    //   5. canonical refs as deterministic tiebreaker
    auto bumpFor = [this]( const MATE_PAIR& p ) -> int
    {
        wxString id = MakeMatePairId( p.instanceA, p.footprintRefA,
                                       p.instanceB, p.footprintRefB );
        auto it = m_pairPriorityBumps.find( id );
        return it == m_pairPriorityBumps.end() ? 0 : it->second;
    };

    for( MATE_EDGE& e : edges )
    {
        std::sort( e.pairs.begin(), e.pairs.end(),
                   [&]( const MATE_PAIR& a, const MATE_PAIR& b )
                   {
                       if( a.alignmentOnly != b.alignmentOnly )
                           return !a.alignmentOnly;   // non-secondary first

                       int ba = bumpFor( a );
                       int bb = bumpFor( b );

                       if( ba != bb )
                           return ba < bb;             // lower bump first

                       if( a.forcedPrimary != b.forcedPrimary )
                           return a.forcedPrimary;     // forced first

                       if( a.pinCount != b.pinCount )
                           return a.pinCount > b.pinCount;

                       return std::tie( a.footprintRefA, a.footprintRefB )
                              < std::tie( b.footprintRefA, b.footprintRefB );
                   } );
    }

    return edges;
}


void ASSEMBLY_3D_MANAGER::ShiftPairUp( const wxString& aPairId )
{
    if( aPairId.IsEmpty() )
        return;

    std::vector<MATE_EDGE> edges = BuildMateGraph();

    for( const MATE_EDGE& edge : edges )
    {
        for( size_t i = 0; i < edge.pairs.size(); i++ )
        {
            const MATE_PAIR& p = edge.pairs[i];
            wxString id = MakeMatePairId( p.instanceA, p.footprintRefA,
                                           p.instanceB, p.footprintRefB );

            if( id != aPairId )
                continue;

            if( i == 0 )
                return;   // already at top

            const MATE_PAIR& above   = edge.pairs[i - 1];
            wxString         aboveId = MakeMatePairId( above.instanceA, above.footprintRefA,
                                                       above.instanceB, above.footprintRefB );

            int aboveBump = m_pairPriorityBumps.count( aboveId )
                            ? m_pairPriorityBumps.at( aboveId ) : 0;
            // Sit one slot below the previous head — stable on repeated
            // clicks (won't drift further negative once at the top).
            m_pairPriorityBumps[aPairId] = aboveBump - 1;
            return;
        }
    }
}


void ASSEMBLY_3D_MANAGER::ShiftPairDown( const wxString& aPairId )
{
    if( aPairId.IsEmpty() )
        return;

    std::vector<MATE_EDGE> edges = BuildMateGraph();

    for( const MATE_EDGE& edge : edges )
    {
        for( size_t i = 0; i < edge.pairs.size(); i++ )
        {
            const MATE_PAIR& p = edge.pairs[i];
            wxString id = MakeMatePairId( p.instanceA, p.footprintRefA,
                                           p.instanceB, p.footprintRefB );

            if( id != aPairId )
                continue;

            if( i + 1 >= edge.pairs.size() )
                return;   // already at bottom

            const MATE_PAIR& below   = edge.pairs[i + 1];
            wxString         belowId = MakeMatePairId( below.instanceA, below.footprintRefA,
                                                       below.instanceB, below.footprintRefB );

            int belowBump = m_pairPriorityBumps.count( belowId )
                            ? m_pairPriorityBumps.at( belowId ) : 0;
            m_pairPriorityBumps[aPairId] = belowBump + 1;
            return;
        }
    }
}


const MATE_PAIR* ASSEMBLY_3D_MANAGER::PickPrimaryPair( const MATE_EDGE& aEdge ) const
{
    if( aEdge.pairs.empty() )
        return nullptr;

    // Phase-2 precedence: any pair with `forcedPrimary` (a custom
    // PRIMARY override) wins over the pinCount heuristic. Two forced
    // primaries on the same edge → CONFLICT — pick the lower
    // customMateUuid for determinism; the panel surfaces a warning.
    const MATE_PAIR* forced = nullptr;

    for( const MATE_PAIR& p : aEdge.pairs )
    {
        if( p.alignmentOnly || p.disabled )
            continue;   // SECONDARY custom mates never win primary; ditto disabled

        if( !p.forcedPrimary )
            continue;

        if( !forced || p.customMateUuid < forced->customMateUuid )
            forced = &p;
    }

    if( forced )
        return forced;

    // No custom override: highest pinCount wins; ties broken by
    // canonical (refA, refB) ordering for determinism across re-opens.
    const MATE_PAIR* best = nullptr;

    for( const MATE_PAIR& p : aEdge.pairs )
    {
        if( p.alignmentOnly || p.disabled )
            continue;   // skip SECONDARY-only and disabled pairs

        if( !best )
        {
            best = &p;
            continue;
        }

        if( p.pinCount > best->pinCount )
            best = &p;
        else if( p.pinCount == best->pinCount
                 && std::tie( p.footprintRefA, p.footprintRefB )
                            < std::tie( best->footprintRefA, best->footprintRefB ) )
            best = &p;
    }

    return best;
}


void ASSEMBLY_3D_MANAGER::SolveMatePoses( const std::vector<MATE_EDGE>& aEdges )
{
    // Adjacency list keyed by instance UUID.
    std::map<KIID, std::vector<const MATE_EDGE*>> adj;

    for( const MATE_EDGE& e : aEdges )
    {
        adj[e.instanceA].push_back( &e );
        adj[e.instanceB].push_back( &e );
    }

    if( adj.empty() )
        return;

    // Pick anchor: highest degree, ties broken by lowest UUID.
    KIID  anchorUuid;
    size_t bestDegree = 0;
    bool   anchorSet = false;

    for( const auto& [u, edgesAt] : adj )
    {
        if( !anchorSet
            || edgesAt.size() > bestDegree
            || ( edgesAt.size() == bestDegree && u < anchorUuid ) )
        {
            anchorUuid = u;
            bestDegree = edgesAt.size();
            anchorSet  = true;
        }
    }

    BOARD_3D_INSTANCE* anchor = GetBoardInstance( anchorUuid );

    if( !anchor )
        return;

    // Seat anchor at world origin with identity rotation. Other
    // boards' poses are computed relative to it.
    anchor->position = SFVEC3F( 0.0f, 0.0f, 0.0f );
    anchor->rotation = SFVEC3F( 0.0f, 0.0f, 0.0f );

    std::set<KIID>  placed;
    std::queue<KIID> bfs;
    placed.insert( anchorUuid );
    bfs.push( anchorUuid );

    while( !bfs.empty() )
    {
        KIID u = bfs.front();
        bfs.pop();

        const BOARD_3D_INSTANCE* parent = GetBoardInstance( u );

        if( !parent )
            continue;

        for( const MATE_EDGE* edge : adj[u] )
        {
            const KIID& v = ( edge->instanceA == u ) ? edge->instanceB : edge->instanceA;

            const MATE_PAIR* primary = PickPrimaryPair( *edge );

            if( !primary )
                continue;

            if( placed.count( v ) )
            {
                // Cycle-closing back-edge: every pair in this edge
                // becomes a residual since neither endpoint can be
                // moved without breaking an already-placed mate.
                for( const MATE_PAIR& p : edge->pairs )
                    m_lastMateResiduals.push_back( ComputeMateResidual( p ) );

                continue;
            }

            BOARD_3D_INSTANCE* child = GetBoardInstance( v );

            if( !child )
                continue;

            // Orient primary so endpoint A == parent (already placed).
            // The MATE_PAIR is keyed canonically; the parent isn't
            // necessarily on the A side. When we swap the instance /
            // footprint refs we MUST also swap the per-pin entries —
            // padPins[i].first refers to the canonical-A side, so
            // post-swap it'd point at the wrong board's pin numbers
            // and PlaceChildOnParent's pad lookups would silently
            // return the same-numbered pin on the wrong footprint
            // (Kabsch then fits to garbage and the child lands at a
            // bizarre rotation).
            MATE_PAIR primaryOriented = *primary;

            if( primaryOriented.instanceA != u )
            {
                std::swap( primaryOriented.instanceA, primaryOriented.instanceB );
                std::swap( primaryOriented.footprintRefA, primaryOriented.footprintRefB );

                for( auto& pp : primaryOriented.padPins )
                    std::swap( pp.first, pp.second );

                // padPinGroups is parallel to padPins but the group
                // identity is symmetric (same group for both ends of
                // the same cross-board net), so it doesn't change.
            }

            if( PlaceChildOnParent( *parent, *child, primaryOriented ) )
            {
                placed.insert( v );
                bfs.push( v );

                // Secondary pairs on this edge are alignment checks.
                for( const MATE_PAIR& p : edge->pairs )
                {
                    if( &p == primary )
                        continue;

                    m_lastMateResiduals.push_back( ComputeMateResidual( p ) );
                }
            }
            else
            {
                // Failed to resolve pads; entire edge dropped to
                // residuals so the panel can warn.
                for( const MATE_PAIR& p : edge->pairs )
                    m_lastMateResiduals.push_back( ComputeMateResidual( p ) );
            }
        }
    }

    // Boards never reached by BFS (disconnected from the mate graph)
    // keep whatever position they had from FLAT layout. No residual is
    // recorded — they're not over-constrained, just disjoint.
}


bool ASSEMBLY_3D_MANAGER::PlaceChildOnParent( const BOARD_3D_INSTANCE& aParent,
                                               BOARD_3D_INSTANCE&       aChild,
                                               const MATE_PAIR&         aPrimary )
{
    // Resolve both endpoints to (FOOTPRINT, PAD, side).
    FOOTPRINT* fpA = findFootprintByRef( aParent.board.get(), aPrimary.footprintRefA );
    FOOTPRINT* fpB = findFootprintByRef( aChild.board.get(),  aPrimary.footprintRefB );

    if( !fpA || !fpB )
        return false;

    // For the mate "center" we average the pad positions of every
    // cross-net endpoint that lands on this footprint. For phase-1
    // we'd need to iterate the project's cross-board nets again to
    // gather them; instead we use the footprint's own pad centroid
    // as a proxy — same answer when the connector's pads are the
    // only ones on the footprint, which is the common case.
    auto padCentroid = []( FOOTPRINT* fp ) -> VECTOR2I
    {
        // Sum in 64-bit. Single coords are 32-bit BIU (nm) but a board
        // placed far from the BIU origin (e.g. at 150 mm = 1.5×10⁸ nm)
        // accumulates past INT32_MAX (~2.15×10⁹) within ~14 pads, then
        // wraps. The wrap produced bizarre centroids ~18 mm for boards
        // actually centered around 147 mm and put the gizmos floating
        // far from the connectors.
        VECTOR2L sum( 0, 0 );
        int      count = 0;

        for( PAD* pad : fp->Pads() )
        {
            if( !pad->IsOnLayer( F_Cu ) && !pad->IsOnLayer( B_Cu ) )
                continue;

            const VECTOR2I& p = pad->GetPosition();
            sum.x += p.x;
            sum.y += p.y;
            count++;
        }

        if( count == 0 )
            return fp->GetPosition();

        return VECTOR2I( static_cast<int>( sum.x / count ),
                         static_cast<int>( sum.y / count ) );
    };

    // Side detection: majority of pads on F_Cu vs B_Cu. Mixed (through-
    // hole) connectors fall through to F_Cu with no warning since
    // through-hole headers commonly mate from either side; phase-2's
    // explicit override is the correct fix.
    auto dominantSide = []( FOOTPRINT* fp ) -> bool   // true = top (F_Cu)
    {
        int top = 0, bot = 0;

        for( PAD* pad : fp->Pads() )
        {
            if( pad->IsOnLayer( F_Cu ) )
                top++;

            if( pad->IsOnLayer( B_Cu ) )
                bot++;
        }

        return top >= bot;
    };

    const VECTOR2I padCentroidA = padCentroid( fpA );
    const VECTOR2I padCentroidB = padCentroid( fpB );

    // Phase-2: non-electrical mates (mounting holes, alignment posts)
    // skip dominant-side detection — there's no copper to vote on.
    // Default to "opposite-side" semantics (no child flip), with the
    // user's optional offset taking responsibility for fine alignment.
    const bool padAOnTop = aPrimary.nonElectrical ? true  : dominantSide( fpA );
    const bool padBOnTop = aPrimary.nonElectrical ? false : dominantSide( fpB );

    // Diagnostic — prints connector centroids in mm, side detection,
    // and pad layer counts for each footprint so the placement math
    // is fully traceable from the log.
    auto countPadLayers = []( FOOTPRINT* fp, int& aTop, int& aBot, int& aBoth )
    {
        aTop = aBot = aBoth = 0;
        for( PAD* pad : fp->Pads() )
        {
            const bool t = pad->IsOnLayer( F_Cu );
            const bool b = pad->IsOnLayer( B_Cu );
            if( t && b )       aBoth++;
            else if( t )       aTop++;
            else if( b )       aBot++;
        }
    };

    int topA = 0, botA = 0, bothA = 0, topB = 0, botB = 0, bothB = 0;
    countPadLayers( fpA, topA, botA, bothA );
    countPadLayers( fpB, topB, botB, bothB );

    wxLogMessage( wxT( "[MATE] PlaceChildOnParent: parent='%s' child='%s' "
                       "primary=%s↔%s parentRot=(%.1f,%.1f,%.1f) parentFlipped=%d" ),
                  aParent.displayName, aChild.displayName,
                  aPrimary.footprintRefA, aPrimary.footprintRefB,
                  aParent.rotation.x, aParent.rotation.y, aParent.rotation.z,
                  ( std::abs( aParent.rotation.x - 180.0f ) < 0.1f
                    && std::abs( aParent.rotation.y ) < 0.1f
                    && std::abs( aParent.rotation.z ) < 0.1f ) ? 1 : 0 );

    wxLogMessage( wxT( "[MATE]   fpA pads: top=%d bot=%d both(TH)=%d → padAOnTop=%d  "
                       "centroidA=(%.3f, %.3f) mm" ),
                  topA, botA, bothA, padAOnTop ? 1 : 0,
                  padCentroidA.x / 1.0e6, padCentroidA.y / 1.0e6 );

    wxLogMessage( wxT( "[MATE]   fpB pads: top=%d bot=%d both(TH)=%d → padBOnTop=%d  "
                       "centroidB=(%.3f, %.3f) mm" ),
                  topB, botB, bothB, padBOnTop ? 1 : 0,
                  padCentroidB.x / 1.0e6, padCentroidB.y / 1.0e6 );

    // Note: this initial log uses the raw padAOnTop comparison; the
    // post-TH-detection effective value is logged later alongside the
    // Kabsch result.
    wxLogMessage( wxT( "[MATE]   raw sameSide=%d (padAOnTop==padBOnTop) "
                       "isThA=%d isThB=%d" ),
                  ( padAOnTop == padBOnTop ) ? 1 : 0,
                  ( bothA > topA + botA ) ? 1 : 0,
                  ( bothB > topB + botB ) ? 1 : 0 );

    // Account for parent's existing flip when projecting its mate side
    // into world frame. The check is "does the parent's full rotation
    // matrix invert the local +Z normal?" — which captures (180,0,0)
    // X-flips AND any (180,0,θ) X-flip-plus-Z-rotation produced by the
    // Kabsch step below when an upstream BFS hop placed this parent.
    glm::mat4 R_P( 1.0f );
    R_P = glm::rotate( R_P, glm::radians( aParent.rotation.z ), glm::vec3( 0, 0, 1 ) );
    R_P = glm::rotate( R_P, glm::radians( aParent.rotation.y ), glm::vec3( 0, 1, 0 ) );
    R_P = glm::rotate( R_P, glm::radians( aParent.rotation.x ), glm::vec3( 1, 0, 0 ) );

    const glm::vec4 zNormalWorld = R_P * glm::vec4( 0.0f, 0.0f, 1.0f, 0.0f );
    const bool      parentFlipped         = ( zNormalWorld.z < 0.0f );
    const bool      parentEffectivelyOnTop = parentFlipped ? !padAOnTop : padAOnTop;

    // Through-hole connectors live on both copper layers — every TH
    // pad makes IsOnLayer(F.Cu) AND IsOnLayer(B.Cu) true, so
    // dominantSide() reports both connectors as "top" by tiebreak and
    // sameEffectiveSide ends up true. That triggers an X-flip (R_x=
    // 180°) on the child, which mirrors render-Y for connectors that
    // physically mate without flipping (USB-stick into pin header,
    // edge connector into socket, etc.). With Y mirrored only on one
    // side, Kabsch sees pad correspondences that no rotation can
    // align and emits a meaningless compromise θ — visible as the
    // green-board fan-out tilt.
    //
    // Detect "both connectors are predominantly TH" via the existing
    // top/bot/both pad counters and force sameEffectiveSide=false in
    // that case so no X-flip applies. The Z-gap direction still uses
    // the parent's effective side: parentEffectivelyOnTop=true ⇒
    // child sits +zGap, false ⇒ -zGap.
    const bool isThA = ( bothA > topA + botA );
    const bool isThB = ( bothB > topB + botB );
    bool       sameEffectiveSide = ( parentEffectivelyOnTop == padBOnTop );

    if( isThA && isThB )
        sameEffectiveSide = false;

    const float thicknessA      = GetBoardThickness( aParent.board.get() );
    const float thicknessB      = GetBoardThickness( aChild.board.get()  );
    const float connectorHeight = ComputeMateZGap( aParent, aPrimary.footprintRefA,
                                                   aChild,  aPrimary.footprintRefB );
    const float zGapMm          = thicknessA * 0.5f + thicknessB * 0.5f + connectorHeight;

    // ----- Z-rotation via robust 2D Kabsch on pin-pair correspondences -----
    //
    // Centroid-to-centroid alignment doesn't constrain rotation around
    // the connector's normal: a single connector pair can't tell us
    // whether the child should be 0° or 180° around Z. With ≥2 cross-
    // net pad pairs we solve the optimal rotation that aligns the
    // child's pin set to the parent's pin set (after the X-flip).
    //
    // Math: working entirely in world render frame (Y-flipped). For
    // each pin pair i:
    //   • vA_i = parent's pad i offset from parent connector centroid,
    //            after parent's full rotation R_P (independent of
    //            parent's board-center pivot since centroid-relative
    //            offsets cancel the pivot).
    //   • vB_i = child's pad i offset from child connector centroid,
    //            after the X-flip (sameEffectiveSide ⇒ flip ⇒ Y mirror).
    //   • Solve R_z(θ) such that R_z(θ) * vB_i ≈ vA_i.
    // Closed form (2D Kabsch via SVD shortcut):
    //   θ = atan2( Σ(vAy·vBx − vAx·vBy), Σ(vAx·vBx + vAy·vBy) ).
    //
    // MOON-1364: a single bad port pin (wrong endpoint pairing in the
    // schematic) is an L2 outlier — least-squares is biased toward it,
    // so the whole board lands tilted. We iterate: fit, score per-pair
    // residuals, drop the worst pair if it's far from the median, and
    // re-fit. Dropped pairs go to m_lastMateResiduals for the
    // validation panel to surface so the user knows which pin to
    // investigate without the placement getting wrecked by it.
    struct PadPairEntry
    {
        wxString  pinA;
        wxString  pinB;
        VECTOR2I  posA;       // BIU on A's stored coords
        VECTOR2I  posB;       // BIU on B's stored coords
        wxString  groupKey;   // electrical group (e.g. "name:GND")
    };

    std::vector<PadPairEntry> padPairs;
    padPairs.reserve( aPrimary.padPins.size() );

    for( size_t i = 0; i < aPrimary.padPins.size(); i++ )
    {
        const auto& [pinA, pinB] = aPrimary.padPins[i];
        const wxString grp = ( i < aPrimary.padPinGroups.size() )
                                    ? aPrimary.padPinGroups[i]
                                    : wxString();

        PAD* pA = fpA->FindPadByNumber( pinA );
        PAD* pB = fpB->FindPadByNumber( pinB );

        if( pA && pB )
            padPairs.push_back(
                    { pinA, pinB, pA->GetPosition(), pB->GetPosition(), grp } );
    }

    // Kabsch is run with the INLIER pad-pair centroid as its pivot —
    // not the full footprint centroid. Footprint centroid sums all pads
    // (including unconnected ones), which can sit far from the
    // correspondence centroid; subtracting it makes a "linear shift"
    // (e.g. schematic wires pin1↔pin4 instead of pin1↔pin1) look like
    // a rotation, and Kabsch flips the connector. Using the inlier
    // centroid lets the translation step absorb the shift naturally
    // and the rotation reflects only the actual geometric mismatch.
    constexpr double biuPerMm = 1.0e6;

    // Compute the centroid of an inlier subset in stored BIU. Returns
    // (cA, cB) — used both to subtract centroids in Kabsch and as the
    // pivot for the placement translation step. Recomputed after each
    // RANSAC drop because the inlier set changes.
    auto inlierCentroids = [&]( const std::vector<size_t>& aIdxs,
                                 VECTOR2I& aOutCA, VECTOR2I& aOutCB )
    {
        if( aIdxs.empty() )
        {
            aOutCA = padCentroidA;
            aOutCB = padCentroidB;
            return;
        }

        VECTOR2L sumA( 0, 0 ), sumB( 0, 0 );

        for( size_t k : aIdxs )
        {
            sumA.x += padPairs[k].posA.x;
            sumA.y += padPairs[k].posA.y;
            sumB.x += padPairs[k].posB.x;
            sumB.y += padPairs[k].posB.y;
        }

        const int N = static_cast<int>( aIdxs.size() );
        aOutCA = VECTOR2I( static_cast<int>( sumA.x / N ), static_cast<int>( sumA.y / N ) );
        aOutCB = VECTOR2I( static_cast<int>( sumB.x / N ), static_cast<int>( sumB.y / N ) );
    };

    // Compute the post-X-flip render-frame offset for one pair around
    // the supplied centroids. Returns (vAx, vAy, vBx, vBy) in mm.
    auto pairVectors = [&]( const PadPairEntry& aEntry,
                             const VECTOR2I& aCA, const VECTOR2I& aCB,
                             double& aOutVAx, double& aOutVAy,
                             double& aOutVBx, double& aOutVBy )
    {
        const double aLocalX =  static_cast<double>( aEntry.posA.x - aCA.x ) / biuPerMm;
        const double aLocalY = -static_cast<double>( aEntry.posA.y - aCA.y ) / biuPerMm;

        const glm::vec4 aRot = R_P * glm::vec4( static_cast<float>( aLocalX ),
                                                 static_cast<float>( aLocalY ),
                                                 0.0f, 0.0f );
        aOutVAx = aRot.x;
        aOutVAy = aRot.y;

        const double bLocalX =  static_cast<double>( aEntry.posB.x - aCB.x ) / biuPerMm;
        const double bLocalY = -static_cast<double>( aEntry.posB.y - aCB.y ) / biuPerMm;
        aOutVBx = bLocalX;
        aOutVBy = sameEffectiveSide ? -bLocalY : bLocalY;
    };

    // Closed-form 2D Kabsch over the supplied indexes around the
    // supplied centroids. Returns θ in degrees.
    auto fitKabsch = [&]( const std::vector<size_t>& aActiveIdxs,
                           const VECTOR2I& aCA, const VECTOR2I& aCB ) -> float
    {
        double sumCross = 0.0;
        double sumDot   = 0.0;

        for( size_t k : aActiveIdxs )
        {
            double vAx, vAy, vBx, vBy;
            pairVectors( padPairs[k], aCA, aCB, vAx, vAy, vBx, vBy );

            sumCross += vAy * vBx - vAx * vBy;
            sumDot   += vAx * vBx + vAy * vBy;
        }

        return static_cast<float>(
                glm::degrees( std::atan2( sumCross, sumDot ) ) );
    };

    // Per-pair residual after applying θ to vB_after_xflip around the
    // supplied centroids. In world mm: |R_z(θ)*vB - vA|.
    auto perPairResidual = [&]( const PadPairEntry& aEntry, float aThetaZDeg,
                                 const VECTOR2I& aCA, const VECTOR2I& aCB ) -> double
    {
        double vAx, vAy, vBx, vBy;
        pairVectors( aEntry, aCA, aCB, vAx, vAy, vBx, vBy );

        const double t  = glm::radians( static_cast<double>( aThetaZDeg ) );
        const double c  = std::cos( t );
        const double s  = std::sin( t );
        const double rx = c * vBx - s * vBy;
        const double ry = s * vBx + c * vBy;

        const double dx = rx - vAx;
        const double dy = ry - vAy;
        return std::sqrt( dx * dx + dy * dy );
    };

    float thetaZDeg = 0.0f;

    // Inlier set — start with all pairs, drop outliers iteratively.
    std::vector<size_t> inliers;
    std::vector<size_t> outliers;
    inliers.reserve( padPairs.size() );

    for( size_t i = 0; i < padPairs.size(); i++ )
        inliers.push_back( i );

    // Final inlier centroid used for placement; falls back to footprint
    // centroid when there's no usable correspondence data.
    VECTOR2I cA_inlier = padCentroidA;
    VECTOR2I cB_inlier = padCentroidB;

    // Identify "anchor" pad pairs: those whose electrical group key
    // appears exactly once on this connector, i.e. unique signal nets
    // with unambiguous 1↔1 schematic correspondence. Power/GND nets
    // (aggregated by name) typically have N pads on each side — their
    // sort-based pre-pairing is a guess, and feeding it into Kabsch
    // alongside signal pads biases θ toward whatever local minimum
    // those guessed pairings sit at. Computing initial θ from anchors
    // only sidesteps that bias; the multi-pad groups then rematch via
    // ICP under the anchor-derived θ.
    std::map<wxString, int> groupCounts;

    for( const PadPairEntry& pe : padPairs )
    {
        if( !pe.groupKey.IsEmpty() )
            groupCounts[pe.groupKey]++;
    }

    std::vector<size_t> anchorIdx;

    for( size_t i = 0; i < padPairs.size(); i++ )
    {
        if( !padPairs[i].groupKey.IsEmpty()
            && groupCounts[padPairs[i].groupKey] == 1 )
        {
            anchorIdx.push_back( i );
        }
    }

    // Tunables. Pin spacing is typically 1.27–2.54 mm; 0.5 mm is a
    // reasonable absolute floor for "actually misaligned" — below
    // it we're in the noise of stackup/router rounding. Ratio 3×
    // catches pads that are clearly worse than the population while
    // staying tolerant of natural spread on connectors with a
    // wide pin layout. Cap drops at 1/3 of the pairs so we can't
    // drop the entire connector to chase noise.
    constexpr double  k_outlierAbsMinMm = 0.5;
    constexpr double  k_outlierRatio    = 3.0;

    if( inliers.size() >= 2 )
    {
        inlierCentroids( inliers, cA_inlier, cB_inlier );

        if( anchorIdx.size() >= 2 )
        {
            // Bootstrap θ from anchors. Their centroid is the
            // appropriate Kabsch pivot for *this* sub-fit — multi-pad
            // groups will get rematched in the ICP pass below before
            // Kabsch sees them again.
            VECTOR2I cA_anchor = padCentroidA;
            VECTOR2I cB_anchor = padCentroidB;
            inlierCentroids( anchorIdx, cA_anchor, cB_anchor );
            thetaZDeg = fitKabsch( anchorIdx, cA_anchor, cB_anchor );

            wxLogMessage( wxT( "[MATE]   anchor-θ: %zu signal pads → "
                               "θ=%.2f° (multi-pad groups deferred to ICP)" ),
                          anchorIdx.size(), thetaZDeg );
        }
        else
        {
            // No anchors (all pads are in multi-pad groups, or only one
            // anchor) — fall back to all-pad Kabsch.
            thetaZDeg = fitKabsch( inliers, cA_inlier, cB_inlier );
        }
    }
    else if( !padPairs.empty() )
    {
        // Single pad pair: no rotation constraint; centroid is just
        // that pad's position. Translation will land that pad exactly.
        inlierCentroids( inliers, cA_inlier, cB_inlier );
    }

    // ----- Post-Kabsch ICP: re-match electrically-equivalent pads -----
    //
    // BuildMateGraph's sort-based 1-to-1 matching can only see stored
    // positions, but Kabsch's chosen θ may rotate the child connector
    // 180° in world (signal pads dictate this when their schematic
    // mapping is "reversed"). The aggregated GND group on the same
    // connector then sits with its sort-asc-asc pairing X-crossed in
    // world. Re-pair within each electrical group by world-space
    // proximity using the current pose, then re-fit Kabsch. Two
    // iterations is plenty in practice — pad sets are small (typically
    // ≤8 pads per group) so greedy NN converges immediately for
    // physical layouts.
    constexpr int     k_maxIcpIters = 4;
    constexpr double  k_icpSwapEpsilonMm = 0.01;

    for( int icpIter = 0; icpIter < k_maxIcpIters; icpIter++ )
    {
        // Group inliers by their electrical group key.
        std::map<wxString, std::vector<size_t>> groupedInliers;

        for( size_t k : inliers )
        {
            if( !padPairs[k].groupKey.IsEmpty() )
                groupedInliers[padPairs[k].groupKey].push_back( k );
        }

        bool anyChange = false;

        for( auto& [grp, idxsInGroup] : groupedInliers )
        {
            if( idxsInGroup.size() < 2 )
                continue;     // nothing to re-match

            // Compute parent and child pad world offsets (XY, in mm,
            // centroid-relative) under the current θ. The world centroids
            // cancel out for matching purposes since both sides share the
            // same target centroid after placement.
            const double t  = glm::radians( static_cast<double>( thetaZDeg ) );
            const double cs = std::cos( t );
            const double sn = std::sin( t );

            std::vector<glm::vec2> aWorld;
            std::vector<glm::vec2> bWorld;
            aWorld.reserve( idxsInGroup.size() );
            bWorld.reserve( idxsInGroup.size() );

            for( size_t k : idxsInGroup )
            {
                double vAx, vAy, vBx, vBy;
                pairVectors( padPairs[k], cA_inlier, cB_inlier,
                             vAx, vAy, vBx, vBy );

                // Apply Z-rotation to the child offset.
                const double rx = cs * vBx - sn * vBy;
                const double ry = sn * vBx + cs * vBy;

                aWorld.emplace_back( static_cast<float>( vAx ),
                                     static_cast<float>( vAy ) );
                bWorld.emplace_back( static_cast<float>( rx ),
                                     static_cast<float>( ry ) );
            }

            // Greedy NN: for each A pad in the group, pair with the
            // closest unused B pad. Snapshots of the original B side
            // info indexed by the within-group position so we can swap
            // freely without invalidating later iterations.
            std::vector<wxString> savedPinB( idxsInGroup.size() );
            std::vector<VECTOR2I> savedPosB( idxsInGroup.size() );

            for( size_t i = 0; i < idxsInGroup.size(); i++ )
            {
                savedPinB[i] = padPairs[idxsInGroup[i]].pinB;
                savedPosB[i] = padPairs[idxsInGroup[i]].posB;
            }

            std::vector<bool> bUsed( idxsInGroup.size(), false );

            for( size_t i = 0; i < idxsInGroup.size(); i++ )
            {
                size_t bestJ    = SIZE_MAX;
                double bestDist = std::numeric_limits<double>::max();

                for( size_t j = 0; j < idxsInGroup.size(); j++ )
                {
                    if( bUsed[j] )
                        continue;

                    const double dx = aWorld[i].x - bWorld[j].x;
                    const double dy = aWorld[i].y - bWorld[j].y;
                    const double d  = std::sqrt( dx * dx + dy * dy );

                    if( d < bestDist )
                    {
                        bestDist = d;
                        bestJ    = j;
                    }
                }

                if( bestJ == SIZE_MAX )
                    continue;

                bUsed[bestJ] = true;

                // If the new pairing differs from the current one, swap
                // the B-side fields. We compare via the saved snapshot
                // so we don't get confused by an earlier swap in the
                // same iteration.
                const wxString& targetPinB = savedPinB[bestJ];

                if( padPairs[idxsInGroup[i]].pinB != targetPinB )
                {
                    wxLogMessage( wxT( "[MATE]   ICP iter=%d group=%s "
                                       "swap A=%s: B %s → %s" ),
                                  icpIter, grp,
                                  padPairs[idxsInGroup[i]].pinA,
                                  padPairs[idxsInGroup[i]].pinB,
                                  targetPinB );

                    padPairs[idxsInGroup[i]].pinB = targetPinB;
                    padPairs[idxsInGroup[i]].posB = savedPosB[bestJ];
                    anyChange                     = true;
                }
            }
        }

        if( !anyChange )
            break;

        // Re-fit Kabsch with the updated pairings.
        inlierCentroids( inliers, cA_inlier, cB_inlier );
        thetaZDeg = fitKabsch( inliers, cA_inlier, cB_inlier );
    }

    // ----- RANSAC outlier rejection (post-ICP) -----
    //
    // Now that ICP has rematched multi-pad groups under the anchor θ,
    // the inlier set's correspondences are as consistent as the data
    // allows. Drop the worst-residual pad pairs that still don't fit:
    // these are *genuine* schematic errors (wrong port-pin mapping in
    // the netlist), not just sort-pairing artifacts. Outliers go to
    // m_lastMateResiduals so the validation panel surfaces them.
    if( inliers.size() >= 3 )
    {
        const size_t k_maxDrops = std::max<size_t>( 1, inliers.size() / 3 );

        for( size_t drop = 0; drop < k_maxDrops && inliers.size() > 2; drop++ )
        {
            std::vector<double> resi( inliers.size() );

            for( size_t k = 0; k < inliers.size(); k++ )
                resi[k] = perPairResidual( padPairs[inliers[k]], thetaZDeg,
                                            cA_inlier, cB_inlier );

            std::vector<double> sorted = resi;
            std::sort( sorted.begin(), sorted.end() );
            const double median = sorted[sorted.size() / 2];

            size_t worstK = 0;
            double worstR = resi[0];

            for( size_t k = 1; k < resi.size(); k++ )
            {
                if( resi[k] > worstR )
                {
                    worstR = resi[k];
                    worstK = k;
                }
            }

            const bool isOutlier =
                    ( worstR > k_outlierAbsMinMm )
                    && ( worstR > k_outlierRatio * std::max( median, 1e-3 ) );

            if( !isOutlier )
                break;

            wxLogMessage( wxT( "[MATE]   robust: dropping pin pair %s↔%s "
                               "residual=%.3fmm (median=%.3fmm) θ_before=%.2f°" ),
                          padPairs[inliers[worstK]].pinA,
                          padPairs[inliers[worstK]].pinB,
                          worstR, median, thetaZDeg );

            outliers.push_back( inliers[worstK] );
            inliers.erase( inliers.begin() + worstK );

            inlierCentroids( inliers, cA_inlier, cB_inlier );
            thetaZDeg = fitKabsch( inliers, cA_inlier, cB_inlier );
        }
    }

    // ----- Crossings-minimization refinement -----
    //
    // Kabsch finds the L2-optimal θ, which produces parallel pin-pair
    // lines for clean, consistent data. When subtle inconsistencies
    // remain (mixed forward/reverse correspondences across nets, or a
    // connector with degenerate-rank pad layout) the L2 optimum can
    // still leave lines crossing — visually obvious, geometrically a
    // local minimum. Sample θ on a coarse grid and refine to the
    // candidate with the fewest pin-pair line crossings (tiebreak by
    // total line length so among "no-crossings" winners we pick the
    // one with the shortest pad-to-pad displacements). The centroid
    // translation step is unchanged — only θ shifts.
    if( inliers.size() >= 2 )
    {
        auto countCrossingsAt = [&]( float aThetaDeg, double& aOutTotalLen ) -> int
        {
            const double t  = glm::radians( static_cast<double>( aThetaDeg ) );
            const double cs = std::cos( t );
            const double sn = std::sin( t );

            std::vector<glm::vec2> aW( inliers.size() );
            std::vector<glm::vec2> bW( inliers.size() );

            for( size_t i = 0; i < inliers.size(); i++ )
            {
                double vAx, vAy, vBx, vBy;
                pairVectors( padPairs[inliers[i]], cA_inlier, cB_inlier,
                             vAx, vAy, vBx, vBy );

                const double rx = cs * vBx - sn * vBy;
                const double ry = sn * vBx + cs * vBy;

                aW[i] = glm::vec2( static_cast<float>( vAx ),
                                    static_cast<float>( vAy ) );
                bW[i] = glm::vec2( static_cast<float>( rx ),
                                    static_cast<float>( ry ) );
            }

            int    crossings = 0;
            double totalLen  = 0.0;

            // Standard 2D segment intersection — works on the centroid-
            // relative offsets (the world centroid cancels for both
            // sides since translation aligns them).
            auto segCross = []( glm::vec2 a1, glm::vec2 a2,
                                 glm::vec2 b1, glm::vec2 b2 ) -> bool
            {
                auto crossXY = []( glm::vec2 p, glm::vec2 q )
                { return p.x * q.y - p.y * q.x; };

                glm::vec2 r = a2 - a1;
                glm::vec2 s = b2 - b1;
                const float rxs = crossXY( r, s );

                if( std::abs( rxs ) < 1e-9f )
                    return false;     // parallel

                const float t  = crossXY( b1 - a1, s ) / rxs;
                const float u2 = crossXY( b1 - a1, r ) / rxs;

                return ( t > 1e-3f && t < 1.0f - 1e-3f
                         && u2 > 1e-3f && u2 < 1.0f - 1e-3f );
            };

            for( size_t i = 0; i < inliers.size(); i++ )
            {
                totalLen += glm::length( aW[i] - bW[i] );

                for( size_t j = i + 1; j < inliers.size(); j++ )
                {
                    if( segCross( aW[i], bW[i], aW[j], bW[j] ) )
                        crossings++;
                }
            }

            aOutTotalLen = totalLen;
            return crossings;
        };

        double bestLen       = 0.0;
        int    bestCrossings = countCrossingsAt( thetaZDeg, bestLen );
        float  bestTheta     = thetaZDeg;

        // Only search if the L2-optimal θ has at least one crossing —
        // otherwise Kabsch's answer is already as parallel as possible
        // and the search is wasted work.
        if( bestCrossings > 0 )
        {
            for( int delta = -180; delta <= 180; delta += 5 )
            {
                if( delta == 0 )
                    continue;

                float candTheta = thetaZDeg + delta;

                while( candTheta > 180.0f )
                    candTheta -= 360.0f;

                while( candTheta < -180.0f )
                    candTheta += 360.0f;

                double candLen;
                int    candCross = countCrossingsAt( candTheta, candLen );

                const bool fewerCrossings = candCross < bestCrossings;
                const bool sameCrossingsShorter = candCross == bestCrossings
                                                  && candLen < bestLen - 1e-3;

                if( fewerCrossings || sameCrossingsShorter )
                {
                    bestCrossings = candCross;
                    bestLen       = candLen;
                    bestTheta     = candTheta;
                }
            }

            if( std::abs( bestTheta - thetaZDeg ) > 0.5f )
            {
                wxLogMessage( wxT( "[MATE]   crossings refine: θ %.1f° "
                                   "(crossings=%d) → %.1f° (crossings=%d, "
                                   "totalLen=%.2fmm)" ),
                              thetaZDeg,
                              countCrossingsAt( thetaZDeg, bestLen ),
                              bestTheta, bestCrossings, bestLen );
                thetaZDeg = bestTheta;
            }
        }
    }

    // Cache the post-ICP padPins so subsequent BuildMateGraph calls
    // (panel + gizmo) see the same rematched pairings the placement
    // used. The cache key is canonical (lower-UUID-first), matching
    // BuildMateGraph's pair-key construction. If SolveMatePoses
    // swapped parent onto the A side, we un-swap when caching so the
    // canonical lookup in BuildMateGraph hits.
    {
        const bool needSwapBack =
                ( aChild.uuid < aParent.uuid )
                || ( aParent.uuid == aChild.uuid
                     && aPrimary.footprintRefB < aPrimary.footprintRefA );

        std::vector<std::pair<wxString, wxString>> finalPadPins;
        finalPadPins.reserve( padPairs.size() );

        for( const auto& pe : padPairs )
        {
            if( needSwapBack )
                finalPadPins.emplace_back( pe.pinB, pe.pinA );
            else
                finalPadPins.emplace_back( pe.pinA, pe.pinB );
        }

        KIID     canonInstA = aParent.uuid;
        KIID     canonInstB = aChild.uuid;
        wxString canonRefA  = aPrimary.footprintRefA;
        wxString canonRefB  = aPrimary.footprintRefB;

        if( needSwapBack )
        {
            std::swap( canonInstA, canonInstB );
            std::swap( canonRefA, canonRefB );
        }

        m_lastIcpPadPins[std::make_tuple( canonInstA, canonRefA,
                                           canonInstB, canonRefB )] =
                std::move( finalPadPins );
    }

    // Surface dropped pairs as residuals so the validation panel can
    // report "this connector has a probable wrong port-pin mapping".
    // Each entry pinpoints the specific pin pair via a synthesized
    // MATE_PAIR carrying just that one padPin tuple.
    for( size_t k : outliers )
    {
        MATE_RESIDUAL r;
        r.pair = aPrimary;
        r.pair.padPins.clear();
        r.pair.padPins.emplace_back( padPairs[k].pinA, padPairs[k].pinB );
        r.residualMm  = static_cast<float>(
                perPairResidual( padPairs[k], thetaZDeg,
                                  cA_inlier, cB_inlier ) );
        r.residualDeg = 0.0f;
        m_lastMateResiduals.push_back( r );
    }

    // X-flip portion of the child's rotation. With Kabsch's Z-rotation
    // applied last (Z·Y·X composition), the X-flip then Z-rotation
    // produces the same orientation regardless of axis order.
    const float childRotXDeg = sameEffectiveSide
                                       ? ( parentFlipped ? 0.0f : 180.0f )
                                       : ( parentFlipped ? 180.0f : 0.0f );

    aChild.rotation = SFVEC3F( childRotXDeg, 0.0f, thetaZDeg );

    wxLogMessage( wxT( "[MATE]   Kabsch inliers=%zu/%zu thetaZ=%.3f° parentFlipped=%d "
                       "sameSide=%d childRotX=%.0f° connectorH=%.3fmm zGap=%.3fmm" ),
                  inliers.size(), padPairs.size(), thetaZDeg,
                  parentFlipped ? 1 : 0,
                  sameEffectiveSide ? 1 : 0, childRotXDeg,
                  connectorHeight, zGapMm );

    // ----- World-frame translation -----
    //
    // Place the child so its connector centroid (under the new R_C)
    // lands at parent's connector centroid + (0,0,±zGap) in world
    // render frame. The previous formula used stored-coord differences
    // and only patched the parent-flipped (180,0,0) case, so a parent
    // with a Kabsch-derived Z-rotation (or any non-X-flip rotation) put
    // its child in the wrong world position — the "far away" symptom
    // for 3+ board chains.

    // Look up parent's adapter (needed for the centroid projection;
    // mirrors the gizmo's pose math). We index instances by UUID since
    // the call site doesn't pass adapters in.
    auto findAdapter = [&]( const KIID& aUuid ) -> const BOARD_ADAPTER*
    {
        for( size_t i = 0; i < m_boardInstances.size(); i++ )
        {
            if( m_boardInstances[i].uuid == aUuid
                && i < m_instanceAdapters.size() )
                return m_instanceAdapters[i].get();
        }

        return nullptr;
    };

    const BOARD_ADAPTER* adapterA = findAdapter( aParent.uuid );
    const BOARD_ADAPTER* adapterB = findAdapter( aChild.uuid );

    if( !adapterA || !adapterB )
        return false;

    const float biuPerMmF = 1.0e6f;
    const float sharedF   = static_cast<float>( m_sharedBiuTo3Dunits );

    // Project parent's INLIER centroid (the same pivot Kabsch used) to
    // world. This is the point we want the child's matching pivot to
    // land on (offset by zGap in Z). Inline the same pose math
    // projectFootprintCentroidToWorld uses but for an arbitrary stored
    // BIU point.
    auto projectStoredBIUToWorld = [&]( const BOARD_3D_INSTANCE& aInst,
                                         const BOARD_ADAPTER&     aAdapter,
                                         const VECTOR2I&          aStoredBIU ) -> glm::vec3
    {
        const double  localFactor = aAdapter.BiuTo3dUnits();
        const double  scaleFactor = ( localFactor != 0.0 )
                                            ? m_sharedBiuTo3Dunits / localFactor
                                            : 1.0;

        const glm::vec3 local3D( static_cast<float>( aStoredBIU.x ) * localFactor,
                                 -static_cast<float>( aStoredBIU.y ) * localFactor,
                                 0.0f );
        const glm::vec3 localShared = local3D * static_cast<float>( scaleFactor );

        const SFVEC3F   lc = aAdapter.GetBoardCenter();
        const glm::vec3 localCenterShared( lc.x * static_cast<float>( scaleFactor ),
                                           lc.y * static_cast<float>( scaleFactor ),
                                           lc.z * static_cast<float>( scaleFactor ) );

        glm::mat4 R( 1.0f );
        R = glm::rotate( R, glm::radians( aInst.rotation.z ), glm::vec3( 0, 0, 1 ) );
        R = glm::rotate( R, glm::radians( aInst.rotation.y ), glm::vec3( 0, 1, 0 ) );
        R = glm::rotate( R, glm::radians( aInst.rotation.x ), glm::vec3( 1, 0, 0 ) );

        const glm::vec3 shifted  = localShared - localCenterShared;
        const glm::vec4 rotated  = R * glm::vec4( shifted, 1.0f );
        const glm::vec3 replaced = glm::vec3( rotated ) + localCenterShared;

        const glm::vec3 instShared(  aInst.position.x * biuPerMmF * sharedF,
                                    -aInst.position.y * biuPerMmF * sharedF,
                                     aInst.position.z * biuPerMmF * sharedF );

        SFVEC3F bboxMin, bboxMax;
        GetAssemblyBoundingBox( bboxMin, bboxMax );
        const SFVEC3F   centerMm = ( bboxMin + bboxMax ) * 0.5f;
        const glm::vec3 centerShared(  centerMm.x * biuPerMmF * sharedF,
                                      -centerMm.y * biuPerMmF * sharedF,
                                       centerMm.z * biuPerMmF * sharedF );

        return replaced + instShared - centerShared;
    };

    const glm::vec3 worldA = projectStoredBIUToWorld( aParent, *adapterA, cA_inlier );

    const float zGapShared = zGapMm * biuPerMmF * sharedF
                             * ( parentEffectivelyOnTop ? 1.0f : -1.0f );

    const glm::vec3 worldB( worldA.x, worldA.y, worldA.z + zGapShared );

    // Solve for child's inst.position. The pose math the renderer
    // applies is:
    //   world = R_C * (localBshared - localCenterShared_C)
    //         + localCenterShared_C
    //         + instShared_C - centerShared
    // → instShared_C = worldB - replaced_C + centerShared
    // where replaced_C = R_C*(localBshared - localCenterShared_C)
    //                  + localCenterShared_C.
    glm::mat4 R_C( 1.0f );
    R_C = glm::rotate( R_C, glm::radians( aChild.rotation.z ), glm::vec3( 0, 0, 1 ) );
    R_C = glm::rotate( R_C, glm::radians( aChild.rotation.y ), glm::vec3( 0, 1, 0 ) );
    R_C = glm::rotate( R_C, glm::radians( aChild.rotation.x ), glm::vec3( 1, 0, 0 ) );

    const double localFactorB = adapterB->BiuTo3dUnits();
    const double scaleFactorB = ( localFactorB != 0.0 )
                                        ? m_sharedBiuTo3Dunits / localFactorB
                                        : 1.0;

    // Child INLIER centroid in board-local 3D-units (Y inverted to
    // match render-frame, Z=0 for the centroid plane). Using the
    // inlier centroid here keeps Kabsch's rotation pivot consistent
    // with the placement target — the same point we projected to
    // world above for the parent.
    const glm::vec3 localB( static_cast<float>( cB_inlier.x )
                                    * static_cast<float>( localFactorB ),
                            -static_cast<float>( cB_inlier.y )
                                    * static_cast<float>( localFactorB ),
                            0.0f );
    const glm::vec3 localBshared = localB * static_cast<float>( scaleFactorB );

    const SFVEC3F   lcB = adapterB->GetBoardCenter();
    const glm::vec3 localCenterSharedB( lcB.x * static_cast<float>( scaleFactorB ),
                                        lcB.y * static_cast<float>( scaleFactorB ),
                                        lcB.z * static_cast<float>( scaleFactorB ) );

    const glm::vec3 shifted  = localBshared - localCenterSharedB;
    const glm::vec4 rotated  = R_C * glm::vec4( shifted, 1.0f );
    const glm::vec3 replaced = glm::vec3( rotated ) + localCenterSharedB;

    SFVEC3F bboxMin, bboxMax;
    GetAssemblyBoundingBox( bboxMin, bboxMax );
    const SFVEC3F   centerMm = ( bboxMin + bboxMax ) * 0.5f;
    const glm::vec3 centerShared(  centerMm.x * biuPerMmF * sharedF,
                                  -centerMm.y * biuPerMmF * sharedF,
                                   centerMm.z * biuPerMmF * sharedF );

    const glm::vec3 instSharedC = worldB - replaced + centerShared;

    aChild.position.x =  instSharedC.x / ( biuPerMmF * sharedF );
    aChild.position.y = -instSharedC.y / ( biuPerMmF * sharedF );
    aChild.position.z =  instSharedC.z / ( biuPerMmF * sharedF );

    // M6.D-phase-2: apply the custom mate's offset override on top of
    // the auto-computed pose. Translation is added in the parent's
    // world frame; rotation is added to the child's Euler set so the
    // existing pose math (Z·Y·X) composes the deltas naturally.
    if( aPrimary.hasOffset )
    {
        aChild.position += SFVEC3F( static_cast<float>( aPrimary.offsetTx ),
                                    static_cast<float>( aPrimary.offsetTy ),
                                    static_cast<float>( aPrimary.offsetTz ) );

        aChild.rotation += SFVEC3F( static_cast<float>( aPrimary.offsetRx ),
                                    static_cast<float>( aPrimary.offsetRy ),
                                    static_cast<float>( aPrimary.offsetRz ) );
    }

    return true;
}


float ASSEMBLY_3D_MANAGER::ComputeMateZGap( const BOARD_3D_INSTANCE& aA,
                                              const wxString&          aFootprintRefA,
                                              const BOARD_3D_INSTANCE& aB,
                                              const wxString&          aFootprintRefB ) const
{
    // Mate Z-gap = larger of the two connectors' 3D-model max-Z extent
    // above the board surface. Falls back to 5 mm only if neither model
    // is loadable (which is what M6.D-phase-1 originally shipped before
    // virtual-model resolution worked end-to-end).
    auto getConnectorMaxZ = []( const BOARD_3D_INSTANCE& aInst,
                                const wxString&          aRef ) -> float
    {
        if( !aInst.board )
            return 0.0f;

        FOOTPRINT* fp = findFootprintByRef( aInst.board.get(), aRef );

        if( !fp || fp->Models().empty() )
            return 0.0f;

        // Prefer the sub-project's S3D cache so KIPRJMOD-relative paths
        // resolve against the right directory; fall back to whatever
        // project the BOARD is bound to (set in LoadProjectBoards).
        PROJECT* prj = aInst.subProject;

        if( !prj )
            prj = aInst.board->GetProject();

        if( !prj )
            return 0.0f;

        S3D_CACHE* cache = PROJECT_PCB::Get3DCacheManager( prj );

        if( !cache )
            return 0.0f;

        float maxZ = 0.0f;
        bool  foundAny = false;

        for( const FP_3DMODEL& fp_model : fp->Models() )
        {
            if( !fp_model.m_Show || fp_model.m_Filename.empty() )
                continue;

            std::vector<const EMBEDDED_FILES*> embeddedFilesStack;
            embeddedFilesStack.push_back( fp->GetEmbeddedFiles() );
            embeddedFilesStack.push_back( aInst.board->GetEmbeddedFiles() );

            const S3DMODEL* model = cache->GetModel( fp_model.m_Filename,
                                                     wxEmptyString,
                                                     std::move( embeddedFilesStack ) );

            if( !model )
                continue;

            // Compute the local-space bbox of the mesh in mm.
            SFVEC3F localMin( 0, 0, 0 );
            SFVEC3F localMax( 0, 0, 0 );
            bool    haveLocal = false;

            for( unsigned int m = 0; m < model->m_MeshesSize; m++ )
            {
                const SMESH& mesh = model->m_Meshes[m];

                for( unsigned int v = 0; v < mesh.m_VertexSize; v++ )
                {
                    const SFVEC3F& p = mesh.m_Positions[v];

                    if( !haveLocal )
                    {
                        localMin = localMax = p;
                        haveLocal = true;
                    }
                    else
                    {
                        localMin.x = std::min( localMin.x, p.x );
                        localMin.y = std::min( localMin.y, p.y );
                        localMin.z = std::min( localMin.z, p.z );
                        localMax.x = std::max( localMax.x, p.x );
                        localMax.y = std::max( localMax.y, p.y );
                        localMax.z = std::max( localMax.z, p.z );
                    }
                }
            }

            if( !haveLocal )
                continue;

            // Build the transform the renderer applies (render_3d_opengl.cpp:1196-1203):
            //   T(offset) · R_z(-rz) · R_y(-ry) · R_x(-rx) · S(scale)
            // EasyEDA models in particular ship with rotations like
            // (-90, 0, -90) to swap their up-axis into world Z; reading
            // raw vertex.z without applying this rotation reads the wrong
            // axis (a connector's length, not height).
            glm::mat4 mtx( 1.0f );
            mtx = glm::translate( mtx, glm::vec3( fp_model.m_Offset.x,
                                                  fp_model.m_Offset.y,
                                                  fp_model.m_Offset.z ) );
            mtx = glm::rotate( mtx,
                               glm::radians( -static_cast<float>( fp_model.m_Rotation.z ) ),
                               glm::vec3( 0.0f, 0.0f, 1.0f ) );
            mtx = glm::rotate( mtx,
                               glm::radians( -static_cast<float>( fp_model.m_Rotation.y ) ),
                               glm::vec3( 0.0f, 1.0f, 0.0f ) );
            mtx = glm::rotate( mtx,
                               glm::radians( -static_cast<float>( fp_model.m_Rotation.x ) ),
                               glm::vec3( 1.0f, 0.0f, 0.0f ) );
            mtx = glm::scale( mtx, glm::vec3( fp_model.m_Scale.x,
                                              fp_model.m_Scale.y,
                                              fp_model.m_Scale.z ) );

            // Transform the 8 bbox corners; world max-Z is the largest Z
            // any of them lands on.
            const glm::vec3 corners[8] = {
                { localMin.x, localMin.y, localMin.z },
                { localMax.x, localMin.y, localMin.z },
                { localMin.x, localMax.y, localMin.z },
                { localMax.x, localMax.y, localMin.z },
                { localMin.x, localMin.y, localMax.z },
                { localMax.x, localMin.y, localMax.z },
                { localMin.x, localMax.y, localMax.z },
                { localMax.x, localMax.y, localMax.z },
            };

            float modelMaxZ = -std::numeric_limits<float>::max();

            for( const glm::vec3& c : corners )
            {
                glm::vec4 t = mtx * glm::vec4( c, 1.0f );

                if( t.z > modelMaxZ )
                    modelMaxZ = t.z;
            }

            if( !foundAny || modelMaxZ > maxZ )
            {
                maxZ = modelMaxZ;
                foundAny = true;
            }
        }

        return foundAny ? maxZ : 0.0f;
    };

    float zA = getConnectorMaxZ( aA, aFootprintRefA );
    float zB = getConnectorMaxZ( aB, aFootprintRefB );
    float bbZ = std::max( zA, zB );

    if( bbZ <= 0.0f )
        return 5.0f;  // last-resort fallback when neither model loaded

    return bbZ;
}


MATE_RESIDUAL ASSEMBLY_3D_MANAGER::ComputeMateResidual( const MATE_PAIR& aPair ) const
{
    // Phase-1 placeholder: report pair identity with zero residual.
    // True residual computation (project both pad centers to world
    // via final poses, take euclidean distance) lands when we wire
    // residuals into the M6.E DRC panel.
    MATE_RESIDUAL r;
    r.pair        = aPair;
    r.residualMm  = 0.0f;
    r.residualDeg = 0.0f;
    return r;
}


bool ASSEMBLY_3D_MANAGER::CanMateConnectors() const
{
    if( !m_project )
        return false;

    return !m_project->GetProjectFile().GetCrossBoardNets().empty()
           || !m_project->GetProjectFile().GetCustomMates().empty();
}


// ========== M6.D-phase-2 Custom Mate API ==========

namespace
{

// Returns an empty vector reference for the no-project case so callers
// can iterate without null-checking. Function-local static is safe and
// avoids lifetime gymnastics — the empty vector outlives every caller.
const std::vector<CUSTOM_MATE>& emptyCustomMates()
{
    static const std::vector<CUSTOM_MATE> kEmpty;
    return kEmpty;
}

} // namespace


const std::vector<CUSTOM_MATE>& ASSEMBLY_3D_MANAGER::GetCustomMates() const
{
    if( !m_project )
        return emptyCustomMates();

    return m_project->GetProjectFile().GetCustomMates();
}


KIID ASSEMBLY_3D_MANAGER::AddCustomMate( const CUSTOM_MATE& aMate )
{
    if( !m_project )
        return KIID( 0 );

    PROJECT_FILE& pf = m_project->GetProjectFile();

    if( !pf.IsMultiBoardContainer() )
        return KIID( 0 );

    CUSTOM_MATE stored = aMate;

    // Auto-assign a UUID if the caller passed a null one. Callers that
    // care about the UUID (e.g. UI rebinding after add) should read
    // the return value rather than the input mate.
    if( stored.uuid == KIID( 0 ) )
        stored.uuid = KIID();

    // Read-modify-write through the setter: writing through the
    // GetCustomMates() non-const reference directly bypasses the
    // T3 NotifyMultiBoardChanged call that marks the project dirty,
    // so the change wouldn't survive an app close. SetCustomMates
    // does the notify and dirty bookkeeping in one go.
    std::vector<CUSTOM_MATE> mates = pf.GetCustomMates();
    mates.push_back( stored );
    pf.SetCustomMates( std::move( mates ) );

    return stored.uuid;
}


bool ASSEMBLY_3D_MANAGER::UpdateCustomMate( const CUSTOM_MATE& aMate )
{
    if( !m_project )
        return false;

    PROJECT_FILE& pf = m_project->GetProjectFile();

    std::vector<CUSTOM_MATE> mates = pf.GetCustomMates();
    auto  it    = std::find_if( mates.begin(), mates.end(),
                                [&]( const CUSTOM_MATE& m ) { return m.uuid == aMate.uuid; } );

    if( it == mates.end() )
        return false;

    *it = aMate;
    pf.SetCustomMates( std::move( mates ) );
    return true;
}


bool ASSEMBLY_3D_MANAGER::RemoveCustomMate( const KIID& aMateUuid )
{
    if( !m_project )
        return false;

    PROJECT_FILE& pf = m_project->GetProjectFile();

    std::vector<CUSTOM_MATE> mates = pf.GetCustomMates();
    auto  it    = std::find_if( mates.begin(), mates.end(),
                                [&]( const CUSTOM_MATE& m ) { return m.uuid == aMateUuid; } );

    if( it == mates.end() )
        return false;

    mates.erase( it );
    pf.SetCustomMates( std::move( mates ) );
    return true;
}


std::vector<COLLISION_RESULT> ASSEMBLY_3D_MANAGER::RunCollisionCheck()
{
    m_lastCollisions.clear();
    m_lastOverlapBoxes.clear();

    // Broad-phase pair counter — used by the BROAD-DUMP diagnostic that
    // fires when rotated boards inexplicably produce zero candidate pairs.
    int diagPairsBroad = 0;

    // Mated pairs are expected contact (mate gizmo renders them green/cyan);
    // skip them in the collision pass so users see only the unintended
    // overlaps. Alignment-only mates still count as collisions — they're
    // not supposed to be in contact.
    std::vector<MATE_EDGE> mateEdges = BuildMateGraph();

    auto isMated = [&]( const KIID& aInstA, const wxString& aFpA,
                        const KIID& aInstB, const wxString& aFpB ) -> bool
    {
        for( const MATE_EDGE& edge : mateEdges )
        {
            for( const MATE_PAIR& p : edge.pairs )
            {
                if( p.alignmentOnly )
                    continue;

                bool dirA = p.instanceA == aInstA && p.footprintRefA == aFpA
                            && p.instanceB == aInstB && p.footprintRefB == aFpB;
                bool dirB = p.instanceA == aInstB && p.footprintRefA == aFpB
                            && p.instanceB == aInstA && p.footprintRefB == aFpA;

                if( dirA || dirB )
                    return true;
            }
        }
        return false;
    };

    // Per-footprint Z extent in board-local mm (top of board → top of
    // tallest 3D model on it). Returns three signals:
    //   • aZMin/aZMax    — Z extent in board-local mm
    //   • aDeclared (out) — footprint references at least one 3D model
    //                       (whether or not the cache could load it)
    //   • aLoaded   (out) — at least one model actually returned a mesh
    //
    // The caller uses (declared && !loaded) to recognize "this footprint
    // is a real component whose mesh just hasn't been resolved yet" —
    // typical with virtual STEP files behind a path the S3D_CACHE
    // hasn't resolved (same first-open pattern as task #52). Such
    // footprints get the 8 mm "assume connector / tall component"
    // fallback so they still trigger collisions on first-open. A
    // footprint with no declared models gets 0 mm Z and won't generate
    // phantom OBB-fallback boxes — silk/fab markers don't have volume.
    auto computeFpZExtent = [this]( const BOARD_3D_INSTANCE& aInst,
                                    const BOARD_ADAPTER*     aAdapter,
                                    const FOOTPRINT*         aFp,
                                    float& aZMin, float& aZMax,
                                    bool& aDeclared, bool& aLoaded )
    {
        constexpr float kUnknownModelFallbackMm = 8.0f;
        float           modelHeight             = 0.0f;
        bool            anyModelLoaded          = false;
        bool            anyModelDeclared        = false;

        PROJECT* prj = aInst.subProject ? aInst.subProject : aInst.board->GetProject();

        if( prj )
        {
            if( S3D_CACHE* cache = PROJECT_PCB::Get3DCacheManager( prj ) )
            {
                for( const FP_3DMODEL& fp_model : aFp->Models() )
                {
                    if( !fp_model.m_Show || fp_model.m_Filename.empty() )
                        continue;

                    anyModelDeclared = true;

                    std::vector<const EMBEDDED_FILES*> stack;
                    stack.push_back( aFp->GetEmbeddedFiles() );
                    stack.push_back( aInst.board->GetEmbeddedFiles() );
                    const S3DMODEL* model = cache->GetModel( fp_model.m_Filename,
                                                             wxEmptyString,
                                                             std::move( stack ) );

                    if( !model )
                        continue;

                    float maxZ = 0.0f;
                    bool  hasVerts = false;

                    for( unsigned int m = 0; m < model->m_MeshesSize; m++ )
                    {
                        const SMESH& mesh = model->m_Meshes[m];

                        for( unsigned int v = 0; v < mesh.m_VertexSize; v++ )
                        {
                            float z = mesh.m_Positions[v].z;
                            if( !hasVerts || z > maxZ )
                            {
                                maxZ = z;
                                hasVerts = true;
                            }
                        }
                    }

                    if( !hasVerts )
                        continue;

                    maxZ *= static_cast<float>( fp_model.m_Scale.z );
                    maxZ += static_cast<float>( fp_model.m_Offset.z );

                    anyModelLoaded = true;

                    if( maxZ > modelHeight )
                        modelHeight = maxZ;
                }
            }
        }

        if( !anyModelLoaded )
        {
            // Declared a model but the cache couldn't resolve it →
            // assume tall component (connector). No declaration →
            // zero Z (silk/fab marker).
            modelHeight = anyModelDeclared ? kUnknownModelFallbackMm : 0.0f;
        }

        aDeclared = anyModelDeclared;
        aLoaded   = anyModelLoaded;

        // Use the BOARD_ADAPTER's F_Paste / B_Paste Z directly so the
        // OBB Z extent matches where the renderer actually places the
        // footprint base. Fallback to boardThickness/2 around inst.z
        // when no adapter is available.
        float fpZLocalMm = 0.0f;

        if( aAdapter )
        {
            const double biuTo3d  = aAdapter->BiuTo3dUnits();
            const float  zPosUnit = aAdapter->GetFootprintZPos( aFp->IsFlipped() );

            if( biuTo3d != 0.0 )
                fpZLocalMm = static_cast<float>( zPosUnit / biuTo3d / 1e6 );
        }
        else
        {
            const float boardThk = GetBoardThickness( aInst.board.get() );
            fpZLocalMm = aFp->IsFlipped() ? -boardThk * 0.5f
                                          :  boardThk * 0.5f;
        }

        const float fpBaseZ = aInst.position.z + fpZLocalMm;

        if( aFp->IsFlipped() )
        {
            // Flipped fp sits on bottom face; model extends *down* from
            // fp base.
            aZMax = fpBaseZ;
            aZMin = fpBaseZ - modelHeight;
        }
        else
        {
            // Non-flipped fp sits on top face; model extends *up* from
            // fp base.
            aZMin = fpBaseZ;
            aZMax = fpBaseZ + modelHeight;
        }
    };

    // OBB representation in world XY: center + axes + half-extents.
    // SAT (separating-axis theorem) on 4 axes (axisX/Y of each OBB) is
    // sufficient for 2D rectangle intersection. Per-instance rotation
    // around Z is the only assembly-pose rotation this models — board
    // tilt (X/Y rotation) is rare in practice and a phase-3 concern.
    //
    // `meshTris` holds the same footprint's 3D-model triangles in
    // world-mm space, used by the M6.E phase-3 narrow phase to tell
    // real penetration apart from "AABB overlap because a header pin
    // fits inside its mating socket's air space."
    struct FPCollisionShape
    {
        const FOOTPRINT*      fp;
        wxString              ref;
        glm::vec2             centerXY;     ///< world XY (mm)
        glm::vec2             axisX;        ///< unit vector
        glm::vec2             axisY;        ///< unit vector, ⊥ axisX
        float                 halfX;        ///< half-extent along axisX
        float                 halfY;        ///< half-extent along axisY
        float                 zMin;
        float                 zMax;
        SFVEC3F               worldMin;     ///< axis-aligned bounds for broad phase
        SFVEC3F               worldMax;
        std::vector<WorldTri> meshTris;     ///< world-mm triangles
        SFVEC3F               meshAabbMin;
        SFVEC3F               meshAabbMax;
        /// Footprint references at least one 3D-model file. Used to
        /// discriminate "real component whose mesh just hasn't loaded
        /// yet" (declared but no triangles → use OBB fallback) from
        /// "silk-only / fab-marker / unmodelled" (no declaration →
        /// no Z volume → never produces fallback boxes).
        bool                  hasDeclaredModels;
        bool                  hasLoadedModels;
    };

    // Build the world-mm triangle list for one footprint by walking
    // every visible FP_3DMODEL in its `Models()` and applying the same
    // transform chain the renderer uses (`render_3d_opengl.cpp` /
    // `raytracing/create_scene.cpp:1942-2030`) — minus the
    // BIU→3d-units scale (we want mm) and the renderer's Y-flip
    // (we keep KiCad's Y-positive convention for collision math).
    //
    // Chain: instance pose · footprint pose · (flip if back-side) ·
    //        FP_3DMODEL offset · -Rz · -Ry · -Rx · Scale
    auto buildFpMeshTris = [this]( const BOARD_3D_INSTANCE&  aInst,
                                    const BOARD_ADAPTER*      aAdapter,
                                    const FOOTPRINT*          aFp,
                                    std::vector<WorldTri>&    aOutTris,
                                    SFVEC3F&                  aOutAabbMin,
                                    SFVEC3F&                  aOutAabbMax,
                                    bool&                     aOutHaveAabb ) -> void
    {
        aOutTris.clear();
        aOutHaveAabb = false;

        if( !aInst.board || !aFp )
            return;

        PROJECT* prj = aInst.subProject ? aInst.subProject : aInst.board->GetProject();

        if( !prj )
            return;

        S3D_CACHE* cache = PROJECT_PCB::Get3DCacheManager( prj );

        if( !cache )
            return;

        // Footprint frame in world mm — Y-positive, no BIU→3d scale.
        // The fp Z is taken from the BOARD_ADAPTER's F_Paste / B_Paste
        // layer Z (the same value the renderer uses via
        // GetFootprintZPos), converted from 3D-units to mm. Without
        // this the AABB sat ~boardThickness/2 above the rendered model
        // because the renderer treats inst.position.z as the BOARD
        // CENTER (substrate centred around z=0 in board-local 3DU)
        // whereas a naive `inst.z + boardThickness` formulation puts
        // the fp at board *bottom* + thickness instead of board
        // *centre* + half-thickness + cu/paste offsets.
        const VECTOR2I fpPos = aFp->GetPosition();
        const float    fpx_mm = fpPos.x / 1e6f;
        const float    fpy_mm = fpPos.y / 1e6f;

        float fpZLocalMm = 0.0f;

        if( aAdapter )
        {
            const double biuTo3d  = aAdapter->BiuTo3dUnits();
            const float  zPosUnit = aAdapter->GetFootprintZPos( aFp->IsFlipped() );

            if( biuTo3d != 0.0 )
                fpZLocalMm = static_cast<float>( zPosUnit / biuTo3d / 1e6 );
        }
        else
        {
            // Fallback when no adapter is wired (shouldn't happen in
            // practice — every visible instance has one).
            const float boardThk = GetBoardThickness( aInst.board.get() );
            fpZLocalMm = aFp->IsFlipped() ? -boardThk * 0.5f
                                          :  boardThk * 0.5f;
        }

        const float fpz_mm = aInst.position.z + fpZLocalMm;

        // Inst rotation pivot is the board's GEOMETRIC CENTER (matches
        // the renderer's per-instance assembly pose, which shifts to
        // localCenterShared, rotates, then shifts back). Without this,
        // a non-zero board rotation rotates fps around the world
        // origin, putting them in wildly wrong positions and breaking
        // the AABBs.
        // BOARD_ADAPTER::GetBoardCenter() returns the centroid in the
        // renderer's CAD-frame (Y inverted from PCB-frame Y). Our
        // collision math works in PCB-frame Y (positive Y matches
        // FOOTPRINT::GetPosition() and BOARD_3D_INSTANCE::position) so
        // negate Y when reading. Without this the rotation pivot lands
        // at -boardCenter.y in PCB frame, swinging rotated footprints
        // ~2*|boardCenter.y| away from where the renderer puts them.
        glm::vec3 boardCenterMm( 0.0f );

        if( aAdapter )
        {
            const double biuTo3d = aAdapter->BiuTo3dUnits();

            if( biuTo3d != 0.0 )
            {
                const SFVEC3F bc = aAdapter->GetBoardCenter();
                boardCenterMm = glm::vec3(
                        static_cast<float>(  bc.x / biuTo3d / 1e6 ),
                        static_cast<float>( -bc.y / biuTo3d / 1e6 ),
                        static_cast<float>(  bc.z / biuTo3d / 1e6 ) );
            }
        }

        glm::mat4 instMat( 1.0f );
        instMat = glm::translate( instMat, glm::vec3( aInst.position.x,
                                                       aInst.position.y, 0.0f ) );
        // Pivot to board center for the rotation, then back. Matrix-
        // application order: -boardCenter → rotate → +boardCenter →
        // T(inst.xy).
        //
        // Rotation angles for axes that involve Y (Z and X) are
        // negated to match the renderer's CAD-frame rotation. Identity
        // F·R(θ)·F = R(-θ) for Z and X axis rotations under a Y-flip
        // (F = scale(1,-1,1)); Y-axis rotation is unaffected. Since
        // our chain Y-flips the model verts to PCB-Y but the renderer
        // keeps them in CAD-Y while applying the same panel rotation,
        // we negate Z and X to get the same final vertex position.
        instMat = glm::translate( instMat, boardCenterMm );
        instMat = glm::rotate( instMat, glm::radians( -aInst.rotation.z ),
                               glm::vec3( 0, 0, 1 ) );
        instMat = glm::rotate( instMat, glm::radians(  aInst.rotation.y ),
                               glm::vec3( 0, 1, 0 ) );
        instMat = glm::rotate( instMat, glm::radians( -aInst.rotation.x ),
                               glm::vec3( 1, 0, 0 ) );
        instMat = glm::translate( instMat, -boardCenterMm );

        glm::mat4 fpMat = instMat;
        fpMat = glm::translate( fpMat, glm::vec3( fpx_mm, fpy_mm, fpz_mm ) );

        // Y-axis convention bridge: KiCad's 3D-model vertices use the
        // CAD convention where +Y is "up" on the screen, which is the
        // OPPOSITE of the PCB editor's +Y (where +Y is screen-down).
        // The renderer compensates by negating the footprint position
        // Y in its `T(px*BiuTo3d, -py*BiuTo3d, zpos)` translation. We
        // work in board-frame Y-positive throughout, so we instead
        // mirror the model coords on Y BEFORE the fp-position shift.
        // Matrix order: this glm::scale appends, so it's APPLIED to
        // the vertex after the model + flip + orient transforms but
        // BEFORE the fp-position translation — exactly the spot the
        // renderer's chain implies.
        fpMat = glm::scale( fpMat, glm::vec3( 1.0f, -1.0f, 1.0f ) );

        const double fpOrientRad = aFp->GetOrientation().AsRadians();

        if( fpOrientRad != 0.0 )
            fpMat = glm::rotate( fpMat, static_cast<float>( fpOrientRad ),
                                 glm::vec3( 0, 0, 1 ) );

        if( aFp->IsFlipped() )
        {
            fpMat = glm::rotate( fpMat, glm::pi<float>(), glm::vec3( 0, 1, 0 ) );
            fpMat = glm::rotate( fpMat, glm::pi<float>(), glm::vec3( 0, 0, 1 ) );
        }

        for( const FP_3DMODEL& fp_model : aFp->Models() )
        {
            if( !fp_model.m_Show || fp_model.m_Filename.empty() )
                continue;

            std::vector<const EMBEDDED_FILES*> stack;
            stack.push_back( aFp->GetEmbeddedFiles() );
            stack.push_back( aInst.board->GetEmbeddedFiles() );
            const S3DMODEL* model = cache->GetModel( fp_model.m_Filename,
                                                     wxEmptyString,
                                                     std::move( stack ) );

            if( !model || model->m_MeshesSize == 0 )
                continue;

            glm::mat4 mtx = fpMat;
            mtx = glm::translate( mtx, glm::vec3( fp_model.m_Offset.x,
                                                   fp_model.m_Offset.y,
                                                   fp_model.m_Offset.z ) );
            mtx = glm::rotate( mtx,
                               glm::radians( -static_cast<float>( fp_model.m_Rotation.z ) ),
                               glm::vec3( 0, 0, 1 ) );
            mtx = glm::rotate( mtx,
                               glm::radians( -static_cast<float>( fp_model.m_Rotation.y ) ),
                               glm::vec3( 0, 1, 0 ) );
            mtx = glm::rotate( mtx,
                               glm::radians( -static_cast<float>( fp_model.m_Rotation.x ) ),
                               glm::vec3( 1, 0, 0 ) );
            mtx = glm::scale( mtx, glm::vec3( fp_model.m_Scale.x,
                                               fp_model.m_Scale.y,
                                               fp_model.m_Scale.z ) );

            for( unsigned int m = 0; m < model->m_MeshesSize; m++ )
            {
                const SMESH& mesh = model->m_Meshes[m];

                if( mesh.m_FaceIdxSize % 3 != 0 )
                    continue;

                // Pre-transform every vertex once. Mesh sizes for
                // typical connector models run 100s–few thousand
                // verts; an 18-flop mat4×vec4 each is fine.
                std::vector<glm::vec3> worldVerts( mesh.m_VertexSize );

                for( unsigned int v = 0; v < mesh.m_VertexSize; v++ )
                {
                    glm::vec4 p( mesh.m_Positions[v].x,
                                 mesh.m_Positions[v].y,
                                 mesh.m_Positions[v].z, 1.0f );
                    worldVerts[v] = glm::vec3( mtx * p );
                }

                for( unsigned int f = 0; f < mesh.m_FaceIdxSize; f += 3 )
                {
                    unsigned int i0 = mesh.m_FaceIdx[f];
                    unsigned int i1 = mesh.m_FaceIdx[f + 1];
                    unsigned int i2 = mesh.m_FaceIdx[f + 2];

                    if( i0 >= mesh.m_VertexSize || i1 >= mesh.m_VertexSize
                        || i2 >= mesh.m_VertexSize )
                        continue;

                    WorldTri t;
                    t.v0 = worldVerts[i0];
                    t.v1 = worldVerts[i1];
                    t.v2 = worldVerts[i2];

                    t.aabbMin = glm::vec3( std::min( { t.v0.x, t.v1.x, t.v2.x } ),
                                           std::min( { t.v0.y, t.v1.y, t.v2.y } ),
                                           std::min( { t.v0.z, t.v1.z, t.v2.z } ) );
                    t.aabbMax = glm::vec3( std::max( { t.v0.x, t.v1.x, t.v2.x } ),
                                           std::max( { t.v0.y, t.v1.y, t.v2.y } ),
                                           std::max( { t.v0.z, t.v1.z, t.v2.z } ) );

                    if( !aOutHaveAabb )
                    {
                        aOutAabbMin = SFVEC3F( t.aabbMin.x, t.aabbMin.y, t.aabbMin.z );
                        aOutAabbMax = SFVEC3F( t.aabbMax.x, t.aabbMax.y, t.aabbMax.z );
                        aOutHaveAabb = true;
                    }
                    else
                    {
                        aOutAabbMin.x = std::min( aOutAabbMin.x, t.aabbMin.x );
                        aOutAabbMin.y = std::min( aOutAabbMin.y, t.aabbMin.y );
                        aOutAabbMin.z = std::min( aOutAabbMin.z, t.aabbMin.z );
                        aOutAabbMax.x = std::max( aOutAabbMax.x, t.aabbMax.x );
                        aOutAabbMax.y = std::max( aOutAabbMax.y, t.aabbMax.y );
                        aOutAabbMax.z = std::max( aOutAabbMax.z, t.aabbMax.z );
                    }

                    aOutTris.push_back( t );
                }
            }
        }
    };

    auto buildShape = [&]( const BOARD_3D_INSTANCE& aInst,
                           const BOARD_ADAPTER*     aAdapter,
                           const FOOTPRINT*         aFp,
                           FPCollisionShape& aOut ) -> bool
    {
        if( !aInst.board || !aFp )
            return false;

        BOX2I bbox = aFp->GetBoundingBox( /*aIncludeText=*/false );
        const float lcx = bbox.GetCenter().x / 1e6f;
        const float lcy = bbox.GetCenter().y / 1e6f;
        const float lhx = ( bbox.GetWidth()  / 2.0f ) / 1e6f;
        const float lhy = ( bbox.GetHeight() / 2.0f ) / 1e6f;

        // Negate the Z rotation angle: the renderer rotates in CAD-Y
        // (Y-inverted from our PCB-Y collision frame). Identity
        // F·R_z(θ)·F = R_z(-θ) under a Y-flip — to land at the same
        // world position as the renderer, we apply -θ in PCB-Y. See
        // the matching comment in buildFpMeshTris.
        const float thetaRad = -aInst.rotation.z * static_cast<float>( M_PI ) / 180.0f;
        const float c = std::cos( thetaRad );
        const float s = std::sin( thetaRad );

        aOut.fp       = aFp;
        aOut.ref      = aFp->GetReference();
        aOut.axisX    = glm::vec2( c, s );
        aOut.axisY    = glm::vec2( -s, c );
        aOut.halfX    = lhx;
        aOut.halfY    = lhy;

        // Inst rotation pivots around the BOARD'S geometric center —
        // matching the renderer's per-instance assembly pose. Without
        // this, footprints rotate around the world origin and end up
        // at wildly wrong world XYs once the user sets a non-zero
        // board rotation.
        //
        // GetBoardCenter() is in CAD-Y; negate to bring into PCB-Y so
        // the pivot lands at the actual board centroid in our frame.
        float bcx_mm = 0.0f;
        float bcy_mm = 0.0f;

        if( aAdapter )
        {
            const double biuTo3d = aAdapter->BiuTo3dUnits();

            if( biuTo3d != 0.0 )
            {
                const SFVEC3F bc = aAdapter->GetBoardCenter();
                bcx_mm = static_cast<float>(  bc.x / biuTo3d / 1e6 );
                bcy_mm = static_cast<float>( -bc.y / biuTo3d / 1e6 );
            }
        }

        // Rotate (lcx, lcy) around (bcx_mm, bcy_mm), then translate
        // by inst.position.xy.
        const float dxLocal = lcx - bcx_mm;
        const float dyLocal = lcy - bcy_mm;
        aOut.centerXY = glm::vec2(
                ( c * dxLocal - s * dyLocal ) + bcx_mm + aInst.position.x,
                ( s * dxLocal + c * dyLocal ) + bcy_mm + aInst.position.y );

        computeFpZExtent( aInst, aAdapter, aFp, aOut.zMin, aOut.zMax,
                          aOut.hasDeclaredModels, aOut.hasLoadedModels );

        // Broad-phase AABB = axis-aligned envelope of the rotated rect.
        const glm::vec2 dx = aOut.axisX * lhx;
        const glm::vec2 dy = aOut.axisY * lhy;
        const float     spanX = std::abs( dx.x ) + std::abs( dy.x );
        const float     spanY = std::abs( dx.y ) + std::abs( dy.y );
        aOut.worldMin = SFVEC3F( aOut.centerXY.x - spanX, aOut.centerXY.y - spanY, aOut.zMin );
        aOut.worldMax = SFVEC3F( aOut.centerXY.x + spanX, aOut.centerXY.y + spanY, aOut.zMax );

        // Build per-footprint mesh-tri list eagerly. Small upfront
        // cost (matrix multiply per vertex) saved across all O(N²)
        // pair checks below.
        bool haveMeshAabb = false;
        buildFpMeshTris( aInst, aAdapter, aFp, aOut.meshTris,
                          aOut.meshAabbMin, aOut.meshAabbMax, haveMeshAabb );

        if( haveMeshAabb )
        {
            // Mesh is loaded — replace the pad/silk bbox-derived
            // worldMin/Max with the actual mesh AABB. The pad/silk
            // bbox tends to be looser than the physical mesh extent
            // (pads carry no Z volume, silk can extend past the part
            // body for documentation), so unioning inflated the
            // broad-phase AABB and caused false-positive pair-tests
            // that the mesh narrow phase then had to reject.
            aOut.worldMin = aOut.meshAabbMin;
            aOut.worldMax = aOut.meshAabbMax;
        }
        else
        {
            // No mesh — keep the pad/silk OBB envelope + Z fallback
            // already computed above, and use it as the mesh-AABB
            // proxy too.
            aOut.meshAabbMin = aOut.worldMin;
            aOut.meshAabbMax = aOut.worldMax;
        }

        return true;
    };

    // SAT XY-overlap test. Fast: 4 dot products and 1 abs per axis × 4
    // axes = O(constant). The per-axis interval test is the standard
    // "if separation axis exists, no overlap."
    auto obbXYOverlap = []( const FPCollisionShape& a, const FPCollisionShape& b ) -> bool
    {
        const glm::vec2 axes[4] = { a.axisX, a.axisY, b.axisX, b.axisY };
        const glm::vec2 d       = b.centerXY - a.centerXY;

        for( const glm::vec2& L : axes )
        {
            float radA = std::abs( glm::dot( a.axisX, L ) ) * a.halfX
                         + std::abs( glm::dot( a.axisY, L ) ) * a.halfY;
            float radB = std::abs( glm::dot( b.axisX, L ) ) * b.halfX
                         + std::abs( glm::dot( b.axisY, L ) ) * b.halfY;
            float dist = std::abs( glm::dot( d, L ) );

            if( dist > radA + radB )
                return false;
        }
        return true;
    };

    // Build per-instance shape lists.
    std::vector<std::vector<FPCollisionShape>> perInstance( m_boardInstances.size() );

    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        const BOARD_3D_INSTANCE& inst = m_boardInstances[i];

        if( !inst.visible || !inst.board )
            continue;

        const BOARD_ADAPTER* adapter = ( i < m_instanceAdapters.size() )
                                        ? m_instanceAdapters[i].get() : nullptr;

        for( const FOOTPRINT* fp : inst.board->Footprints() )
        {
            FPCollisionShape s;

            if( !buildShape( inst, adapter, fp, s ) )
                continue;

            // Skip silk-only / reference-marker / fab-only footprints
            // entirely. They have no declared 3D model, so no physical
            // volume to collide with — their AABB collapses to a zero-
            // thickness slab at the board top face, which can register
            // as "overlapping" with anything whose Z range crosses
            // that exact height (the AE1 / REF** false-positive case
            // from the diagnostic log).
            if( !s.hasDeclaredModels )
                continue;

            perInstance[i].push_back( std::move( s ) );
        }
    }

    // Debug: emit a BROAD entry for every per-fp AABB the broad phase
    // will use. With AABB debug toggled on, you see a thin blue
    // wireframe around each footprint's actual collision envelope —
    // useful for verifying that a footprint we expect to register an
    // overlap actually has the AABB extent we think it has. If two
    // wireframes that should obviously intersect don't, the issue is
    // in `buildShape` / `computeFpZExtent` / mesh-AABB union, not in
    // the pair test.
    if( m_showBroadAabbDebug )
    {
        for( size_t i = 0; i < perInstance.size(); i++ )
        {
            const KIID instUuid = m_boardInstances[i].uuid;

            for( const FPCollisionShape& fp : perInstance[i] )
            {
                OverlapBox dbg;
                dbg.minMm     = fp.worldMin;
                dbg.maxMm     = fp.worldMax;
                dbg.kind      = OVERLAP_KIND::BROAD;
                dbg.instanceA = instUuid;
                dbg.instanceB = instUuid;   // same instance — this is a per-fp box
                dbg.refA      = fp.ref;
                dbg.refB      = wxT( "<self>" );
                m_lastOverlapBoxes.push_back( dbg );
            }
        }
    }

    // Penetration threshold — collisions shallower than this are
    // demoted to a CONTACT highlight (yellow) rather than COLLISION
    // (red). 0.05 mm is well below pad thickness and typical model
    // tolerance, so anything above is a real collision.
    constexpr float kMinPenetrationMm = 0.05f;

    // Proximity threshold — pairs within this many mm of touching but
    // NOT penetrating get a CONTACT highlight too. Lets the user spot
    // "this header pin clears the housing by 0.2 mm" issues without
    // having to manually run a clearance pass.
    constexpr float kContactMarginMm  = 0.5f;

    // Cross-board cross-footprint pair check: inflated AABB broad →
    // OBB-XY narrow with proximity-aware penetration.
    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        if( perInstance[i].empty() )
            continue;

        for( size_t j = i + 1; j < m_boardInstances.size(); j++ )
        {
            if( perInstance[j].empty() )
                continue;

            const BOARD_3D_INSTANCE& inst1 = m_boardInstances[i];
            const BOARD_3D_INSTANCE& inst2 = m_boardInstances[j];

            for( const FPCollisionShape& a : perInstance[i] )
            {
                for( const FPCollisionShape& b : perInstance[j] )
                {
                    // Broad phase — AABB inflated by the contact margin
                    // on each side so we capture near-misses, not just
                    // actual overlaps. Margin is symmetric (each box
                    // gets the full margin), so two boxes 1.0 mm apart
                    // pass the broad phase if margin = 0.5 mm.
                    const float m = kContactMarginMm;
                    if( a.worldMax.x + m <= b.worldMin.x - m
                        || b.worldMax.x + m <= a.worldMin.x - m )
                        continue;
                    if( a.worldMax.y + m <= b.worldMin.y - m
                        || b.worldMax.y + m <= a.worldMin.y - m )
                        continue;
                    if( a.worldMax.z + m <= b.worldMin.z - m
                        || b.worldMax.z + m <= a.worldMin.z - m )
                        continue;

                    // Narrow phase — full OBB SAT (4 XY axes + Z). The
                    // SAT theorem says: if there is any axis on which
                    // the projected radii fail to overlap, the OBBs
                    // are separated. minPen is the signed min across
                    // all 4 XY axes plus Z; positive = overlap on
                    // every axis (real intersection), negative = gap
                    // of that magnitude on the worst-case axis.
                    //
                    // Earlier 2-axis variant only checked a's frame,
                    // giving false positives on rotated boards (it
                    // missed separating axes from b's frame).
                    auto axisPen = [&]( const glm::vec2& L ) -> float
                    {
                        float radA = std::abs( glm::dot( a.axisX, L ) ) * a.halfX
                                     + std::abs( glm::dot( a.axisY, L ) ) * a.halfY;
                        float radB = std::abs( glm::dot( b.axisX, L ) ) * b.halfX
                                     + std::abs( glm::dot( b.axisY, L ) ) * b.halfY;
                        float dist = std::abs( glm::dot( b.centerXY - a.centerXY, L ) );
                        return ( radA + radB ) - dist;
                    };

                    float xyPen = std::min( { axisPen( a.axisX ),
                                               axisPen( a.axisY ),
                                               axisPen( b.axisX ),
                                               axisPen( b.axisY ) } );

                    // Z is interval-only (no per-instance X/Y tilt
                    // yet) — same signed convention.
                    float zPenA = a.worldMax.z - b.worldMin.z;
                    float zPenB = b.worldMax.z - a.worldMin.z;
                    float zPen  = std::min( zPenA, zPenB );

                    float minPen = std::min( xyPen, zPen );

                    // Skip pairs whose nearest separation exceeds the
                    // contact margin — they're not even close.
                    if( minPen < -kContactMarginMm )
                        continue;

                    diagPairsBroad++;

                    bool mated = isMated( inst1.uuid, a.ref, inst2.uuid, b.ref );

                    // Helper used by both the mated and non-mated
                    // branches to grow a box around triangle vertices.
                    auto growBoxLocal = []( glm::vec3& aMin, glm::vec3& aMax,
                                            const glm::vec3& aP, bool& aHave )
                    {
                        if( !aHave )
                        {
                            aMin  = aMax = aP;
                            aHave = true;
                        }
                        else
                        {
                            aMin = glm::min( aMin, aP );
                            aMax = glm::max( aMax, aP );
                        }
                    };

                    // Mated pairs are EXPECTED to touch (that's mating)
                    // but the mesh-tri test still tells us whether
                    // they're at "proper contact" (parts fit as
                    // designed — meshes don't actually intersect, the
                    // pin sits in the socket's air space) vs. "over-
                    // penetration" (parts pushed too deep — meshes
                    // physically intersect, design issue).
                    if( mated )
                    {
                        // Mated pairs run the same mesh-level analysis
                        // as non-mated pairs. The COLLISION-vs-CONTACT
                        // verdict is driven by actual penetration
                        // depth (max signed distance of any
                        // intersecting triangle's vertex from the
                        // opposite triangle's plane), not by whether
                        // the pair is mated in the schematic. This
                        // catches "connector pushed too deep into the
                        // socket" — a real engineering concern even
                        // for intentionally mated pairs.
                        constexpr size_t kMaxTriVerts = 6 * 256;
                        // Per-project user-tunable threshold. Default
                        // 0.5 mm; the user can raise it for connectors
                        // with larger mating depth via the panel's
                        // "Collision threshold" field.
                        const float kCollisionPenThresholdMm = m_collisionThresholdMm;
                        std::vector<SFVEC3F> hitTriVertsM;

                        // Best closest tri pair when meshes don't
                        // actually intersect — used for "almost
                        // touching" visualization.
                        float                bestDistSqM = std::numeric_limits<float>::infinity();
                        std::vector<SFVEC3F> nearTriVertsM;

                        if( !a.meshTris.empty() && !b.meshTris.empty() )
                        {
                            const float marginM   = kContactMarginMm;
                            const float marginSqM = marginM * marginM;

                            for( const WorldTri& ta : a.meshTris )
                            {
                                for( const WorldTri& tb : b.meshTris )
                                {
                                    // Per-tri AABB broad phase — with
                                    // margin so the closest-pair
                                    // tracking covers near-touching
                                    // tris.
                                    if( ta.aabbMax.x + marginM < tb.aabbMin.x
                                        || tb.aabbMax.x + marginM < ta.aabbMin.x )
                                        continue;
                                    if( ta.aabbMax.y + marginM < tb.aabbMin.y
                                        || tb.aabbMax.y + marginM < ta.aabbMin.y )
                                        continue;
                                    if( ta.aabbMax.z + marginM < tb.aabbMin.z
                                        || tb.aabbMax.z + marginM < ta.aabbMin.z )
                                        continue;

                                    if( trianglesIntersect(
                                                ta.v0, ta.v1, ta.v2,
                                                tb.v0, tb.v1, tb.v2 ) )
                                    {
                                        if( hitTriVertsM.size() < kMaxTriVerts )
                                        {
                                            hitTriVertsM.push_back(
                                                    SFVEC3F( ta.v0.x, ta.v0.y, ta.v0.z ) );
                                            hitTriVertsM.push_back(
                                                    SFVEC3F( ta.v1.x, ta.v1.y, ta.v1.z ) );
                                            hitTriVertsM.push_back(
                                                    SFVEC3F( ta.v2.x, ta.v2.y, ta.v2.z ) );
                                            hitTriVertsM.push_back(
                                                    SFVEC3F( tb.v0.x, tb.v0.y, tb.v0.z ) );
                                            hitTriVertsM.push_back(
                                                    SFVEC3F( tb.v1.x, tb.v1.y, tb.v1.z ) );
                                            hitTriVertsM.push_back(
                                                    SFVEC3F( tb.v2.x, tb.v2.y, tb.v2.z ) );
                                        }
                                    }
                                    else if( hitTriVertsM.empty() )
                                    {
                                        // Only track closest pair when
                                        // we haven't found any actual
                                        // intersection yet.
                                        float d2;
                                        d2 = pointTriangleDistSq( ta.v0, tb.v0, tb.v1, tb.v2 );
                                        d2 = std::min( d2, pointTriangleDistSq( ta.v1, tb.v0, tb.v1, tb.v2 ) );
                                        d2 = std::min( d2, pointTriangleDistSq( ta.v2, tb.v0, tb.v1, tb.v2 ) );
                                        d2 = std::min( d2, pointTriangleDistSq( tb.v0, ta.v0, ta.v1, ta.v2 ) );
                                        d2 = std::min( d2, pointTriangleDistSq( tb.v1, ta.v0, ta.v1, ta.v2 ) );
                                        d2 = std::min( d2, pointTriangleDistSq( tb.v2, ta.v0, ta.v1, ta.v2 ) );

                                        if( d2 < bestDistSqM && d2 <= marginSqM )
                                        {
                                            bestDistSqM = d2;
                                            nearTriVertsM = {
                                                SFVEC3F( ta.v0.x, ta.v0.y, ta.v0.z ),
                                                SFVEC3F( ta.v1.x, ta.v1.y, ta.v1.z ),
                                                SFVEC3F( ta.v2.x, ta.v2.y, ta.v2.z ),
                                                SFVEC3F( tb.v0.x, tb.v0.y, tb.v0.z ),
                                                SFVEC3F( tb.v1.x, tb.v1.y, tb.v1.z ),
                                                SFVEC3F( tb.v2.x, tb.v2.y, tb.v2.z )
                                            };
                                        }
                                    }
                                }
                            }
                        }

                        std::vector<SFVEC3F>* tris = nullptr;
                        OVERLAP_KIND          kindM;
                        float                 overlapThicknessM = 0.0f;

                        if( !hitTriVertsM.empty() )
                        {
                            tris = &hitTriVertsM;

                            // "Overlap thickness" = minimum extent of
                            // the AABB containing all intersecting
                            // triangle vertices, across the X/Y/Z
                            // axes. This is the THINNEST dimension of
                            // the volume where the meshes interpene-
                            // trate. Tangent housing-wall contact
                            // produces a thin slab (small min extent);
                            // a connector pushed past its mating
                            // depth produces a chunky volume (large
                            // min extent). Threshold says "how much
                            // mesh-overlap thickness is acceptable
                            // before it's a real over-mate"; default
                            // is 0.5 mm, which gives normal connector
                            // mating fit room while flagging genuine
                            // over-penetration.
                            SFVEC3F tMin = ( *tris )[0];
                            SFVEC3F tMax = ( *tris )[0];

                            for( const SFVEC3F& v : *tris )
                            {
                                tMin.x = std::min( tMin.x, v.x );
                                tMin.y = std::min( tMin.y, v.y );
                                tMin.z = std::min( tMin.z, v.z );
                                tMax.x = std::max( tMax.x, v.x );
                                tMax.y = std::max( tMax.y, v.y );
                                tMax.z = std::max( tMax.z, v.z );
                            }

                            overlapThicknessM = std::min(
                                    { tMax.x - tMin.x,
                                      tMax.y - tMin.y,
                                      tMax.z - tMin.z } );

                            kindM = overlapThicknessM > kCollisionPenThresholdMm
                                            ? OVERLAP_KIND::COLLISION
                                            : OVERLAP_KIND::CONTACT;
                        }
                        else if( !nearTriVertsM.empty() )
                        {
                            tris  = &nearTriVertsM;
                            kindM = OVERLAP_KIND::CONTACT;
                        }

                        if( !tris )
                            continue;

                        // Compute AABB of the participating triangles
                        // for the box backstop.
                        SFVEC3F bMin = ( *tris )[0];
                        SFVEC3F bMax = ( *tris )[0];

                        for( const SFVEC3F& v : *tris )
                        {
                            bMin.x = std::min( bMin.x, v.x );
                            bMin.y = std::min( bMin.y, v.y );
                            bMin.z = std::min( bMin.z, v.z );
                            bMax.x = std::max( bMax.x, v.x );
                            bMax.y = std::max( bMax.y, v.y );
                            bMax.z = std::max( bMax.z, v.z );
                        }

                        OverlapBox box;
                        box.instanceA  = inst1.uuid;
                        box.instanceB  = inst2.uuid;
                        box.refA       = a.ref;
                        box.refB       = b.ref;
                        box.kind       = kindM;
                        box.minMm      = bMin;
                        box.maxMm      = bMax;
                        box.triVertsMm = std::move( *tris );
                        m_lastOverlapBoxes.push_back( box );

                        if( kindM == OVERLAP_KIND::COLLISION )
                        {
                            COLLISION_RESULT result;
                            result.board1Uuid = inst1.uuid;
                            result.board2Uuid = inst2.uuid;
                            result.item1Desc  = wxString::Format(
                                    wxT( "%s:%s" ), inst1.displayName, a.ref );
                            result.item2Desc  = wxString::Format(
                                    wxT( "%s:%s" ), inst2.displayName, b.ref );
                            glm::vec3 mid(
                                    ( bMin.x + bMax.x ) * 0.5f,
                                    ( bMin.y + bMax.y ) * 0.5f,
                                    ( bMin.z + bMax.z ) * 0.5f );
                            result.collisionPoint = SFVEC3F( mid.x, mid.y, mid.z );
                            result.penetrationMm  = overlapThicknessM;
                            m_lastCollisions.push_back( result );
                        }

                        continue;
                    }

                    // Skip pairs where BOTH footprints are purely
                    // mechanical (no electrical pads — mounting holes,
                    // fiducials, fab markers, edge cuts annotations).
                    // When boards stack, the mounting hole on top
                    // aligns with the one underneath BY DESIGN; that's
                    // not a collision the user wants to see flagged.
                    auto isMechanicalOnly = []( const FOOTPRINT* aFp ) -> bool
                    {
                        return aFp
                               && aFp->GetPadCount( DO_NOT_INCLUDE_NPTH ) == 0;
                    };

                    if( isMechanicalOnly( a.fp ) && isMechanicalOnly( b.fp ) )
                        continue;

                    // Debug visualization: blue wireframe AABB for
                    // every pair that survives the broad-phase pre-
                    // filter AND the post-filter (mated/mech-only).
                    // Lets the user visually verify the actual set of
                    // candidate pairs the narrow phase will run on —
                    // emitting BROAD before the filters meant the
                    // debug viz showed mounting-hole-vs-mounting-hole
                    // and other already-skipped pairs.
                    {
                        OverlapBox dbg;
                        dbg.minMm     = SFVEC3F(
                                std::max( a.worldMin.x, b.worldMin.x ),
                                std::max( a.worldMin.y, b.worldMin.y ),
                                std::max( a.worldMin.z, b.worldMin.z ) );
                        dbg.maxMm     = SFVEC3F(
                                std::min( a.worldMax.x, b.worldMax.x ),
                                std::min( a.worldMax.y, b.worldMax.y ),
                                std::min( a.worldMax.z, b.worldMax.z ) );
                        dbg.kind      = OVERLAP_KIND::BROAD;
                        dbg.instanceA = inst1.uuid;
                        dbg.instanceB = inst2.uuid;
                        dbg.refA      = a.ref;
                        dbg.refB      = b.ref;
                        m_lastOverlapBoxes.push_back( dbg );
                    }

                    // M6.E phase-3 mesh-level narrow phase. The OBB
                    // SAT above only knows about footprint pad/silk
                    // bboxes × model height, which doesn't tell a
                    // mated header from a collision — both have AABBs
                    // that deeply overlap. Walk the actual 3D-model
                    // triangles to discriminate.
                    //
                    // OBB fallback for one-sided no-mesh: if EITHER
                    // footprint has no mesh triangles, we can't run
                    // the proper narrow phase. Two cases:
                    //
                    //   • Both footprints are silk-only / no model
                    //     declared → no Z volume → skip; nothing to
                    //     collide.
                    //   • At least one footprint declared a model
                    //     that just hasn't loaded (virtual STEP file)
                    //     → fall back to OBB-only with a stricter
                    //     threshold (require real penetration, not
                    //     proximity) so we still flag connector-class
                    //     overlaps until the cache warms. CONTACT is
                    //     skipped in this path because the OBB
                    //     approximation is too coarse for proximity
                    //     calls.
                    if( a.meshTris.empty() || b.meshTris.empty() )
                    {
                        // Mesh not loaded for at least one side. We
                        // used to fall back to an OBB-rectangle
                        // highlight here, but the rectangle visibly
                        // diverges from the actual mesh shape and gave
                        // the impression that collision detection was
                        // approximate. Skip until the mesh cache is
                        // ready — auto-run will retry on the next
                        // refresh once the model has finished loading.
                        continue;
                    }

                    const float       margin   = kContactMarginMm;
                    const float       marginSq = margin * margin;
                    bool              anyHit   = false;
                    float             minDistSq = std::numeric_limits<float>::infinity();
                    glm::vec3         hitMin( 0.0f );
                    glm::vec3         hitMax( 0.0f );
                    bool              haveHit  = false;
                    glm::vec3         nearMin( 0.0f );
                    glm::vec3         nearMax( 0.0f );
                    bool              haveNear = false;

                    // Triangle vertices participating in the
                    // intersection. Capped to keep memory bounded.
                    // hitTriVerts: actually-intersecting tri pairs
                    // (used for COLLISION render). nearTriVerts:
                    // tri pairs within kContactMarginMm but not
                    // intersecting (used for CONTACT render — same
                    // mesh-tri visualization, just yellow not red).
                    constexpr size_t     kMaxTriVerts = 6 * 256;
                    std::vector<SFVEC3F> hitTriVerts;
                    std::vector<SFVEC3F> nearTriVerts;

                    auto growBox = [&]( glm::vec3& aMin, glm::vec3& aMax,
                                        const glm::vec3& aP, bool& aHave )
                    {
                        if( !aHave )
                        {
                            aMin = aMax = aP;
                            aHave = true;
                        }
                        else
                        {
                            aMin = glm::min( aMin, aP );
                            aMax = glm::max( aMax, aP );
                        }
                    };

                    // Mesh-level pair walk. Per-tri AABB inflated by
                    // contact margin keeps this cheap when the meshes
                    // are far apart on most triangles.
                    for( const WorldTri& ta : a.meshTris )
                    {
                        for( const WorldTri& tb : b.meshTris )
                        {
                            // Tri-tri AABB broad phase with margin.
                            if( ta.aabbMax.x + margin < tb.aabbMin.x
                                || tb.aabbMax.x + margin < ta.aabbMin.x )
                                continue;
                            if( ta.aabbMax.y + margin < tb.aabbMin.y
                                || tb.aabbMax.y + margin < ta.aabbMin.y )
                                continue;
                            if( ta.aabbMax.z + margin < tb.aabbMin.z
                                || tb.aabbMax.z + margin < ta.aabbMin.z )
                                continue;

                            if( trianglesIntersect( ta.v0, ta.v1, ta.v2,
                                                     tb.v0, tb.v1, tb.v2 ) )
                            {
                                anyHit = true;
                                growBox( hitMin, hitMax, ta.v0, haveHit );
                                growBox( hitMin, hitMax, ta.v1, haveHit );
                                growBox( hitMin, hitMax, ta.v2, haveHit );
                                growBox( hitMin, hitMax, tb.v0, haveHit );
                                growBox( hitMin, hitMax, tb.v1, haveHit );
                                growBox( hitMin, hitMax, tb.v2, haveHit );

                                if( hitTriVerts.size() < kMaxTriVerts )
                                {
                                    hitTriVerts.push_back(
                                            SFVEC3F( ta.v0.x, ta.v0.y, ta.v0.z ) );
                                    hitTriVerts.push_back(
                                            SFVEC3F( ta.v1.x, ta.v1.y, ta.v1.z ) );
                                    hitTriVerts.push_back(
                                            SFVEC3F( ta.v2.x, ta.v2.y, ta.v2.z ) );
                                    hitTriVerts.push_back(
                                            SFVEC3F( tb.v0.x, tb.v0.y, tb.v0.z ) );
                                    hitTriVerts.push_back(
                                            SFVEC3F( tb.v1.x, tb.v1.y, tb.v1.z ) );
                                    hitTriVerts.push_back(
                                            SFVEC3F( tb.v2.x, tb.v2.y, tb.v2.z ) );
                                }
                                continue;
                            }

                            // Not intersecting — track min vertex-to-
                            // triangle distance for CONTACT detection.
                            // Sample a's 3 vertices vs b and vice-versa
                            // → 6 point-tri tests per tri pair, far
                            // cheaper than full edge-edge closest-
                            // point but enough for connector-scale
                            // proximity checks.
                            float d2;

                            d2 = pointTriangleDistSq( ta.v0, tb.v0, tb.v1, tb.v2 );
                            if( d2 < minDistSq ) minDistSq = d2;
                            d2 = pointTriangleDistSq( ta.v1, tb.v0, tb.v1, tb.v2 );
                            if( d2 < minDistSq ) minDistSq = d2;
                            d2 = pointTriangleDistSq( ta.v2, tb.v0, tb.v1, tb.v2 );
                            if( d2 < minDistSq ) minDistSq = d2;
                            d2 = pointTriangleDistSq( tb.v0, ta.v0, ta.v1, ta.v2 );
                            if( d2 < minDistSq ) minDistSq = d2;
                            d2 = pointTriangleDistSq( tb.v1, ta.v0, ta.v1, ta.v2 );
                            if( d2 < minDistSq ) minDistSq = d2;
                            d2 = pointTriangleDistSq( tb.v2, ta.v0, ta.v1, ta.v2 );
                            if( d2 < minDistSq ) minDistSq = d2;

                            if( minDistSq <= marginSq )
                            {
                                // Track the bbox of the participating
                                // tri pair so the highlight box hugs
                                // the contact zone and not the whole
                                // footprint.
                                growBox( nearMin, nearMax, ta.v0, haveNear );
                                growBox( nearMin, nearMax, ta.v1, haveNear );
                                growBox( nearMin, nearMax, ta.v2, haveNear );
                                growBox( nearMin, nearMax, tb.v0, haveNear );
                                growBox( nearMin, nearMax, tb.v1, haveNear );
                                growBox( nearMin, nearMax, tb.v2, haveNear );

                                // Capture the actual tri verts so
                                // CONTACT can render as filled mesh
                                // tris (yellow), matching how
                                // COLLISION renders. Without this the
                                // CONTACT highlight falls back to the
                                // AABB rectangle, which looks
                                // inconsistent with the COLLISION
                                // mesh-tri render.
                                if( nearTriVerts.size() < kMaxTriVerts )
                                {
                                    nearTriVerts.push_back(
                                            SFVEC3F( ta.v0.x, ta.v0.y, ta.v0.z ) );
                                    nearTriVerts.push_back(
                                            SFVEC3F( ta.v1.x, ta.v1.y, ta.v1.z ) );
                                    nearTriVerts.push_back(
                                            SFVEC3F( ta.v2.x, ta.v2.y, ta.v2.z ) );
                                    nearTriVerts.push_back(
                                            SFVEC3F( tb.v0.x, tb.v0.y, tb.v0.z ) );
                                    nearTriVerts.push_back(
                                            SFVEC3F( tb.v1.x, tb.v1.y, tb.v1.z ) );
                                    nearTriVerts.push_back(
                                            SFVEC3F( tb.v2.x, tb.v2.y, tb.v2.z ) );
                                }
                            }
                        }
                    }

                    OVERLAP_KIND kind;
                    glm::vec3    boxMin, boxMax;

                    if( anyHit )
                    {
                        kind   = OVERLAP_KIND::COLLISION;
                        boxMin = hitMin;
                        boxMax = hitMax;
                    }
                    else if( haveNear )
                    {
                        kind   = OVERLAP_KIND::CONTACT;
                        boxMin = nearMin;
                        boxMax = nearMax;
                    }
                    else
                    {
                        // Mesh-level test cleared this pair — no
                        // intersection, no proximity. AABB false
                        // positive (e.g. mated header inside its
                        // socket's air space). Move on.
                        continue;
                    }

                    OverlapBox box;
                    box.minMm     = SFVEC3F( boxMin.x, boxMin.y, boxMin.z );
                    box.maxMm     = SFVEC3F( boxMax.x, boxMax.y, boxMax.z );
                    box.kind      = kind;
                    box.instanceA = inst1.uuid;
                    box.instanceB = inst2.uuid;
                    box.refA      = a.ref;
                    box.refB      = b.ref;

                    if( kind == OVERLAP_KIND::COLLISION )
                        box.triVertsMm = std::move( hitTriVerts );
                    else if( kind == OVERLAP_KIND::CONTACT )
                        box.triVertsMm = std::move( nearTriVerts );

                    m_lastOverlapBoxes.push_back( box );

                    // Only true collisions populate m_lastCollisions
                    // (which drives the panel status label).
                    if( kind == OVERLAP_KIND::COLLISION )
                    {
                        COLLISION_RESULT result;
                        result.board1Uuid = inst1.uuid;
                        result.board2Uuid = inst2.uuid;
                        result.item1Desc  = wxString::Format( wxT( "%s:%s" ),
                                                              inst1.displayName, a.ref );
                        result.item2Desc  = wxString::Format( wxT( "%s:%s" ),
                                                              inst2.displayName, b.ref );

                        glm::vec3 mid     = ( boxMin + boxMax ) * 0.5f;
                        result.collisionPoint = SFVEC3F( mid.x, mid.y, mid.z );
                        result.penetrationMm  = std::max( minPen,
                                                          kMinPenetrationMm );

                        m_lastCollisions.push_back( result );
                    }
                }
            }
        }
    }

    // ===== Board-substrate vs board-substrate collision =====
    //
    // Per-footprint pairs only fire when two components happen to sit
    // at matching XY positions across boards (mounting holes mostly).
    // The PCB substrate itself is not a footprint — it never enters
    // the per-fp loop above — so two boards completely overlapping in
    // 3D produced almost no boxes. That's the "embedded board, only
    // the corners flagged" case.
    //
    // This pass treats each visible board's edge-cuts bounding box ×
    // thickness as an OBB at the instance pose, and runs the same
    // AABB broad / OBB SAT narrow logic on every pair of visible
    // boards. Result: a single COLLISION box covering the substrate
    // intersection volume — exactly what the user expects when one
    // board is sitting inside another.
    struct BoardSubstrateShape
    {
        KIID      uuid;
        wxString  display;
        glm::vec2 centerXY;
        glm::vec2 axisX;
        glm::vec2 axisY;
        float     halfX;
        float     halfY;
        float     zMin;
        float     zMax;
        SFVEC3F   worldMin;
        SFVEC3F   worldMax;

        // 3D pose for the rotated slab — used by the substrate-vs-
        // substrate mesh-tri visualization. The 6 OBB face planes
        // are derived as (axisXYZWorld, worldCenter ± halfXYZ); the
        // 12 surface tris are built from the 8 world corners.
        glm::vec3 worldCenter;
        glm::vec3 axisXWorld;
        glm::vec3 axisYWorld;
        glm::vec3 axisZWorld;
        float     halfZ = 0.0f;
    };

    std::vector<BoardSubstrateShape> boardShapes;

    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        if( !inst.visible || !inst.board )
            continue;

        BOX2I bbox = inst.board->GetBoardEdgesBoundingBox();

        if( bbox.GetWidth() <= 0 || bbox.GetHeight() <= 0 )
            continue;   // placeholder / no outline

        const float lcx = bbox.GetCenter().x / 1e6f;
        const float lcy = bbox.GetCenter().y / 1e6f;
        const float lhx = ( bbox.GetWidth()  / 2.0f ) / 1e6f;
        const float lhy = ( bbox.GetHeight() / 2.0f ) / 1e6f;

        // OBB axes use Z-only rotation. Z-rotation angle is negated
        // for the same CAD-Y/PCB-Y reason buildShape negates it (see
        // identity F·R_z(θ)·F = R_z(-θ) under Y-flip). The X/Y tilt
        // rotations DON'T change the projection of the substrate onto
        // the XY plane — they're handled in the world-AABB derivation
        // below.
        const float thetaRad = -inst.rotation.z * static_cast<float>( M_PI ) / 180.0f;
        const float c = std::cos( thetaRad );
        const float s = std::sin( thetaRad );

        BoardSubstrateShape sh;
        sh.uuid     = inst.uuid;
        sh.display  = inst.displayName;
        sh.axisX    = glm::vec2( c, s );
        sh.axisY    = glm::vec2( -s, c );
        sh.halfX    = lhx;
        sh.halfY    = lhy;
        // Inst rotation pivots around the board's geometric center
        // (which IS lcx, lcy here). For the XY centre this is fine —
        // pure Z rotation around the bbox center keeps the centre at
        // (lcx, lcy). For the broad-phase world AABB though, X/Y
        // tilt swings the substrate corners up/down in world space,
        // so we derive worldMin/Max from the rotated 8 corners
        // below.
        sh.centerXY = glm::vec2( lcx + inst.position.x,
                                 lcy + inst.position.y );

        const float boardThickness = GetBoardThickness( inst.board.get() );

        // Substrate Z uses the renderer's centered convention — the
        // board body spans ±boardThickness/2 around inst.position.z
        // (board CENTER), not inst.z to inst.z+thk (board BOTTOM).
        // This matches the same fix applied to fp Z via
        // BOARD_ADAPTER::GetFootprintZPos. Without it, the substrate
        // AABB sat ~boardThickness/2 above where it should, and fps
        // on the OTHER board's top face fell INSIDE the (wrongly-
        // positioned) substrate volume, producing 26 phantom fpSub
        // collisions even when the boards weren't actually
        // interpenetrating.
        sh.zMin = inst.position.z - boardThickness * 0.5f;
        sh.zMax = inst.position.z + boardThickness * 0.5f;

        // Build world AABB from the substrate's 8 corners under the
        // FULL 3D rotation (X, Y, Z). Without this, a tilted board's
        // substrate broad-phase Z range stays at the un-tilted
        // ±thk/2, missing obvious substrate-vs-substrate overlaps
        // when the tilt sweeps the board through the other board's
        // Z range. Same negate-Z-and-X convention as buildShape
        // (matches the renderer's CAD-Y rotation in our PCB-Y
        // collision frame).
        glm::mat4 rotMat( 1.0f );
        rotMat = glm::rotate( rotMat, glm::radians( -inst.rotation.z ),
                              glm::vec3( 0, 0, 1 ) );
        rotMat = glm::rotate( rotMat, glm::radians(  inst.rotation.y ),
                              glm::vec3( 0, 1, 0 ) );
        rotMat = glm::rotate( rotMat, glm::radians( -inst.rotation.x ),
                              glm::vec3( 1, 0, 0 ) );

        const glm::vec3 pivot( lcx, lcy, inst.position.z );
        const glm::vec3 instTrans( inst.position.x, inst.position.y, 0.0f );
        const float     thkHalf = boardThickness * 0.5f;

        float wmnX = std::numeric_limits<float>::infinity();
        float wmnY = wmnX;
        float wmnZ = wmnX;
        float wmxX = -wmnX;
        float wmxY = -wmnX;
        float wmxZ = -wmnX;

        for( int sx = -1; sx <= 1; sx += 2 )
        {
            for( int sy = -1; sy <= 1; sy += 2 )
            {
                for( int sz = -1; sz <= 1; sz += 2 )
                {
                    glm::vec3 cornerLocal( lcx + sx * lhx,
                                            lcy + sy * lhy,
                                            inst.position.z + sz * thkHalf );
                    glm::vec4 rel( cornerLocal - pivot, 1.0f );
                    glm::vec3 rotated = glm::vec3( rotMat * rel );
                    glm::vec3 world   = pivot + rotated + instTrans;

                    wmnX = std::min( wmnX, world.x );
                    wmnY = std::min( wmnY, world.y );
                    wmnZ = std::min( wmnZ, world.z );
                    wmxX = std::max( wmxX, world.x );
                    wmxY = std::max( wmxY, world.y );
                    wmxZ = std::max( wmxZ, world.z );
                }
            }
        }

        sh.worldMin = SFVEC3F( wmnX, wmnY, wmnZ );
        sh.worldMax = SFVEC3F( wmxX, wmxY, wmxZ );

        // Replace zMin/zMax with the rotated-corner extents so the
        // narrow-phase Z interval test matches the broad-phase AABB.
        // Without this the OBB SAT's Z-axis penetration term keeps
        // using the un-tilted thin slab and rejects pairs that the
        // broad phase admits.
        sh.zMin = wmnZ;
        sh.zMax = wmxZ;

        // World-space slab axes. Used by the mesh-tri clip pass that
        // visualizes substrate-vs-substrate intersections as filled
        // tris (instead of the conservative AABB box). Apply rotMat
        // to the local OBB axes — for axis-aligned (un-rotated) input
        // these reduce to (1,0,0)/(0,1,0)/(0,0,1) in world.
        sh.axisXWorld = glm::vec3( rotMat * glm::vec4( 1, 0, 0, 0 ) );
        sh.axisYWorld = glm::vec3( rotMat * glm::vec4( 0, 1, 0, 0 ) );
        sh.axisZWorld = glm::vec3( rotMat * glm::vec4( 0, 0, 1, 0 ) );
        sh.worldCenter = pivot + instTrans;
        sh.halfZ      = thkHalf;

        boardShapes.push_back( sh );
    }

    for( size_t i = 0; i < boardShapes.size(); i++ )
    {
        for( size_t j = i + 1; j < boardShapes.size(); j++ )
        {
            const BoardSubstrateShape& a = boardShapes[i];
            const BoardSubstrateShape& b = boardShapes[j];

            // AABB broad phase, NOT inflated — board substrates within
            // 0.5 mm of touching but not overlapping is normal mating
            // and shouldn't flag as collision.
            if( a.worldMax.x <= b.worldMin.x || b.worldMax.x <= a.worldMin.x )
                continue;
            if( a.worldMax.y <= b.worldMin.y || b.worldMax.y <= a.worldMin.y )
                continue;
            if( a.worldMax.z <= b.worldMin.z || b.worldMax.z <= a.worldMin.z )
                continue;

            // OBB SAT (4 XY axes + Z interval).
            auto axisPen = [&]( const glm::vec2& L ) -> float
            {
                float radA = std::abs( glm::dot( a.axisX, L ) ) * a.halfX
                             + std::abs( glm::dot( a.axisY, L ) ) * a.halfY;
                float radB = std::abs( glm::dot( b.axisX, L ) ) * b.halfX
                             + std::abs( glm::dot( b.axisY, L ) ) * b.halfY;
                float dist = std::abs( glm::dot( b.centerXY - a.centerXY, L ) );
                return ( radA + radB ) - dist;
            };

            float xyPen = std::min( { axisPen( a.axisX ), axisPen( a.axisY ),
                                       axisPen( b.axisX ), axisPen( b.axisY ) } );
            float zPen  = std::min( a.worldMax.z - b.worldMin.z,
                                     b.worldMax.z - a.worldMin.z );
            float minPen = std::min( xyPen, zPen );

            if( minPen <= 0.0f )
                continue;   // separated — boards aren't actually overlapping

            // Debug AABB box (blue wireframe) — every overlapping
            // substrate pair, regardless of how thin the overlap.
            {
                OverlapBox dbg;
                dbg.minMm     = SFVEC3F(
                        std::max( a.worldMin.x, b.worldMin.x ),
                        std::max( a.worldMin.y, b.worldMin.y ),
                        std::max( a.worldMin.z, b.worldMin.z ) );
                dbg.maxMm     = SFVEC3F(
                        std::min( a.worldMax.x, b.worldMax.x ),
                        std::min( a.worldMax.y, b.worldMax.y ),
                        std::min( a.worldMax.z, b.worldMax.z ) );
                dbg.kind      = OVERLAP_KIND::BROAD;
                dbg.instanceA = a.uuid;
                dbg.instanceB = b.uuid;
                dbg.refA      = wxT( "<board>" );
                dbg.refB      = wxT( "<board>" );
                m_lastOverlapBoxes.push_back( dbg );
            }

            // Confirmed COLLISION when penetration is substantial.
            if( minPen < kMinPenetrationMm )
                continue;

            // Build the 12 surface tris of a substrate slab in world
            // space. 8 corners → 6 faces × 2 tris. Returned vector is
            // 12 triangles; each triangle is 3 consecutive verts.
            auto buildSlabTris =
                    []( const BoardSubstrateShape& aSh ) -> std::vector<glm::vec3>
            {
                const glm::vec3& C  = aSh.worldCenter;
                const glm::vec3  ex = aSh.axisXWorld * aSh.halfX;
                const glm::vec3  ey = aSh.axisYWorld * aSh.halfY;
                const glm::vec3  ez = aSh.axisZWorld * aSh.halfZ;

                // 8 corners: signs for (x, y, z) extents.
                glm::vec3 c[8] = {
                    C - ex - ey - ez, // 0  -X-Y-Z
                    C + ex - ey - ez, // 1  +X-Y-Z
                    C + ex + ey - ez, // 2  +X+Y-Z
                    C - ex + ey - ez, // 3  -X+Y-Z
                    C - ex - ey + ez, // 4  -X-Y+Z
                    C + ex - ey + ez, // 5  +X-Y+Z
                    C + ex + ey + ez, // 6  +X+Y+Z
                    C - ex + ey + ez, // 7  -X+Y+Z
                };

                // 6 quads, each split into 2 tris. Wind so the normal
                // points outward (not strictly required for the clip
                // pass — Sutherland-Hodgman is winding-agnostic — but
                // keeps the triangulation tidy).
                auto quad = []( std::vector<glm::vec3>& aOut,
                                 const glm::vec3& aA, const glm::vec3& aB,
                                 const glm::vec3& aC, const glm::vec3& aD )
                {
                    aOut.push_back( aA ); aOut.push_back( aB ); aOut.push_back( aC );
                    aOut.push_back( aA ); aOut.push_back( aC ); aOut.push_back( aD );
                };

                std::vector<glm::vec3> tris;
                tris.reserve( 36 );
                quad( tris, c[0], c[1], c[2], c[3] );  // -Z (bottom)
                quad( tris, c[4], c[7], c[6], c[5] );  // +Z (top)
                quad( tris, c[0], c[3], c[7], c[4] );  // -X
                quad( tris, c[1], c[5], c[6], c[2] );  // +X
                quad( tris, c[0], c[4], c[5], c[1] );  // -Y
                quad( tris, c[2], c[6], c[7], c[3] );  // +Y
                return tris;
            };

            // Sutherland-Hodgman clip of a polygon against one OBB
            // plane. The "inside" half-space is dot(p - faceCenter,
            // faceNormal) <= 0 (faceNormal points outward). Same
            // structure as the axis-aligned clipAxis lambda used in
            // fpSub, just on an arbitrary plane.
            auto clipPlane =
                    []( const std::vector<glm::vec3>& aIn,
                        std::vector<glm::vec3>&        aOut,
                        const glm::vec3&               aFaceCenter,
                        const glm::vec3&               aFaceNormal )
            {
                aOut.clear();

                if( aIn.empty() )
                    return;

                auto signedDist = [&]( const glm::vec3& aP ) -> float
                {
                    return glm::dot( aP - aFaceCenter, aFaceNormal );
                };

                glm::vec3 prev    = aIn.back();
                float     prevD   = signedDist( prev );
                bool      prevIn  = ( prevD <= 0.0f );

                for( const glm::vec3& curr : aIn )
                {
                    float currD  = signedDist( curr );
                    bool  currIn = ( currD <= 0.0f );

                    if( currIn )
                    {
                        if( !prevIn )
                        {
                            float denom = prevD - currD;
                            float t     = ( std::abs( denom ) > 1e-9f )
                                              ? ( prevD / denom ) : 0.5f;
                            aOut.push_back( prev + t * ( curr - prev ) );
                        }
                        aOut.push_back( curr );
                    }
                    else if( prevIn )
                    {
                        float denom = prevD - currD;
                        float t     = ( std::abs( denom ) > 1e-9f )
                                          ? ( prevD / denom ) : 0.5f;
                        aOut.push_back( prev + t * ( curr - prev ) );
                    }

                    prev   = curr;
                    prevD  = currD;
                    prevIn = currIn;
                }
            };

            // Clip every tri in `aSourceTris` against `aClipper`'s 6
            // OBB faces. Append the inside polygon (fan-triangulated)
            // to aOut. This is the symmetric mesh-tri visualization
            // for substrate-vs-substrate collisions: the user sees
            // the actual surface of one board that sits inside the
            // other's volume, instead of an AABB box hovering over
            // both.
            auto clipSlabTris =
                    [&]( const std::vector<glm::vec3>&  aSourceTris,
                         const BoardSubstrateShape&     aClipper,
                         std::vector<SFVEC3F>&          aOut )
            {
                const glm::vec3 faces[6][2] = {
                    { aClipper.worldCenter + aClipper.axisXWorld * aClipper.halfX,
                       aClipper.axisXWorld },
                    { aClipper.worldCenter - aClipper.axisXWorld * aClipper.halfX,
                      -aClipper.axisXWorld },
                    { aClipper.worldCenter + aClipper.axisYWorld * aClipper.halfY,
                       aClipper.axisYWorld },
                    { aClipper.worldCenter - aClipper.axisYWorld * aClipper.halfY,
                      -aClipper.axisYWorld },
                    { aClipper.worldCenter + aClipper.axisZWorld * aClipper.halfZ,
                       aClipper.axisZWorld },
                    { aClipper.worldCenter - aClipper.axisZWorld * aClipper.halfZ,
                      -aClipper.axisZWorld },
                };

                std::vector<glm::vec3> bufA, bufB;

                constexpr size_t kMaxBoardTriVerts = 6 * 256;

                for( size_t i = 0; i + 2 < aSourceTris.size(); i += 3 )
                {
                    if( aOut.size() >= kMaxBoardTriVerts )
                        break;

                    bufA = { aSourceTris[i], aSourceTris[i + 1],
                             aSourceTris[i + 2] };

                    for( int f = 0; f < 6; f++ )
                    {
                        clipPlane( bufA, bufB, faces[f][0], faces[f][1] );
                        std::swap( bufA, bufB );
                        if( bufA.empty() ) break;
                    }

                    if( bufA.size() < 3 )
                        continue;

                    for( size_t k = 1; k + 1 < bufA.size(); k++ )
                    {
                        if( aOut.size() + 3 > kMaxBoardTriVerts )
                            break;

                        aOut.push_back( SFVEC3F( bufA[0].x, bufA[0].y, bufA[0].z ) );
                        aOut.push_back( SFVEC3F( bufA[k].x, bufA[k].y, bufA[k].z ) );
                        aOut.push_back( SFVEC3F( bufA[k + 1].x,
                                                  bufA[k + 1].y,
                                                  bufA[k + 1].z ) );
                    }
                }
            };

            std::vector<glm::vec3> aSlabTris = buildSlabTris( a );
            std::vector<glm::vec3> bSlabTris = buildSlabTris( b );
            std::vector<SFVEC3F>   triVerts;

            clipSlabTris( aSlabTris, b, triVerts );  // a's surface inside b
            clipSlabTris( bSlabTris, a, triVerts );  // b's surface inside a

            OverlapBox box;
            box.minMm     = SFVEC3F(
                    std::max( a.worldMin.x, b.worldMin.x ),
                    std::max( a.worldMin.y, b.worldMin.y ),
                    std::max( a.worldMin.z, b.worldMin.z ) );
            box.maxMm     = SFVEC3F(
                    std::min( a.worldMax.x, b.worldMax.x ),
                    std::min( a.worldMax.y, b.worldMax.y ),
                    std::min( a.worldMax.z, b.worldMax.z ) );
            box.kind      = OVERLAP_KIND::COLLISION;
            box.instanceA = a.uuid;
            box.instanceB = b.uuid;
            box.refA      = wxT( "<board>" );
            box.refB      = wxT( "<board>" );
            box.triVertsMm = std::move( triVerts );
            m_lastOverlapBoxes.push_back( box );

            COLLISION_RESULT result;
            result.board1Uuid = a.uuid;
            result.board2Uuid = b.uuid;
            result.item1Desc  = wxString::Format( wxT( "%s:<board>" ), a.display );
            result.item2Desc  = wxString::Format( wxT( "%s:<board>" ), b.display );

            glm::vec2 midXY = ( a.centerXY + b.centerXY ) * 0.5f;
            float     midZ  = ( std::max( a.worldMin.z, b.worldMin.z )
                                + std::min( a.worldMax.z, b.worldMax.z ) ) * 0.5f;
            result.collisionPoint = SFVEC3F( midXY.x, midXY.y, midZ );
            result.penetrationMm  = minPen;
            m_lastCollisions.push_back( result );
        }
    }

    // ===== Footprint vs OTHER board's substrate =====
    //
    // The most common "boards interpenetrating" case isn't fp-vs-fp
    // overlap — it's a component on board A poking through board B's
    // PCB substrate. Two board layouts with components scattered at
    // different XYs rarely produce fp-vs-fp AABB overlaps even when
    // their substrates clearly intersect. So an fp on board A whose
    // mesh AABB extends into board B's substrate volume is a real
    // collision the user expects to see flagged.
    //
    // Implementation: AABB intersection test between each fp's
    // worldMin/Max (already computed above, mesh-tightened) and each
    // OTHER instance's substrate AABB. No mesh-tri test against the
    // substrate (the substrate is just a slab, no real mesh data
    // we'd want to test against per-tri), so this is broad-phase
    // only — but it correctly catches the "tall component clears the
    // other board" case.
    //
    // Skipped when:
    // - The fp belongs to the same instance as the substrate (a fp
    //   intersects its own substrate by definition; that's normal).
    // - The fp is mated to anything on the other instance (mating
    //   connectors poke through pads, that's intentional).
    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        if( perInstance[i].empty() )
            continue;

        for( const BoardSubstrateShape& sub : boardShapes )
        {
            if( sub.uuid == m_boardInstances[i].uuid )
                continue;   // same instance — fp on its own board

            for( const FPCollisionShape& fp : perInstance[i] )
            {
                // AABB overlap test against the other board's
                // substrate volume.
                if( fp.worldMax.x <= sub.worldMin.x
                    || sub.worldMax.x <= fp.worldMin.x )
                    continue;
                if( fp.worldMax.y <= sub.worldMin.y
                    || sub.worldMax.y <= fp.worldMin.y )
                    continue;
                if( fp.worldMax.z <= sub.worldMin.z
                    || sub.worldMax.z <= fp.worldMin.z )
                    continue;

                // Skip if this fp is mated to ANY footprint on the
                // other instance — connector pins legitimately pass
                // through pads/holes on the mating board.
                bool fpMated = false;

                for( const MATE_EDGE& edge : mateEdges )
                {
                    for( const MATE_PAIR& p : edge.pairs )
                    {
                        if( p.alignmentOnly )
                            continue;

                        const KIID& fpInst = m_boardInstances[i].uuid;

                        bool dirA = p.instanceA == fpInst
                                    && p.footprintRefA == fp.ref
                                    && p.instanceB == sub.uuid;
                        bool dirB = p.instanceB == fpInst
                                    && p.footprintRefB == fp.ref
                                    && p.instanceA == sub.uuid;

                        if( dirA || dirB )
                        {
                            fpMated = true;
                            break;
                        }
                    }

                    if( fpMated )
                        break;
                }

                if( fpMated )
                {
                    // Mated fp's relationship with the other board is
                    // already represented by the fp-vs-fp pair test
                    // (which runs the mesh-tri test and tags
                    // CONTACT or COLLISION depending on whether the
                    // mate is over-penetrating). Don't ALSO emit a
                    // substrate-level box — that would double up the
                    // visualization at the same location.
                    continue;
                }

                // Mech-only fp going into the other board's substrate
                // is usually a mounting hole on a board sandwiched
                // between two layers — flag it; the user can decide
                // whether the alignment is intentional.

                SFVEC3F bMin( std::max( fp.worldMin.x, sub.worldMin.x ),
                              std::max( fp.worldMin.y, sub.worldMin.y ),
                              std::max( fp.worldMin.z, sub.worldMin.z ) );
                SFVEC3F bMax( std::min( fp.worldMax.x, sub.worldMax.x ),
                              std::min( fp.worldMax.y, sub.worldMax.y ),
                              std::min( fp.worldMax.z, sub.worldMax.z ) );

                // Collect only the portion of each fp tri that's
                // physically inside the substrate slab. Sutherland-
                // Hodgman clip against the 6 substrate AABB planes
                // produces a convex polygon (≤9 verts from a tri); we
                // fan-triangulate that polygon and emit the resulting
                // tris as triVertsMm. The user sees a tight highlight
                // hugging the actual intrusion — no over-large tris
                // hanging out of the parent board, no false positives
                // from tris that just brushed the AABB.
                constexpr size_t     kMaxFpSubTriVerts = 6 * 256;
                std::vector<SFVEC3F> intrudeTriVerts;

                // axis = 0/1/2 (X/Y/Z), keepGreater controls which
                // half-space survives: true keeps coord >= bound
                // (used for the worldMin face), false keeps
                // coord <= bound (used for the worldMax face).
                auto clipAxis = []( std::vector<glm::vec3>& aIn,
                                     std::vector<glm::vec3>& aOut,
                                     int aAxis, float aBound,
                                     bool aKeepGreater )
                {
                    aOut.clear();

                    if( aIn.empty() )
                        return;

                    auto inside = [&]( const glm::vec3& aP ) -> bool
                    {
                        const float c = aP[aAxis];
                        return aKeepGreater ? ( c >= aBound ) : ( c <= aBound );
                    };

                    auto cross = [&]( const glm::vec3& aA,
                                       const glm::vec3& aB ) -> glm::vec3
                    {
                        const float da = aA[aAxis] - aBound;
                        const float db = aB[aAxis] - aBound;
                        const float denom = ( da - db );

                        // Degenerate: both verts on the plane (denom 0)
                        // — fall back to midpoint to avoid div-by-zero.
                        const float t = ( std::abs( denom ) > 1e-9f )
                                            ? ( da / denom )
                                            : 0.5f;
                        return aA + t * ( aB - aA );
                    };

                    glm::vec3 prev       = aIn.back();
                    bool      prevInside = inside( prev );

                    for( const glm::vec3& curr : aIn )
                    {
                        bool currInside = inside( curr );

                        if( currInside )
                        {
                            if( !prevInside )
                                aOut.push_back( cross( prev, curr ) );
                            aOut.push_back( curr );
                        }
                        else if( prevInside )
                        {
                            aOut.push_back( cross( prev, curr ) );
                        }

                        prev       = curr;
                        prevInside = currInside;
                    }
                };

                std::vector<glm::vec3> bufA, bufB;

                for( const WorldTri& t : fp.meshTris )
                {
                    if( intrudeTriVerts.size() >= kMaxFpSubTriVerts )
                        break;

                    // Cheap reject: tri AABB fully outside substrate
                    // AABB → no intrusion.
                    if( t.aabbMax.x < sub.worldMin.x
                        || t.aabbMin.x > sub.worldMax.x
                        || t.aabbMax.y < sub.worldMin.y
                        || t.aabbMin.y > sub.worldMax.y
                        || t.aabbMax.z < sub.worldMin.z
                        || t.aabbMin.z > sub.worldMax.z )
                        continue;

                    bufA = { t.v0, t.v1, t.v2 };

                    clipAxis( bufA, bufB, 0, sub.worldMin.x, true  );
                    clipAxis( bufB, bufA, 0, sub.worldMax.x, false );
                    clipAxis( bufA, bufB, 1, sub.worldMin.y, true  );
                    clipAxis( bufB, bufA, 1, sub.worldMax.y, false );
                    clipAxis( bufA, bufB, 2, sub.worldMin.z, true  );
                    clipAxis( bufB, bufA, 2, sub.worldMax.z, false );

                    // bufA holds the final clipped polygon. Fan-
                    // triangulate (verts[0] + i + i+1) into the output.
                    if( bufA.size() < 3 )
                        continue;

                    for( size_t k = 1; k + 1 < bufA.size(); k++ )
                    {
                        if( intrudeTriVerts.size() + 3 > kMaxFpSubTriVerts )
                            break;

                        intrudeTriVerts.push_back(
                                SFVEC3F( bufA[0].x, bufA[0].y, bufA[0].z ) );
                        intrudeTriVerts.push_back(
                                SFVEC3F( bufA[k].x, bufA[k].y, bufA[k].z ) );
                        intrudeTriVerts.push_back(
                                SFVEC3F( bufA[k + 1].x,
                                          bufA[k + 1].y,
                                          bufA[k + 1].z ) );
                    }
                }

                // No tri actually inside the substrate volume — pure
                // AABB false positive (fp's bbox brushes the sub
                // bbox but the geometry doesn't actually intrude).
                // Skip; don't push a misleading box.
                if( intrudeTriVerts.empty() )
                    continue;

                OverlapBox box;
                box.minMm     = bMin;
                box.maxMm     = bMax;
                box.kind      = OVERLAP_KIND::COLLISION;
                box.instanceA = m_boardInstances[i].uuid;
                box.instanceB = sub.uuid;
                box.refA      = fp.ref;
                box.refB      = wxT( "<board>" );
                box.triVertsMm = std::move( intrudeTriVerts );
                m_lastOverlapBoxes.push_back( box );

                COLLISION_RESULT result;
                result.board1Uuid = m_boardInstances[i].uuid;
                result.board2Uuid = sub.uuid;
                result.item1Desc  = wxString::Format(
                        wxT( "%s:%s" ), m_boardInstances[i].displayName, fp.ref );
                result.item2Desc  = wxString::Format(
                        wxT( "%s:<board>" ), sub.display );

                glm::vec3 mid = ( glm::vec3( bMin.x, bMin.y, bMin.z )
                                  + glm::vec3( bMax.x, bMax.y, bMax.z ) ) * 0.5f;
                result.collisionPoint = SFVEC3F( mid.x, mid.y, mid.z );
                result.penetrationMm  = kMinPenetrationMm;
                m_lastCollisions.push_back( result );
            }
        }
    }

    // Rotation-debug dump: when any instance is rotated AND fp-vs-fp
    // broad found nothing, emit per-fp world AABBs so we can see where
    // each footprint is actually positioned in world space. Lets the
    // user verify visually-overlapping connectors really are landing at
    // overlapping AABBs (or not) under non-trivial board rotations.
    bool anyRotated = false;

    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        if( !inst.visible )
            continue;

        if( std::abs( inst.rotation.x ) > 0.01f
            || std::abs( inst.rotation.y ) > 0.01f
            || std::abs( inst.rotation.z ) > 0.01f )
        {
            anyRotated = true;
            break;
        }
    }

    if( anyRotated && diagPairsBroad == 0 && m_boardInstances.size() >= 2 )
    {
        wxLogMessage( wxT( "[BROAD-DUMP] rotated boards but broad=0 — per-fp AABBs:" ) );

        for( size_t i = 0; i < perInstance.size(); i++ )
        {
            const BOARD_3D_INSTANCE& inst = m_boardInstances[i];

            wxLogMessage( wxT( "[BROAD-DUMP]   inst[%zu] '%s' pos=(%.2f,%.2f,%.2f) "
                               "rot=(%.1f,%.1f,%.1f) fps=%zu" ),
                          i, inst.displayName,
                          inst.position.x, inst.position.y, inst.position.z,
                          inst.rotation.x, inst.rotation.y, inst.rotation.z,
                          perInstance[i].size() );

            for( const FPCollisionShape& fp : perInstance[i] )
            {
                wxLogMessage( wxT( "[BROAD-DUMP]     %s aabb=[%.2f,%.2f,%.2f]→"
                                   "[%.2f,%.2f,%.2f] mesh=%zu" ),
                              fp.ref,
                              fp.worldMin.x, fp.worldMin.y, fp.worldMin.z,
                              fp.worldMax.x, fp.worldMax.y, fp.worldMax.z,
                              fp.meshTris.size() );
            }
        }
    }

    return m_lastCollisions;
}


// File-static so the 3d-viewer library has no link-time reference to
// the pcbnew-only OCCT exporter. The pcbnew kiface populates this
// pointer at startup; cvpcb's footprint preview leaves it null.
static ASSEMBLY_3D_MANAGER::STEPExportCallback g_stepExportCallback = nullptr;


void ASSEMBLY_3D_MANAGER::SetSTEPExportCallback( STEPExportCallback aCallback )
{
    g_stepExportCallback = aCallback;
}


bool ASSEMBLY_3D_MANAGER::ExportAssemblySTEP( const wxString& aFilename )
{
    if( !g_stepExportCallback )
        return false;

    std::vector<STEPBoardEntry> entries;
    entries.reserve( m_boardInstances.size() );

    for( const BOARD_3D_INSTANCE& inst : m_boardInstances )
    {
        if( !inst.visible || !inst.board )
            continue;

        STEPBoardEntry e;
        e.board       = inst.board.get();
        e.name        = inst.displayName.IsEmpty() ? wxString( wxT( "board" ) )
                                                    : inst.displayName;
        e.positionMm  = inst.position;
        e.rotationDeg = inst.rotation;
        entries.push_back( std::move( e ) );
    }

    if( entries.empty() )
        return false;

    return g_stepExportCallback( entries, aFilename );
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


float ASSEMBLY_3D_MANAGER::GetBoardThickness( const BOARD* aBoard ) const
{
    if( !aBoard )
        return 1.6f;  // Default 1.6mm

    const BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();
    return static_cast<float>( bds.GetBoardThickness() ) / 1000000.0f;
}


// ========== M6.G persistence helpers ==========

void ASSEMBLY_3D_MANAGER::persistInstanceState( const BOARD_3D_INSTANCE& aInst )
{
    if( !m_project )
        return;

    PROJECT_FILE& pf = m_project->GetProjectFile();

    if( !pf.IsMultiBoardContainer() )
        return;

    // Anonymous instances (added via AddBoardInstance with a null
    // sub-project UUID) have no stable key to persist against.
    if( aInst.subProjectUuid == niluuid )
        return;

    // Read-modify-write through the setter so the T3 multi-board
    // observer chain marks the project dirty and the state actually
    // gets flushed to .kicad_pro on save / app close. Writing through
    // GetAssemblyInstances()'s non-const reference directly was the
    // root of MOON-1280: in-memory state updated, project never
    // dirtied, positions reset to last-saved values on next open.
    std::vector<ASSEMBLY_INSTANCE_STATE> states = pf.GetAssemblyInstances();
    auto it = std::find_if( states.begin(), states.end(),
                             [&]( const ASSEMBLY_INSTANCE_STATE& s )
                             { return s.subProjectUuid == aInst.subProjectUuid; } );

    if( it == states.end() )
    {
        ASSEMBLY_INSTANCE_STATE s;
        s.subProjectUuid = aInst.subProjectUuid;
        states.push_back( s );
        it = states.end() - 1;
    }

    it->position    = VECTOR3D( aInst.position.x, aInst.position.y, aInst.position.z );
    it->rotation    = VECTOR3D( aInst.rotation.x, aInst.rotation.y, aInst.rotation.z );
    it->visible     = aInst.visible;
    it->transparent = aInst.transparent;
    it->opacity     = aInst.opacity;

    pf.SetAssemblyInstances( std::move( states ) );
}


void ASSEMBLY_3D_MANAGER::SetCollisionThresholdMm( float aMm )
{
    m_collisionThresholdMm = aMm;

    // Push through to the project file so the value survives close/
    // reopen. SetCollisionThresholdMm short-circuits when value is
    // unchanged so this is safe to call from the panel's text-changed
    // handler without spurious dirty marks.
    if( m_project )
    {
        PROJECT_FILE& pf = m_project->GetProjectFile();

        if( pf.IsMultiBoardContainer() )
            pf.SetCollisionThresholdMm( static_cast<double>( aMm ) );
    }
}


bool ASSEMBLY_3D_MANAGER::HasPersistedInstanceStates() const
{
    if( !m_project )
        return false;

    const PROJECT_FILE& pf = m_project->GetProjectFile();

    if( !pf.IsMultiBoardContainer() )
        return false;

    return !pf.GetAssemblyInstances().empty();
}


void ASSEMBLY_3D_MANAGER::applyPersistedInstanceStates()
{
    if( !m_project )
        return;

    const PROJECT_FILE& pf = m_project->GetProjectFile();

    if( !pf.IsMultiBoardContainer() )
        return;

    for( const ASSEMBLY_INSTANCE_STATE& s : pf.GetAssemblyInstances() )
    {
        for( BOARD_3D_INSTANCE& inst : m_boardInstances )
        {
            if( inst.subProjectUuid != s.subProjectUuid )
                continue;

            inst.position    = SFVEC3F( static_cast<float>( s.position.x ),
                                        static_cast<float>( s.position.y ),
                                        static_cast<float>( s.position.z ) );
            inst.rotation    = SFVEC3F( static_cast<float>( s.rotation.x ),
                                        static_cast<float>( s.rotation.y ),
                                        static_cast<float>( s.rotation.z ) );
            inst.visible     = s.visible;
            inst.transparent = s.transparent;
            inst.opacity     = static_cast<float>( s.opacity );
            break;
        }
    }
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

            // Route this sub-board to its OWN S3D_CACHE so 3D-model
            // paths using `${KIPRJMOD}` resolve against the sub-project
            // directory (not the container's). Without this, virtual /
            // project-local 3D models silently fail to load.
            // Falls back to the container's cache when the sub-project
            // didn't load (best-effort: stock library models still work).
            S3D_CACHE* cache = inst.subProject
                                       ? PROJECT_PCB::Get3DCacheManager( inst.subProject )
                                       : a3DCache;
            m_instanceAdapters[i]->Set3dCacheManager( cache );

            // Each per-instance adapter gets its OWN settings copy so
            // appearance / visibility / engine toggles don't bleed
            // across boards inside the MBS composite (or back into the
            // app-wide cfg the local single-board viewer uses).
            //
            // Initialised first by deep-copying the canvas's main
            // settings (the sane defaults), then layered with
            // per-sub-project overrides loaded from the sub-project's
            // own .kicad_pro (`viewer3d.color_overrides`,
            // `viewer3d.visibility_overrides`,
            // `viewer3d.use_stackup_colors`). That second layer is
            // what makes board A's local-viewer F.Mask=red survive
            // through to MBS without bleeding into board B.
            if( !inst.perInstanceCfg )
            {
                inst.perInstanceCfg = std::make_unique<EDA_3D_VIEWER_SETTINGS>();
                copyRenderState( *inst.perInstanceCfg, *sharedCfg );

                if( inst.subProject )
                {
                    const PROJECT_FILE& pf = inst.subProject->GetProjectFile();

                    if( std::optional<bool> useStackup =
                                pf.Get3DViewerUseStackupColors() )
                    {
                        inst.perInstanceCfg->m_UseStackupColors = *useStackup;
                    }

                    // Visibility overrides: m_Render flags only flip
                    // for layers the user touched in the sub-project's
                    // local viewer. We round-trip through the bitset
                    // helper so the layer→flag mapping stays in one
                    // place (BOARD_ADAPTER::SetVisibleLayers).
                    const auto& visOverrides = pf.Get3DViewerVisibilityOverrides();

                    if( !visOverrides.empty() )
                    {
                        // Stash on the cfg now; applied to the adapter
                        // immediately below once m_Cfg is wired.
                    }
                }
            }
            m_instanceAdapters[i]->m_Cfg = inst.perInstanceCfg.get();

            // Pre-InitSettings overrides: colour overrides feed
            // directly into BOARD_ADAPTER::m_ColorOverrides which
            // GetLayerColors applies as the LAST step on top of
            // theme + stackup. Visibility overrides go through the
            // bitset helper so a partial map merges with current
            // flags rather than blanking unseen layers.
            if( inst.subProject )
            {
                const PROJECT_FILE& pf = inst.subProject->GetProjectFile();

                m_instanceAdapters[i]->m_ColorOverrides =
                        pf.Get3DViewerColorOverrides();

                const auto& visOverrides = pf.Get3DViewerVisibilityOverrides();

                if( !visOverrides.empty() )
                {
                    std::bitset<LAYER_3D_END> bits =
                            m_instanceAdapters[i]->GetVisibleLayers();

                    for( const auto& [layer, vis] : visOverrides )
                    {
                        if( layer >= 0 && layer < LAYER_3D_END )
                            bits.set( layer, vis );
                    }

                    m_instanceAdapters[i]->SetVisibleLayers( bits );
                }
            }

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

    // Locate the LAST visible-and-rendered instance index. The gizmo
    // render glClears the depth buffer (carves its own viewport) and
    // would wipe the shared depth for any pass after it — so only the
    // final pass is allowed to render the gizmo.
    size_t lastRenderedIdx = static_cast<size_t>( -1 );
    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        const BOARD_3D_INSTANCE& inst = m_boardInstances[i];
        if( inst.visible && inst.board && i < m_instanceRenderers.size()
            && m_instanceRenderers[i] )
        {
            lastRenderedIdx = i;
        }
    }

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

        // Local 3D centroid (Y already inverted inside BOARD_ADAPTER's
        // m_boardCenter, which is m_boardPos * biuTo3Dunits). Shared
        // centroid = local centroid scaled into shared space.
        const SFVEC3F   localCenter       = m_instanceAdapters[i]->GetBoardCenter();
        const glm::vec3 localCenterShared(
                localCenter.x * static_cast<float>( scaleFactor ),
                localCenter.y * static_cast<float>( scaleFactor ),
                localCenter.z * static_cast<float>( scaleFactor ) );

        // Build pose so:
        //   1. Local vertex first scales into shared units.
        //   2. Shifts so the board's local centroid sits at origin.
        //   3. Rotates around the origin (= around the centroid).
        //   4. Un-shifts so the centroid returns to its per-instance
        //      world slot, then adds the final world offset.
        // GL matrix order is right-to-left, so glm calls are written
        // in REVERSE of the application order (last call → first
        // applied to the vertex).
        glm::mat4 pose = glm::mat4( 1.0f );

        // Outer: world positioning (current behaviour, identity
        // rotation leaves this unchanged).
        pose = glm::translate( pose, instShared - centerShared );
        pose = glm::translate( pose, localCenterShared );

        // Euler rotation (degrees → radians). Apply Z, then Y, then
        // X — matches BOARD_3D_INSTANCE::GetTransformMatrix so panel
        // edits behave consistently with any other code that reads
        // that helper.
        pose = glm::rotate( pose, glm::radians( inst.rotation.z ),
                            glm::vec3( 0.0f, 0.0f, 1.0f ) );
        pose = glm::rotate( pose, glm::radians( inst.rotation.y ),
                            glm::vec3( 0.0f, 1.0f, 0.0f ) );
        pose = glm::rotate( pose, glm::radians( inst.rotation.x ),
                            glm::vec3( 1.0f, 0.0f, 0.0f ) );

        pose = glm::translate( pose, -localCenterShared );
        pose = glm::scale( pose, glm::vec3( static_cast<float>( scaleFactor ) ) );

        m_instanceRenderers[i]->SetAssemblyPose( pose );
        m_instanceRenderers[i]->SetSkipBufferClear( !firstPass );
        m_instanceRenderers[i]->SetSkipGizmo( i != lastRenderedIdx );

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
    // renderer's reload happened to set it). Also reset zoom + pan
    // so the autoframe shows the whole assembly (the shared BIU→3D
    // factor scales the composite into ±RANGE_SCALE_3D so the default
    // zoom fits) instead of stranding the user at whatever zoom the
    // single-board reload left behind.
    if( m_camera && m_cameraFitPending && !firstPass )
    {
        m_camera->SetBoardLookAtPos( SFVEC3F( 0.0f, 0.0f, 0.0f ) );
        m_camera->ResetXYpos();
        m_camera->ZoomReset();
        m_cameraFitPending = false;
        requestAnother = true;
    }

    // M6.D-phase-2: mate gizmo render pass. Runs AFTER every per-
    // instance Redraw so the gizmo geometry overlays the boards. Uses
    // the camera's view matrix directly (no per-instance pose) — the
    // gizmo entries already carry world-space coords computed from the
    // same pose math the renderer used.
    //
    // The outer gate fires when ANY of the three overlay toggles is on
    // — per-entry filtering inside rebuildMateGizmoEntries decides
    // which entries actually populate the entry list. This way "Show
    // collisions" alone is enough to keep collision markers visible
    // when the user has hidden mate gizmos.
    const bool anyOverlayOn = m_showMateGizmos
                              || m_showPinPairGizmos
                              || m_showCollisionHighlights
                              || m_showContactHighlights
                              || m_showBroadAabbDebug;

    // One-shot initial collision check — `!firstPass` means at least
    // one per-instance Redraw ran, which means InitRenderers populated
    // `m_instanceAdapters`. Now `buildShape` / `buildFpMeshTris` can
    // see real adapters and compute the right footprint world poses,
    // so the mesh-tri test produces actual overlap geometry. Without
    // this, the panel ctor's earlier `RunCollisionCheck` saw null
    // adapters and produced an empty box list; user had to nudge a
    // board to retrigger.
    //
    // Run regardless of overlay toggles so the panel's validation
    // count refreshes on every viewer open even when no overlays are
    // visible. The cost is one-time-per-session.
    if( m_initialCollisionCheckPending && !firstPass )
    {
        RunCollisionCheck();
        m_initialCollisionCheckPending = false;
    }

    if( anyOverlayOn && !firstPass && m_camera )
    {
        if( !m_mateGizmo )
            m_mateGizmo = std::make_unique<MATE_GIZMO>();

        rebuildMateGizmoEntries();
        m_mateGizmo->Render( m_camera->GetViewMatrix(),
                             m_camera->GetProjectionMatrix() );
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


void ASSEMBLY_3D_MANAGER::ReleaseOpenGL()
{
    // Callers must hold the GL context lock. Clearing the vector runs
    // each RENDER_3D_OPENGL's destructor in turn, which calls
    // glDeleteTextures + freeAllLists — safe only with the context
    // current. Adapters are preserved (CPU-side only).
    m_instanceRenderers.clear();
    m_cameraFitPending = false;

    // Mate gizmo also owns a GLU quadric — destroy under the same
    // lock so the GLU teardown runs against the right context.
    m_mateGizmo.reset();
}


void ASSEMBLY_3D_MANAGER::SetSelectedMatePair( const wxString& aPairId )
{
    m_selectedMatePairId = aPairId;
}


wxString ASSEMBLY_3D_MANAGER::MakeMatePairId( const KIID&     aInstanceA,
                                              const wxString& aFootprintRefA,
                                              const KIID&     aInstanceB,
                                              const wxString& aFootprintRefB )
{
    // Canonical ordering: lower instance UUID first; ties broken by
    // footprint ref. Matches the keying used in BuildMateGraph so the
    // same physical pair always encodes to the same id regardless of
    // which side the panel iterated first.
    KIID     iA = aInstanceA;
    KIID     iB = aInstanceB;
    wxString rA = aFootprintRefA;
    wxString rB = aFootprintRefB;

    if( iB < iA || ( iA == iB && rB < rA ) )
    {
        std::swap( iA, iB );
        std::swap( rA, rB );
    }

    return iA.AsString() + wxT( "|" ) + rA + wxT( "||" ) + iB.AsString() + wxT( "|" ) + rB;
}


bool ASSEMBLY_3D_MANAGER::projectFootprintCentroidToWorld(
        const BOARD_3D_INSTANCE& aInst,
        const BOARD_ADAPTER&     aAdapter,
        const wxString&          aFootprintRef,
        glm::vec3&               aOutWorld ) const
{
    FOOTPRINT* fp = findFootprintByRef( aInst.board.get(), aFootprintRef );

    if( !fp )
        return false;

    // Pad-centroid in BIU. Same logic the placement solver uses so the
    // gizmo lands exactly where the pose math placed the connector.
    // 64-bit accumulator — the running sum overflows int32 once a board
    // placed >~150 mm from origin gets past ~14 pads (each is ~1.5×10⁸
    // nm; INT32_MAX is ~2.15×10⁹). Without this the wrap produced
    // garbage centroids and the gizmo floated far from the actual pads.
    VECTOR2L sum( 0, 0 );
    int      count = 0;

    for( PAD* pad : fp->Pads() )
    {
        if( !pad->IsOnLayer( F_Cu ) && !pad->IsOnLayer( B_Cu ) )
            continue;

        const VECTOR2I& p = pad->GetPosition();
        sum.x += p.x;
        sum.y += p.y;
        count++;
    }

    const VECTOR2I centroidBIU =
            ( count > 0 ) ? VECTOR2I( static_cast<int>( sum.x / count ),
                                       static_cast<int>( sum.y / count ) )
                          : fp->GetPosition();

    // Mirror RedrawAll's pose composition for this single point.
    const double localFactor  = aAdapter.BiuTo3dUnits();
    const double scaleFactor  = ( localFactor != 0.0 )
                                        ? m_sharedBiuTo3Dunits / localFactor
                                        : 1.0;
    const float  sharedF      = static_cast<float>( m_sharedBiuTo3Dunits );
    const float  biuPerMm     = 1.0e6f;

    // Footprint board-local 3D — note Y is inverted at footprint render
    // (see render_3d_opengl.cpp:1140). Z is 0 (board centre plane) for
    // the gizmo so it appears at the connector's anchor, not buried in
    // copper.
    const glm::vec3 local3D( static_cast<float>( centroidBIU.x ) * localFactor,
                             -static_cast<float>( centroidBIU.y ) * localFactor,
                             0.0f );

    glm::vec3 localShared = local3D * static_cast<float>( scaleFactor );

    SFVEC3F   lc = aAdapter.GetBoardCenter();
    glm::vec3 localCenterShared( lc.x * static_cast<float>( scaleFactor ),
                                 lc.y * static_cast<float>( scaleFactor ),
                                 lc.z * static_cast<float>( scaleFactor ) );

    glm::mat4 R = glm::mat4( 1.0f );
    R = glm::rotate( R, glm::radians( aInst.rotation.z ), glm::vec3( 0, 0, 1 ) );
    R = glm::rotate( R, glm::radians( aInst.rotation.y ), glm::vec3( 0, 1, 0 ) );
    R = glm::rotate( R, glm::radians( aInst.rotation.x ), glm::vec3( 1, 0, 0 ) );

    glm::vec3 shifted = localShared - localCenterShared;
    glm::vec4 rotated = R * glm::vec4( shifted, 1.0f );
    glm::vec3 replaced = glm::vec3( rotated ) + localCenterShared;

    glm::vec3 instShared(  aInst.position.x * biuPerMm * sharedF,
                          -aInst.position.y * biuPerMm * sharedF,
                           aInst.position.z * biuPerMm * sharedF );

    SFVEC3F bboxMin, bboxMax;
    GetAssemblyBoundingBox( bboxMin, bboxMax );
    SFVEC3F   centerMm = ( bboxMin + bboxMax ) * 0.5f;
    glm::vec3 centerShared(  centerMm.x * biuPerMm * sharedF,
                            -centerMm.y * biuPerMm * sharedF,
                             centerMm.z * biuPerMm * sharedF );

    aOutWorld = replaced + instShared - centerShared;
    return true;
}


void ASSEMBLY_3D_MANAGER::rebuildMateGizmoEntries()
{
    std::vector<MATE_GIZMO::ENTRY> entries;

    if( !m_mateGizmo )
        return;

    // Push current user colour selections to the gizmo so the next
    // Render uses them. Cheap (just stores 4 vec3s).
    m_mateGizmo->SetMateGizmoColor(
            glm::vec3( m_mateGizmoColor.r, m_mateGizmoColor.g, m_mateGizmoColor.b ) );
    m_mateGizmo->SetPinPairColor(
            glm::vec3( m_pinPairColor.r, m_pinPairColor.g, m_pinPairColor.b ) );
    m_mateGizmo->SetCollisionColor(
            glm::vec3( m_collisionColor.r, m_collisionColor.g, m_collisionColor.b ) );
    m_mateGizmo->SetContactColor(
            glm::vec3( m_contactColor.r, m_contactColor.g, m_contactColor.b ) );

    std::vector<MATE_EDGE> edges = BuildMateGraph();

    // Quick lookup from instance UUID → BOARD_ADAPTER index so we can
    // pose-project pads using the right per-board scale + centroid.
    std::map<KIID, size_t> adapterIndexByInst;

    for( size_t i = 0; i < m_boardInstances.size(); i++ )
        adapterIndexByInst.emplace( m_boardInstances[i].uuid, i );

    const bool anySelected = !m_selectedMatePairId.IsEmpty();

    // Mate-pair line gizmos render strictly under m_showMateGizmos.
    // Earlier round had this OR'd with m_showContactHighlights (since
    // mate connections are a form of "contact"), but that meant
    // unchecking "Show mate gizmos" with contacts still on left the
    // lines drawn — confusing because the user's mental model is
    // "this checkbox = these lines." Each toggle now controls one
    // visual concept: mate gizmos = lines, contact highlights = on-
    // model yellow boxes, collision highlights = on-model red boxes.
    // Helper: project an arbitrary board-stored BIU point to shared
    // world units, mirroring projectFootprintCentroidToWorld's pose
    // math but for a caller-supplied point (a specific pad, not the
    // footprint centroid). Used for the per-pin-pair gizmo lines below.
    auto projectStoredBIUPoint = [&]( const BOARD_3D_INSTANCE& aInst,
                                       const BOARD_ADAPTER&     aAdapter,
                                       const VECTOR2I&          aStoredBIU,
                                       glm::vec3&               aOutWorld )
    {
        const double localFactor = aAdapter.BiuTo3dUnits();
        const double scaleFactor = ( localFactor != 0.0 )
                                            ? m_sharedBiuTo3Dunits / localFactor
                                            : 1.0;
        const float  sharedF     = static_cast<float>( m_sharedBiuTo3Dunits );
        const float  biuPerMm    = 1.0e6f;

        const glm::vec3 local3D( static_cast<float>( aStoredBIU.x ) * localFactor,
                                 -static_cast<float>( aStoredBIU.y ) * localFactor,
                                 0.0f );
        const glm::vec3 localShared = local3D * static_cast<float>( scaleFactor );

        const SFVEC3F   lc = aAdapter.GetBoardCenter();
        const glm::vec3 localCenterShared( lc.x * static_cast<float>( scaleFactor ),
                                           lc.y * static_cast<float>( scaleFactor ),
                                           lc.z * static_cast<float>( scaleFactor ) );

        glm::mat4 R( 1.0f );
        R = glm::rotate( R, glm::radians( aInst.rotation.z ), glm::vec3( 0, 0, 1 ) );
        R = glm::rotate( R, glm::radians( aInst.rotation.y ), glm::vec3( 0, 1, 0 ) );
        R = glm::rotate( R, glm::radians( aInst.rotation.x ), glm::vec3( 1, 0, 0 ) );

        const glm::vec3 shifted  = localShared - localCenterShared;
        const glm::vec4 rotated  = R * glm::vec4( shifted, 1.0f );
        const glm::vec3 replaced = glm::vec3( rotated ) + localCenterShared;

        const glm::vec3 instShared(  aInst.position.x * biuPerMm * sharedF,
                                    -aInst.position.y * biuPerMm * sharedF,
                                     aInst.position.z * biuPerMm * sharedF );

        SFVEC3F bboxMin, bboxMax;
        GetAssemblyBoundingBox( bboxMin, bboxMax );
        const SFVEC3F   centerMm = ( bboxMin + bboxMax ) * 0.5f;
        const glm::vec3 centerShared(  centerMm.x * biuPerMm * sharedF,
                                      -centerMm.y * biuPerMm * sharedF,
                                       centerMm.z * biuPerMm * sharedF );

        aOutWorld = replaced + instShared - centerShared;
    };

    if( m_showMateGizmos || m_showPinPairGizmos )
    for( const MATE_EDGE& edge : edges )
    {
        const MATE_PAIR* primary = PickPrimaryPair( edge );

        for( const MATE_PAIR& p : edge.pairs )
        {
            if( p.disabled )
                continue;   // suppressed by user — keep tree row, drop gizmo

            const auto itA = adapterIndexByInst.find( p.instanceA );
            const auto itB = adapterIndexByInst.find( p.instanceB );

            if( itA == adapterIndexByInst.end() || itB == adapterIndexByInst.end() )
                continue;

            const size_t idxA = itA->second;
            const size_t idxB = itB->second;

            if( idxA >= m_instanceAdapters.size() || idxB >= m_instanceAdapters.size() )
                continue;

            // MOON-1362: skip the entire mate when either side's board
            // is hidden — its gizmo would otherwise float over empty
            // space (or, worse, overlap an unrelated visible board the
            // user is currently inspecting).
            if( !m_boardInstances[idxA].visible || !m_boardInstances[idxB].visible )
                continue;

            const BOARD_ADAPTER* adapterA = m_instanceAdapters[idxA].get();
            const BOARD_ADAPTER* adapterB = m_instanceAdapters[idxB].get();

            if( !adapterA || !adapterB )
                continue;

            glm::vec3 worldA, worldB;

            if( !projectFootprintCentroidToWorld( m_boardInstances[idxA], *adapterA,
                                                   p.footprintRefA, worldA ) )
                continue;

            if( !projectFootprintCentroidToWorld( m_boardInstances[idxB], *adapterB,
                                                   p.footprintRefB, worldB ) )
                continue;

            MATE_GIZMO::ENTRY e;
            e.posA   = worldA;
            e.posB   = worldB;
            e.source = ( p.customMateUuid == KIID( 0 ) ) ? MATE_GIZMO::SOURCE::AUTO
                                                         : MATE_GIZMO::SOURCE::CUSTOM;

            // Role precedence for the gizmo: alignmentOnly outranks
            // primary because PickPrimaryPair already filters those
            // out — if a pair is alignmentOnly we definitely won't
            // select it as primary.
            if( p.alignmentOnly )
                e.role = MATE_GIZMO::ROLE::SECONDARY;
            else if( primary && &p == primary )
                e.role = MATE_GIZMO::ROLE::PRIMARY;
            else
                e.role = MATE_GIZMO::ROLE::SECONDARY;

            // Identity is the canonical pair-key string — same
            // encoding both auto and custom rows produce in the
            // panel, so selecting EITHER kind highlights its gizmo.
            e.matePairId  = MakeMatePairId( p.instanceA, p.footprintRefA,
                                            p.instanceB, p.footprintRefB );
            e.selected    = anySelected && ( e.matePairId == m_selectedMatePairId );
            e.anySelected = anySelected;

            // The bold connector-centroid entry — render only when its
            // own toggle is on (m_showMateGizmos). Pin-pair lines below
            // have their own independent toggle (m_showPinPairGizmos)
            // so the user can show centroid summary without pin noise,
            // or vice versa for diagnosing wrong correspondences.
            if( m_showMateGizmos )
                entries.push_back( e );

            if( !m_showPinPairGizmos )
                continue;

            // Per-pin diagnostic gizmos. One thin line per cross-net
            // pad correspondence, drawn between the actual pad
            // positions (not centroids). When the Kabsch solver
            // produces a tilted pose, an inverted pin mapping shows
            // up here as a line that crosses the others — visible
            // sanity check on the schematic-side endpoint pairing.
            FOOTPRINT* fpAforPins = findFootprintByRef(
                    m_boardInstances[idxA].board.get(), p.footprintRefA );
            FOOTPRINT* fpBforPins = findFootprintByRef(
                    m_boardInstances[idxB].board.get(), p.footprintRefB );

            if( !fpAforPins || !fpBforPins )
                continue;

            for( const auto& [pinA, pinB] : p.padPins )
            {
                PAD* padA = fpAforPins->FindPadByNumber( pinA );
                PAD* padB = fpBforPins->FindPadByNumber( pinB );

                if( !padA || !padB )
                    continue;

                glm::vec3 padWorldA, padWorldB;
                projectStoredBIUPoint( m_boardInstances[idxA], *adapterA,
                                       padA->GetPosition(), padWorldA );
                projectStoredBIUPoint( m_boardInstances[idxB], *adapterB,
                                       padB->GetPosition(), padWorldB );

                MATE_GIZMO::ENTRY pe = e;        // inherit selection state
                pe.posA = padWorldA;
                pe.posB = padWorldB;
                pe.role = MATE_GIZMO::ROLE::PIN_PAIR;
                entries.push_back( pe );
            }
        }
    }

    m_mateGizmo->SetEntries( std::move( entries ) );

    // M6.E phase-2 visualization: convert OverlapBox records (mm) to
    // shared 3D-viewer world units and hand them to the gizmo as
    // translucent boxes. The same conversion factors the mate-pair
    // entries use are applied here, plus the Y-flip the renderer
    // applies to footprint coordinates. Result: boxes overlay on top
    // of the actual model geometry instead of floating as line
    // gizmos between centroids.
    std::vector<MATE_GIZMO::OVERLAP_BOX> boxes;

    if( m_showCollisionHighlights || m_showContactHighlights || m_showBroadAabbDebug )
    {
        constexpr float biuPerMm = 1e6f;
        const float     sharedF  = static_cast<float>( m_sharedBiuTo3Dunits );

        SFVEC3F   bboxMin, bboxMax;
        GetAssemblyBoundingBox( bboxMin, bboxMax );
        SFVEC3F   centerMm = ( bboxMin + bboxMax ) * 0.5f;
        glm::vec3 centerShared(  centerMm.x * biuPerMm * sharedF,
                                -centerMm.y * biuPerMm * sharedF,
                                 centerMm.z * biuPerMm * sharedF );

        auto mmToShared = [&]( const SFVEC3F& aMm ) -> glm::vec3
        {
            return glm::vec3(  aMm.x * biuPerMm * sharedF,
                              -aMm.y * biuPerMm * sharedF,
                               aMm.z * biuPerMm * sharedF ) - centerShared;
        };

        for( const OverlapBox& ob : m_lastOverlapBoxes )
        {
            // Per-kind toggle gating.
            switch( ob.kind )
            {
            case OVERLAP_KIND::COLLISION:
                if( !m_showCollisionHighlights )
                    continue;
                break;

            case OVERLAP_KIND::CONTACT:
                if( !m_showContactHighlights )
                    continue;
                break;

            case OVERLAP_KIND::BROAD:
                if( !m_showBroadAabbDebug )
                    continue;
                break;
            }

            // Y is negated in the world conversion, so the AABB's
            // post-conversion min/max can swap on the Y axis. Build
            // the box from the corner-by-corner mapping and re-extract
            // axis-aligned min/max afterwards.
            glm::vec3 sMin = mmToShared( ob.minMm );
            glm::vec3 sMax = mmToShared( ob.maxMm );

            glm::vec3 actualMin( std::min( sMin.x, sMax.x ),
                                 std::min( sMin.y, sMax.y ),
                                 std::min( sMin.z, sMax.z ) );
            glm::vec3 actualMax( std::max( sMin.x, sMax.x ),
                                 std::max( sMin.y, sMax.y ),
                                 std::max( sMin.z, sMax.z ) );

            MATE_GIZMO::OVERLAP_BOX b;
            b.minWorld = actualMin;
            b.maxWorld = actualMax;

            switch( ob.kind )
            {
            case OVERLAP_KIND::COLLISION:
                b.kind = MATE_GIZMO::OVERLAP_KIND::COLLISION; break;
            case OVERLAP_KIND::CONTACT:
                b.kind = MATE_GIZMO::OVERLAP_KIND::CONTACT;   break;
            case OVERLAP_KIND::BROAD:
                b.kind = MATE_GIZMO::OVERLAP_KIND::BROAD;     break;
            }

            // Convert intersecting-triangle vertices into shared-
            // world coordinates so the gizmo can draw the actual
            // overlap surface, not just the AABB envelope.
            if( !ob.triVertsMm.empty() )
            {
                b.triVerts.reserve( ob.triVertsMm.size() );

                for( const SFVEC3F& v : ob.triVertsMm )
                    b.triVerts.push_back( mmToShared( v ) );
            }

            boxes.push_back( b );
        }
    }

    m_mateGizmo->SetOverlapBoxes( std::move( boxes ) );
}


void ASSEMBLY_3D_MANAGER::BuildRaytraceInstances(
        std::vector<ASSEMBLY_3D_MANAGER::RAYTRACE_INSTANCE>& aOut ) const
{
    aOut.clear();

    if( m_boardInstances.empty() || m_instanceAdapters.size() != m_boardInstances.size() )
        return;

    // Mirror the OpenGL composite's centerShared offset so the raytraced
    // scene is co-located with how RedrawAll positions the OpenGL
    // composite (keeps the camera framing identical between engines).
    SFVEC3F bboxMinMm, bboxMaxMm;
    const_cast<ASSEMBLY_3D_MANAGER*>( this )->GetAssemblyBoundingBox(
            bboxMinMm, bboxMaxMm );
    const SFVEC3F centerMm = ( bboxMinMm + bboxMaxMm ) * 0.5f;
    const float   sharedF  = static_cast<float>( m_sharedBiuTo3Dunits );
    const float   biuPerMm = 1.0e6f;
    const glm::vec3 centerShared(  centerMm.x * biuPerMm * sharedF,
                                  -centerMm.y * biuPerMm * sharedF,
                                   centerMm.z * biuPerMm * sharedF );

    aOut.reserve( m_boardInstances.size() );

    for( size_t i = 0; i < m_boardInstances.size(); i++ )
    {
        const BOARD_3D_INSTANCE& inst = m_boardInstances[i];

        if( !inst.visible || !inst.board || !m_instanceAdapters[i] )
            continue;

        BOARD_ADAPTER* adapter = m_instanceAdapters[i].get();

        // Match the OpenGL pose math in RedrawAll exactly. Each per-
        // instance adapter has its own biuTo3Dunits; the wrapper's
        // pose carries the shared/local scale ratio so the raytracer's
        // INSTANCE_OBJECT_3D maps the inner per-board scene into the
        // shared assembly frame.
        const double localFactor = adapter->BiuTo3dUnits();
        const double scaleFactor = ( localFactor != 0.0 )
                                       ? m_sharedBiuTo3Dunits / localFactor
                                       : 1.0;

        const glm::vec3 instShared(  inst.position.x * biuPerMm * sharedF,
                                    -inst.position.y * biuPerMm * sharedF,
                                     inst.position.z * biuPerMm * sharedF );

        const SFVEC3F   localCenter = adapter->GetBoardCenter();
        const glm::vec3 localCenterShared(
                localCenter.x * static_cast<float>( scaleFactor ),
                localCenter.y * static_cast<float>( scaleFactor ),
                localCenter.z * static_cast<float>( scaleFactor ) );

        glm::mat4 pose( 1.0f );
        pose = glm::translate( pose, instShared - centerShared );
        pose = glm::translate( pose, localCenterShared );
        pose = glm::rotate( pose, glm::radians( inst.rotation.z ),
                            glm::vec3( 0.0f, 0.0f, 1.0f ) );
        pose = glm::rotate( pose, glm::radians( inst.rotation.y ),
                            glm::vec3( 0.0f, 1.0f, 0.0f ) );
        pose = glm::rotate( pose, glm::radians( inst.rotation.x ),
                            glm::vec3( 1.0f, 0.0f, 0.0f ) );
        pose = glm::translate( pose, -localCenterShared );
        pose = glm::scale( pose, glm::vec3( static_cast<float>( scaleFactor ) ) );

        RAYTRACE_INSTANCE entry;
        entry.adapter = adapter;
        entry.pose    = pose;
        aOut.push_back( entry );
    }
}
