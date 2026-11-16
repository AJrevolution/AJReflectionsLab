// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJRendererExtension.h"
#include "AJReflectionTrace.h"
#include "AJDebugShader.h"
#include "AJMaterialHitRayTracing.h"
#include "AJReflectionCompose.h"

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

    const int32 DebugMode = GetAJDebugMode_AnyThread();
    if (DebugMode >= 16 && DebugMode <= 27)
    {
        AddAJReflectionComposePass(Context, Outputs, Previous, DebugMode);
    }

    // Queue extraction only AFTER previous RDG inputs and presentation are set.
    // Destination slots live in stable shared-allocated history entries.
    if (Outputs.HasRadiance())
    {
        History.CommitViewHistory(
            Context.GraphBuilder, Context.View, Context.ViewRect,
            Outputs.ReflectionRadiance, Outputs.ReflectionRadianceFlags);
    }
}
#endif
