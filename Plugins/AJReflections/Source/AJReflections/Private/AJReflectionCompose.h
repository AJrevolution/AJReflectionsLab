// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

struct FOpaqueReflectionRenderContext;
struct FAJMaterialHitOutputs;
struct FAJPreviousReflectionHistory;
struct FAJHistoryReprojectionOutputs;


/** Current opaque-stage composition entry point (future HDR radiance extension). */
void AddAJReflectionComposePass(
    const FOpaqueReflectionRenderContext& Context,
    const FAJMaterialHitOutputs& MaterialHit,
    const FAJPreviousReflectionHistory& Previous,
    const FAJHistoryReprojectionOutputs& Reprojected,
    int32 DebugMode);
