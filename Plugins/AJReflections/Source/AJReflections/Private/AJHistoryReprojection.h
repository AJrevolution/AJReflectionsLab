// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RenderGraphFwd.h"

struct FOpaqueReflectionRenderContext;
struct FAJMaterialHitOutputs;
struct FAJTemporalInputs;
struct FAJPreviousReflectionHistory;

/** Graph-local result: a single accepted prior-frame sample, never accumulated. */
struct FAJHistoryReprojectionOutputs
{
    FRDGTextureRef ReprojectedRadiance = nullptr;      // RGBA32F; previous alpha kept raw.
    FRDGTextureRef ReprojectedHistoryFlags = nullptr;  // R32_UINT; previous Week4B bits.
    FRDGTextureRef HistoryAcceptance = nullptr;        // R32_FLOAT; binary 0/1.
    FRDGTextureRef HistoryRejectionFlags = nullptr;    // R32_UINT; Week5B reasons.
    FRDGTextureRef HistoryConsistency = nullptr;       // RGBA32F; diagnostic metrics only.

    bool IsValid() const
    {
        return ReprojectedRadiance && ReprojectedHistoryFlags
            && HistoryAcceptance && HistoryRejectionFlags && HistoryConsistency;
    }
};

/** Render-thread snapshot of the four development CVar values. */
struct FAJHistoryReprojectionThresholds
{
    float Depth = 0.02f;
    float Normal = 0.90f;
    float Roughness = 0.12f;
    float F0 = 0.12f;
};

FAJHistoryReprojectionThresholds GetAJHistoryReprojectionThresholds_RenderThread();

FAJHistoryReprojectionOutputs AddAJHistoryReprojectionPass(
    const FOpaqueReflectionRenderContext& Context,
    const FAJMaterialHitOutputs& Current,
    const FAJTemporalInputs& Temporal,
    const FAJPreviousReflectionHistory& Previous);
