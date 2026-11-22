// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

struct FOpaqueReflectionRenderContext;
struct FAJMaterialHitOutputs;
struct FAJPreviousReflectionHistory;
struct FAJHistoryReprojectionOutputs;
struct FAJTemporalAccumulationOutputs;

/** Opaque-stage diagnostics and source + destination HDR presentation. */
int32 GetAJReflectionPresentationMode_RenderThread();

void AddAJReflectionComposePass(
    const FOpaqueReflectionRenderContext& Context,
    const FAJMaterialHitOutputs& MaterialHit,
    const FAJPreviousReflectionHistory& Previous,
    const FAJHistoryReprojectionOutputs& Reprojected,
    const FAJTemporalAccumulationOutputs& Accumulated,
    int32 DebugMode,
    int32 PresentationMode);
