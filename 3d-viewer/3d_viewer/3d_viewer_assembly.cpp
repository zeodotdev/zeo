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
#include <board_stackup_manager/board_stackup.h>
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

    m_lastMateResiduals.clear();

    // M6.D-phase-1 pipeline: aggregate cross-board nets into a board
    // mate graph, then BFS-place every reachable instance from the
    // highest-degree anchor with primary-mate-wins per board edge.
    // Secondary mate pairs and graph-cycle back-edges become entries
    // in m_lastMateResiduals (consumed by M6.E DRC panel).
    std::vector<MATE_EDGE> edges = BuildMateGraph();

    if( edges.empty() )
        return;

    SolveMatePoses( edges );

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

    for( const MB_CROSS_BOARD_NET& net : projectFile.GetCrossBoardNets() )
    {
        if( net.endpoints.size() < 2 )
            continue;

        // Multi-endpoint nets (≥3 endpoints) decompose into all
        // pairwise combinations; primary-mate-wins picks the strongest
        // per board edge, so over-counting connector pin shares across
        // mate pairs is fine.
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

                bool swap = ( iB->uuid < iA->uuid )
                            || ( iA->uuid == iB->uuid && refB < refA );

                if( swap )
                {
                    std::swap( iA, iB );
                    std::swap( refA, refB );
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

    return edges;
}


const MATE_PAIR* ASSEMBLY_3D_MANAGER::PickPrimaryPair( const MATE_EDGE& aEdge ) const
{
    if( aEdge.pairs.empty() )
        return nullptr;

    const MATE_PAIR* best = &aEdge.pairs.front();

    for( const MATE_PAIR& p : aEdge.pairs )
    {
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
            // necessarily on the A side.
            MATE_PAIR primaryOriented = *primary;

            if( primaryOriented.instanceA != u )
            {
                std::swap( primaryOriented.instanceA, primaryOriented.instanceB );
                std::swap( primaryOriented.footprintRefA, primaryOriented.footprintRefB );
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
        VECTOR2I sum( 0, 0 );
        int      count = 0;

        for( PAD* pad : fp->Pads() )
        {
            if( !pad->IsOnLayer( F_Cu ) && !pad->IsOnLayer( B_Cu ) )
                continue;

            sum += pad->GetPosition();
            count++;
        }

        if( count == 0 )
            return fp->GetPosition();

        return VECTOR2I( sum.x / count, sum.y / count );
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

    const bool padAOnTop = dominantSide( fpA );
    const bool padBOnTop = dominantSide( fpB );

    // Account for parent's existing flip when projecting its mate side
    // into world frame. Phase-1 only supports 0° / 180° X-rotation, so
    // a parent rotation of (180, 0, 0) inverts its effective top/bot.
    const bool parentFlipped =
            std::abs( aParent.rotation.x - 180.0f ) < 0.1f
            && std::abs( aParent.rotation.y ) < 0.1f
            && std::abs( aParent.rotation.z ) < 0.1f;

    const bool parentEffectivelyOnTop = parentFlipped ? !padAOnTop : padAOnTop;
    const bool sameEffectiveSide      = ( parentEffectivelyOnTop == padBOnTop );

    const float thicknessA      = GetBoardThickness( aParent.board.get() );
    const float thicknessB      = GetBoardThickness( aChild.board.get()  );
    const float connectorHeight = ComputeMateZGap( aParent, aPrimary.footprintRefA,
                                                   aChild,  aPrimary.footprintRefB );
    const float zGap            = thicknessA * 0.5f + thicknessB * 0.5f + connectorHeight;

    // Pad centroids in mm, in stored (un-Y-inverted) coords.
    float dx = ( padCentroidA.x - padCentroidB.x ) / 1.0e6f;
    float dy;
    float dz;

    if( sameEffectiveSide )
    {
        // Child gets flipped 180° about X. After the flip, the child's
        // pad-centroid Y mirrors around the child's centroid, so the
        // alignment formula sums Ys instead of subtracting.
        dy = ( padCentroidA.y + padCentroidB.y ) / 1.0e6f;
        aChild.rotation = SFVEC3F( parentFlipped ? 0.0f : 180.0f, 0.0f, 0.0f );
    }
    else
    {
        dy = ( padCentroidA.y - padCentroidB.y ) / 1.0e6f;
        aChild.rotation = SFVEC3F( parentFlipped ? 180.0f : 0.0f, 0.0f, 0.0f );
    }

    // Place child on the side parent's connector faces in world.
    // parentEffectivelyOnTop true → +Z gap; false → −Z gap.
    dz = parentEffectivelyOnTop ? zGap : -zGap;

    // Parent flip mirrors the child's Y offset: the parent's pad-Y in
    // stored coords becomes effectively −pad-Y in world Y after the
    // parent's 180°-X rotation.
    if( parentFlipped )
        dy = -dy;

    aChild.position = aParent.position + SFVEC3F( dx, dy, dz );

    return true;
}


float ASSEMBLY_3D_MANAGER::ComputeMateZGap( const BOARD_3D_INSTANCE& aA,
                                              const wxString&          aFootprintRefA,
                                              const BOARD_3D_INSTANCE& aB,
                                              const wxString&          aFootprintRefB ) const
{
    // Phase-1: fixed 5 mm fallback. Phase-2 / future work reads
    // the larger of the two connectors' 3D-model boundingBox.max.z
    // via S3D_CACHE.
    (void) aA;
    (void) aFootprintRefA;
    (void) aB;
    (void) aFootprintRefB;
    return 5.0f;
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

            // M6.C DIAGNOSTIC: per-instance color + unit sanity log.
            // Writes to ~/Library/Logs/Zeo/agent-*.log (macOS) via
            // wxLogMessage. Compare the "copper" RGBA across indices:
            // if idx=1+ is blue while idx=0 is gold, the per-adapter
            // color init is picking something different for later
            // boards despite shared m_Cfg/preset — points to stackup
            // finish-match divergence or m_ColorOverrides contamination.
            const SFVEC4F& c = m_instanceAdapters[i]->m_CopperColor;
            const SFVEC4F& bb = m_instanceAdapters[i]->m_BoardBodyColor;
            const SFVEC4F& mt = m_instanceAdapters[i]->m_SolderMaskColorTop;
            const SFVEC4F& mb = m_instanceAdapters[i]->m_SolderMaskColorBot;
            const bool useStackup = sharedCfg ? sharedCfg->m_UseStackupColors : false;
            wxString maskNameF, maskNameB;
            if( inst.board )
            {
                for( const BOARD_STACKUP_ITEM* it :
                     inst.board->GetDesignSettings().GetStackupDescriptor().GetList() )
                {
                    if( it->GetType() == BS_ITEM_TYPE_SOLDERMASK )
                    {
                        if( it->GetBrdLayerId() == F_Mask )
                            maskNameF = it->GetColor();
                        else if( it->GetBrdLayerId() == B_Mask )
                            maskNameB = it->GetColor();
                    }
                }
            }
            wxLogMessage( wxT( "[ASSEMBLY] inst[%zu] display='%s' ref=%s "
                               "copper=(%.3f,%.3f,%.3f,%.3f) body=(%.3f,%.3f,%.3f,%.3f) "
                               "maskTop=(%.3f,%.3f,%.3f,%.3f) maskBot=(%.3f,%.3f,%.3f,%.3f) "
                               "stackupColors=%d maskNameF='%s' maskNameB='%s' biuTo3D=%g" ),
                          i,
                          inst.displayName,
                          inst.subProjectUuid.AsString(),
                          c.r, c.g, c.b, c.a,
                          bb.r, bb.g, bb.b, bb.a,
                          mt.r, mt.g, mt.b, mt.a,
                          mb.r, mb.g, mb.b, mb.a,
                          useStackup ? 1 : 0,
                          maskNameF, maskNameB,
                          m_instanceAdapters[i]->BiuTo3dUnits() );
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

    // M6.C DIAGNOSTIC: log first ~3 frames only so logs don't flood.
    static int s_frameLog = 0;
    const bool doLog = s_frameLog < 3;

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

        if( doLog )
        {
            GLboolean dTest = GL_FALSE, dMask = GL_FALSE;
            GLint     dFunc = 0;
            glGetBooleanv( GL_DEPTH_TEST, &dTest );
            glGetBooleanv( GL_DEPTH_WRITEMASK, &dMask );
            glGetIntegerv( GL_DEPTH_FUNC, &dFunc );
            wxLogMessage( wxT( "[ASSEMBLY] frame=%d PRE[%zu] test=%d mask=%d func=0x%x "
                               "pose.translate=(%.3f,%.3f,%.3f) skip=%d scale=%.3f" ),
                          s_frameLog, i, (int) dTest, (int) dMask, dFunc,
                          pose[3][0], pose[3][1], pose[3][2],
                          (int) ( !firstPass ),
                          static_cast<float>( scaleFactor ) );
        }

        bool wants = m_instanceRenderers[i]->Redraw( aIsMoving, aStatusReporter,
                                                      aWarningReporter );
        requestAnother = requestAnother || wants;

        if( doLog )
        {
            GLboolean dTest = GL_FALSE, dMask = GL_FALSE;
            GLint     dFunc = 0;
            glGetBooleanv( GL_DEPTH_TEST, &dTest );
            glGetBooleanv( GL_DEPTH_WRITEMASK, &dMask );
            glGetIntegerv( GL_DEPTH_FUNC, &dFunc );
            wxLogMessage( wxT( "[ASSEMBLY] frame=%d POST[%zu] test=%d mask=%d func=0x%x" ),
                          s_frameLog, i, (int) dTest, (int) dMask, dFunc );
        }

        firstPass = false;
    }

    if( doLog )
        ++s_frameLog;

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


void ASSEMBLY_3D_MANAGER::ReleaseOpenGL()
{
    // Callers must hold the GL context lock. Clearing the vector runs
    // each RENDER_3D_OPENGL's destructor in turn, which calls
    // glDeleteTextures + freeAllLists — safe only with the context
    // current. Adapters are preserved (CPU-side only).
    m_instanceRenderers.clear();
    m_cameraFitPending = false;
}
