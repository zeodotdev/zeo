/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015-2022 Mario Luzeiro <mrluzeiro@ua.pt>
 * Copyright (C) 2023 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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

#include "render_3d_raytrace_base.h"
#include "shapes3D/plane_3d.h"
#include "shapes3D/round_segment_3d.h"
#include "shapes3D/layer_item_3d.h"
#include "shapes3D/cylinder_3d.h"
#include "shapes3D/frustum_3d.h"
#include "shapes3D/triangle_3d.h"
#include "shapes2D/layer_item_2d.h"
#include "shapes2D/ring_2d.h"
#include "shapes2D/polygon_2d.h"
#include "shapes2D/filled_circle_2d.h"
#include "shapes2D/round_segment_2d.h"
#include "accelerators/bvh_pbrt.h"
#include "3d_fastmath.h"
#include "3d_math.h"

#include <board.h>
#include <footprint.h>
#include <footprint_library_adapter.h>
#include <eda_3d_viewer_frame.h>
#include <project_pcb.h>

#include <base_units.h>
#include <core/profile.h>        // To use GetRunningMicroSecs or another profiling utility

/**
 * Perform an interpolation step to easy control the transparency based on the
 * gray color value and transparency.
 *
 * @param aGrayColorValue - diffuse gray value
 * @param aTransparency - control
 * @return transparency to use in material
 */
static float TransparencyControl( float aGrayColorValue, float aTransparency )
{
    const float aaa = aTransparency * aTransparency * aTransparency;

    // 1.00-1.05*(1.0-x)^3
    float ca = 1.0f - aTransparency;
    ca       = 1.00f - 1.05f * ca * ca * ca;

    return glm::max( glm::min( aGrayColorValue * ca + aaa, 1.0f ), 0.0f );
}

/**
 * Scale conversion from 3d model units to pcb units
 */
#define UNITS3D_TO_UNITSPCB ( pcbIUScale.IU_PER_MM )


void RENDER_3D_RAYTRACE_BASE::setupMaterials()
{
    MATERIAL::SetDefaultRefractionRayCount(
            currentAdapter().m_Cfg->m_Render.raytrace_nrsamples_refractions );
    MATERIAL::SetDefaultReflectionRayCount(
            currentAdapter().m_Cfg->m_Render.raytrace_nrsamples_reflections );

    MATERIAL::SetDefaultRefractionRecursionCount(
            currentAdapter().m_Cfg->m_Render.raytrace_recursivelevel_refractions );
    MATERIAL::SetDefaultReflectionRecursionCount(
            currentAdapter().m_Cfg->m_Render.raytrace_recursivelevel_reflections );

    double mmTo3Dunits = pcbIUScale.IU_PER_MM * currentAdapter().BiuTo3dUnits();

    if( currentAdapter().m_Cfg->m_Render.raytrace_procedural_textures )
    {
        m_boardMaterial = BOARD_NORMAL( 0.40f * mmTo3Dunits );
        m_copperMaterial = COPPER_NORMAL( 4.0f * mmTo3Dunits, &m_boardMaterial );
        m_platedCopperMaterial = PLATED_COPPER_NORMAL( 0.5f * mmTo3Dunits );
        m_solderMaskMaterial = SOLDER_MASK_NORMAL( &m_boardMaterial );
        m_plasticMaterial = PLASTIC_NORMAL( 0.05f * mmTo3Dunits );
        m_shinyPlasticMaterial = PLASTIC_SHINE_NORMAL( 0.1f * mmTo3Dunits );
        m_brushedMetalMaterial = BRUSHED_METAL_NORMAL( 0.05f * mmTo3Dunits );
        m_silkScreenMaterial = SILK_SCREEN_NORMAL( 0.25f * mmTo3Dunits );
    }

    // http://devernay.free.fr/cours/opengl/materials.html
    // Copper
    const SFVEC3F copperSpecularLinear =
            ConvertSRGBToLinear( glm::clamp( (SFVEC3F) currentAdapter().m_CopperColor * 0.5f + 0.25f,
                    SFVEC3F( 0.0f ), SFVEC3F( 1.0f ) ) );

    m_materials.m_Copper = BLINN_PHONG_MATERIAL(
            ConvertSRGBToLinear( (SFVEC3F) currentAdapter().m_CopperColor * 0.3f ),
            SFVEC3F( 0.0f ), copperSpecularLinear, 0.4f * 128.0f, 0.0f, 0.0f );

    if( currentAdapter().m_Cfg->m_Render.raytrace_procedural_textures )
        m_materials.m_Copper.SetGenerator( &m_platedCopperMaterial );

    m_materials.m_NonPlatedCopper = BLINN_PHONG_MATERIAL(
            ConvertSRGBToLinear( SFVEC3F( 0.191f, 0.073f, 0.022f ) ), SFVEC3F( 0.0f, 0.0f, 0.0f ),
            SFVEC3F( 0.256f, 0.137f, 0.086f ), 0.15f * 128.0f, 0.0f, 0.0f );

    if( currentAdapter().m_Cfg->m_Render.raytrace_procedural_textures )
        m_materials.m_NonPlatedCopper.SetGenerator( &m_copperMaterial );

    m_materials.m_Paste = BLINN_PHONG_MATERIAL(
            ConvertSRGBToLinear( (SFVEC3F) currentAdapter().m_SolderPasteColor )
                    * ConvertSRGBToLinear( (SFVEC3F) currentAdapter().m_SolderPasteColor ),
            SFVEC3F( 0.0f, 0.0f, 0.0f ),
            ConvertSRGBToLinear( (SFVEC3F) currentAdapter().m_SolderPasteColor )
                    * ConvertSRGBToLinear(
                            (SFVEC3F) currentAdapter().m_SolderPasteColor ),
            0.10f * 128.0f, 0.0f, 0.0f );

    m_materials.m_SilkS = BLINN_PHONG_MATERIAL( ConvertSRGBToLinear( SFVEC3F( 0.11f ) ),
            SFVEC3F( 0.0f, 0.0f, 0.0f ),
            glm::clamp( ( ( SFVEC3F )( 1.0f ) - ConvertSRGBToLinear(
                                  (SFVEC3F) currentAdapter().m_SilkScreenColorTop ) ),
                        SFVEC3F( 0.0f ), SFVEC3F( 0.10f ) ), 0.078125f * 128.0f, 0.0f, 0.0f );

    if( currentAdapter().m_Cfg->m_Render.raytrace_procedural_textures )
        m_materials.m_SilkS.SetGenerator( &m_silkScreenMaterial );

    // Assume that SolderMaskTop == SolderMaskBot
    const float solderMask_gray =
            ( currentAdapter().m_SolderMaskColorTop.r + currentAdapter().m_SolderMaskColorTop.g
                    + currentAdapter().m_SolderMaskColorTop.b )
            / 3.0f;

    const float solderMask_transparency = TransparencyControl( solderMask_gray,
            1.0f - currentAdapter().m_SolderMaskColorTop.a );

    // For darker solder mask colors, increase shininess for a more realistic appearance.
    // Darker colors appear to have a sharper specular highlight in real life.
    const float minSolderMaskShininess = 0.85f * 128.0f;
    const float maxSolderMaskShininess = 512.0f;
    const float solderMaskShininess = minSolderMaskShininess
            + ( maxSolderMaskShininess - minSolderMaskShininess ) * ( 1.0f - solderMask_gray );

    // Darker solder mask colors need lower reflection to prevent washed-out appearance
    const float solderMaskReflection = glm::clamp( solderMask_gray * 0.3f, 0.02f, 0.16f );

    m_materials.m_SolderMask = BLINN_PHONG_MATERIAL(
            ConvertSRGBToLinear( (SFVEC3F) currentAdapter().m_SolderMaskColorTop ) * 0.10f,
            SFVEC3F( 0.0f, 0.0f, 0.0f ),
            SFVEC3F( glm::clamp( solderMask_gray * 2.0f, 0.30f, 1.0f ) ), solderMaskShininess,
            solderMask_transparency, solderMaskReflection );

    m_materials.m_SolderMask.SetCastShadows( true );
    m_materials.m_SolderMask.SetRefractionRayCount( 1 );

    if( currentAdapter().m_Cfg->m_Render.raytrace_procedural_textures )
        m_materials.m_SolderMask.SetGenerator( &m_solderMaskMaterial );

    m_materials.m_EpoxyBoard =
            BLINN_PHONG_MATERIAL( ConvertSRGBToLinear( SFVEC3F( 16.0f / 255.0f, 14.0f / 255.0f,
                                                                10.0f / 255.0f ) ),
                                  SFVEC3F( 0.0f, 0.0f, 0.0f ),
                                  ConvertSRGBToLinear( SFVEC3F( 10.0f / 255.0f, 8.0f / 255.0f,
                                                                10.0f / 255.0f ) ),
                                  0.1f * 128.0f, 1.0f - currentAdapter().m_BoardBodyColor.a, 0.0f );

    m_materials.m_EpoxyBoard.SetAbsorvance( 10.0f );

    if( currentAdapter().m_Cfg->m_Render.raytrace_procedural_textures )
        m_materials.m_EpoxyBoard.SetGenerator( &m_boardMaterial );

    SFVEC3F bgTop = ConvertSRGBToLinear( (SFVEC3F) currentAdapter().m_BgColorTop );

    m_materials.m_Floor = BLINN_PHONG_MATERIAL( bgTop * 0.125f, SFVEC3F( 0.0f, 0.0f, 0.0f ),
                                                ( SFVEC3F( 1.0f ) - bgTop ) / 3.0f,
                                                0.10f * 128.0f, 1.0f, 0.50f );
    m_materials.m_Floor.SetCastShadows( false );
    m_materials.m_Floor.SetReflectionRecursionCount( 1 );
}


void RENDER_3D_RAYTRACE_BASE::createObject( CONTAINER_3D& aDstContainer, const OBJECT_2D* aObject2D,
                                            float aZMin, float aZMax, const MATERIAL* aMaterial,
                                            const SFVEC3F& aObjColor )
{
    switch( aObject2D->GetObjectType() )
    {
    case OBJECT_2D_TYPE::DUMMYBLOCK:
    {
        m_convertedDummyBlockCount++;

        XY_PLANE* objPtr;
        objPtr = new XY_PLANE( BBOX_3D(
                SFVEC3F( aObject2D->GetBBox().Min().x, aObject2D->GetBBox().Min().y, aZMin ),
                SFVEC3F( aObject2D->GetBBox().Max().x, aObject2D->GetBBox().Max().y, aZMin ) ) );
        objPtr->SetMaterial( aMaterial );
        objPtr->SetColor( ConvertSRGBToLinear( aObjColor ) );
        aDstContainer.Add( objPtr );

        objPtr = new XY_PLANE( BBOX_3D(
                SFVEC3F( aObject2D->GetBBox().Min().x, aObject2D->GetBBox().Min().y, aZMax ),
                SFVEC3F( aObject2D->GetBBox().Max().x, aObject2D->GetBBox().Max().y, aZMax ) ) );
        objPtr->SetMaterial( aMaterial );
        objPtr->SetColor( ConvertSRGBToLinear( aObjColor ) );
        aDstContainer.Add( objPtr );
        break;
    }

    case OBJECT_2D_TYPE::ROUNDSEG:
    {
        m_converted2dRoundSegmentCount++;

        const ROUND_SEGMENT_2D* aRoundSeg2D = static_cast<const ROUND_SEGMENT_2D*>( aObject2D );
        ROUND_SEGMENT*          objPtr      = new ROUND_SEGMENT( *aRoundSeg2D, aZMin, aZMax );
        objPtr->SetMaterial( aMaterial );
        objPtr->SetColor( ConvertSRGBToLinear( aObjColor ) );
        aDstContainer.Add( objPtr );
        break;
    }

    default:
    {
        LAYER_ITEM* objPtr = new LAYER_ITEM( aObject2D, aZMin, aZMax );
        objPtr->SetMaterial( aMaterial );
        objPtr->SetColor( ConvertSRGBToLinear( aObjColor ) );
        aDstContainer.Add( objPtr );
        break;
    }
    }
}


