// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJRendererExtension.h"
#include "AJReflectionTrace.h"
#include "AJDebugShader.h"
#include "AJMaterialHitRayTracing.h"
#include "AJReflectionCompose.h"
#include "AJTemporalInputs.h"
#include "AJHistoryReprojection.h"

#include "RenderUtils.h"
#include "RHI.h"
#include "SceneView.h"

bool FAJRendererExtension::IsEnabledForView(const FSceneView& View) const
{
#if RHI_RAYTRACING
    return IsAJReflectionTracingEnabled_AnyThread()
        && View.IsRayTracingAllowedForView()
        && IsRayTracingEnabled(View.GetShaderPlatform())
        && GRHISupportsRayTracingShaders;
#else
    (void)View;
    return false;
#endif
}

#if RHI_RAYTRACING
void FAJRendererExtension::GatherMaterialRayGenShaders(
    const FSceneView& View,
    TArray<FRHIRayTracingShader*>& OutRayGenShaders) const
{
    if (IsEnabledForView(View))
    {
        GatherAJMaterialRayGenShader(View, OutRayGenShaders);
    }
}

void FAJRendererExtension::RenderOpaqueReflections(
    const FOpaqueReflectionRenderContext& Context) const
{
    if (!IsEnabledForView(Context.View))
    {
        return;
    }

    // Begin previous-frame ownership BEFORE queuing current extraction.
    // Even in DebugMode 0, the semantic history is produced each frame.
    const bool bBackendAvailable = IsAJMaterialHitBackendEnabled_AnyThread();
    FAJPreviousReflectionHistory Previous = History.BeginViewHistory(
        Context.GraphBuilder, Context.View, Context.ViewRect, bBackendAvailable);

    const FAJMaterialHitOutputs Outputs = AddAJMaterialHitPass(Context);
    if (!Outputs.HasRadiance())
    {
        // No current semantic sample means no valid history for this view.
        History.InvalidateViewHistory(Context.View);
        Previous = FAJPreviousReflectionHistory{};
        Previous.ResetReason = bBackendAvailable
            ? EAJReflectionHistoryResetReason::MissingResource
            : EAJReflectionHistoryResetReason::BackendUnavailable;
    }

    // Deterministic primary surface + motion: always produced for a valid
    // material backend, independent of DebugMode or previous-history validity.
    FAJTemporalInputs Temporal;
    FAJHistoryReprojectionOutputs Reprojected;
    if (Outputs.HasRadiance())
    {
        Temporal = AddAJTemporalInputsPass(Context);
        if (Temporal.IsValid())
        {
            // This never writes current ReflectionRadiance or mixes two frames.
            Reprojected = AddAJHistoryReprojectionPass(Context, Outputs, Temporal, Previous);
        }
        else
        {
            History.InvalidateViewHistory(Context.View);
            Previous = FAJPreviousReflectionHistory{};
            Previous.ResetReason = EAJReflectionHistoryResetReason::MissingResource;
        }
    }

    const int32 DebugMode = GetAJDebugMode_AnyThread();
    if (DebugMode >= 16 && DebugMode <= 34)
    {
        AddAJReflectionComposePass(Context, Outputs, Previous, Reprojected, DebugMode);
    }

    // Old resources were registered before scheduling all four new extractions.
    // Each destination is a field in a stable, separately allocated entry.
    if (Outputs.HasRadiance() && Temporal.IsValid())
    {
        History.CommitViewHistory(
            Context.GraphBuilder, Context.View, Context.ViewRect,
            Outputs.ReflectionRadiance, Outputs.ReflectionRadianceFlags,
            Temporal.Surface0, Temporal.Surface1);
    }
}
#endif
