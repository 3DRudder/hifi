
//
//  RenderDeferredTask.cpp
//  render-utils/src/
//
//  Created by Sam Gateau on 5/29/15.
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderDeferredTask.h"

#include <DependencyManager.h>

#include <PerfStat.h>
#include <PathUtils.h>
#include <ViewFrustum.h>
#include <gpu/Context.h>

#include <render/CullTask.h>
#include <render/FilterTask.h>
#include <render/SortTask.h>
#include <render/DrawTask.h>
#include <render/DrawStatus.h>
#include <render/DrawSceneOctree.h>
#include <render/BlurTask.h>

#include "RenderCommonTask.h"
#include "LightingModel.h"
#include "StencilMaskPass.h"
#include "DebugDeferredBuffer.h"
#include "DeferredFramebuffer.h"
#include "DeferredLightingEffect.h"
#include "SurfaceGeometryPass.h"
#include "VelocityBufferPass.h"
#include "FramebufferCache.h"
#include "TextureCache.h"
#include "ZoneRenderer.h"
#include "FadeEffect.h"
#include "RenderUtilsLogging.h"

#include "AmbientOcclusionEffect.h"
#include "AntialiasingEffect.h"
#include "ToneMappingEffect.h"
#include "SubsurfaceScattering.h"
#include "DrawHaze.h"
#include "BloomEffect.h"
#include "HighlightEffect.h"

#include <sstream>

using namespace render;
extern void initOverlay3DPipelines(render::ShapePlumber& plumber, bool depthTest = false);
extern void initDeferredPipelines(render::ShapePlumber& plumber, const render::ShapePipeline::BatchSetter& batchSetter, const render::ShapePipeline::ItemSetter& itemSetter);

RenderDeferredTask::RenderDeferredTask()
{
}

void RenderDeferredTask::configure(const Config& config)
{
}

const render::Varying RenderDeferredTask::addSelectItemJobs(JobModel& task, const char* selectionName,
                                                            const render::Varying& metas, 
                                                            const render::Varying& opaques, 
                                                            const render::Varying& transparents) {
    const auto selectMetaInput = SelectItems::Inputs(metas, Varying(), std::string()).asVarying();
    const auto selectedMetas = task.addJob<SelectItems>("MetaSelection", selectMetaInput, selectionName);
    const auto selectMetaAndOpaqueInput = SelectItems::Inputs(opaques, selectedMetas, std::string()).asVarying();
    const auto selectedMetasAndOpaques = task.addJob<SelectItems>("OpaqueSelection", selectMetaAndOpaqueInput, selectionName);
    const auto selectItemInput = SelectItems::Inputs(transparents, selectedMetasAndOpaques, std::string()).asVarying();
    return task.addJob<SelectItems>("TransparentSelection", selectItemInput, selectionName);
}