void RENDER_3D_RAYTRACE_BASE::createItemsFromContainer( const BVH_CONTAINER_2D* aContainer2d,
                                                   PCB_LAYER_ID aLayer_id,
                                                   const MATERIAL* aMaterialLayer,
                                                   const SFVEC3F& aLayerColor,
                                                   float aLayerZOffset )
{
    if( aContainer2d == nullptr )
        return;

    EDA_3D_VIEWER_SETTINGS::RENDER_SETTINGS& cfg = currentAdapter().m_Cfg->m_Render;
    bool                                     isSilk = aLayer_id == B_SilkS || aLayer_id == F_SilkS;
    const LIST_OBJECT2D&                     listObject2d = aContainer2d->GetList();

    if( listObject2d.size() == 0 )
        return;

    for( const OBJECT_2D* object2d_A : listObject2d )
    {
        // not yet used / implemented (can be used in future to clip the objects in the
        // board borders
        OBJECT_2D* object2d_C = CSGITEM_FULL;

        std::vector<const OBJECT_2D*>* object2d_B = CSGITEM_EMPTY;

        object2d_B = new std::vector<const OBJECT_2D*>();

        // Subtract holes but not in SolderPaste
        // (can be added as an option in future)
        if( !( aLayer_id == B_Paste || aLayer_id == F_Paste ) )
        {
            // Check if there are any layerhole that intersects this object
            // Eg: a segment is cut by a via hole or THT hole.
            const MAP_CONTAINER_2D_BASE& layerHolesMap = currentAdapter().GetLayerHoleMap();

            if( layerHolesMap.find( aLayer_id ) != layerHolesMap.end() )
            {
                const BVH_CONTAINER_2D* holes2d = layerHolesMap.at( aLayer_id );

                CONST_LIST_OBJECT2D intersecting;

                holes2d->GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                for( const OBJECT_2D* hole2d : intersecting )
                    object2d_B->push_back( hole2d );
            }

            // Check if there are any THT that intersects this object. If we're processing a silk
            // layer and the flag is set, then clip the silk at the outer edge of the annular ring,
            // rather than the at the outer edge of the copper plating.
            const BVH_CONTAINER_2D& throughHoleOuter =
                        cfg.clip_silk_on_via_annuli && isSilk ? currentAdapter().GetViaAnnuli()
                                                              : currentAdapter().GetTH_ODs();

            if( !throughHoleOuter.GetList().empty() )
            {
                CONST_LIST_OBJECT2D intersecting;

                throughHoleOuter.GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                for( const OBJECT_2D* hole2d : intersecting )
                    object2d_B->push_back( hole2d );
            }

            // Clip counterbore/countersink cutouts from copper layers
            // Front cutouts affect F_Cu, back cutouts affect B_Cu
            auto clipCutouts = [this, &object2d_A, &object2d_B]( const BVH_CONTAINER_2D& cutouts )
            {
                if( !cutouts.GetList().empty() )
                {
                    CONST_LIST_OBJECT2D intersecting;
                    cutouts.GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                    for( const OBJECT_2D* cutout : intersecting )
                        object2d_B->push_back( cutout );
                }
            };

            if( aLayer_id == F_Cu )
            {
                clipCutouts( currentAdapter().GetFrontCounterboreCutouts() );
                clipCutouts( currentAdapter().GetFrontCountersinkCutouts() );
                clipCutouts( currentAdapter().GetTertiarydrillCutouts() );
            }
            else if( aLayer_id == B_Cu )
            {
                clipCutouts( currentAdapter().GetBackCounterboreCutouts() );
                clipCutouts( currentAdapter().GetBackCountersinkCutouts() );
                clipCutouts( currentAdapter().GetBackdrillCutouts() );
            }
        }

        if( !m_antioutlineBoard2dObjects->GetList().empty() )
        {
            CONST_LIST_OBJECT2D intersecting;

            m_antioutlineBoard2dObjects->GetIntersectingObjects( object2d_A->GetBBox(),
                                                                 intersecting );

            for( const OBJECT_2D* obj : intersecting )
                object2d_B->push_back( obj );
        }

        const MAP_CONTAINER_2D_BASE& mapLayers = currentAdapter().GetLayerMap();

        if( cfg.subtract_mask_from_silk
            && (    ( aLayer_id == B_SilkS && mapLayers.find( B_Mask ) != mapLayers.end() )
                 || ( aLayer_id == F_SilkS && mapLayers.find( F_Mask ) != mapLayers.end() ) ) )
        {
            const PCB_LAYER_ID maskLayer = ( aLayer_id == B_SilkS ) ? B_Mask : F_Mask;

            const BVH_CONTAINER_2D* containerMaskLayer2d = mapLayers.at( maskLayer );

            CONST_LIST_OBJECT2D intersecting;

            if( containerMaskLayer2d ) // can be null if B_Mask or F_Mask is not shown
                containerMaskLayer2d->GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

            for( const OBJECT_2D* obj2d : intersecting )
                object2d_B->push_back( obj2d );
        }

        if( object2d_B->empty() )
        {
            delete object2d_B;
            object2d_B = CSGITEM_EMPTY;
        }

        if( ( object2d_B == CSGITEM_EMPTY ) && ( object2d_C == CSGITEM_FULL ) )
        {
            LAYER_ITEM* objPtr = new LAYER_ITEM( object2d_A,
                                    currentAdapter().GetLayerBottomZPos( aLayer_id ) - aLayerZOffset,
                                    currentAdapter().GetLayerTopZPos( aLayer_id ) + aLayerZOffset );
            objPtr->SetMaterial( aMaterialLayer );
            objPtr->SetColor( ConvertSRGBToLinear( aLayerColor ) );
            currentContainer().Add( objPtr );
        }
        else
        {
            LAYER_ITEM_2D* itemCSG2d = new LAYER_ITEM_2D( object2d_A, object2d_B, object2d_C,
                                                          object2d_A->GetBoardItem() );
            m_containerWithObjectsToDelete.Add( itemCSG2d );

            LAYER_ITEM* objPtr = new LAYER_ITEM( itemCSG2d,
                                    currentAdapter().GetLayerBottomZPos( aLayer_id ) - aLayerZOffset,
                                    currentAdapter().GetLayerTopZPos( aLayer_id ) + aLayerZOffset );

            objPtr->SetMaterial( aMaterialLayer );
            objPtr->SetColor( ConvertSRGBToLinear( aLayerColor ) );

            currentContainer().Add( objPtr );
        }
    }
}


extern void buildBoardBoundingBoxPoly( const BOARD* aBoard, SHAPE_POLY_SET& aOutline );


