// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RenderGraphFwd.h"

struct FOpaqueReflectionRenderContext;
struct FAJMaterialHitOutputs;
struct FAJHistoryReprojectionOutputs;

/** Current-frame, graph-local bounded temporal estimator (never a cross-graph RDG pointer). */
struct FAJTemporalAccumulationOutputs
{
    FRDGTextureRef AccumulatedRadiance = nullptr;       // PF_A32B32G32R32F; RGB HDR, A reserved 0.
    FRDGTextureRef AccumulatedHistoryLength = nullptr;  // PF_R32_UINT; effective bounded length.
    FRDGTextureRef TemporalCurrentWeight = nullptr;    // PF_R32_FLOAT; diagnostic only.

    bool IsValid() const
    {
        return AccumulatedRadiance && AccumulatedHistoryLength && TemporalCurrentWeight;
    }
};

/** Render-thread CVar snapshots used by this pass and the existing composer. */
int32 GetAJTemporalMaxHistoryFrames_RenderThread();
bool IsAJTemporalAccumulationEnabled_RenderThread();

/** Always produces current estimator outputs when current material radiance exists. */
FAJTemporalAccumulationOutputs AddAJTemporalAccumulationPass(
    const FOpaqueReflectionRenderContext& Context,
    const FAJMaterialHitOutputs& Current,
    const FAJHistoryReprojectionOutputs& Reprojected);
