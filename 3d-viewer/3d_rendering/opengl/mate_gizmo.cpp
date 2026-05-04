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

#include <kicad_gl/kiglu.h>   // Must be included first — pulls in GL + GLU prototypes

#include "mate_gizmo.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <wx/log.h>


namespace
{

// Phase-2 colour palette — kept here so the SOURCE × ROLE matrix is
// easy to scan and tune. Alpha is set per-entry by the renderer based
// on selection state, not in the palette.
struct STYLE
{
    glm::vec3 color;
    float     lineRadius;        ///< World-space cylinder radius (mm)
    float     sphereRadius;      ///< World-space sphere radius (mm)
};


STYLE styleFor( MATE_GIZMO::SOURCE aSource, MATE_GIZMO::ROLE aRole, bool aSelected )
{
    // M6.E collision entries always render in red regardless of role —
    // the user is supposed to fix the collision, not categorize it as
    // primary/secondary/disabled.
    if( aSource == MATE_GIZMO::SOURCE::COLLISION )
    {
        STYLE s;
        glm::vec3 red( 1.0f, 0.20f, 0.20f );
        s.color        = aSelected ? glm::min( red * 1.4f + glm::vec3( 0.05f ),
                                                glm::vec3( 1.0f ) )
                                    : red;
        s.lineRadius   = aSelected ? 0.10f : 0.06f;
        s.sphereRadius = aSelected ? 0.18f : 0.12f;
        return s;
    }

    // Custom mates lean blue/cyan; auto mates lean green/yellow.
    glm::vec3 base = ( aSource == MATE_GIZMO::SOURCE::CUSTOM )
                             ? glm::vec3( 0.20f, 0.80f, 1.00f )      // cyan
                             : glm::vec3( 0.30f, 1.00f, 0.40f );     // green

    if( aRole == MATE_GIZMO::ROLE::SECONDARY )
        base *= 0.7f;                                                 // dim

    if( aRole == MATE_GIZMO::ROLE::DISABLED )
        base = glm::vec3( 0.45f, 0.45f, 0.45f );                      // grey

    STYLE s;
    s.color        = aSelected ? glm::min( base * 1.4f + glm::vec3( 0.05f ),
                                            glm::vec3( 1.0f ) )
                                : base;

    // Sizes are in shared 3D units. The whole assembly fits in ±4
    // (RANGE_SCALE_3D=8). Keep gizmos small enough to read on small
    // boards but visible from the default camera framing.
    s.lineRadius   = ( aRole == MATE_GIZMO::ROLE::PRIMARY ) ? 0.06f : 0.03f;
    s.sphereRadius = ( aRole == MATE_GIZMO::ROLE::PRIMARY ) ? 0.12f : 0.07f;

    if( aSelected )
    {
        s.lineRadius   *= 1.6f;
        s.sphereRadius *= 1.6f;
    }

    return s;
}

} // namespace


MATE_GIZMO::MATE_GIZMO()
{
    m_quadric = gluNewQuadric();
    gluQuadricNormals( m_quadric, GLU_SMOOTH );
}


MATE_GIZMO::~MATE_GIZMO()
{
    if( m_quadric )
    {
        gluDeleteQuadric( m_quadric );
        m_quadric = nullptr;
    }
}


void MATE_GIZMO::SetEntries( std::vector<ENTRY> aEntries )
{
    m_entries = std::move( aEntries );
}


void MATE_GIZMO::SetOverlapBoxes( std::vector<OVERLAP_BOX> aBoxes )
{
    m_overlapBoxes = std::move( aBoxes );
}


