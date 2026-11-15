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

    // Feature runs independent of DebugMode. The backend toggle only affects
    // the programmable material pass; inline diagnostic backend is untouched.
    const FAJMaterialHitOutputs Outputs = AddAJMaterialHitPass(Context);
    const int32 DebugMode = GetAJDebugMode_AnyThread();
    if (DebugMode >= 16 && DebugMode <= 23)
    {
        AddAJReflectionComposePass(Context, Outputs, DebugMode);
    }
}
#endif
