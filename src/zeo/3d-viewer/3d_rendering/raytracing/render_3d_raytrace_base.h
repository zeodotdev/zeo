/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015-2020 Mario Luzeiro <mrluzeiro@ua.pt>
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

#ifndef RENDER_3D_RAYTRACE_BASE_H
#define RENDER_3D_RAYTRACE_BASE_H

#include "accelerators/container_3d.h"
#include "accelerators/accelerator_3d.h"
#include "../render_3d_base.h"
#include "light.h"
#include "../post_shader_ssao.h"
#include "material.h"
#include "shapes3D/instance_object_3d.h"
#include <plugins/3dapi/c3dmodel.h>

#include <map>
#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>

/// Vector of materials.
typedef std::vector< BLINN_PHONG_MATERIAL > MODEL_MATERIALS;

/// Maps a #S3DMODEL pointer with a created BLINN_PHONG_MATERIAL vector.
typedef std::map< const S3DMODEL* , MODEL_MATERIALS > MAP_MODEL_MATERIALS;

typedef enum
{
    RT_RENDER_STATE_TRACING = 0,
    RT_RENDER_STATE_POST_PROCESS_SHADE,
    RT_RENDER_STATE_POST_PROCESS_BLUR_AND_FINISH,
    RT_RENDER_STATE_FINISH,
    RT_RENDER_STATE_MAX
} RT_RENDER_STATE;


class RENDER_3D_RAYTRACE_BASE : public RENDER_3D_BASE
{
public:
    // TODO: Take into account board thickness so that the camera won't move inside of the board
    // when facing it perpendicularly.
    static constexpr float MIN_DISTANCE_IU = 4 * PCB_IU_PER_MM;

    explicit RENDER_3D_RAYTRACE_BASE( BOARD_ADAPTER& aAdapter, CAMERA& aCamera );

    ~RENDER_3D_RAYTRACE_BASE();

    int GetWaitForEditingTimeOut() override;

    void Reload( REPORTER* aStatusReporter, REPORTER* aWarningReporter,
                 bool aOnlyLoadCopperAndShapes );

    /**
     * One sub-board to merge into the multi-instance scene.
     */
    struct INSTANCE_DESC
    {
        BOARD_ADAPTER* adapter;     ///< per-instance scene source (must outlive Reload)
        glm::mat4      pose;        ///< local→world pose for this instance
    };

    /**
     * Multi-instance scene build for the MBS multi-board path. For each
     * descriptor: temporarily target a per-instance container, run the
     * existing Reload geometry pipeline against that instance's adapter,
     * build a per-instance BVH, wrap it in an INSTANCE_OBJECT_3D under
     * the instance pose, and add the wrapper to the top-level container.
     * Top-level BVH then dispatches to per-instance BVHs.
     *
     * The first instance also drives the camera framing and shared scene
     * setup (background, lights). Pose for instance 0 is generally
     * identity but doesn't have to be.
     */
    void ReloadMultiInstance( const std::vector<INSTANCE_DESC>& aInstances,
                               REPORTER* aStatusReporter,
                               REPORTER* aWarningReporter );

    /**
     * Stage a multi-instance descriptor list to be consumed on the NEXT
     * Reload call. Used by the assembly canvas to publish the current MBS
     * instance set without bypassing the existing reload-on-Redraw flow.
     *
     * Pass an empty vector to revert to single-board Reload behavior on
     * the next reload.
     */
    void SetPendingInstances( std::vector<INSTANCE_DESC> aInstances )
    {
        m_pendingInstances = std::move( aInstances );
    }

    BOARD_ITEM *IntersectBoardItem( const RAY& aRay );

protected:
    virtual void initPbo() = 0;
    virtual void deletePbo() = 0;
    void createItemsFromContainer( const BVH_CONTAINER_2D* aContainer2d, PCB_LAYER_ID aLayer_id,
                                   const MATERIAL* aMaterialLayer, const SFVEC3F& aLayerColor,
                                   float aLayerZOffset );

    void restartRenderState();
    void renderTracing( uint8_t* ptrPBO, REPORTER* aStatusReporter );
    void postProcessShading( uint8_t* ptrPBO, REPORTER* aStatusReporter );
    void postProcessBlurFinish( uint8_t* ptrPBO, REPORTER* aStatusReporter );
    void renderBlockTracing( uint8_t* ptrPBO , signed int iBlock );
    void renderFinalColor( uint8_t* ptrPBO, const SFVEC4F& rgbColor,
                           bool applyColorSpaceConversion );

    void renderRayPackets( const SFVEC4F* bgColorY, const RAY* aRayPkt, HITINFO_PACKET* aHitPacket,
                           bool is_testShadow, SFVEC4F* aOutHitColor );

    void renderAntiAliasPackets( const SFVEC4F* aBgColorY, const HITINFO_PACKET* aHitPck_X0Y0,
                                 const HITINFO_PACKET* aHitPck_AA_X1Y1, const RAY* aRayPck,
                                 SFVEC4F* aOutHitColor );