void RENDER_3D_RAYTRACE_BASE::Reload( REPORTER* aStatusReporter, REPORTER* aWarningReporter,
                                 bool aOnlyLoadCopperAndShapes )
{
    // Top-level dispatch: if the caller staged a multi-instance list via
    // SetPendingInstances (the MBS multi-board path), redirect to the
    // multi-instance build and clear the staged list so a subsequent
    // single-board reload won't pick it back up.
    if( !m_pendingInstances.empty() && !m_overrideContainer )
    {
        std::vector<INSTANCE_DESC> instances = std::move( m_pendingInstances );
        m_pendingInstances.clear();
        ReloadMultiInstance( instances, aStatusReporter, aWarningReporter );
        return;
    }

    // Multi-instance scene-build mode: when m_overrideContainer is set,
    // ReloadMultiInstance is driving us per sub-board. Skip the top-level
    // setup (it manages container/material/camera state itself, once for
    // the whole assembly) but still run the geometry-build body against
    // the per-instance adapter+container under our overrides. We also
    // skip the global stat reset / outline-2D rebuild since those are
    // assembly-wide.
    const bool inInstanceBuild = ( m_overrideContainer != nullptr );

    int64_t stats_startReloadTime = GetRunningMicroSecs();

    if( !inInstanceBuild )
    {
        m_reloadRequested = false;
        m_modelMaterialMap.clear();

        OBJECT_2D_STATS::Instance().ResetStats();
        OBJECT_3D_STATS::Instance().ResetStats();

        // Single-board path: trace-time queries fall through to
        // m_boardAdapter directly. (Multi-instance path overrides this
        // in ReloadMultiInstance.) The single adapter IS the world
        // frame, so its thickness is already in world 3D-units.
        m_renderAdapter                    = &m_boardAdapter;
        m_renderNonCopperLayerThickness3DU = m_boardAdapter.GetNonCopperLayerThickness();
    }

    if( !aOnlyLoadCopperAndShapes )
    {
        // Each adapter (top-level OR per-instance) needs InitSettings so
        // its BiuTo3dUnits / footprint Z lookup are valid for THIS build.
        currentAdapter().InitSettings( aStatusReporter, aWarningReporter );

        if( !inInstanceBuild )
        {
            // Global camera framing — only at the top level. In multi-
            // instance mode the assembly canvas frames the whole composite.
            SFVEC3F camera_pos = currentAdapter().GetBoardCenter();
            m_camera.SetBoardLookAtPos( camera_pos );
        }
    }

    currentContainer().Clear();

    if( !inInstanceBuild )
        m_containerWithObjectsToDelete.Clear();

    setupMaterials();

    if( aStatusReporter )
        aStatusReporter->Report( _( "Load Raytracing: board" ) );

    // Create and add the outline board
    delete m_outlineBoard2dObjects;
    delete m_antioutlineBoard2dObjects;

    m_outlineBoard2dObjects     = new CONTAINER_2D;
    m_antioutlineBoard2dObjects = new BVH_CONTAINER_2D;

    std::bitset<LAYER_3D_END> layerFlags = currentAdapter().GetVisibleLayers();

    if( !aOnlyLoadCopperAndShapes )
    {
        const int outlineCount = currentAdapter().GetBoardPoly().OutlineCount();

        if( outlineCount > 0 )
        {
            float divFactor = 0.0f;

            if( currentAdapter().GetViaCount() )
                divFactor = currentAdapter().GetAverageViaHoleDiameter() * 18.0f;
            else if( currentAdapter().GetHoleCount() )
                divFactor = currentAdapter().GetAverageHoleDiameter() * 8.0f;

            SHAPE_POLY_SET boardPolyCopy = currentAdapter().GetBoardPoly();

            // Calculate an antiboard outline
            SHAPE_POLY_SET antiboardPoly;

            buildBoardBoundingBoxPoly( currentAdapter().GetBoard(), antiboardPoly );

            antiboardPoly.BooleanSubtract( boardPolyCopy );
            antiboardPoly.Fracture();

            for( int ii = 0; ii < antiboardPoly.OutlineCount(); ii++ )
            {
                ConvertPolygonToBlocks( antiboardPoly, *m_antioutlineBoard2dObjects,
                                        currentAdapter().BiuTo3dUnits(), -1.0f,
                                        *currentAdapter().GetBoard(), ii );
            }

            m_antioutlineBoard2dObjects->BuildBVH();

            boardPolyCopy.Fracture();

            for( int ii = 0; ii < boardPolyCopy.OutlineCount(); ii++ )
            {
                ConvertPolygonToBlocks( boardPolyCopy, *m_outlineBoard2dObjects,
                                        currentAdapter().BiuTo3dUnits(), divFactor,
                                        *currentAdapter().GetBoard(), ii );
            }

            if( layerFlags.test( LAYER_3D_BOARD ) )
            {
                const LIST_OBJECT2D& listObjects = m_outlineBoard2dObjects->GetList();

                for( const OBJECT_2D* object2d_A : listObjects )
                {
                    std::vector<const OBJECT_2D*>* object2d_B = new std::vector<const OBJECT_2D*>();

                    // Check if there are any THT that intersects this outline object part
                    if( !currentAdapter().GetTH_ODs().GetList().empty() )
                    {
                        const BVH_CONTAINER_2D& throughHoles = currentAdapter().GetTH_ODs();
                        CONST_LIST_OBJECT2D     intersecting;

                        throughHoles.GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                        for( const OBJECT_2D* hole : intersecting )
                        {
                            if( object2d_A->Intersects( hole->GetBBox() ) )
                                object2d_B->push_back( hole );
                        }
                    }

                    // Subtract counterbore/countersink cutouts from board body
                    auto addCutoutsFromContainer =
                            [&]( const BVH_CONTAINER_2D& aContainer )
                            {
                                if( !aContainer.GetList().empty() )
                                {
                                    CONST_LIST_OBJECT2D intersecting;
                                    aContainer.GetIntersectingObjects( object2d_A->GetBBox(),
                                                                       intersecting );

                                    for( const OBJECT_2D* cutout : intersecting )
                                    {
                                        if( object2d_A->Intersects( cutout->GetBBox() ) )
                                            object2d_B->push_back( cutout );
                                    }
                                }
                            };

                    addCutoutsFromContainer( currentAdapter().GetFrontCounterboreCutouts() );
                    addCutoutsFromContainer( currentAdapter().GetBackCounterboreCutouts() );
                    addCutoutsFromContainer( currentAdapter().GetFrontCountersinkCutouts() );
                    addCutoutsFromContainer( currentAdapter().GetBackCountersinkCutouts() );

                    // Subtract backdrill holes (which are in layerHoleMap for F_Cu and B_Cu)
                    const MAP_CONTAINER_2D_BASE& layerHolesMap = currentAdapter().GetLayerHoleMap();

                    if( layerHolesMap.find( F_Cu ) != layerHolesMap.end() )
                    {
                        const BVH_CONTAINER_2D* holes2d = layerHolesMap.at( F_Cu );
                        CONST_LIST_OBJECT2D     intersecting;

                        holes2d->GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                        for( const OBJECT_2D* hole2d : intersecting )
                        {
                            if( object2d_A->Intersects( hole2d->GetBBox() ) )
                                object2d_B->push_back( hole2d );
                        }
                    }

                    if( layerHolesMap.find( B_Cu ) != layerHolesMap.end() )
                    {
                        const BVH_CONTAINER_2D* holes2d = layerHolesMap.at( B_Cu );
                        CONST_LIST_OBJECT2D     intersecting;

                        holes2d->GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                        for( const OBJECT_2D* hole2d : intersecting )
                        {
                            if( object2d_A->Intersects( hole2d->GetBBox() ) )
                                object2d_B->push_back( hole2d );
                        }
                    }

                    if( !m_antioutlineBoard2dObjects->GetList().empty() )
                    {
                        CONST_LIST_OBJECT2D intersecting;

                        m_antioutlineBoard2dObjects->GetIntersectingObjects( object2d_A->GetBBox(),
                                                                             intersecting );

                        for( const OBJECT_2D* obj : intersecting )
                            object2d_B->push_back( obj );
                    }

                    if( object2d_B->empty() )
                    {
                        delete object2d_B;
                        object2d_B = CSGITEM_EMPTY;
                    }

                    const float zTop = currentAdapter().GetLayerBottomZPos( F_Cu );
                    const float zBot = currentAdapter().GetLayerBottomZPos( B_Cu );

                    // Diagnostic: emit per-build body Z values + color so
                    // we can tell whether the substrate slab actually got
                    // built and where it sits in adapter-frame Z. In MBS
                    // mode each instance reports its own values; if any
                    // pair shows zTop ≈ zBot the body has degenerate Z
                    // extent (the symptom would be "no body visible").
                    wxLogMessage( wxT( "[RT-BODY] inst='%s' zTop=%.6f zBot=%.6f thk=%.6f "
                                       "color=(%.3f,%.3f,%.3f,%.3f) BiuTo3d=%.3e" ),
                                  currentAdapter().GetBoard()
                                          ? wxString::Format( wxT( "%p" ),
                                                               currentAdapter().GetBoard() )
                                          : wxString( wxT( "<none>" ) ),
                                  zTop, zBot, zTop - zBot,
                                  currentAdapter().m_BoardBodyColor.r,
                                  currentAdapter().m_BoardBodyColor.g,
                                  currentAdapter().m_BoardBodyColor.b,
                                  currentAdapter().m_BoardBodyColor.a,
                                  currentAdapter().BiuTo3dUnits() );

                    if( object2d_B == CSGITEM_EMPTY )
                    {
                        LAYER_ITEM* objPtr = new LAYER_ITEM( object2d_A, zTop, zBot );

                        objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                        objPtr->SetColor( ConvertSRGBToLinear( currentAdapter().m_BoardBodyColor ) );
                        currentContainer().Add( objPtr );
                    }
                    else
                    {

                        LAYER_ITEM_2D* itemCSG2d = new LAYER_ITEM_2D( object2d_A, object2d_B,
                                                                      CSGITEM_FULL,
                                                                      *currentAdapter().GetBoard() );

                        m_containerWithObjectsToDelete.Add( itemCSG2d );

                        LAYER_ITEM* objPtr = new LAYER_ITEM( itemCSG2d, zTop, zBot );

                        objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                        objPtr->SetColor( ConvertSRGBToLinear( currentAdapter().m_BoardBodyColor ) );
                        currentContainer().Add( objPtr );
                    }
                }

                // Add cylinders of the board body to container
                // Note: This is actually a workaround for the holes in the board.
                // The issue is because if a hole is in a border of a divided polygon ( ex
                // a polygon or dummy block) it will cut also the render of the hole.
                // So this will add a full hole.
                // In fact, that is not need if the hole have copper.
                if( !currentAdapter().GetTH_ODs().GetList().empty() )
                {
                    const LIST_OBJECT2D& holeList = currentAdapter().GetTH_ODs().GetList();

                    for( const OBJECT_2D* hole2d : holeList )
                    {
                        if( !m_antioutlineBoard2dObjects->GetList().empty() )
                        {
                            CONST_LIST_OBJECT2D intersecting;

                            m_antioutlineBoard2dObjects->GetIntersectingObjects( hole2d->GetBBox(),
                                                                                 intersecting );

                            // Do not add cylinder if it intersects the edge of the board
                            if( !intersecting.empty() )
                                continue;
                        }

                        switch( hole2d->GetObjectType() )
                        {
                        case OBJECT_2D_TYPE::FILLED_CIRCLE:
                        {
                            const float radius = hole2d->GetBBox().GetExtent().x * 0.5f * 0.999f;

                             CYLINDER* objPtr = new CYLINDER( hole2d->GetCentroid(),
                                    NextFloatDown( currentAdapter().GetLayerBottomZPos( F_Cu ) ),
                                    NextFloatUp( currentAdapter().GetLayerBottomZPos( B_Cu ) ),
                                    radius );

                            objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                            objPtr->SetColor(
                                    ConvertSRGBToLinear( currentAdapter().m_BoardBodyColor ) );

                            currentContainer().Add( objPtr );
                        }
                        break;

                        default:
                            break;
                        }
                    }
                }

                // Create plugs for backdrilled and post-machined areas
                backfillPostMachine();
            }
        }
    }

    if( aStatusReporter )
        aStatusReporter->Report( _( "Load Raytracing: layers" ) );

    // Add layers maps (except B_Mask and F_Mask)
    for( const std::pair<const PCB_LAYER_ID, BVH_CONTAINER_2D*>& entry :
         currentAdapter().GetLayerMap() )
    {
        const PCB_LAYER_ID      layer_id = entry.first;
        const BVH_CONTAINER_2D* container2d = entry.second;

        // Only process layers that exist
        if( !container2d )
            continue;

        if( aOnlyLoadCopperAndShapes && !IsCopperLayer( layer_id ) )
            continue;

        // Mask layers are not processed here because they are a special case
        if( layer_id == B_Mask || layer_id == F_Mask )
            continue;

        MATERIAL* materialLayer = &m_materials.m_SilkS;
        SFVEC3F   layerColor    = SFVEC3F( 0.0f, 0.0f, 0.0f );

        switch( layer_id )
        {
        case B_Adhes:
        case F_Adhes:
            break;

        case B_Paste:
        case F_Paste:
            materialLayer = &m_materials.m_Paste;
            layerColor = currentAdapter().m_SolderPasteColor;
            break;

        case B_SilkS:
            materialLayer = &m_materials.m_SilkS;
            layerColor = currentAdapter().m_SilkScreenColorBot;
            break;

        case F_SilkS:
            materialLayer = &m_materials.m_SilkS;
            layerColor = currentAdapter().m_SilkScreenColorTop;
            break;

        case Dwgs_User:
            layerColor = currentAdapter().m_UserDrawingsColor;
            break;

        case Cmts_User:
            layerColor = currentAdapter().m_UserCommentsColor;
            break;

        case Eco1_User:
            layerColor = currentAdapter().m_ECO1Color;
            break;

        case Eco2_User:
            layerColor = currentAdapter().m_ECO2Color;
            break;

        case B_CrtYd:
        case F_CrtYd:
            break;

        case B_Fab:
        case F_Fab:
            break;

        default:
        {
            int layer3D = MapPCBLayerTo3DLayer( layer_id );

            // Note: MUST do this in LAYER_3D space; User_1..User_45 are NOT contiguous
            if( layer3D >= LAYER_3D_USER_1 && layer3D <= LAYER_3D_USER_45 )
            {
                layerColor = currentAdapter().m_UserDefinedLayerColor[ layer3D - LAYER_3D_USER_1 ];
            }
            else if( currentAdapter().m_Cfg->m_Render.differentiate_plated_copper )
            {
                layerColor = SFVEC3F( 184.0f / 255.0f, 115.0f / 255.0f, 50.0f / 255.0f );
                materialLayer = &m_materials.m_NonPlatedCopper;
            }
            else
            {
                layerColor = currentAdapter().m_CopperColor;
                materialLayer = &m_materials.m_Copper;
            }

            break;
        }
        }

        createItemsFromContainer( container2d, layer_id, materialLayer, layerColor, 0.0f );
    } // for each layer on map

    // Create plated copper
    if( currentAdapter().m_Cfg->m_Render.differentiate_plated_copper )
    {
        createItemsFromContainer( currentAdapter().GetPlatedPadsFront(), F_Cu, &m_materials.m_Copper,
                                  currentAdapter().m_CopperColor,
                                  currentAdapter().GetFrontCopperThickness() * 0.1f );

        createItemsFromContainer( currentAdapter().GetPlatedPadsBack(), B_Cu, &m_materials.m_Copper,
                                  currentAdapter().m_CopperColor,
                                  -currentAdapter().GetBackCopperThickness() * 0.1f );
    }

    if( !aOnlyLoadCopperAndShapes )
    {
        // Add Mask layer
        // Solder mask layers are "negative" layers so the elements that we have in the container
        // should remove the board outline. We will check for all objects in the outline if it
        // intersects any object in the layer container and also any hole.
        if( ( layerFlags.test( LAYER_3D_SOLDERMASK_TOP )
                || layerFlags.test( LAYER_3D_SOLDERMASK_BOTTOM ) )
            && !m_outlineBoard2dObjects->GetList().empty() )
        {
            const MATERIAL* materialLayer = &m_materials.m_SolderMask;

            for( const std::pair<const PCB_LAYER_ID, BVH_CONTAINER_2D*>& entry :
                 currentAdapter().GetLayerMap() )
            {
                const PCB_LAYER_ID      layer_id = entry.first;
                const BVH_CONTAINER_2D* container2d = entry.second;

                // Only process layers that exist
                if( !container2d )
                    continue;

                // Only get the Solder mask layers (and only if the board has them)
                if( layer_id == F_Mask && !layerFlags.test( LAYER_3D_SOLDERMASK_TOP ) )
                    continue;

                if( layer_id == B_Mask && !layerFlags.test( LAYER_3D_SOLDERMASK_BOTTOM ) )
                    continue;

                // Only Mask layers are processed here because they are negative layers
                if( layer_id != F_Mask && layer_id != B_Mask )
                    continue;

                SFVEC3F layerColor;

                if( layer_id == B_Mask )
                    layerColor = currentAdapter().m_SolderMaskColorBot;
                else
                    layerColor = currentAdapter().m_SolderMaskColorTop;

                const float zLayerMin = currentAdapter().GetLayerBottomZPos( layer_id );
                const float zLayerMax = currentAdapter().GetLayerTopZPos( layer_id );

                // Get the outline board objects
                for( const OBJECT_2D* object2d_A : m_outlineBoard2dObjects->GetList() )
                {
                    std::vector<const OBJECT_2D*>* object2d_B = new std::vector<const OBJECT_2D*>();

                    // Check if there are any THT that intersects this outline object part
                    if( !currentAdapter().GetTH_ODs().GetList().empty() )
                    {
                        const BVH_CONTAINER_2D& throughHoles = currentAdapter().GetTH_ODs();
                        CONST_LIST_OBJECT2D     intersecting;

                        throughHoles.GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                        for( const OBJECT_2D* hole : intersecting )
                        {
                            if( object2d_A->Intersects( hole->GetBBox() ) )
                                object2d_B->push_back( hole );
                        }
                    }

                    // Check if there are any objects in the layer to subtract with the current
                    // object
                    if( !container2d->GetList().empty() )
                    {
                        CONST_LIST_OBJECT2D intersecting;

                        container2d->GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

                        for( const OBJECT_2D* obj : intersecting )
                            object2d_B->push_back( obj );
                    }

                    if( object2d_B->empty() )
                    {
                        delete object2d_B;
                        object2d_B = CSGITEM_EMPTY;
                    }

                    if( object2d_B == CSGITEM_EMPTY )
                    {
#if 0
                       createObject( currentContainer(), object2d_A, zLayerMin, zLayerMax,
                                     materialLayer, layerColor );
#else
                        LAYER_ITEM* objPtr = new LAYER_ITEM( object2d_A, zLayerMin, zLayerMax );

                        objPtr->SetMaterial( materialLayer );
                        objPtr->SetColor( ConvertSRGBToLinear( layerColor ) );

                        currentContainer().Add( objPtr );
#endif
                    }
                    else
                    {
                        LAYER_ITEM_2D* itemCSG2d = new LAYER_ITEM_2D( object2d_A, object2d_B,
                                                                      CSGITEM_FULL,
                                                                      object2d_A->GetBoardItem() );

                        m_containerWithObjectsToDelete.Add( itemCSG2d );

                        LAYER_ITEM* objPtr = new LAYER_ITEM( itemCSG2d, zLayerMin, zLayerMax );
                        objPtr->SetMaterial( materialLayer );
                        objPtr->SetColor( ConvertSRGBToLinear( layerColor ) );

                        currentContainer().Add( objPtr );
                    }
                }
            }
        }

        addPadsAndVias();
    }

#ifdef PRINT_STATISTICS_3D_VIEWER
    int64_t stats_endConvertTime = GetRunningMicroSecs();
    int64_t stats_startLoad3DmodelsTime = stats_endConvertTime;
#endif

    if( aStatusReporter )
        aStatusReporter->Report( _( "Loading 3D models..." ) );

    load3DModels( currentContainer(), aOnlyLoadCopperAndShapes );

#ifdef PRINT_STATISTICS_3D_VIEWER
    int64_t stats_endLoad3DmodelsTime = GetRunningMicroSecs();
#endif

    // Floor + lights are assembly-wide. In multi-instance build mode
    // ReloadMultiInstance owns both: it adds a single floor at the
    // assembly bbox bottom and lights positioned around the assembly
    // centroid AFTER all per-instance wrappers are in place. Skipping
    // here prevents the per-instance Reload from emitting one floor
    // per board (overlapping at different Z's, "interesting border"
    // artifact) and from clearing m_lights down to just the last
    // instance's set.
    if( !aOnlyLoadCopperAndShapes && !inInstanceBuild )
    {
        // Add floor
        if( currentAdapter().m_Cfg->m_Render.raytrace_backfloor )
        {
            BBOX_3D boardBBox = currentAdapter().GetBBox();

            if( boardBBox.IsInitialized() )
            {
                boardBBox.Scale( 3.0f );

                if( currentContainer().GetList().size() > 0 )
                {
                    BBOX_3D containerBBox = currentContainer().GetBBox();

                    containerBBox.Scale( 1.3f );

                    const SFVEC3F centerBBox = containerBBox.GetCenter();

                    // Floor triangles
                    const float minZ = glm::min( containerBBox.Min().z, boardBBox.Min().z );

                    const SFVEC3F v1 =
                            SFVEC3F( -RANGE_SCALE_3D * 4.0f, -RANGE_SCALE_3D * 4.0f, minZ )
                            + SFVEC3F( centerBBox.x, centerBBox.y, 0.0f );

                    const SFVEC3F v3 =
                            SFVEC3F( +RANGE_SCALE_3D * 4.0f, +RANGE_SCALE_3D * 4.0f, minZ )
                            + SFVEC3F( centerBBox.x, centerBBox.y, 0.0f );

                    const SFVEC3F v2 = SFVEC3F( v1.x, v3.y, v1.z );
                    const SFVEC3F v4 = SFVEC3F( v3.x, v1.y, v1.z );

                    SFVEC3F floorColor = ConvertSRGBToLinear( currentAdapter().m_BgColorTop );

                    TRIANGLE* newTriangle1 = new TRIANGLE( v1, v2, v3 );
                    TRIANGLE* newTriangle2 = new TRIANGLE( v3, v4, v1 );

                    currentContainer().Add( newTriangle1 );
                    currentContainer().Add( newTriangle2 );

                    newTriangle1->SetMaterial( &m_materials.m_Floor );
                    newTriangle2->SetMaterial( &m_materials.m_Floor );

                    newTriangle1->SetColor( floorColor );
                    newTriangle2->SetColor( floorColor );

                    // Ceiling triangles
                    const float maxZ = glm::max( containerBBox.Max().z, boardBBox.Max().z );

                    const SFVEC3F v5 = SFVEC3F( v1.x, v1.y, maxZ );
                    const SFVEC3F v6 = SFVEC3F( v2.x, v2.y, maxZ );
                    const SFVEC3F v7 = SFVEC3F( v3.x, v3.y, maxZ );
                    const SFVEC3F v8 = SFVEC3F( v4.x, v4.y, maxZ );

                    TRIANGLE* newTriangle3 = new TRIANGLE( v7, v6, v5 );
                    TRIANGLE* newTriangle4 = new TRIANGLE( v5, v8, v7 );

                    currentContainer().Add( newTriangle3 );
                    currentContainer().Add( newTriangle4 );

                    newTriangle3->SetMaterial( &m_materials.m_Floor );
                    newTriangle4->SetMaterial( &m_materials.m_Floor );

                    newTriangle3->SetColor( floorColor );
                    newTriangle4->SetColor( floorColor );
                }
            }
        }

        // Init initial lights
        for( LIGHT* light : m_lights )
            delete light;

        m_lights.clear();

        auto IsColorZero =
                []( const SFVEC3F& aSource )
                {
                    return ( ( aSource.r < ( 1.0f / 255.0f ) ) && ( aSource.g < ( 1.0f / 255.0f ) )
                           && ( aSource.b < ( 1.0f / 255.0f ) ) );
                };

        SFVEC3F cameraLightColor =
                currentAdapter().GetColor( currentAdapter().m_Cfg->m_Render.raytrace_lightColorCamera );
        SFVEC3F topLightColor =
                currentAdapter().GetColor( currentAdapter().m_Cfg->m_Render.raytrace_lightColorTop );
        SFVEC3F bottomLightColor =
                currentAdapter().GetColor( currentAdapter().m_Cfg->m_Render.raytrace_lightColorBottom );

        m_cameraLight = new DIRECTIONAL_LIGHT( SFVEC3F( 0.0f, 0.0f, 0.0f ), cameraLightColor );
        m_cameraLight->SetCastShadows( false );

        if( !IsColorZero( cameraLightColor ) )
            m_lights.push_back( m_cameraLight );

        const SFVEC3F& boardCenter = currentAdapter().GetBBox().GetCenter();

        if( !IsColorZero( topLightColor ) )
        {
            m_lights.push_back( new POINT_LIGHT( SFVEC3F( boardCenter.x, boardCenter.y,
                                                          +RANGE_SCALE_3D * 2.0f ),
                                                 topLightColor ) );
        }

        if( !IsColorZero( bottomLightColor ) )
        {
            m_lights.push_back( new POINT_LIGHT( SFVEC3F( boardCenter.x, boardCenter.y,
                                                          -RANGE_SCALE_3D * 2.0f ),
                                                 bottomLightColor ) );
        }

        for( size_t i = 0; i < currentAdapter().m_Cfg->m_Render.raytrace_lightColor.size(); ++i )
        {
            SFVEC3F lightColor =
                    currentAdapter().GetColor( currentAdapter().m_Cfg->m_Render.raytrace_lightColor[i] );

            if( !IsColorZero( lightColor ) )
            {
                const SFVEC2F sc = currentAdapter().GetSphericalCoord( i );

                m_lights.push_back( new DIRECTIONAL_LIGHT(
                        SphericalToCartesian( glm::pi<float>() * sc.x, glm::pi<float>() * sc.y ),
                        lightColor ) );
            }
        }
    }

    // Set min. and max. zoom range. This doesn't really fit here, but moving this outside of this
    // class would require reimplementing bounding box calculation (feel free to do this if you
    // have time and patience).
    //
    // Skip in multi-instance build mode — zoom limits are assembly-wide
    // and ReloadMultiInstance handles them once after wrapping all the
    // sub-board scenes.
    if( !inInstanceBuild && currentContainer().GetList().size() > 0 )
    {
        float ratio =
                std::max( 1.0f, currentContainer().GetBBox().GetMaxDimension() / RANGE_SCALE_3D );

        float max_zoom = CAMERA::DEFAULT_MAX_ZOOM * ratio;
        float min_zoom = static_cast<float>( MIN_DISTANCE_IU * currentAdapter().BiuTo3dUnits()
                                             / -m_camera.GetCameraInitPos().z );

        if( min_zoom > max_zoom )
            std::swap( min_zoom, max_zoom );

        float zoom_ratio = max_zoom / min_zoom;

        // Set the minimum number of zoom 'steps' between max and min.
        int steps = 3 * 3;
        steps -= static_cast<int>( ceil( log( zoom_ratio ) / log( 1.26f ) ) );
        steps = std::max( steps, 0 );

        // Resize max and min zoom to accomplish the number of steps.
        float increased_zoom = pow( 1.26f, steps / 2 );
        max_zoom *= increased_zoom;
        min_zoom /= increased_zoom;

        if( steps & 1 )
            min_zoom /= 1.26f;

        min_zoom = std::min( min_zoom, 1.0f );

        m_camera.SetMaxZoom( max_zoom );
        m_camera.SetMinZoom( min_zoom );
    }

    // Top-level accelerator — built only when finalizing the top-level
    // scene. ReloadMultiInstance builds per-instance accelerators inside
    // each INSTANCE_OBJECT_3D wrapper and a separate top-level BVH after
    // wrapping every instance.
    if( !inInstanceBuild )
    {
        delete m_accelerator;
        m_accelerator = new BVH_PBRT( m_objectContainer, 8, SPLITMETHOD::MIDDLE );
    }

    if( aStatusReporter )
    {
        // Calculation time in seconds
        double calculation_time = (double) ( GetRunningMicroSecs() - stats_startReloadTime ) / 1e6;

        aStatusReporter->Report( wxString::Format( _( "Reload time %.3f s" ), calculation_time ) );
    }
}


