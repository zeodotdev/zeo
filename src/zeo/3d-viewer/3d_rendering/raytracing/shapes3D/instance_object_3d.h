/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef _INSTANCE_OBJECT_3D_H_
#define _INSTANCE_OBJECT_3D_H_

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/mat3x3.hpp>

#include "object_3d.h"
#include "../accelerators/container_3d.h"
#include "../accelerators/accelerator_3d.h"


/**
 * Wraps a single sub-board's geometry (CONTAINER_3D + ACCELERATOR_3D) under a
 * 4×4 pose matrix. Intersect / IntersectP transform incoming rays into the
 * inner container's local frame, run the inner accelerator, then transform
 * the hit data back into world space.
 *
 * Used by RENDER_3D_RAYTRACE_BASE::ReloadMultiInstance to represent one
 * BOARD_3D_INSTANCE in the merged top-level scene without requiring every
 * OBJECT_3D leaf to be transform-aware.
 *
 * Ownership: the wrapper owns its inner container and accelerator, since
 * each lives only as part of this one assembly instance. Inner container's
 * destructor deletes its OBJECT_3D leaves; we delete the accelerator
 * explicitly.
 */
class INSTANCE_OBJECT_3D : public OBJECT_3D
{
public:
    /**
     * @param aInner       container holding this instance's per-board OBJECT_3D
     *                     leaves (already in local-frame coordinates, NOT
     *                     pose-transformed).
     * @param aAccelerator BVH built from `aInner` for fast inner-frame ray
     *                     queries.
     * @param aPose        local→world pose. Treated as a rigid transform
     *                     (rotation + translation); non-uniform scale will
     *                     break the t-distance preservation that lets us
     *                     reuse HITINFO::m_tHit unchanged. The OpenGL multi-
     *                     instance code only uses uniform scale × rotation
     *                     × translate, so the rigid assumption holds for
     *                     equivalent assembly poses.
     */
    INSTANCE_OBJECT_3D( std::unique_ptr<CONTAINER_3D> aInner,
                        std::unique_ptr<ACCELERATOR_3D> aAccelerator,
                        const glm::mat4& aPose );

    ~INSTANCE_OBJECT_3D() override;

    // OBJECT_3D interface.
    bool    Intersect( const RAY& aRay, HITINFO& aHitInfo ) const override;
    bool    IntersectP( const RAY& aRay, float aMaxDistance ) const override;
    bool    Intersects( const BBOX_3D& aBBox ) const override;
    SFVEC3F GetDiffuseColor( const HITINFO& aHitInfo ) const override;

private:
    std::unique_ptr<CONTAINER_3D>   m_inner;
    std::unique_ptr<ACCELERATOR_3D> m_accelerator;

    glm::mat4 m_pose;                 ///< local → world
    glm::mat4 m_inversePose;          ///< world → local
    glm::mat3 m_normalLocalToWorld;

    // Uniform scale extracted from the pose (length of axis 0). The MBS
    // assembly poses produced by the canvas are uniform scale × rotation
    // × translate, so a single scalar suffices. Used to convert HITINFO::
    // m_tHit between the inner (local-unit) ray and the outer (world-unit)
    // ray; without this, tHit early-outs in the inner BVH compare against
    // the wrong scale and either skip valid hits or accept invalid ones.
    float m_localToWorldScale = 1.0f;
    float m_worldToLocalScale = 1.0f;
};

#endif // _INSTANCE_OBJECT_3D_H_