    // Materials
    void setupMaterials();

    SFVEC4F shadeHit( const SFVEC4F& aBgColor, const RAY& aRay, HITINFO& aHitInfo,
                      bool aIsInsideObject, unsigned int aRecursiveLevel,
                      bool is_testShadow ) const;

    /**
     * Create one or more 3D objects form a 2D object and Z positions.
     *
     * It tries to optimize some types of objects that will be faster to trace than the
     * LAYER_ITEM object.
     */
    void createObject( CONTAINER_3D& aDstContainer, const OBJECT_2D* aObject2D, float aZMin,
                       float aZMax, const MATERIAL* aMaterial, const SFVEC3F& aObjColor );

    void addPadsAndVias();
    void insertHole( const PCB_VIA* aVia );
    void insertHole( const PAD* aPad );
    void addCounterborePlating( const BOARD_ITEM& aSource, const SFVEC2F& aCenter,
                                float aInnerRadius, float aDepth, float aSurfaceZ,
                                bool aIsFront );
    void addCountersinkPlating( const SFVEC2F& aCenter, float aTopInnerRadius,
                                float aBottomInnerRadius, float aSurfaceZ, float aDepth,
                                bool aIsFront );
    void backfillPostMachine();
    void load3DModels( CONTAINER_3D& aDstContainer, bool aSkipMaterialInformation );
    void addModels( CONTAINER_3D& aDstContainer, const S3DMODEL* a3DModel,
                    const glm::mat4& aModelMatrix, float aFPOpacity,
                    bool aSkipMaterialInformation, BOARD_ITEM* aBoardItem );

    MODEL_MATERIALS* getModelMaterial( const S3DMODEL* a3DModel );

    void initializeBlockPositions();

    void render( uint8_t* ptrPBO, REPORTER* aStatusReporter );
    void renderPreview( uint8_t* ptrPBO );

    static SFVEC4F premultiplyAlpha( const SFVEC4F& aInput );

    struct
    {
        BLINN_PHONG_MATERIAL m_Paste;
        BLINN_PHONG_MATERIAL m_SilkS;
        BLINN_PHONG_MATERIAL m_SolderMask;
        BLINN_PHONG_MATERIAL m_EpoxyBoard;
        BLINN_PHONG_MATERIAL m_Copper;
        BLINN_PHONG_MATERIAL m_NonPlatedCopper;
        BLINN_PHONG_MATERIAL m_Floor;
    } m_materials;

    BOARD_NORMAL         m_boardMaterial;
    COPPER_NORMAL        m_copperMaterial;
    PLATED_COPPER_NORMAL m_platedCopperMaterial;
    SOLDER_MASK_NORMAL   m_solderMaskMaterial;
    PLASTIC_NORMAL       m_plasticMaterial;
    PLASTIC_SHINE_NORMAL m_shinyPlasticMaterial;
    BRUSHED_METAL_NORMAL m_brushedMetalMaterial;
    SILK_SCREEN_NORMAL   m_silkScreenMaterial;

    bool m_is_canvas_initialized;
    bool m_isPreview;

    /// State used on quality render
    RT_RENDER_STATE m_renderState;

    /// Time that the render starts
    int64_t m_renderStartTime;

    /// Save the number of blocks progress of the render
    size_t m_blockRenderProgressCount;

    POST_SHADER_SSAO m_postShaderSsao;

    std::list<LIGHT*> m_lights;

    DIRECTIONAL_LIGHT* m_cameraLight;

    /*GLuint m_pboId;
    GLuint m_pboDataSize;*/

    CONTAINER_3D m_objectContainer;

    /// Store the list of created objects special for RT that will be clear in the end.
    CONTAINER_2D m_containerWithObjectsToDelete;

    /**
     * Per-instance scene-build override. When non-null these point at the
     * instance under construction inside ReloadMultiInstance; otherwise
     * the accessors below fall back to the top-level
     * m_boardAdapter / m_objectContainer. Use the accessors throughout
     * create_scene.cpp instead of touching the top-level members directly,
     * so the scene-build code is reusable for both single- and multi-
     * instance paths without copy-pasting.
     */
    BOARD_ADAPTER* m_overrideAdapter   = nullptr;
    CONTAINER_3D*  m_overrideContainer = nullptr;

    BOARD_ADAPTER& currentAdapter()
    {
        return m_overrideAdapter ? *m_overrideAdapter : m_boardAdapter;
    }

    const BOARD_ADAPTER& currentAdapter() const
    {
        return m_overrideAdapter ? *m_overrideAdapter : m_boardAdapter;
    }

    CONTAINER_3D& currentContainer()
    {
        return m_overrideContainer ? *m_overrideContainer : m_objectContainer;
    }

    /**
     * Owned wrappers populated by ReloadMultiInstance. Each holds one
     * sub-board's container + BVH + pose. The top-level m_objectContainer
     * receives raw pointers to these wrappers and the main BVH dispatches
     * through them.
     */
    std::vector<std::unique_ptr<INSTANCE_OBJECT_3D>> m_instanceWrappers;