void RENDER_3D_RAYTRACE_BASE::ReloadMultiInstance(
        const std::vector<INSTANCE_DESC>& aInstances,
        REPORTER* aStatusReporter, REPORTER* aWarningReporter )
{
    int64_t stats_start = GetRunningMicroSecs();

    // Top-level state reset — same shape as Reload's setup, done ONCE
    // for the whole assembly.
    m_reloadRequested = false;
    m_modelMaterialMap.clear();
    OBJECT_2D_STATS::Instance().ResetStats();
    OBJECT_3D_STATS::Instance().ResetStats();

    // Order matters here: clear the 3D wrappers FIRST (deletes per-
    // instance LAYER_ITEMs that hold non-owning pointers into the 2D
    // outline containers), THEN drop the 2D containers themselves.
    // Reversing the order would dangle the LAYER_ITEMs' 2D refs while
    // they still try to participate in CONTAINER_3D::Clear().
    m_objectContainer.Clear();
    m_instanceWrappers.clear();
    m_perInstanceOutlines2d.clear();
    m_perInstanceAntiOutlines2d.clear();
    m_containerWithObjectsToDelete.Clear();

    // Run setupMaterials against the FIRST sub-board adapter, not the
    // top-level (MBS container) adapter. The container has no real
    // board loaded, so its m_SolderMaskColorTop, m_BoardBodyColor and
    // m_Cfg->m_Render values are defaults (alpha = 1.0 → fully opaque
    // mask). Without this override the F.Mask renders opaque red over
    // F.Cu, hiding traces from view despite the geometry being correct.
    //
    // Same adapter is also published as m_renderAdapter so the trace-
    // time path (shadeHit's refraction bias, shadow nudge, etc.) reads
    // a real GetNonCopperLayerThickness, not the container's 0.
    if( !aInstances.empty() && aInstances[0].adapter )
    {
        m_overrideAdapter = aInstances[0].adapter;
        m_renderAdapter   = aInstances[0].adapter;

        // InitSettings BEFORE setupMaterials so the first sub-board's
        // stackup colors are populated. setupMaterials reads
        // m_BoardBodyColor.a to compute EpoxyBoard's transparency
        // (1.0 - alpha); on a freshly-constructed adapter the
        // constructor default is alpha=0.9 → transparency=0.1, so the
        // body material renders translucent (you can see "into" the
        // board edge — the symptom in Image #19). After InitSettings
        // the adapter's m_BoardBodyColor reflects the real stackup
        // value (typically alpha=1.0 → fully opaque).
        aInstances[0].adapter->InitSettings( aStatusReporter, aWarningReporter );

        // Convert the first adapter's per-adapter thickness (in its
        // local 3D-units) into world 3D-units. The wrapper's pose is
        // uniform-scale × rotate × translate; extract the scale from
        // pose[0]'s length. Then world thickness = local thickness ×
        // scale. shadeHit's bias offsets (refraction startpoint,
        // shadow nudge) are applied to world-frame ray.at(t) calls,
        // so the bias must be in world units; using the per-adapter
        // value directly produced a wrong-sized bias for any board
        // whose biuTo3dUnits differs from the world's
        // m_sharedBiuTo3Dunits — which is every board in MBS mode.
        float poseScale = glm::length( glm::vec3( aInstances[0].pose[0] ) );

        if( poseScale <= 0.0f )
            poseScale = 1.0f;

        m_renderNonCopperLayerThickness3DU =
                aInstances[0].adapter->GetNonCopperLayerThickness() * poseScale;

        setupMaterials();
        m_overrideAdapter = nullptr;
    }
    else
    {
        m_renderAdapter                    = &m_boardAdapter;
        m_renderNonCopperLayerThickness3DU = m_boardAdapter.GetNonCopperLayerThickness();
        setupMaterials();
    }

    // Per-sub-board geometry build. For each instance: redirect
    // currentAdapter() / currentContainer() to a per-instance pair, run
    // Reload (which will detect override and skip top-level setup +
    // BVH-build), build the per-instance BVH, wrap it under the
    // instance's pose, and add the wrapper to the top-level container.
    //
    // The top-level container then "owns" the wrappers (CONTAINER_3D's
    // Clear() deletes its members, so the wrappers' lifetime tracks the
    // raytracer's). Each wrapper in turn owns its inner CONTAINER_3D
    // and ACCELERATOR_3D.
    for( size_t i = 0; i < aInstances.size(); i++ )
    {
        const INSTANCE_DESC& desc = aInstances[i];

        if( !desc.adapter )
            continue;

        std::unique_ptr<CONTAINER_3D> perInstContainer
                = std::make_unique<CONTAINER_3D>();

        m_overrideAdapter   = desc.adapter;
        m_overrideContainer = perInstContainer.get();

        // Null out the outline 2D containers so the inner Reload's
        // `delete m_outlineBoard2dObjects` is a no-op and we don't kill
        // the previous instance's 2D objects (which prior instances'
        // 3D LAYER_ITEMs still reference). The freshly-allocated
        // containers from this Reload will be transferred into
        // m_perInstanceOutlines2d below so they outlive THIS instance's
        // 3D LAYER_ITEMs too.
        m_outlineBoard2dObjects     = nullptr;
        m_antioutlineBoard2dObjects = nullptr;

        // Reload populates currentContainer() = perInstContainer with this
        // instance's full per-board geometry. BVH build is skipped because
        // override is active.
        Reload( aStatusReporter, aWarningReporter, /*onlyCopperAndShapes*/ false );

        m_overrideAdapter   = nullptr;
        m_overrideContainer = nullptr;

        // Transfer ownership of the just-built 2D outline containers
        // into the multi-instance keep-alive vectors, then null out
        // the members so the next iteration's Reload allocates fresh
        // ones. Without this, the 2D OBJECT_2Ds inside these
        // containers would be deleted at the top of the next inner
        // Reload, dangling the LAYER_ITEMs we just added to
        // perInstContainer.
        if( m_outlineBoard2dObjects )
        {
            m_perInstanceOutlines2d.emplace_back( m_outlineBoard2dObjects );
            m_outlineBoard2dObjects = nullptr;
        }

        if( m_antioutlineBoard2dObjects )
        {
            m_perInstanceAntiOutlines2d.emplace_back( m_antioutlineBoard2dObjects );
            m_antioutlineBoard2dObjects = nullptr;
        }

        // Per-instance BVH for the wrapper to dispatch into.
        std::unique_ptr<ACCELERATOR_3D> perInstAccel(
                new BVH_PBRT( *perInstContainer, 8, SPLITMETHOD::MIDDLE ) );

        std::unique_ptr<INSTANCE_OBJECT_3D> wrapper(
                new INSTANCE_OBJECT_3D( std::move( perInstContainer ),
                                         std::move( perInstAccel ),
                                         desc.pose ) );

        // Transfer ownership to top-level container (its Clear() deletes
        // members). Keep a non-owning copy in m_instanceWrappers for
        // diagnostic/debug introspection — currently unused but cheap to
        // maintain alongside.
        INSTANCE_OBJECT_3D* rawWrapper = wrapper.release();
        m_objectContainer.Add( rawWrapper );
    }

    // Assembly-wide finalize: lights + floor. Run AFTER all wrapper
    // bboxes are in m_objectContainer so we can size the floor and
    // anchor lights against the merged composite, not any one sub-
    // board.
    {
        for( LIGHT* light : m_lights )
            delete light;

        m_lights.clear();
        m_cameraLight = nullptr;

        // Use the first instance's adapter for color/light-config
        // lookups (raytrace_lightColorCamera etc. live in EDA_3D_VIEWER's
        // settings, which is shared across all sub-boards anyway).
        BOARD_ADAPTER* cfgAdapter = aInstances.empty() ? &m_boardAdapter
                                                       : aInstances[0].adapter;

        if( cfgAdapter && cfgAdapter->m_Cfg )
        {
            auto isColorZero = []( const SFVEC3F& aSource )
            {
                return ( aSource.r < 1.0f / 255.0f )
                       && ( aSource.g < 1.0f / 255.0f )
                       && ( aSource.b < 1.0f / 255.0f );
            };

            const SFVEC3F cameraLightColor =
                    cfgAdapter->GetColor( cfgAdapter->m_Cfg->m_Render.raytrace_lightColorCamera );
            const SFVEC3F topLightColor =
                    cfgAdapter->GetColor( cfgAdapter->m_Cfg->m_Render.raytrace_lightColorTop );
            const SFVEC3F bottomLightColor =
                    cfgAdapter->GetColor( cfgAdapter->m_Cfg->m_Render.raytrace_lightColorBottom );

            m_cameraLight = new DIRECTIONAL_LIGHT(
                    SFVEC3F( 0.0f, 0.0f, 0.0f ), cameraLightColor );
            m_cameraLight->SetCastShadows( false );

            if( !isColorZero( cameraLightColor ) )
                m_lights.push_back( m_cameraLight );

            const SFVEC3F assemblyCenter = m_objectContainer.GetBBox().IsInitialized()
                                            ? m_objectContainer.GetBBox().GetCenter()
                                            : SFVEC3F( 0.0f, 0.0f, 0.0f );

            if( !isColorZero( topLightColor ) )
            {
                m_lights.push_back( new POINT_LIGHT(
                        SFVEC3F( assemblyCenter.x, assemblyCenter.y,
                                  +RANGE_SCALE_3D * 2.0f ),
                        topLightColor ) );
            }

            if( !isColorZero( bottomLightColor ) )
            {
                m_lights.push_back( new POINT_LIGHT(
                        SFVEC3F( assemblyCenter.x, assemblyCenter.y,
                                  -RANGE_SCALE_3D * 2.0f ),
                        bottomLightColor ) );
            }

            for( size_t k = 0; k < cfgAdapter->m_Cfg->m_Render.raytrace_lightColor.size(); ++k )
            {
                SFVEC3F color = cfgAdapter->GetColor(
                        cfgAdapter->m_Cfg->m_Render.raytrace_lightColor[k] );

                if( !isColorZero( color ) )
                {
                    const SFVEC2F sc = cfgAdapter->GetSphericalCoord( k );
                    m_lights.push_back( new DIRECTIONAL_LIGHT(
                            SphericalToCartesian( glm::pi<float>() * sc.x,
                                                   glm::pi<float>() * sc.y ),
                            color ) );
                }
            }

            // Single floor + ceiling at the assembly bbox extents.
            // Single-board uses Scale(3.0f) on its boardBBox before
            // taking minZ/maxZ — that pushes the ceiling well above
            // any reflection ray cast from the board surface, so
            // F.Mask reflections off flat-on view angles bounce up
            // past the ceiling and into the background gradient
            // (and traces show through). With Scale(1.3f) and a
            // multi-board container, the ceiling lands right above
            // the boards; flat-on reflection rays hit the red
            // ceiling and return its color, occluding the F.Cu
            // traces — that's the curved "no-trace" boundary
            // visible in the user's Image #13. Match the single-
            // board math by scaling 3x on Z (X/Y already use
            // ±RANGE_SCALE_3D * 4 for the floor extents).
            if( cfgAdapter->m_Cfg->m_Render.raytrace_backfloor
                && m_objectContainer.GetBBox().IsInitialized() )
            {
                BBOX_3D containerBBox = m_objectContainer.GetBBox();
                containerBBox.Scale( 3.0f );

                const SFVEC3F centerBBox = containerBBox.GetCenter();
                const float   minZ       = containerBBox.Min().z;
                const float   maxZ       = containerBBox.Max().z;

                const SFVEC3F v1(
                        -RANGE_SCALE_3D * 4.0f + centerBBox.x,
                        -RANGE_SCALE_3D * 4.0f + centerBBox.y, minZ );
                const SFVEC3F v3(
                        +RANGE_SCALE_3D * 4.0f + centerBBox.x,
                        +RANGE_SCALE_3D * 4.0f + centerBBox.y, minZ );
                const SFVEC3F v2( v1.x, v3.y, v1.z );
                const SFVEC3F v4( v3.x, v1.y, v1.z );

                SFVEC3F floorColor = ConvertSRGBToLinear( cfgAdapter->m_BgColorTop );

                TRIANGLE* t1 = new TRIANGLE( v1, v2, v3 );
                TRIANGLE* t2 = new TRIANGLE( v3, v4, v1 );
                t1->SetMaterial( &m_materials.m_Floor );
                t2->SetMaterial( &m_materials.m_Floor );
                t1->SetColor( floorColor );
                t2->SetColor( floorColor );
                m_objectContainer.Add( t1 );
                m_objectContainer.Add( t2 );

                const SFVEC3F v5( v1.x, v1.y, maxZ );
                const SFVEC3F v6( v2.x, v2.y, maxZ );
                const SFVEC3F v7( v3.x, v3.y, maxZ );
                const SFVEC3F v8( v4.x, v4.y, maxZ );

                TRIANGLE* t3 = new TRIANGLE( v7, v6, v5 );
                TRIANGLE* t4 = new TRIANGLE( v5, v8, v7 );
                t3->SetMaterial( &m_materials.m_Floor );
                t4->SetMaterial( &m_materials.m_Floor );
                t3->SetColor( floorColor );
                t4->SetColor( floorColor );
                m_objectContainer.Add( t3 );
                m_objectContainer.Add( t4 );
            }
        }
    }

    // Top-level BVH dispatches to the per-instance wrappers. The wrappers'
    // Intersect transforms the ray into each instance's local frame and
    // delegates to the inner per-instance BVH. Floor tris added above
    // are NOT wrapped — they participate directly in the top BVH.
    delete m_accelerator;
    m_accelerator = new BVH_PBRT( m_objectContainer, 8, SPLITMETHOD::MIDDLE );

    if( aStatusReporter )
    {
        double calc_time = (double) ( GetRunningMicroSecs() - stats_start ) / 1e6;
        aStatusReporter->Report(
                wxString::Format( _( "Reload time %.3f s (%zu instances)" ),
                                  calc_time, aInstances.size() ) );
    }
}


