// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OpaqueReflectionExtension.h"

/** Plugin-owned implementation of the generic renderer reflection extension. */
class FAJRendererExtension final : public IRendererOpaqueReflectionExtension
{
public:
    virtual bool IsEnabledForView(const FSceneView& View) const override;

#if RHI_RAYTRACING
    virtual void GatherMaterialRayGenShaders(
        const FSceneView& View,
        TArray<FRHIRayTracingShader*>& OutRayGenShaders) const override;

    virtual void RenderOpaqueReflections(
        const FOpaqueReflectionRenderContext& Context) const override;
#endif
};