void MATE_GIZMO::Render( const glm::mat4& aCameraView, const glm::mat4& aCameraProjection )
{
    if( m_entries.empty() && m_overlapBoxes.empty() )
        return;

    static int s_renderLog = 0;
    if( s_renderLog < 4 )
    {
        wxLogMessage( wxT( "[GIZMO-RENDER] entries=%zu camera.view[3]=(%.3f,%.3f,%.3f,%.3f)" ),
                      m_entries.size(),
                      aCameraView[3][0], aCameraView[3][1], aCameraView[3][2], aCameraView[3][3] );
        for( size_t i = 0; i < m_entries.size(); i++ )
        {
            const ENTRY& e = m_entries[i];
            wxLogMessage( wxT( "[GIZMO-RENDER]   entry[%zu] posA=(%.3f,%.3f,%.3f) posB=(%.3f,%.3f,%.3f)" ),
                          i, e.posA.x, e.posA.y, e.posA.z, e.posB.x, e.posB.y, e.posB.z );
        }
        s_renderLog++;
    }

    glPushAttrib( GL_ALL_ATTRIB_BITS );

    // Reset modelview to *just* the camera view — per-instance
    // assembly poses don't apply to gizmo geometry, which is already
    // in shared world space.
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadMatrixf( glm::value_ptr( aCameraProjection ) );

    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadMatrixf( glm::value_ptr( aCameraView ) );

    // Lighting off so the gizmo colours come through unshaded; this
    // keeps PRIMARY / SECONDARY / SELECTED hues distinguishable
    // regardless of camera angle relative to the assembly's lighting.
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    // Gizmos are debug / annotation overlays — should be visible even
    // when the connector is on the back side of the board or buried
    // under copper. Skip depth test entirely so they always render on
    // top; depth writes off so they don't pollute the depth buffer
    // for the next frame's board passes.
    glDisable( GL_DEPTH_TEST );
    glDepthMask( GL_FALSE );

    glDisable( GL_CULL_FACE );

    // Boxes first so the line gizmo entries draw on top — handy when
    // a mate-pair line cuts through an overlap zone. Within the box
    // pass, render filled (COLLISION/CONTACT) boxes BEFORE wireframe-
    // only (BROAD) so the BROAD wireframe is the last thing painted —
    // otherwise a board-level BROAD wireframe gets hidden under a
    // confirmed-COLLISION fill at the same coordinates.
    for( const OVERLAP_BOX& b : m_overlapBoxes )
    {
        if( b.kind != OVERLAP_KIND::BROAD )
            renderOverlapBox( b );
    }

    for( const OVERLAP_BOX& b : m_overlapBoxes )
    {
        if( b.kind == OVERLAP_KIND::BROAD )
            renderOverlapBox( b );
    }

    for( const ENTRY& e : m_entries )
        renderEntry( e );

    glPopMatrix();   // modelview
    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );

    glPopAttrib();
}


void MATE_GIZMO::renderEntry( const ENTRY& aEntry )
{
    STYLE s = styleFor( aEntry.source, aEntry.role, aEntry.selected );

    // When the user has selected a mate row, fade non-selected entries
    // so the chosen one stands out. Selected (or no-selection) entries
    // render fully opaque.
    const float alpha = ( aEntry.anySelected && !aEntry.selected ) ? 0.25f : 0.95f;

    // Sphere at each endpoint.
    const auto drawSphere = [&]( const glm::vec3& aPos, float aRadius )
    {
        glPushMatrix();
        glTranslatef( aPos.x, aPos.y, aPos.z );
        glColor4f( s.color.r, s.color.g, s.color.b, alpha );
        gluSphere( m_quadric, aRadius, 16, 12 );
        glPopMatrix();
    };

    drawSphere( aEntry.posA, s.sphereRadius );
    drawSphere( aEntry.posB, s.sphereRadius );

    // Connecting "rod". Use a thin cylinder so width is consistent
    // across drivers (glLineWidth caps at 1px on many GL backends).
    renderLineSegment( aEntry.posA, aEntry.posB, s.lineRadius, s.color, alpha );

    // DISABLED entries get an X marker through the midpoint so the
    // user can spot disabled mates at a glance.
    if( aEntry.role == ROLE::DISABLED )
    {
        const glm::vec3 mid = 0.5f * ( aEntry.posA + aEntry.posB );
        const float     halfX = 0.6f * s.sphereRadius * 4.0f;

        const auto bar = [&]( const glm::vec3& a, const glm::vec3& b )
        {
            renderLineSegment( a, b, 0.10f, s.color, alpha );
        };

        bar( mid + glm::vec3( -halfX, -halfX, 0 ), mid + glm::vec3( halfX, halfX, 0 ) );
        bar( mid + glm::vec3( -halfX,  halfX, 0 ), mid + glm::vec3( halfX, -halfX, 0 ) );
    }
}