void RenderDeferredTask::build(JobModel& task, const render::Varying& input, render::Varying& output) {
    const auto& items = input.get<Input>();
    auto fadeEffect = DependencyManager::get<FadeEffect>();

    // Prepare the ShapePipelines
    ShapePlumberPointer shapePlumber = std::make_shared<ShapePlumber>();
    initDeferredPipelines(*shapePlumber, fadeEffect->getBatchSetter(), fadeEffect->getItemUniformSetter());

    // Extract opaques / transparents / lights / metas / overlays / background
    const auto& opaques = items.get0()[RenderFetchCullSortTask::OPAQUE_SHAPE];
    const auto& transparents = items.get0()[RenderFetchCullSortTask::TRANSPARENT_SHAPE];
    const auto& lights = items.get0()[RenderFetchCullSortTask::LIGHT];
    const auto& metas = items.get0()[RenderFetchCullSortTask::META];
    const auto& overlayOpaques = items.get0()[RenderFetchCullSortTask::OVERLAY_OPAQUE_SHAPE];
    const auto& overlayTransparents = items.get0()[RenderFetchCullSortTask::OVERLAY_TRANSPARENT_SHAPE];
    //const auto& background = items.get0()[RenderFetchCullSortTask::BACKGROUND];
    const auto& spatialSelection = items[1];

    fadeEffect->build(task, opaques);

    task.addJob<JitterSample>("JitterCam");

    // Prepare deferred, generate the shared Deferred Frame Transform
    const auto deferredFrameTransform = task.addJob<GenerateDeferredFrameTransform>("DeferredFrameTransform");
    const auto lightingModel = task.addJob<MakeLightingModel>("LightingModel");
    

    // GPU jobs: Start preparing the primary, deferred and lighting buffer
    const auto primaryFramebuffer = task.addJob<PreparePrimaryFramebuffer>("PreparePrimaryBuffer");

    const auto opaqueRangeTimer = task.addJob<BeginGPURangeTimer>("BeginOpaqueRangeTimer", "DrawOpaques");

    const auto prepareDeferredInputs = PrepareDeferred::Inputs(primaryFramebuffer, lightingModel).asVarying();
    const auto prepareDeferredOutputs = task.addJob<PrepareDeferred>("PrepareDeferred", prepareDeferredInputs);
    const auto deferredFramebuffer = prepareDeferredOutputs.getN<PrepareDeferred::Outputs>(0);
    const auto lightingFramebuffer = prepareDeferredOutputs.getN<PrepareDeferred::Outputs>(1);

    // draw a stencil mask in hidden regions of the framebuffer.
    task.addJob<PrepareStencil>("PrepareStencil", primaryFramebuffer);

    // Render opaque objects in DeferredBuffer
    const auto opaqueInputs = DrawStateSortDeferred::Inputs(opaques, lightingModel).asVarying();
    task.addJob<DrawStateSortDeferred>("DrawOpaqueDeferred", opaqueInputs, shapePlumber);

    task.addJob<EndGPURangeTimer>("OpaqueRangeTimer", opaqueRangeTimer);


    // Opaque all rendered

    // Linear Depth Pass
    const auto linearDepthPassInputs = LinearDepthPass::Inputs(deferredFrameTransform, deferredFramebuffer).asVarying();
    const auto linearDepthPassOutputs = task.addJob<LinearDepthPass>("LinearDepth", linearDepthPassInputs);
    const auto linearDepthTarget = linearDepthPassOutputs.getN<LinearDepthPass::Outputs>(0);
    
    // Curvature pass
    const auto surfaceGeometryPassInputs = SurfaceGeometryPass::Inputs(deferredFrameTransform, deferredFramebuffer, linearDepthTarget).asVarying();
    const auto surfaceGeometryPassOutputs = task.addJob<SurfaceGeometryPass>("SurfaceGeometry", surfaceGeometryPassInputs);
    const auto surfaceGeometryFramebuffer = surfaceGeometryPassOutputs.getN<SurfaceGeometryPass::Outputs>(0);
    const auto curvatureFramebuffer = surfaceGeometryPassOutputs.getN<SurfaceGeometryPass::Outputs>(1);
    const auto midCurvatureNormalFramebuffer = surfaceGeometryPassOutputs.getN<SurfaceGeometryPass::Outputs>(2);
    const auto lowCurvatureNormalFramebuffer = surfaceGeometryPassOutputs.getN<SurfaceGeometryPass::Outputs>(3);

    // Simply update the scattering resource
    const auto scatteringResource = task.addJob<SubsurfaceScattering>("Scattering");

    // AO job
    const auto ambientOcclusionInputs = AmbientOcclusionEffect::Inputs(deferredFrameTransform, deferredFramebuffer, linearDepthTarget).asVarying();
    const auto ambientOcclusionOutputs = task.addJob<AmbientOcclusionEffect>("AmbientOcclusion", ambientOcclusionInputs);
    const auto ambientOcclusionFramebuffer = ambientOcclusionOutputs.getN<AmbientOcclusionEffect::Outputs>(0);
    const auto ambientOcclusionUniforms = ambientOcclusionOutputs.getN<AmbientOcclusionEffect::Outputs>(1);

    // Velocity
    const auto velocityBufferInputs = VelocityBufferPass::Inputs(deferredFrameTransform, deferredFramebuffer).asVarying();
    const auto velocityBufferOutputs = task.addJob<VelocityBufferPass>("VelocityBuffer", velocityBufferInputs);
    const auto velocityBuffer = velocityBufferOutputs.getN<VelocityBufferPass::Outputs>(0);

    // Clear Light, Haze and Skybox Stages and render zones from the general metas bucket
    const auto zones = task.addJob<ZoneRendererTask>("ZoneRenderer", metas);

    // Draw Lights just add the lights to the current list of lights to deal with. NOt really gpu job for now.
    task.addJob<DrawLight>("DrawLight", lights);

    // Light Clustering
    // Create the cluster grid of lights, cpu job for now
    const auto lightClusteringPassInputs = LightClusteringPass::Inputs(deferredFrameTransform, lightingModel, linearDepthTarget).asVarying();
    const auto lightClusters = task.addJob<LightClusteringPass>("LightClustering", lightClusteringPassInputs);
 
    // Add haze model
    const auto hazeModel = task.addJob<FetchHazeStage>("HazeModel");

    // DeferredBuffer is complete, now let's shade it into the LightingBuffer
    const auto deferredLightingInputs = RenderDeferred::Inputs(deferredFrameTransform, deferredFramebuffer, lightingModel,
        surfaceGeometryFramebuffer, ambientOcclusionFramebuffer, scatteringResource, lightClusters, hazeModel).asVarying();
    
    task.addJob<RenderDeferred>("RenderDeferred", deferredLightingInputs);


    // Similar to light stage, background stage has been filled by several potential render items and resolved for the frame in this job
    task.addJob<DrawBackgroundStage>("DrawBackgroundDeferred", lightingModel);

    const auto drawHazeInputs = render::Varying(DrawHaze::Inputs(hazeModel, lightingFramebuffer, linearDepthTarget, deferredFrameTransform, lightingFramebuffer));
    task.addJob<DrawHaze>("DrawHazeDeferred", drawHazeInputs);

    // Render transparent objects forward in LightingBuffer
    const auto transparentsInputs = DrawDeferred::Inputs(transparents, lightingModel, lightClusters).asVarying();
    task.addJob<DrawDeferred>("DrawTransparentDeferred", transparentsInputs, shapePlumber);

    // Light Cluster Grid Debuging job
    {
        const auto debugLightClustersInputs = DebugLightClusters::Inputs(deferredFrameTransform, deferredFramebuffer, lightingModel, linearDepthTarget, lightClusters).asVarying();
        task.addJob<DebugLightClusters>("DebugLightClusters", debugLightClustersInputs);
    }

    const auto toneAndPostRangeTimer = task.addJob<BeginGPURangeTimer>("BeginToneAndPostRangeTimer", "PostToneOverlaysAntialiasing");

    // Add bloom
    const auto bloomInputs = Bloom::Inputs(deferredFrameTransform, lightingFramebuffer).asVarying();
    task.addJob<Bloom>("Bloom", bloomInputs);

    // Lighting Buffer ready for tone mapping
    const auto toneMappingInputs = ToneMappingDeferred::Inputs(lightingFramebuffer, primaryFramebuffer).asVarying();
    task.addJob<ToneMappingDeferred>("ToneMapping", toneMappingInputs);

    const auto outlineRangeTimer = task.addJob<BeginGPURangeTimer>("BeginHighlightRangeTimer", "Highlight");
    // Select items that need to be outlined
    const auto selectionBaseName = "contextOverlayHighlightList";
    const auto selectedItems = addSelectItemJobs(task, selectionBaseName, metas, opaques, transparents);

    const auto outlineInputs = DrawHighlightTask::Inputs(items.get0(), deferredFramebuffer, primaryFramebuffer, deferredFrameTransform).asVarying();
    task.addJob<DrawHighlightTask>("DrawHighlight", outlineInputs);

    task.addJob<EndGPURangeTimer>("HighlightRangeTimer", outlineRangeTimer);

    { // Debug the bounds of the rendered items, still look at the zbuffer
        task.addJob<DrawBounds>("DrawMetaBounds", metas);
        task.addJob<DrawBounds>("DrawOpaqueBounds", opaques);
        task.addJob<DrawBounds>("DrawTransparentBounds", transparents);
    
        task.addJob<DrawBounds>("DrawLightBounds", lights);
        task.addJob<DrawBounds>("DrawZones", zones);
        const auto frustums = task.addJob<ExtractFrustums>("ExtractFrustums");
        const auto viewFrustum = frustums.getN<ExtractFrustums::Output>(ExtractFrustums::VIEW_FRUSTUM);
        task.addJob<DrawFrustum>("DrawViewFrustum", viewFrustum, glm::vec3(1.0f, 1.0f, 0.0f));
        for (auto i = 0; i < ExtractFrustums::SHADOW_CASCADE_FRUSTUM_COUNT; i++) {
            const auto shadowFrustum = frustums.getN<ExtractFrustums::Output>(ExtractFrustums::SHADOW_CASCADE0_FRUSTUM+i);
            float tint = 1.0f - i / float(ExtractFrustums::SHADOW_CASCADE_FRUSTUM_COUNT - 1);
            char jobName[64];
            sprintf(jobName, "DrawShadowFrustum%d", i);
            task.addJob<DrawFrustum>(jobName, shadowFrustum, glm::vec3(0.0f, tint, 1.0f));
        }

        // Render.getConfig("RenderMainView.DrawSelectionBounds").enabled = true
        task.addJob<DrawBounds>("DrawSelectionBounds", selectedItems);
    }

    // Layered Overlays
    const auto filteredOverlaysOpaque = task.addJob<FilterLayeredItems>("FilterOverlaysLayeredOpaque", overlayOpaques, Item::LAYER_3D_FRONT);
    const auto filteredOverlaysTransparent = task.addJob<FilterLayeredItems>("FilterOverlaysLayeredTransparent", overlayTransparents, Item::LAYER_3D_FRONT);
    const auto overlaysInFrontOpaque =  filteredOverlaysOpaque.getN<FilterLayeredItems::Outputs>(0);
    const auto overlaysInFrontTransparent = filteredOverlaysTransparent.getN<FilterLayeredItems::Outputs>(0);

    const auto overlayInFrontOpaquesInputs = DrawOverlay3D::Inputs(overlaysInFrontOpaque, lightingModel).asVarying();
    const auto overlayInFrontTransparentsInputs = DrawOverlay3D::Inputs(overlaysInFrontTransparent, lightingModel).asVarying();
    task.addJob<DrawOverlay3D>("DrawOverlayInFrontOpaque", overlayInFrontOpaquesInputs, true);
    task.addJob<DrawOverlay3D>("DrawOverlayInFrontTransparent", overlayInFrontTransparentsInputs, false);

    { // Debug the bounds of the rendered Overlay items that are marked drawInFront, still look at the zbuffer
        task.addJob<DrawBounds>("DrawOverlayInFrontOpaqueBounds", overlaysInFrontOpaque);
        task.addJob<DrawBounds>("DrawOverlayInFrontTransparentBounds", overlaysInFrontTransparent);
    }

    // AA job
    const auto antialiasingInputs = Antialiasing::Inputs(deferredFrameTransform, primaryFramebuffer, linearDepthTarget, velocityBuffer).asVarying();
    task.addJob<Antialiasing>("Antialiasing", antialiasingInputs);

    // Debugging stages
    {
        // Debugging Deferred buffer job
        const auto debugFramebuffers = render::Varying(DebugDeferredBuffer::Inputs(deferredFramebuffer, linearDepthTarget, surfaceGeometryFramebuffer, ambientOcclusionFramebuffer, velocityBuffer, deferredFrameTransform));
        task.addJob<DebugDeferredBuffer>("DebugDeferredBuffer", debugFramebuffers);

        const auto debugSubsurfaceScatteringInputs = DebugSubsurfaceScattering::Inputs(deferredFrameTransform, deferredFramebuffer, lightingModel,
            surfaceGeometryFramebuffer, ambientOcclusionFramebuffer, scatteringResource).asVarying();
        task.addJob<DebugSubsurfaceScattering>("DebugScattering", debugSubsurfaceScatteringInputs);

        const auto debugAmbientOcclusionInputs = DebugAmbientOcclusion::Inputs(deferredFrameTransform, deferredFramebuffer, linearDepthTarget, ambientOcclusionUniforms).asVarying();
        task.addJob<DebugAmbientOcclusion>("DebugAmbientOcclusion", debugAmbientOcclusionInputs);

        // Scene Octree Debugging job
        {
            task.addJob<DrawSceneOctree>("DrawSceneOctree", spatialSelection);
            task.addJob<DrawItemSelection>("DrawItemSelection", spatialSelection);
        }

        // Status icon rendering job
        {
            // Grab a texture map representing the different status icons and assign that to the drawStatsuJob
            auto iconMapPath = PathUtils::resourcesPath() + "icons/statusIconAtlas.svg";
            auto statusIconMap = DependencyManager::get<TextureCache>()->getImageTexture(iconMapPath, image::TextureUsage::STRICT_TEXTURE);
            task.addJob<DrawStatus>("DrawStatus", opaques, DrawStatus(statusIconMap));
        }

        task.addJob<DebugZoneLighting>("DrawZoneStack", deferredFrameTransform);
    }

    // Composite the HUD and HUD overlays
    task.addJob<CompositeHUD>("HUD");

    const auto overlaysHUDOpaque = filteredOverlaysOpaque.getN<FilterLayeredItems::Outputs>(1);
    const auto overlaysHUDTransparent = filteredOverlaysTransparent.getN<FilterLayeredItems::Outputs>(1);

    const auto overlayHUDOpaquesInputs = DrawOverlay3D::Inputs(overlaysHUDOpaque, lightingModel).asVarying();
    const auto overlayHUDTransparentsInputs = DrawOverlay3D::Inputs(overlaysHUDTransparent, lightingModel).asVarying();
    task.addJob<DrawOverlay3D>("DrawOverlayHUDOpaque", overlayHUDOpaquesInputs, true);
    task.addJob<DrawOverlay3D>("DrawOverlayHUDTransparent", overlayHUDTransparentsInputs, false);

    { // Debug the bounds of the rendered Overlay items that are marked drawHUDLayer, still look at the zbuffer
        task.addJob<DrawBounds>("DrawOverlayHUDOpaqueBounds", overlaysHUDOpaque);
        task.addJob<DrawBounds>("DrawOverlayHUDTransparentBounds", overlaysHUDTransparent);
    }

    task.addJob<EndGPURangeTimer>("ToneAndPostRangeTimer", toneAndPostRangeTimer);

    // Blit!
    task.addJob<Blit>("Blit", primaryFramebuffer);
}

