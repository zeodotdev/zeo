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

#ifndef MATE_GIZMO_H
#define MATE_GIZMO_H

#include <kiid.h>
#include <plugins/3dapi/xv3d_types.h>
#include <wx/string.h>

#include <glm/glm.hpp>

#include <vector>

class GLUquadric;

/**
 * Renders 3D-assembly mate-pair visualizations on top of the
 * composited multi-board scene. For each connector mate pair the
 * solver knows about, draws:
 *   - a small sphere at each footprint's pad-centroid in world space,
 *   - a coloured line linking the two,
 *   - a thicker / brighter style for the *primary* pair on each edge,
 *   - a fade for non-selected pairs once the user picks one in the
 *     Mates panel.
 *
 * The gizmo is rendered AFTER every per-instance `RENDER_3D_OPENGL`
 * pass completes — in world space, so it ignores the per-instance
 * assembly pose. `Render` resets the modelview to just the camera
 * view before drawing.
 */
class MATE_GIZMO
{
public:
    /// Source of the gizmo entry — drives hue.
    /// AUTO/CUSTOM: M6.D mate-pair connections (intentional contact).
    /// COLLISION : M6.E unintended component overlap (rendered red so
    /// users can find the offending pair in the 3D view).
    enum class SOURCE
    {
        AUTO,
        CUSTOM,
        COLLISION
    };

    /// Solver role — drives line weight + sphere size.
    enum class ROLE
    {
        PRIMARY,           ///< Highest-weight pair on its edge; constrains placement
        SECONDARY,         ///< Alignment-check pair (over-constrained on its edge)
        DISABLED           ///< User-disabled mate (drawn dim, dashed)
    };

    /// One mate gizmo entry: two world-space endpoints + style.
    struct ENTRY
    {
        glm::vec3   posA;             ///< World position of mate centre on the parent board
        glm::vec3   posB;             ///< World position of mate centre on the child board
        SOURCE      source;
        ROLE        role;

        /// Stable identity for selection / highlight. Encoded as
        /// "instanceA_uuid|footprintRefA|instanceB_uuid|footprintRefB"
        /// in canonical (lower-uuid-first) order so the same physical
        /// pair always produces the same id regardless of which side
        /// the panel iterated first. Empty string = "no id" (entry
        /// never highlights).
        wxString    matePairId;

        bool        selected;         ///< Highlight (drawn opaque, larger sphere)
        bool        anySelected;      ///< If any entry is selected, non-selected entries fade
    };

    MATE_GIZMO();
    ~MATE_GIZMO();

    /// Replace the gizmo's entry list. Cheap — entries are POD-ish.
    void SetEntries( std::vector<ENTRY> aEntries );

    /// Render every entry. Caller must have a live GL context.
    /// The gizmo manages its own modelview matrix (sets it from the
    /// supplied camera view + projection; preserves the prior matrix
    /// state by glPushMatrix / glPopMatrix).
    void Render( const glm::mat4& aCameraView, const glm::mat4& aCameraProjection );

private:
    /// Draw one entry — sphere A, sphere B, line A↔B, optional residual.
    void renderEntry( const ENTRY& aEntry );

    /// Helper for thicker-than-1px lines that work even where
    /// glLineWidth caps at 1.0 — draws a short cylinder approximation.
    void renderLineSegment( const glm::vec3& aFrom, const glm::vec3& aTo,
                            float aRadius, const glm::vec3& aColor, float aAlpha );

    GLUquadric* m_quadric = nullptr;

    std::vector<ENTRY> m_entries;
};

#endif // MATE_GIZMO_H