void RENDER_3D_RAYTRACE_BASE::addCounterborePlating( const BOARD_ITEM& aSource,
                                                     const SFVEC2F& aCenter,
                                                     float aInnerRadius, float aDepth,
                                                     float aSurfaceZ, bool aIsFront )
{
    const float platingThickness = currentAdapter().GetHolePlatingThickness()
                                   * currentAdapter().BiuTo3dUnits();

    if( platingThickness <= 0.0f || aInnerRadius <= 0.0f || aDepth <= 0.0f )
        return;

    const float outerRadius = aInnerRadius + platingThickness;
    const float zOther = aIsFront ? ( aSurfaceZ - aDepth ) : ( aSurfaceZ + aDepth );
    const float zMin = std::min( aSurfaceZ, zOther );
    const float zMax = std::max( aSurfaceZ, zOther );

    RING_2D* ring = new RING_2D( aCenter, aInnerRadius, outerRadius, aSource );
    m_containerWithObjectsToDelete.Add( ring );

    LAYER_ITEM* objPtr = new LAYER_ITEM( ring, zMin, zMax );
    objPtr->SetMaterial( &m_materials.m_Copper );
    objPtr->SetColor( ConvertSRGBToLinear( currentAdapter().m_CopperColor ) );

    currentContainer().Add( objPtr );
}


void RENDER_3D_RAYTRACE_BASE::addCountersinkPlating( const SFVEC2F& aCenter,
                                                     float aTopInnerRadius,
                                                     float aBottomInnerRadius,
                                                     float aSurfaceZ, float aDepth,
                                                     bool aIsFront )
{
    const float platingThickness = currentAdapter().GetHolePlatingThickness()
                                   * currentAdapter().BiuTo3dUnits();

    if( platingThickness <= 0.0f || aTopInnerRadius <= 0.0f || aBottomInnerRadius <= 0.0f
            || aDepth <= 0.0f )
    {
        return;
    }

    const float topOuterRadius = aTopInnerRadius + platingThickness;
    const float bottomOuterRadius = aBottomInnerRadius + platingThickness;

    const float zOther = aIsFront ? ( aSurfaceZ - aDepth ) : ( aSurfaceZ + aDepth );
    const float zTop = std::max( aSurfaceZ, zOther );
    const float zBot = std::min( aSurfaceZ, zOther );

    if( topOuterRadius <= 0.0f || bottomOuterRadius <= 0.0f )
        return;

    const float largestDiameter = 2.0f * std::max( aTopInnerRadius, aBottomInnerRadius );
    unsigned int segments = std::max( 12u, currentAdapter().GetCircleSegmentCount( largestDiameter ) );

    const SFVEC3F copperColor = ConvertSRGBToLinear( currentAdapter().m_CopperColor );

    auto addQuad = [&]( const SFVEC3F& p0, const SFVEC3F& p1,
                        const SFVEC3F& p2, const SFVEC3F& p3 )
    {
        TRIANGLE* tri1 = new TRIANGLE( p0, p1, p2 );
        TRIANGLE* tri2 = new TRIANGLE( p0, p2, p3 );

        tri1->SetMaterial( &m_materials.m_Copper );
        tri2->SetMaterial( &m_materials.m_Copper );
        tri1->SetColor( copperColor );
        tri2->SetColor( copperColor );

        currentContainer().Add( tri1 );
        currentContainer().Add( tri2 );
    };

    auto makePoint = [&]( float radius, float angle, float z )
    {
        return SFVEC3F( aCenter.x + cosf( angle ) * radius,
                        aCenter.y + sinf( angle ) * radius,
                        z );
    };

    const float step = 2.0f * glm::pi<float>() / (float) segments;

    SFVEC3F innerTopPrev = makePoint( aTopInnerRadius, 0.0f, zTop );
    SFVEC3F innerBotPrev = makePoint( aBottomInnerRadius, 0.0f, zBot );
    SFVEC3F outerTopPrev = makePoint( topOuterRadius, 0.0f, zTop );
    SFVEC3F outerBotPrev = makePoint( bottomOuterRadius, 0.0f, zBot );

    const SFVEC3F innerTopFirst = innerTopPrev;
    const SFVEC3F innerBotFirst = innerBotPrev;
    const SFVEC3F outerTopFirst = outerTopPrev;
    const SFVEC3F outerBotFirst = outerBotPrev;

    for( unsigned int i = 1; i <= segments; ++i )
    {
        const float angle = ( i == segments ) ? 0.0f : step * i;

        const SFVEC3F innerTopCurr = ( i == segments ) ? innerTopFirst
                                                       : makePoint( aTopInnerRadius, angle, zTop );
        const SFVEC3F innerBotCurr = ( i == segments ) ? innerBotFirst
                                                       : makePoint( aBottomInnerRadius, angle, zBot );
        const SFVEC3F outerTopCurr = ( i == segments ) ? outerTopFirst
                                                       : makePoint( topOuterRadius, angle, zTop );
        const SFVEC3F outerBotCurr = ( i == segments ) ? outerBotFirst
                                                       : makePoint( bottomOuterRadius, angle, zBot );

        // Inner wall
        addQuad( innerTopPrev, innerTopCurr, innerBotCurr, innerBotPrev );

        // Outer wall
        addQuad( outerTopPrev, outerBotPrev, outerBotCurr, outerTopCurr );

        // Top rim
        addQuad( outerTopPrev, outerTopCurr, innerTopCurr, innerTopPrev );

        // Bottom rim
        addQuad( outerBotPrev, innerBotPrev, innerBotCurr, outerBotCurr );

        innerTopPrev = innerTopCurr;
        innerBotPrev = innerBotCurr;
        outerTopPrev = outerTopCurr;
        outerBotPrev = outerBotCurr;
    }
}