void DrawDeferred::run(const RenderContextPointer& renderContext, const Inputs& inputs) {
    assert(renderContext->args);
    assert(renderContext->args->hasViewFrustum());

    auto config = std::static_pointer_cast<Config>(renderContext->jobConfig);

    const auto& inItems = inputs.get0();
    const auto& lightingModel = inputs.get1();
    const auto& lightClusters = inputs.get2();
    auto deferredLightingEffect = DependencyManager::get<DeferredLightingEffect>();

    RenderArgs* args = renderContext->args;

    gpu::doInBatch("DrawDeferred::run", args->_context, [&](gpu::Batch& batch) {
        args->_batch = &batch;
        
        // Setup camera, projection and viewport for all items
        batch.setViewportTransform(args->_viewport);
        batch.setStateScissorRect(args->_viewport);

        glm::mat4 projMat;
        Transform viewMat;
        args->getViewFrustum().evalProjectionMatrix(projMat);
        args->getViewFrustum().evalViewTransform(viewMat);

        batch.setProjectionTransform(projMat);
        batch.setViewTransform(viewMat);

        // Setup lighting model for all items;
        batch.setUniformBuffer(render::ShapePipeline::Slot::LIGHTING_MODEL, lightingModel->getParametersBuffer());

        deferredLightingEffect->setupLocalLightsBatch(batch, 
                                                      render::ShapePipeline::Slot::LIGHT_CLUSTER_GRID_CLUSTER_GRID_SLOT,
                                                      render::ShapePipeline::Slot::LIGHT_CLUSTER_GRID_CLUSTER_CONTENT_SLOT,
                                                      render::ShapePipeline::Slot::LIGHT_CLUSTER_GRID_FRUSTUM_GRID_SLOT,
                                                      lightClusters);

        // Setup haze if current zone has haze
        auto hazeStage = args->_scene->getStage<HazeStage>();
        if (hazeStage && hazeStage->_currentFrame._hazes.size() > 0) {
            graphics::HazePointer hazePointer = hazeStage->getHaze(hazeStage->_currentFrame._hazes.front());
            if (hazePointer) {
                batch.setUniformBuffer(render::ShapePipeline::Slot::HAZE_MODEL, hazePointer->getHazeParametersBuffer());
            }
        }

        // From the lighting model define a global shapKey ORED with individiual keys
        ShapeKey::Builder keyBuilder;
        if (lightingModel->isWireframeEnabled()) {
            keyBuilder.withWireframe();
        }

        ShapeKey globalKey = keyBuilder.build();
        args->_globalShapeKey = globalKey._flags.to_ulong();

        renderShapes(renderContext, _shapePlumber, inItems, _maxDrawn, globalKey);

        args->_batch = nullptr;
        args->_globalShapeKey = 0;

        deferredLightingEffect->unsetLocalLightsBatch(batch,
                                                      render::ShapePipeline::Slot::LIGHT_CLUSTER_GRID_CLUSTER_GRID_SLOT,
                                                      render::ShapePipeline::Slot::LIGHT_CLUSTER_GRID_CLUSTER_CONTENT_SLOT,
                                                      render::ShapePipeline::Slot::LIGHT_CLUSTER_GRID_FRUSTUM_GRID_SLOT);
    });

    config->setNumDrawn((int)inItems.size());
}

