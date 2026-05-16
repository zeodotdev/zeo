/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "instance_object_3d.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../ray.h"
#include "../hitinfo.h"


INSTANCE_OBJECT_3D::INSTANCE_OBJECT_3D( std::unique_ptr<CONTAINER_3D>   aInner,
                                          std::unique_ptr<ACCELERATOR_3D> aAccelerator,
                                          const glm::mat4& aPose ) :
        OBJECT_3D( OBJECT_3D_TYPE::DUMMYBLOCK ),
        m_inner( std::move( aInner ) ),
        m_accelerator( std::move( aAccelerator ) ),
        m_pose( aPose )
{
    m_inversePose       = glm::inverse( m_pose );
    m_normalLocalToWorld = glm::transpose( glm::mat3( m_inversePose ) );

    // Uniform-scale extraction. axisLen ≈ s for all axes; we take axis 0
    // as the canonical sample and rely on the assembly canvas producing
    // poses with uniform scale.
    m_localToWorldScale = glm::length( glm::vec3( m_pose[0] ) );

    if( m_localToWorldScale <= 0.0f )
        m_localToWorldScale = 1.0f;

    m_worldToLocalScale = 1.0f / m_localToWorldScale;

    // World-space AABB = pose-transformed inner BBox. Iterate the 8 corners
    // of the inner bbox under the pose; the new AABB is the envelope.
    const BBOX_3D& innerBBox = m_inner->GetBBox();

    if( innerBBox.IsInitialized() )
    {
        const SFVEC3F& mn = innerBBox.Min();
        const SFVEC3F& mx = innerBBox.Max();

        SFVEC3F worldMin( std::numeric_limits<float>::infinity() );
        SFVEC3F worldMax( -std::numeric_limits<float>::infinity() );

        for( int sx = 0; sx < 2; sx++ )
        {
            for( int sy = 0; sy < 2; sy++ )
            {
                for( int sz = 0; sz < 2; sz++ )
                {
                    glm::vec4 corner( sx ? mx.x : mn.x,
                                      sy ? mx.y : mn.y,
                                      sz ? mx.z : mn.z, 1.0f );
                    glm::vec3 world = glm::vec3( m_pose * corner );
                    worldMin = glm::min( worldMin, world );
                    worldMax = glm::max( worldMax, world );
                }
            }
        }

        m_bbox.Set( worldMin, worldMax );
        m_centroid = ( worldMin + worldMax ) * 0.5f;
    }
}


INSTANCE_OBJECT_3D::~INSTANCE_OBJECT_3D() = default;


bool INSTANCE_OBJECT_3D::Intersects( const BBOX_3D& aBBox ) const
{
    return m_bbox.Intersects( aBBox );
}


bool INSTANCE_OBJECT_3D::Intersect( const RAY& aRay, HITINFO& aHitInfo ) const
{
    if( !m_accelerator )
        return false;

    // Cheap world-AABB reject — the inner accelerator's traversal also
    // rejects, but skipping the ray transform when the wrapper isn't on
    // the ray's path is worth the extra slab test.
    if( !m_bbox.Intersect( aRay ) )
        return false;

    // Transform ray into inner local frame. We pass the unnormalized
    // local direction to the inner accelerator on purpose: under the
    // pose M = T·R·s·I (uniform scale), the ray's t parameter is
    // identical in world and local frames since M·M⁻¹ = I on vectors,
    // so a hit at parametric t in local space corresponds to world
    // point origin_world + t·dir_world. Normalizing local dir and
    // rescaling t loses precision when the inner geometry has thin Z
    // layers (F.Mask vs F.Cu, ~0.01 mm apart), causing the refracted
    // ray to skip through to background at near-flat-on view angles.
    const glm::vec4 originWorld( aRay.m_Origin, 1.0f );
    const glm::vec4 dirWorld(    aRay.m_Dir,    0.0f );

    const SFVEC3F originLocal = SFVEC3F( m_inversePose * originWorld );
    const SFVEC3F dirLocal    = SFVEC3F( m_inversePose * dirWorld );

    RAY localRay;
    localRay.Init( originLocal, dirLocal );

    if( !m_accelerator->Intersect( localRay, aHitInfo ) )
        return false;

    // tHit is already in world units (= local t since dir is non-
    // normalized). Hit point + normal need transforming back from
    // local to world frame.
    aHitInfo.m_HitPoint  = SFVEC3F( m_pose * glm::vec4( aHitInfo.m_HitPoint, 1.0f ) );
    aHitInfo.m_HitNormal = glm::normalize( m_normalLocalToWorld * aHitInfo.m_HitNormal );
    return true;
}


bool INSTANCE_OBJECT_3D::IntersectP( const RAY& aRay, float aMaxDistance ) const
{
    if( !m_accelerator )
        return false;

    if( !m_bbox.Intersect( aRay ) )
        return false;

    const glm::vec4 originWorld( aRay.m_Origin, 1.0f );
    const glm::vec4 dirWorld(    aRay.m_Dir,    0.0f );

    const SFVEC3F originLocal = SFVEC3F( m_inversePose * originWorld );
    const SFVEC3F dirLocal    = SFVEC3F( m_inversePose * dirWorld );

    RAY localRay;
    localRay.Init( originLocal, dirLocal );

    // aMaxDistance is in world units; with non-normalized local dir,
    // the local t parameter equals world distance, so it can be passed
    // through unchanged.
    return m_accelerator->IntersectP( localRay, aMaxDistance );
}


SFVEC3F INSTANCE_OBJECT_3D::GetDiffuseColor( const HITINFO& aHitInfo ) const
{
    // The inner-most hit object's diffuse color is what the user sees.
    // pHitObject is set by the inner BVH traversal.
    if( aHitInfo.pHitObject )
        return aHitInfo.pHitObject->GetDiffuseColor( aHitInfo );

    return SFVEC3F( 0.5f, 0.5f, 0.5f );
}