void MATE_GIZMO::renderOverlapBox( const OVERLAP_BOX& aBox )
{
    // Inflate degenerate axes a tiny bit so the box is always visible.
    // OBB intersections collapse to a 0-thickness slab when the parts
    // exactly touch on one axis; without this the user sees nothing.
    glm::vec3 mn = aBox.minWorld;
    glm::vec3 mx = aBox.maxWorld;

    constexpr float kMinExtent = 0.02f;   // shared 3D-viewer units

    for( int i = 0; i < 3; i++ )
    {
        if( ( mx[i] - mn[i] ) < kMinExtent )
        {
            float midI = 0.5f * ( mx[i] + mn[i] );
            mn[i]      = midI - 0.5f * kMinExtent;
            mx[i]      = midI + 0.5f * kMinExtent;
        }
    }

    glm::vec3 fillColor;
    glm::vec3 lineColor;
    float     fillAlpha;
    float     lineAlpha;
    bool      drawFill = true;

    switch( aBox.kind )
    {
    case OVERLAP_KIND::COLLISION:
        // Saturated red, ~35% fill so the underlying model still reads
        // through; brighter red wireframe so the silhouette pops.
        fillColor = glm::vec3( 1.0f, 0.18f, 0.18f );
        lineColor = glm::vec3( 1.0f, 0.55f, 0.55f );
        fillAlpha = 0.35f;
        lineAlpha = 0.95f;
        break;

    case OVERLAP_KIND::CONTACT:
        // Yellow proximity highlight — clearly distinct from the red
        // collision box and from the green/cyan mate-pair gizmos.
        fillColor = glm::vec3( 1.0f, 0.85f, 0.20f );
        lineColor = glm::vec3( 1.0f, 1.00f, 0.55f );
        fillAlpha = 0.22f;
        lineAlpha = 0.85f;
        break;

    case OVERLAP_KIND::BROAD:
    default:
        // Wireframe-only blue box for the broad-phase debug view.
        // No fill so the user can see the model through it; thinner
        // lines so it doesn't dominate when stacked under a real
        // collision/contact box.
        fillColor = glm::vec3( 0.20f, 0.55f, 1.00f );
        lineColor = glm::vec3( 0.45f, 0.75f, 1.00f );
        fillAlpha = 0.0f;
        lineAlpha = 0.85f;
        drawFill  = false;
        break;
    }

    // 8 corners of the AABB.
    const glm::vec3 c[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
        { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
        { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z },
        { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z }
    };

    // If the box carries actual intersecting triangles (populated
    // from the mesh-tri test for confirmed COLLISIONs / CONTACTs),
    // render those triangles as a filled coloured surface — that's
    // the "actual overlap" visualization. The AABB box is suppressed
    // entirely when tris are present so the user's eye is drawn to
    // the real intersection geometry instead of a rectangular box
    // that would extend far beyond the actual contact zone.
    const bool drawTris = !aBox.triVerts.empty()
                          && ( aBox.kind == OVERLAP_KIND::COLLISION
                               || aBox.kind == OVERLAP_KIND::CONTACT );

    if( drawTris )
    {
        // Bright red fill for collisions, yellow for contacts.
        glm::vec3 triFill = ( aBox.kind == OVERLAP_KIND::COLLISION )
                              ? glm::vec3( 1.0f, 0.18f, 0.18f )
                              : glm::vec3( 1.0f, 0.82f, 0.20f );

        glColor4f( triFill.r, triFill.g, triFill.b, 0.65f );
        glBegin( GL_TRIANGLES );

        for( size_t i = 0; i + 2 < aBox.triVerts.size(); i += 3 )
        {
            glVertex3fv( &aBox.triVerts[i].x );
            glVertex3fv( &aBox.triVerts[i + 1].x );
            glVertex3fv( &aBox.triVerts[i + 2].x );
        }

        glEnd();

        // Skip the AABB box entirely — the triangle render IS the
        // visualization. Drawing a wireframe AABB on top of the
        // tris gave the impression we were "still using bounding
        // boxes", which we explicitly are not in this mode.
        return;
    }

    // Filled translucent faces — 6 quads. Skipped for BROAD-debug
    // boxes so the user can see right through them.
    if( drawFill )
    {
        glColor4f( fillColor.r, fillColor.g, fillColor.b, fillAlpha );
        glBegin( GL_QUADS );
        // -Z (bottom)
        glVertex3fv( &c[0].x ); glVertex3fv( &c[3].x );
        glVertex3fv( &c[2].x ); glVertex3fv( &c[1].x );
        // +Z (top)
        glVertex3fv( &c[4].x ); glVertex3fv( &c[5].x );
        glVertex3fv( &c[6].x ); glVertex3fv( &c[7].x );
        // -Y
        glVertex3fv( &c[0].x ); glVertex3fv( &c[1].x );
        glVertex3fv( &c[5].x ); glVertex3fv( &c[4].x );
        // +Y
        glVertex3fv( &c[3].x ); glVertex3fv( &c[7].x );
        glVertex3fv( &c[6].x ); glVertex3fv( &c[2].x );
        // -X
        glVertex3fv( &c[0].x ); glVertex3fv( &c[4].x );
        glVertex3fv( &c[7].x ); glVertex3fv( &c[3].x );
        // +X
        glVertex3fv( &c[1].x ); glVertex3fv( &c[2].x );
        glVertex3fv( &c[6].x ); glVertex3fv( &c[5].x );
        glEnd();
    }

    // Wireframe — 12 edges, drawn as thin cylinders so it's visible
    // even where glLineWidth caps at 1px. BROAD boxes use a slightly
    // thinner edge so they don't dominate stacked under a confirmed-
    // COLLISION box at the same location, but thick enough to read
    // when they cover a whole board substrate.
    const float edgeRadius = ( aBox.kind == OVERLAP_KIND::BROAD ) ? 0.010f : 0.014f;

    auto edge = [&]( int a, int b )
    {
        renderLineSegment( c[a], c[b], edgeRadius, lineColor, lineAlpha );
    };

    // bottom rectangle
    edge( 0, 1 ); edge( 1, 2 ); edge( 2, 3 ); edge( 3, 0 );
    // top rectangle
    edge( 4, 5 ); edge( 5, 6 ); edge( 6, 7 ); edge( 7, 4 );
    // verticals
    edge( 0, 4 ); edge( 1, 5 ); edge( 2, 6 ); edge( 3, 7 );
}


void MATE_GIZMO::renderLineSegment( const glm::vec3& aFrom, const glm::vec3& aTo,
                                     float aRadius, const glm::vec3& aColor, float aAlpha )
{
    const glm::vec3 delta  = aTo - aFrom;
    const float     length = glm::length( delta );

    if( length < 1e-4f )
        return;

    glPushMatrix();
    glTranslatef( aFrom.x, aFrom.y, aFrom.z );

    // gluCylinder draws along +Z by default; rotate so +Z aligns with
    // (aTo - aFrom). Build the rotation by axis-angle from the cross
    // product with the default +Z axis.
    const glm::vec3 zAxis( 0.0f, 0.0f, 1.0f );
    const glm::vec3 dirNorm = delta / length;

    const glm::vec3 axis = glm::cross( zAxis, dirNorm );
    const float     dot  = glm::dot( zAxis, dirNorm );

    if( glm::length( axis ) > 1e-4f )
    {
        const float angleDeg = glm::degrees( std::acos( glm::clamp( dot, -1.0f, 1.0f ) ) );
        glRotatef( angleDeg, axis.x, axis.y, axis.z );
    }
    else if( dot < 0.0f )
    {
        // Anti-parallel: rotate 180° around X to flip direction.
        glRotatef( 180.0f, 1.0f, 0.0f, 0.0f );
    }

    glColor4f( aColor.r, aColor.g, aColor.b, aAlpha );
    gluCylinder( m_quadric, aRadius, aRadius, length, 12, 1 );

    glPopMatrix();
}