void DrawStateSortDeferred::run(const RenderContextPointer& renderContext, const Inputs& inputs) {
    assert(renderContext->args);
    assert(renderContext->args->hasViewFrustum());

    auto config = std::static_pointer_cast<Config>(renderContext->jobConfig);

    const auto& inItems = inputs.get0();
    const auto& lightingModel = inputs.get1();

    RenderArgs* args = renderContext->args;

    gpu::doInBatch("DrawStateSortDeferred::run", args->_context, [&](gpu::Batch& batch) {
        args->_batch = &batch;

        // Setup camera, projection and viewport for all items
        batch.setViewportTransform(args->_viewport);
        batch.setStateScissorRect(args->_viewport);

        glm::mat4 projMat;
        Transform viewMat;
        args->getViewFrustum().evalProjectionMatrix(projMat);
        args->getViewFrustum().evalViewTransform(viewMat);

        batch.setProjectionTransform(projMat);
        batch.setViewTransform(viewMat);

        // Setup lighting model for all items;
        batch.setUniformBuffer(render::ShapePipeline::Slot::LIGHTING_MODEL, lightingModel->getParametersBuffer());

        // From the lighting model define a global shapeKey ORED with individiual keys
        ShapeKey::Builder keyBuilder;
        if (lightingModel->isWireframeEnabled()) {
            keyBuilder.withWireframe();
        }

        ShapeKey globalKey = keyBuilder.build();
        args->_globalShapeKey = globalKey._flags.to_ulong();

        if (_stateSort) {
            renderStateSortShapes(renderContext, _shapePlumber, inItems, _maxDrawn, globalKey);
        } else {
            renderShapes(renderContext, _shapePlumber, inItems, _maxDrawn, globalKey);
        }
        args->_batch = nullptr;
        args->_globalShapeKey = 0;
    });

    config->setNumDrawn((int)inItems.size());
}