    /**
     * Per-instance 2D outline containers captured during multi-instance
     * Reload. Each per-instance 3D LAYER_ITEM holds a non-owning
     * pointer to a 2D OBJECT_2D inside one of these containers, so the
     * containers must outlive the wrappers. Cleared (and 2D objects
     * deleted) only AFTER m_objectContainer.Clear() at the top of the
     * NEXT ReloadMultiInstance call, so the live LAYER_ITEMs are
     * destroyed first and no dangling refs survive.
     */
    std::vector<std::unique_ptr<CONTAINER_2D>>     m_perInstanceOutlines2d;
    std::vector<std::unique_ptr<BVH_CONTAINER_2D>> m_perInstanceAntiOutlines2d;

    /**
     * Staged multi-instance list. Consumed by the next Reload (which
     * dispatches to ReloadMultiInstance when this is non-empty). Cleared
     * by Reload after use so a stale list can't drive an unintended
     * multi-instance build.
     */
    std::vector<INSTANCE_DESC> m_pendingInstances;

    /**
     * Adapter used by the trace-time path (shadeHit, render setup) for
     * per-board geometric queries that don't have a sensible top-level
     * answer in MBS multi-board mode — most importantly
     * GetNonCopperLayerThickness, which drives the bias offsets applied
     * to refraction/shadow ray origins. The MBS container's m_boardAdapter
     * has no real board and reports 0 thickness, which collapses the
     * refraction bias and prevents the F.Mask refracted ray from
     * reaching F.Cu underneath. Set to:
     *   • &m_boardAdapter         for single-board Reload, OR
     *   • aInstances[0].adapter   for ReloadMultiInstance.
     * Falls back to &m_boardAdapter if unset.
     */
    BOARD_ADAPTER* m_renderAdapter = nullptr;

    BOARD_ADAPTER& renderAdapter()
    {
        return m_renderAdapter ? *m_renderAdapter : m_boardAdapter;
    }

    const BOARD_ADAPTER& renderAdapter() const
    {
        return m_renderAdapter ? *m_renderAdapter : m_boardAdapter;
    }

    /**
     * Non-copper layer thickness expressed in WORLD 3D-units, suitable
     * for the bias offsets shadeHit applies to refraction-startpoint
     * and shadow-ray-nudge math. In single-board mode the world frame
     * is the (single) adapter's frame, so this equals
     * m_boardAdapter.GetNonCopperLayerThickness(). In MBS multi-board
     * mode each sub-board adapter has its own biuTo3dUnits — the
     * wrapper's pose carries a sharedBiu/localBiu scale to map between
     * frames — so the per-adapter thickness must be rescaled by the
     * first wrapper's scale before it can be used as a world-frame
     * bias. ReloadMultiInstance writes this; trace code should read
     * `renderNonCopperLayerThickness()` instead of going to the
     * adapter directly.
     */
    float m_renderNonCopperLayerThickness3DU = 0.0f;

    float renderNonCopperLayerThickness() const
    {
        return m_renderNonCopperLayerThickness3DU > 0.0f
                       ? m_renderNonCopperLayerThickness3DU
                       : m_boardAdapter.GetNonCopperLayerThickness();
    }

    CONTAINER_2D* m_outlineBoard2dObjects;
    BVH_CONTAINER_2D* m_antioutlineBoard2dObjects;

    ACCELERATOR_3D* m_accelerator;

    SFVEC4F m_backgroundColorTop;
    SFVEC4F m_backgroundColorBottom;

    /// Used to see if the windows size changed.
    wxSize m_oldWindowsSize;

    /// Encode Morton code positions.
    std::vector< SFVEC2UI > m_blockPositions;

    /// Flag if a position was already processed (cleared each new render).
    std::vector< int > m_blockPositionsWasProcessed;

    /// Encode the Morton code positions (on fast preview mode).
    std::vector< SFVEC2UI > m_blockPositionsFast;

    SFVEC2UI m_realBufferSize;
    SFVEC2UI m_fastPreviewModeSize;

    HITINFO_PACKET* m_firstHitinfo;

    SFVEC3F* m_shaderBuffer;

    // Display Offset
    unsigned int m_xoffset;
    unsigned int m_yoffset;

    /// Stores materials of the 3D models
    MAP_MODEL_MATERIALS m_modelMaterialMap;

    // Statistics
    unsigned int m_convertedDummyBlockCount;
    unsigned int m_converted2dRoundSegmentCount;
};

#define USE_SRGB_SPACE

#ifdef USE_SRGB_SPACE
extern SFVEC3F ConvertSRGBToLinear( const SFVEC3F& aSRGBcolor );
extern SFVEC4F ConvertSRGBAToLinear( const SFVEC4F& aSRGBAcolor );
#else
#define ConvertSRGBToLinear( v ) ( v )
#define ConvertSRGBAToLinear( v ) ( v )
#endif

#endif // RENDER_3D_RAYTRACE_BASE_H