void RENDER_3D_RAYTRACE_BASE::backfillPostMachine()
{
    if( !currentAdapter().GetBoard() )
        return;

    const float unitScale = currentAdapter().BiuTo3dUnits();
    const int platingThickness = currentAdapter().GetHolePlatingThickness();
    const float platingThickness3d = platingThickness * unitScale;
    const SFVEC3F boardColor = ConvertSRGBToLinear( currentAdapter().m_BoardBodyColor );

    const float boardZTop = currentAdapter().GetLayerBottomZPos( F_Cu );
    const float boardZBot = currentAdapter().GetLayerBottomZPos( B_Cu );

    // Process vias for backdrill and post-machining plugs
    for( const PCB_TRACK* track : currentAdapter().GetBoard()->Tracks() )
    {
        if( track->Type() != PCB_VIA_T )
            continue;

        const PCB_VIA* via = static_cast<const PCB_VIA*>( track );

        const float holeDiameter = via->GetDrillValue() * unitScale;
        const float holeInnerRadius = holeDiameter / 2.0f;
        const float holeOuterRadius = holeInnerRadius + platingThickness3d;
        const SFVEC2F center( via->GetStart().x * unitScale, -via->GetStart().y * unitScale );

        PCB_LAYER_ID topLayer, bottomLayer;
        via->LayerPair( &topLayer, &bottomLayer );

        const float viaZTop = currentAdapter().GetLayerBottomZPos( topLayer );
        const float viaZBot = currentAdapter().GetLayerBottomZPos( bottomLayer );

        // Handle backdrill plugs
        const auto secondaryDrillSize = via->GetSecondaryDrillSize();

        if( secondaryDrillSize.has_value() && secondaryDrillSize.value() > 0 )
        {
            const float backdrillRadius = secondaryDrillSize.value() * 0.5f * unitScale;

            if( backdrillRadius > holeOuterRadius )
            {
                PCB_LAYER_ID secStart = via->GetSecondaryDrillStartLayer();
                PCB_LAYER_ID secEnd = via->GetSecondaryDrillEndLayer();

                // Calculate where the backdrill ends and plug should start
                const float secEndZ = currentAdapter().GetLayerBottomZPos( secEnd );

                float plugZTop, plugZBot;

                if( secStart == F_Cu )
                {
                    // Backdrill from top: plug goes from below backdrill end to via bottom
                    plugZTop = secEndZ;
                    plugZBot = viaZBot;
                }
                else
                {
                    // Backdrill from bottom: plug goes from via top to above backdrill end
                    plugZTop = viaZTop;
                    plugZBot = secEndZ;
                }

                if( plugZTop > plugZBot )
                {
                    // Create a ring from holeOuterRadius to backdrillRadius
                    RING_2D* ring = new RING_2D( center, holeOuterRadius, backdrillRadius, *via );
                    m_containerWithObjectsToDelete.Add( ring );

                    LAYER_ITEM* objPtr = new LAYER_ITEM( ring, plugZBot, plugZTop );
                    objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                    objPtr->SetColor( boardColor );
                    currentContainer().Add( objPtr );
                }
            }
        }

        // Handle front post-machining plugs
        const auto frontMode = via->GetFrontPostMachining();

        if( frontMode.has_value()
            && frontMode.value() != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED
            && frontMode.value() != PAD_DRILL_POST_MACHINING_MODE::UNKNOWN )
        {
            const float frontRadius = via->GetFrontPostMachiningSize() * 0.5f * unitScale;
            const float frontDepth = via->GetFrontPostMachiningDepth() * unitScale;

            if( frontRadius > holeOuterRadius && frontDepth > 0 )
            {
                // Plug goes from bottom of post-machining to bottom of via
                const float pmBottomZ = viaZTop - frontDepth;
                const float plugZBot = viaZBot;

                if( pmBottomZ > plugZBot )
                {
                    if( frontMode.value() == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
                    {
                        // For countersink, use a frustum (truncated cone)
                        EDA_ANGLE angle( via->GetFrontPostMachiningAngle(), TENTHS_OF_A_DEGREE_T );
                        float angleRad = angle.AsRadians();
                        if( angleRad < 0.01f )
                            angleRad = 0.01f;

                        float radialDiff = frontRadius - holeOuterRadius;
                        float innerHeight = radialDiff / tanf( angleRad );
                        float totalHeight = pmBottomZ - plugZBot;

                        if( innerHeight > totalHeight )
                            innerHeight = totalHeight;

                        float zInnerTop = plugZBot + innerHeight;

                        // Create frustum from holeOuterRadius at zInnerTop to frontRadius at pmBottomZ
                        TRUNCATED_CONE* frustum = new TRUNCATED_CONE( center, zInnerTop, pmBottomZ,
                                                        holeOuterRadius, frontRadius );
                        frustum->SetMaterial( &m_materials.m_EpoxyBoard );
                        frustum->SetColor( boardColor );
                        currentContainer().Add( frustum );

                        // If there's a cylindrical portion below the cone
                        if( zInnerTop > plugZBot )
                        {
                            RING_2D* ring = new RING_2D( center, holeOuterRadius, frontRadius, *via );
                            m_containerWithObjectsToDelete.Add( ring );

                            LAYER_ITEM* objPtr = new LAYER_ITEM( ring, plugZBot, zInnerTop );
                            objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                            objPtr->SetColor( boardColor );
                            currentContainer().Add( objPtr );
                        }
                    }
                    else
                    {
                        RING_2D* ring = new RING_2D( center, holeOuterRadius, frontRadius, *via );
                        m_containerWithObjectsToDelete.Add( ring );

                        LAYER_ITEM* objPtr = new LAYER_ITEM( ring, plugZBot, pmBottomZ );
                        objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                        objPtr->SetColor( boardColor );
                        currentContainer().Add( objPtr );
                    }
                }
            }
        }

        // Handle back post-machining plugs
        const auto backMode = via->GetBackPostMachining();

        if( backMode.has_value()
            && backMode.value() != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED
            && backMode.value() != PAD_DRILL_POST_MACHINING_MODE::UNKNOWN )
        {
            const float backRadius = via->GetBackPostMachiningSize() * 0.5f * unitScale;
            const float backDepth = via->GetBackPostMachiningDepth() * unitScale;

            if( backRadius > holeOuterRadius && backDepth > 0 )
            {
                // Plug goes from top of via to top of post-machining
                const float plugZTop = viaZTop;
                const float pmTopZ = viaZBot + backDepth;

                if( plugZTop > pmTopZ )
                {
                    if( backMode.value() == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
                    {
                        // For countersink, use a frustum (truncated cone)
                        EDA_ANGLE angle( via->GetBackPostMachiningAngle(), TENTHS_OF_A_DEGREE_T );
                        float angleRad = angle.AsRadians();
                        if( angleRad < 0.01f )
                            angleRad = 0.01f;

                        float radialDiff = backRadius - holeOuterRadius;
                        float innerHeight = radialDiff / tanf( angleRad );
                        float totalHeight = plugZTop - pmTopZ;

                        if( innerHeight > totalHeight )
                            innerHeight = totalHeight;

                        float zInnerBot = plugZTop - innerHeight;

                        // Create frustum from holeOuterRadius at zInnerBot to backRadius at pmTopZ
                        TRUNCATED_CONE* frustum = new TRUNCATED_CONE( center, pmTopZ, zInnerBot,
                                                        backRadius, holeOuterRadius );
                        frustum->SetMaterial( &m_materials.m_EpoxyBoard );
                        frustum->SetColor( boardColor );
                        currentContainer().Add( frustum );

                        // If there's a cylindrical portion above the cone
                        if( zInnerBot < plugZTop )
                        {
                            RING_2D* ring = new RING_2D( center, holeOuterRadius, backRadius, *via );
                            m_containerWithObjectsToDelete.Add( ring );

                            LAYER_ITEM* objPtr = new LAYER_ITEM( ring, zInnerBot, plugZTop );
                            objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                            objPtr->SetColor( boardColor );
                            currentContainer().Add( objPtr );
                        }
                    }
                    else
                    {
                        RING_2D* ring = new RING_2D( center, holeOuterRadius, backRadius, *via );
                        m_containerWithObjectsToDelete.Add( ring );

                        LAYER_ITEM* objPtr = new LAYER_ITEM( ring, pmTopZ, plugZTop );
                        objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                        objPtr->SetColor( boardColor );
                        currentContainer().Add( objPtr );
                    }
                }
            }
        }
    }

    // Process pads for post-machining plugs
    for( const FOOTPRINT* footprint : currentAdapter().GetBoard()->Footprints() )
    {
        for( const PAD* pad : footprint->Pads() )
        {
            if( pad->GetAttribute() == PAD_ATTRIB::NPTH )
                continue;

            if( !pad->HasHole() )
                continue;

            if( pad->GetDrillShape() != PAD_DRILL_SHAPE::CIRCLE )
                continue;

            const SFVEC2F padCenter( pad->GetPosition().x * unitScale,
                                     -pad->GetPosition().y * unitScale );
            const float holeInnerRadius = pad->GetDrillSize().x * 0.5f * unitScale;
            const float holeOuterRadius = holeInnerRadius + platingThickness3d;

            const float padZTop = boardZTop;
            const float padZBot = boardZBot;

            // Handle front post-machining plugs for pads
            const auto frontMode = pad->GetFrontPostMachining();

            if( frontMode.has_value()
                && frontMode.value() != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED
                && frontMode.value() != PAD_DRILL_POST_MACHINING_MODE::UNKNOWN )
            {
                const float frontRadius = pad->GetFrontPostMachiningSize() * 0.5f * unitScale;
                const float frontDepth = pad->GetFrontPostMachiningDepth() * unitScale;

                if( frontRadius > holeOuterRadius && frontDepth > 0 )
                {
                    const float pmBottomZ = padZTop - frontDepth;
                    const float plugZBot = padZBot;

                    if( pmBottomZ > plugZBot )
                    {
                        if( frontMode.value() == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
                        {
                            // For countersink, use a frustum (truncated cone)
                            EDA_ANGLE angle( pad->GetFrontPostMachiningAngle(), TENTHS_OF_A_DEGREE_T );
                            float angleRad = angle.AsRadians();
                            if( angleRad < 0.01f )
                                angleRad = 0.01f;

                            float radialDiff = frontRadius - holeOuterRadius;
                            float innerHeight = radialDiff / tanf( angleRad );
                            float totalHeight = pmBottomZ - plugZBot;

                            if( innerHeight > totalHeight )
                                innerHeight = totalHeight;

                            float zInnerTop = plugZBot + innerHeight;

                            // Create frustum from holeOuterRadius at zInnerTop to frontRadius at pmBottomZ
                            TRUNCATED_CONE* frustum = new TRUNCATED_CONE( padCenter, zInnerTop, pmBottomZ,
                                                            holeOuterRadius, frontRadius );
                            frustum->SetMaterial( &m_materials.m_EpoxyBoard );
                            frustum->SetColor( boardColor );
                            currentContainer().Add( frustum );

                            // If there's a cylindrical portion below the cone
                            if( zInnerTop > plugZBot )
                            {
                                RING_2D* ring = new RING_2D( padCenter, holeOuterRadius, frontRadius, *pad );
                                m_containerWithObjectsToDelete.Add( ring );

                                LAYER_ITEM* objPtr = new LAYER_ITEM( ring, plugZBot, zInnerTop );
                                objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                                objPtr->SetColor( boardColor );
                                currentContainer().Add( objPtr );
                            }
                        }
                        else
                        {
                            RING_2D* ring = new RING_2D( padCenter, holeOuterRadius, frontRadius, *pad );
                            m_containerWithObjectsToDelete.Add( ring );

                            LAYER_ITEM* objPtr = new LAYER_ITEM( ring, plugZBot, pmBottomZ );
                            objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                            objPtr->SetColor( boardColor );
                            currentContainer().Add( objPtr );
                        }
                    }
                }
            }

            // Handle back post-machining plugs for pads
            const auto backMode = pad->GetBackPostMachining();

            if( backMode.has_value()
                && backMode.value() != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED
                && backMode.value() != PAD_DRILL_POST_MACHINING_MODE::UNKNOWN )
            {
                const float backRadius = pad->GetBackPostMachiningSize() * 0.5f * unitScale;
                const float backDepth = pad->GetBackPostMachiningDepth() * unitScale;

                if( backRadius > holeOuterRadius && backDepth > 0 )
                {
                    const float plugZTop = padZTop;
                    const float pmTopZ = padZBot + backDepth;

                    if( plugZTop > pmTopZ )
                    {
                        if( backMode.value() == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
                        {
                            // For countersink, use a frustum (truncated cone)
                            EDA_ANGLE angle( pad->GetBackPostMachiningAngle(), TENTHS_OF_A_DEGREE_T );
                            float angleRad = angle.AsRadians();
                            if( angleRad < 0.01f )
                                angleRad = 0.01f;

                            float radialDiff = backRadius - holeOuterRadius;
                            float innerHeight = radialDiff / tanf( angleRad );
                            float totalHeight = plugZTop - pmTopZ;

                            if( innerHeight > totalHeight )
                                innerHeight = totalHeight;

                            float zInnerBot = plugZTop - innerHeight;

                            // Create frustum from holeOuterRadius at zInnerBot to backRadius at pmTopZ
                            TRUNCATED_CONE* frustum = new TRUNCATED_CONE( padCenter, pmTopZ, zInnerBot,
                                                            backRadius, holeOuterRadius );
                            frustum->SetMaterial( &m_materials.m_EpoxyBoard );
                            frustum->SetColor( boardColor );
                            currentContainer().Add( frustum );

                            // If there's a cylindrical portion above the cone
                            if( zInnerBot < plugZTop )
                            {
                                RING_2D* ring = new RING_2D( padCenter, holeOuterRadius, backRadius, *pad );
                                m_containerWithObjectsToDelete.Add( ring );

                                LAYER_ITEM* objPtr = new LAYER_ITEM( ring, zInnerBot, plugZTop );
                                objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                                objPtr->SetColor( boardColor );
                                currentContainer().Add( objPtr );
                            }
                        }
                        else
                        {
                            RING_2D* ring = new RING_2D( padCenter, holeOuterRadius, backRadius, *pad );
                            m_containerWithObjectsToDelete.Add( ring );

                            LAYER_ITEM* objPtr = new LAYER_ITEM( ring, pmTopZ, plugZTop );
                            objPtr->SetMaterial( &m_materials.m_EpoxyBoard );
                            objPtr->SetColor( boardColor );
                            currentContainer().Add( objPtr );
                        }
                    }
                }
            }
        }
    }
}


void RENDER_3D_RAYTRACE_BASE::insertHole( const PCB_VIA* aVia )
{
    if( !currentAdapter().m_Cfg->m_Render.show_plated_barrels )
        return;

    PCB_LAYER_ID top_layer, bottom_layer;
    int          radiusBUI = ( aVia->GetDrillValue() / 2 );

    aVia->LayerPair( &top_layer, &bottom_layer );

    float frontDepth = 0.0f;
    float backDepth = 0.0f;

    if( aVia->Padstack().FrontPostMachining().mode.value_or( PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED ) != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED )
        frontDepth = aVia->Padstack().FrontPostMachining().depth * currentAdapter().BiuTo3dUnits();

    if( aVia->Padstack().BackPostMachining().mode.value_or( PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED ) != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED )
        backDepth = aVia->Padstack().BackPostMachining().depth * currentAdapter().BiuTo3dUnits();

    float topZ = currentAdapter().GetLayerBottomZPos( top_layer )
                 + currentAdapter().GetFrontCopperThickness() - frontDepth;

    float botZ = currentAdapter().GetLayerBottomZPos( bottom_layer )
                 - currentAdapter().GetBackCopperThickness() + backDepth;

    const float unitScale = currentAdapter().BiuTo3dUnits();
    const SFVEC2F center = SFVEC2F( aVia->GetStart().x * unitScale, -aVia->GetStart().y * unitScale );

    RING_2D* ring = new RING_2D( center, radiusBUI * unitScale,
                                 ( radiusBUI + currentAdapter().GetHolePlatingThickness() ) * unitScale, *aVia );

    m_containerWithObjectsToDelete.Add( ring );

    LAYER_ITEM* objPtr = new LAYER_ITEM( ring, topZ, botZ );

    objPtr->SetMaterial( &m_materials.m_Copper );
    objPtr->SetColor( ConvertSRGBToLinear( currentAdapter().m_CopperColor ) );

    currentContainer().Add( objPtr );

    const float holeInnerRadius = radiusBUI * unitScale;
    const float frontSurface = topZ + frontDepth;
    const float backSurface = botZ - backDepth;

    const PAD_DRILL_POST_MACHINING_MODE frontMode =
            aVia->GetFrontPostMachining().value_or( PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED );
    const float frontRadius = 0.5f * aVia->GetFrontPostMachiningSize() * unitScale;

    if( frontDepth > 0.0f && frontRadius > holeInnerRadius )
    {
        if( frontMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERBORE )
        {
            addCounterborePlating( *aVia, center, frontRadius, frontDepth, frontSurface, true );
        }
        else if( frontMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
        {
            addCountersinkPlating( center, frontRadius, holeInnerRadius, frontSurface, frontDepth,
                                   true );
        }
    }

    const PAD_DRILL_POST_MACHINING_MODE backMode =
            aVia->GetBackPostMachining().value_or( PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED );
    const float backRadius = 0.5f * aVia->GetBackPostMachiningSize() * unitScale;

    if( backDepth > 0.0f && backRadius > holeInnerRadius )
    {
        if( backMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERBORE )
        {
            addCounterborePlating( *aVia, center, backRadius, backDepth, backSurface, false );
        }
        else if( backMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
        {
            addCountersinkPlating( center, backRadius, holeInnerRadius, backSurface, backDepth,
                                   false );
        }
    }
}


void RENDER_3D_RAYTRACE_BASE::insertHole( const PAD* aPad )
{
    if( !currentAdapter().m_Cfg->m_Render.show_plated_barrels )
        return;

    const OBJECT_2D* object2d_A = nullptr;

    SFVEC3F        objColor = currentAdapter().m_CopperColor;
    const VECTOR2I drillsize = aPad->GetDrillSize();
    const bool     hasHole = drillsize.x && drillsize.y;
    const float    unitScale = currentAdapter().BiuTo3dUnits();
    const bool     isRoundHole = drillsize.x == drillsize.y;
    SFVEC2F        holeCenter = SFVEC2F( 0.0f, 0.0f );
    float          holeInnerRadius = 0.0f;

    if( !hasHole )
        return;

    CONST_LIST_OBJECT2D antiOutlineIntersectionList;

    float frontDepth = 0.0f;
    float backDepth = 0.0f;

    if( aPad->GetFrontPostMachining() != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED )
        frontDepth = aPad->GetFrontPostMachiningDepth() * currentAdapter().BiuTo3dUnits();

    if( aPad->GetBackPostMachining() != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED )
        backDepth = aPad->GetBackPostMachiningDepth() * currentAdapter().BiuTo3dUnits();

    const float topZ = currentAdapter().GetLayerBottomZPos( F_Cu )
                       + currentAdapter().GetFrontCopperThickness() * 0.99f - frontDepth;

    const float botZ = currentAdapter().GetLayerBottomZPos( B_Cu )
                       - currentAdapter().GetBackCopperThickness() * 0.99f + backDepth;

    if( isRoundHole ) // usual round hole
    {
        holeCenter = SFVEC2F( aPad->GetPosition().x * unitScale,
                              -aPad->GetPosition().y * unitScale );

        int innerRadius = drillsize.x / 2;
        int outerRadius = innerRadius + currentAdapter().GetHolePlatingThickness();
        holeInnerRadius = innerRadius * unitScale;

        RING_2D* ring = new RING_2D( holeCenter, innerRadius * unitScale,
                                     outerRadius * unitScale, *aPad );

        m_containerWithObjectsToDelete.Add( ring );

        object2d_A = ring;

        // If the object (ring) is intersected by an antioutline board,
        // it will use instead a CSG of two circles.
        if( object2d_A && !m_antioutlineBoard2dObjects->GetList().empty() )
        {
            m_antioutlineBoard2dObjects->GetIntersectingObjects( object2d_A->GetBBox(),
                                                                 antiOutlineIntersectionList );
        }

        if( !antiOutlineIntersectionList.empty() )
        {
                FILLED_CIRCLE_2D* innerCircle = new FILLED_CIRCLE_2D(
                    holeCenter, innerRadius * unitScale, *aPad );

                FILLED_CIRCLE_2D* outterCircle = new FILLED_CIRCLE_2D(
                    holeCenter, outerRadius * unitScale, *aPad );
            std::vector<const OBJECT_2D*>* object2d_B = new std::vector<const OBJECT_2D*>();
            object2d_B->push_back( innerCircle );

            LAYER_ITEM_2D* itemCSG2d = new LAYER_ITEM_2D( outterCircle, object2d_B, CSGITEM_FULL,
                                                          *aPad );

            m_containerWithObjectsToDelete.Add( itemCSG2d );
            m_containerWithObjectsToDelete.Add( innerCircle );
            m_containerWithObjectsToDelete.Add( outterCircle );

            object2d_A = itemCSG2d;
        }
    }
    else // Oblong hole
    {
        VECTOR2I ends_offset;
        int     width;

        if( drillsize.x > drillsize.y ) // Horizontal oval
        {
            ends_offset.x = ( drillsize.x - drillsize.y ) / 2;
            width         = drillsize.y;
        }
        else // Vertical oval
        {
            ends_offset.y = ( drillsize.y - drillsize.x ) / 2;
            width         = drillsize.x;
        }

        RotatePoint( ends_offset, aPad->GetOrientation() );

        VECTOR2I start = VECTOR2I( aPad->GetPosition() ) + ends_offset;
        VECTOR2I end = VECTOR2I( aPad->GetPosition() ) - ends_offset;

        ROUND_SEGMENT_2D* innerSeg =
            new ROUND_SEGMENT_2D( SFVEC2F( start.x * unitScale,
                               -start.y * unitScale ),
                          SFVEC2F( end.x * unitScale,
                               -end.y * unitScale ),
                          width * unitScale, *aPad );

        ROUND_SEGMENT_2D* outerSeg =
            new ROUND_SEGMENT_2D( SFVEC2F( start.x * unitScale,
                               -start.y * unitScale ),
                          SFVEC2F( end.x * unitScale,
                              -end.y * unitScale ),
                          ( width + currentAdapter().GetHolePlatingThickness() * 2 )
                          * unitScale, *aPad );

        // NOTE: the round segment width is the "diameter", so we double the thickness
        std::vector<const OBJECT_2D*>* object2d_B = new std::vector<const OBJECT_2D*>();
        object2d_B->push_back( innerSeg );

        LAYER_ITEM_2D* itemCSG2d = new LAYER_ITEM_2D( outerSeg, object2d_B, CSGITEM_FULL, *aPad );

        m_containerWithObjectsToDelete.Add( itemCSG2d );
        m_containerWithObjectsToDelete.Add( innerSeg );
        m_containerWithObjectsToDelete.Add( outerSeg );

        object2d_A = itemCSG2d;

        if( object2d_A && !m_antioutlineBoard2dObjects->GetList().empty() )
        {
            m_antioutlineBoard2dObjects->GetIntersectingObjects( object2d_A->GetBBox(),
                                                                 antiOutlineIntersectionList );
        }
    }

    if( object2d_A )
    {
        std::vector<const OBJECT_2D*>* object2d_B = new std::vector<const OBJECT_2D*>();

        // Check if there are any other THT that intersects this hole
        // It will use the non inflated holes
        if( !currentAdapter().GetTH_IDs().GetList().empty() )
        {
            CONST_LIST_OBJECT2D intersecting;

            currentAdapter().GetTH_IDs().GetIntersectingObjects( object2d_A->GetBBox(), intersecting );

            for( const OBJECT_2D* hole2d : intersecting )
            {
                if( object2d_A->Intersects( hole2d->GetBBox() ) )
                    object2d_B->push_back( hole2d );
            }
        }

        for( const OBJECT_2D* obj : antiOutlineIntersectionList )
            object2d_B->push_back( obj );

        if( object2d_B->empty() )
        {
            delete object2d_B;
            object2d_B = CSGITEM_EMPTY;
        }

        if( object2d_B == CSGITEM_EMPTY )
        {
            LAYER_ITEM* objPtr = new LAYER_ITEM( object2d_A, topZ, botZ );

            objPtr->SetMaterial( &m_materials.m_Copper );
            objPtr->SetColor( ConvertSRGBToLinear( objColor ) );
            currentContainer().Add( objPtr );
        }
        else
        {
            LAYER_ITEM_2D* itemCSG2d = new LAYER_ITEM_2D( object2d_A, object2d_B, CSGITEM_FULL,
                                                          *aPad );

            m_containerWithObjectsToDelete.Add( itemCSG2d );

            LAYER_ITEM* objPtr = new LAYER_ITEM( itemCSG2d, topZ, botZ );

            objPtr->SetMaterial( &m_materials.m_Copper );
            objPtr->SetColor( ConvertSRGBToLinear( objColor ) );

            currentContainer().Add( objPtr );
        }
    }

    if( object2d_A && isRoundHole )
    {
        const float frontSurface = topZ + frontDepth;
        const float backSurface = botZ - backDepth;

        const PAD_DRILL_POST_MACHINING_MODE frontMode =
                aPad->GetFrontPostMachining().value_or( PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED );
        const float frontRadius = 0.5f * aPad->GetFrontPostMachiningSize() * unitScale;

        if( frontDepth > 0.0f && frontRadius > holeInnerRadius )
        {
            if( frontMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERBORE )
            {
                addCounterborePlating( *aPad, holeCenter, frontRadius, frontDepth, frontSurface,
                                       true );
            }
            else if( frontMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
            {
                addCountersinkPlating( holeCenter, frontRadius, holeInnerRadius, frontSurface,
                                       frontDepth, true );
            }
        }

        const PAD_DRILL_POST_MACHINING_MODE backMode =
                aPad->GetBackPostMachining().value_or( PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED );
        const float backRadius = 0.5f * aPad->GetBackPostMachiningSize() * unitScale;

        if( backDepth > 0.0f && backRadius > holeInnerRadius )
        {
            if( backMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERBORE )
            {
                addCounterborePlating( *aPad, holeCenter, backRadius, backDepth, backSurface,
                                       false );
            }
            else if( backMode == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
            {
                addCountersinkPlating( holeCenter, backRadius, holeInnerRadius, backSurface,
                                       backDepth, false );
            }
        }
    }
}


void RENDER_3D_RAYTRACE_BASE::addPadsAndVias()
{
    if( !currentAdapter().GetBoard() )
        return;

    // Insert plated vertical holes inside the board

    // Insert vias holes (vertical cylinders)
    for( PCB_TRACK* track : currentAdapter().GetBoard()->Tracks() )
    {
        if( track->Type() == PCB_VIA_T )
        {
            const PCB_VIA* via = static_cast<const PCB_VIA*>( track );
            insertHole( via );
        }
    }

    // Insert pads holes (vertical cylinders)
    for( FOOTPRINT* footprint : currentAdapter().GetBoard()->Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
        {
            if( pad->GetAttribute() != PAD_ATTRIB::NPTH )
                insertHole( pad );
        }
    }
}


void RENDER_3D_RAYTRACE_BASE::load3DModels( CONTAINER_3D& aDstContainer,
                                            bool aSkipMaterialInformation )
{
    if( !currentAdapter().GetBoard() )
        return;

    if( !currentAdapter().m_IsPreviewer
          && !currentAdapter().m_Cfg->m_Render.show_footprints_normal
          && !currentAdapter().m_Cfg->m_Render.show_footprints_insert
          && !currentAdapter().m_Cfg->m_Render.show_footprints_virtual )
    {
        return;
    }

    const wxString currentVariant = currentAdapter().GetBoard()->GetCurrentVariant();

    // Go for all footprints
    for( FOOTPRINT* fp : currentAdapter().GetBoard()->Footprints() )
    {
        if( !fp->Models().empty()
          && currentAdapter().IsFootprintShown( fp ) )
        {
            // Skip 3D models for footprints that are DNP in the current variant
            if( fp->GetDNPForVariant( currentVariant ) )
                continue;

            double zpos = currentAdapter().GetFootprintZPos( fp->IsFlipped() );

            VECTOR2I pos = fp->GetPosition();

            glm::mat4 fpMatrix = glm::mat4( 1.0f );

            fpMatrix = glm::translate( fpMatrix,
                                       SFVEC3F( pos.x * currentAdapter().BiuTo3dUnits(),
                                                -pos.y * currentAdapter().BiuTo3dUnits(),
                                                zpos ) );

            if( !fp->GetOrientation().IsZero() )
            {
                fpMatrix = glm::rotate( fpMatrix, (float) fp->GetOrientation().AsRadians(),
                                        SFVEC3F( 0.0f, 0.0f, 1.0f ) );
            }

            if( fp->IsFlipped() )
            {
                fpMatrix = glm::rotate( fpMatrix, glm::pi<float>(), SFVEC3F( 0.0f, 1.0f, 0.0f ) );

                fpMatrix = glm::rotate( fpMatrix, glm::pi<float>(), SFVEC3F( 0.0f, 0.0f, 1.0f ) );
            }

            const double modelunit_to_3d_units_factor =
                    currentAdapter().BiuTo3dUnits() * UNITS3D_TO_UNITSPCB;

            fpMatrix = glm::scale(
                    fpMatrix, SFVEC3F( modelunit_to_3d_units_factor, modelunit_to_3d_units_factor,
                                       modelunit_to_3d_units_factor ) );

            // Get the list of model files for this model
            S3D_CACHE* cacheMgr = currentAdapter().Get3dCacheManager();

            wxString                libraryName = fp->GetFPID().GetLibNickname();

            wxString                footprintBasePath = wxEmptyString;

            if( currentAdapter().GetBoard()->GetProject() )
            {
                try
                {
                    // FindRow() can throw an exception
                    std::optional<LIBRARY_TABLE_ROW*> fpRow =
                    PROJECT_PCB::FootprintLibAdapter( currentAdapter().GetBoard()->GetProject() )
                            ->GetRow( libraryName );

                    if( fpRow )
                        footprintBasePath = LIBRARY_MANAGER::GetFullURI( *fpRow, true );
                }
                catch( ... )
                {
                    // Do nothing if the libraryName is not found in lib table
                }
            }

            for( FP_3DMODEL& model : fp->Models() )
            {
                if( !model.m_Show || model.m_Filename.empty() )
                    continue;

                // get it from cache
                std::vector<const EMBEDDED_FILES*> embeddedFilesStack;
                embeddedFilesStack.push_back( fp->GetEmbeddedFiles() );
                embeddedFilesStack.push_back( currentAdapter().GetBoard()->GetEmbeddedFiles() );

                const S3DMODEL* modelPtr = cacheMgr->GetModel( model.m_Filename, footprintBasePath,
                                                               std::move( embeddedFilesStack ) );

                // only add it if the return is not NULL.
                if( modelPtr )
                {
                    glm::mat4 modelMatrix = fpMatrix;

                    modelMatrix = glm::translate( modelMatrix,
                            SFVEC3F( model.m_Offset.x, model.m_Offset.y, model.m_Offset.z ) );

                    modelMatrix = glm::rotate( modelMatrix,
                            (float) -( model.m_Rotation.z / 180.0f ) * glm::pi<float>(),
                            SFVEC3F( 0.0f, 0.0f, 1.0f ) );

                    modelMatrix = glm::rotate( modelMatrix,
                            (float) -( model.m_Rotation.y / 180.0f ) * glm::pi<float>(),
                            SFVEC3F( 0.0f, 1.0f, 0.0f ) );

                    modelMatrix = glm::rotate( modelMatrix,
                            (float) -( model.m_Rotation.x / 180.0f ) * glm::pi<float>(),
                            SFVEC3F( 1.0f, 0.0f, 0.0f ) );

                    modelMatrix = glm::scale( modelMatrix,
                            SFVEC3F( model.m_Scale.x, model.m_Scale.y, model.m_Scale.z ) );

                    addModels( aDstContainer, modelPtr, modelMatrix, (float) model.m_Opacity,
                               aSkipMaterialInformation, fp );
                }
            }
        }
    }
}


MODEL_MATERIALS* RENDER_3D_RAYTRACE_BASE::getModelMaterial( const S3DMODEL* a3DModel )
{
    MODEL_MATERIALS* materialVector;

    // Try find if the materials already exists in the map list
    if( m_modelMaterialMap.find( a3DModel ) != m_modelMaterialMap.end() )
    {
        // Found it, so get the pointer
        materialVector = &m_modelMaterialMap[a3DModel];
    }
    else
    {
        // Materials was not found in the map, so it will create a new for
        // this model.

        m_modelMaterialMap[a3DModel] = MODEL_MATERIALS();
        materialVector               = &m_modelMaterialMap[a3DModel];

        materialVector->resize( a3DModel->m_MaterialsSize );

        for( unsigned int imat = 0; imat < a3DModel->m_MaterialsSize; ++imat )
        {
            if( currentAdapter().m_Cfg->m_Render.material_mode == MATERIAL_MODE::NORMAL )
            {
                const SMATERIAL& material = a3DModel->m_Materials[imat];

                // http://www.fooplot.com/#W3sidHlwZSI6MCwiZXEiOiJtaW4oc3FydCh4LTAuMzUpKjAuNDAtMC4wNSwxLjApIiwiY29sb3IiOiIjMDAwMDAwIn0seyJ0eXBlIjoxMDAwLCJ3aW5kb3ciOlsiMC4wNzA3NzM2NzMyMzY1OTAxMiIsIjEuNTY5NTcxNjI5MjI1NDY5OCIsIi0wLjI3NDYzNTMyMTc1OTkyOTMiLCIwLjY0NzcwMTg4MTkyNTUzNjIiXSwic2l6ZSI6WzY0NCwzOTRdfV0-

                float reflectionFactor = 0.0f;

                if( ( material.m_Shininess - 0.35f ) > FLT_EPSILON )
                {
                    reflectionFactor = glm::clamp(
                            glm::sqrt( ( material.m_Shininess - 0.35f ) ) * 0.40f - 0.05f, 0.0f,
                            0.5f );
                }

                BLINN_PHONG_MATERIAL& blinnMaterial = ( *materialVector )[imat];

                blinnMaterial = BLINN_PHONG_MATERIAL( ConvertSRGBToLinear( material.m_Ambient ),
                        ConvertSRGBToLinear( material.m_Emissive ),
                        ConvertSRGBToLinear( material.m_Specular ), material.m_Shininess * 180.0f,
                        material.m_Transparency, reflectionFactor );

                if( currentAdapter().m_Cfg->m_Render.raytrace_procedural_textures )
                {
                    // Guess material type and apply a normal perturbator
                    if( ( RGBtoGray( material.m_Diffuse ) < 0.3f )
                            && ( material.m_Shininess < 0.36f )
                            && ( material.m_Transparency == 0.0f )
                            && ( ( glm::abs( material.m_Diffuse.r - material.m_Diffuse.g ) < 0.15f )
                                    && ( glm::abs( material.m_Diffuse.b - material.m_Diffuse.g )
                                            < 0.15f )
                                    && ( glm::abs( material.m_Diffuse.r - material.m_Diffuse.b )
                                            < 0.15f ) ) )
                    {
                        // This may be a black plastic..
                        blinnMaterial.SetGenerator( &m_plasticMaterial );
                    }
                    else
                    {
                        if( ( RGBtoGray( material.m_Diffuse ) > 0.3f )
                          && ( material.m_Shininess < 0.30f )
                          && ( material.m_Transparency == 0.0f )
                          && ( ( glm::abs( material.m_Diffuse.r - material.m_Diffuse.g ) > 0.25f )
                             || ( glm::abs( material.m_Diffuse.b - material.m_Diffuse.g ) > 0.25f )
                             || ( glm::abs( material.m_Diffuse.r - material.m_Diffuse.b )
                                > 0.25f ) ) )
                        {
                            // This may be a color plastic ...
                            blinnMaterial.SetGenerator( &m_shinyPlasticMaterial );
                        }
                        else
                        {
                            if( ( RGBtoGray( material.m_Diffuse ) > 0.6f )
                              && ( material.m_Shininess > 0.35f )
                              && ( material.m_Transparency == 0.0f )
                              && ( ( glm::abs( material.m_Diffuse.r - material.m_Diffuse.g )
                                   < 0.40f )
                                 && ( glm::abs( material.m_Diffuse.b - material.m_Diffuse.g )
                                    < 0.40f )
                                 && ( glm::abs( material.m_Diffuse.r - material.m_Diffuse.b )
                                    < 0.40f ) ) )
                            {
                                // This may be a brushed metal
                                blinnMaterial.SetGenerator( &m_brushedMetalMaterial );
                            }
                        }
                    }
                }
            }
            else
            {
                ( *materialVector )[imat] = BLINN_PHONG_MATERIAL(
                        SFVEC3F( 0.2f ), SFVEC3F( 0.0f ), SFVEC3F( 0.0f ), 0.0f, 0.0f, 0.0f );
            }
        }
    }

    return materialVector;
}


void RENDER_3D_RAYTRACE_BASE::addModels( CONTAINER_3D& aDstContainer, const S3DMODEL* a3DModel,
                                         const glm::mat4& aModelMatrix, float aFPOpacity,
                                         bool aSkipMaterialInformation, BOARD_ITEM* aBoardItem )
{
    // Validate a3DModel pointers
    wxASSERT( a3DModel != nullptr );

    if( a3DModel == nullptr )
        return;

    wxASSERT( a3DModel->m_Materials != nullptr );
    wxASSERT( a3DModel->m_Meshes != nullptr );
    wxASSERT( a3DModel->m_MaterialsSize > 0 );
    wxASSERT( a3DModel->m_MeshesSize > 0 );

    if( aFPOpacity > 1.0f )
        aFPOpacity = 1.0f;

    if( aFPOpacity < 0.0f )
        aFPOpacity = 0.0f;

    if( ( a3DModel->m_Materials != nullptr ) && ( a3DModel->m_Meshes != nullptr )
      && ( a3DModel->m_MaterialsSize > 0 ) && ( a3DModel->m_MeshesSize > 0 ) )
    {
        MODEL_MATERIALS* materialVector = nullptr;

        if( !aSkipMaterialInformation )
        {
            materialVector = getModelMaterial( a3DModel );
        }

        const glm::mat3 normalMatrix = glm::transpose( glm::inverse( glm::mat3( aModelMatrix ) ) );

        for( unsigned int mesh_i = 0; mesh_i < a3DModel->m_MeshesSize; ++mesh_i )
        {
            const SMESH& mesh = a3DModel->m_Meshes[mesh_i];

            // Validate the mesh pointers
            wxASSERT( mesh.m_Positions != nullptr );
            wxASSERT( mesh.m_FaceIdx != nullptr );
            wxASSERT( mesh.m_Normals != nullptr );
            wxASSERT( mesh.m_FaceIdxSize > 0 );
            wxASSERT( ( mesh.m_FaceIdxSize % 3 ) == 0 );


            if( ( mesh.m_Positions != nullptr ) && ( mesh.m_Normals != nullptr )
              && ( mesh.m_FaceIdx != nullptr ) && ( mesh.m_FaceIdxSize > 0 )
              && ( mesh.m_VertexSize > 0 ) && ( ( mesh.m_FaceIdxSize % 3 ) == 0 )
              && ( mesh.m_MaterialIdx < a3DModel->m_MaterialsSize ) )
            {
                float                       fpTransparency;
                const BLINN_PHONG_MATERIAL* blinn_material;

                if( !aSkipMaterialInformation )
                {
                    blinn_material = &( *materialVector )[mesh.m_MaterialIdx];

                    fpTransparency =
                            1.0f - ( ( 1.0f - blinn_material->GetTransparency() ) * aFPOpacity );
                }

                // Add all face triangles
                for( unsigned int faceIdx = 0; faceIdx < mesh.m_FaceIdxSize; faceIdx += 3 )
                {
                    const unsigned int idx0 = mesh.m_FaceIdx[faceIdx + 0];
                    const unsigned int idx1 = mesh.m_FaceIdx[faceIdx + 1];
                    const unsigned int idx2 = mesh.m_FaceIdx[faceIdx + 2];

                    wxASSERT( idx0 < mesh.m_VertexSize );
                    wxASSERT( idx1 < mesh.m_VertexSize );
                    wxASSERT( idx2 < mesh.m_VertexSize );

                    if( ( idx0 < mesh.m_VertexSize ) && ( idx1 < mesh.m_VertexSize )
                      && ( idx2 < mesh.m_VertexSize ) )
                    {
                        const SFVEC3F& v0 = mesh.m_Positions[idx0];
                        const SFVEC3F& v1 = mesh.m_Positions[idx1];
                        const SFVEC3F& v2 = mesh.m_Positions[idx2];

                        const SFVEC3F& n0 = mesh.m_Normals[idx0];
                        const SFVEC3F& n1 = mesh.m_Normals[idx1];
                        const SFVEC3F& n2 = mesh.m_Normals[idx2];

                        // Transform vertex with the model matrix
                        const SFVEC3F vt0 = SFVEC3F( aModelMatrix * glm::vec4( v0, 1.0f ) );
                        const SFVEC3F vt1 = SFVEC3F( aModelMatrix * glm::vec4( v1, 1.0f ) );
                        const SFVEC3F vt2 = SFVEC3F( aModelMatrix * glm::vec4( v2, 1.0f ) );

                        const SFVEC3F nt0 = glm::normalize( SFVEC3F( normalMatrix * n0 ) );
                        const SFVEC3F nt1 = glm::normalize( SFVEC3F( normalMatrix * n1 ) );
                        const SFVEC3F nt2 = glm::normalize( SFVEC3F( normalMatrix * n2 ) );

                        TRIANGLE* newTriangle = new TRIANGLE( vt0, vt2, vt1, nt0, nt2, nt1 );

                        newTriangle->SetBoardItem( aBoardItem );

                        aDstContainer.Add( newTriangle );

                        if( !aSkipMaterialInformation )
                        {
                            newTriangle->SetMaterial( blinn_material );
                            newTriangle->SetModelTransparency( fpTransparency );

                            if( mesh.m_Color == nullptr )
                            {
                                const SFVEC3F diffuseColor =
                                        a3DModel->m_Materials[mesh.m_MaterialIdx].m_Diffuse;

                                if( currentAdapter().m_Cfg->m_Render.material_mode == MATERIAL_MODE::CAD_MODE )
                                    newTriangle->SetColor( ConvertSRGBToLinear(
                                            MaterialDiffuseToColorCAD( diffuseColor ) ) );
                                else
                                    newTriangle->SetColor( ConvertSRGBToLinear( diffuseColor ) );
                            }
                            else
                            {
                                if( currentAdapter().m_Cfg->m_Render.material_mode == MATERIAL_MODE::CAD_MODE )
                                {
                                    newTriangle->SetColor(
                                            ConvertSRGBToLinear( MaterialDiffuseToColorCAD(
                                                    mesh.m_Color[idx0] ) ),
                                            ConvertSRGBToLinear( MaterialDiffuseToColorCAD(
                                                    mesh.m_Color[idx1] ) ),
                                            ConvertSRGBToLinear( MaterialDiffuseToColorCAD(
                                                    mesh.m_Color[idx2] ) ) );
                                }
                                else
                                {
                                    newTriangle->SetColor(
                                            ConvertSRGBToLinear( mesh.m_Color[idx0] ),
                                            ConvertSRGBToLinear( mesh.m_Color[idx1] ),
                                            ConvertSRGBToLinear( mesh.m_Color[idx2] ) );
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
