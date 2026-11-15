// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

struct FOpaqueReflectionRenderContext;
class FSceneView;
class FRHIRayTracingShader;

/** Semantic, graph-local secondary material diagnostics (never extracted). */
struct FAJMaterialHitOutputs
{
    FRDGTextureRef MaterialHitData0 = nullptr;
    FRDGTextureRef MaterialHitData1 = nullptr;
    FRDGTextureRef MaterialHitFlags = nullptr;

    // Week 4B semantic outputs; all remain graph-local, never extracted.
    FRDGTextureRef PrimaryTransport = nullptr;
    FRDGTextureRef ReflectionRadiance = nullptr;
    FRDGTextureRef SecondaryEmissive = nullptr;
    FRDGTextureRef SecondaryDirect = nullptr;
    FRDGTextureRef ReflectionRadianceFlags = nullptr;

    bool HasRadiance() const
    {
        return PrimaryTransport && ReflectionRadiance && SecondaryEmissive
            && SecondaryDirect && ReflectionRadianceFlags;
    }

    bool IsValid() const
    {
        return MaterialHitData0 && MaterialHitData1 && MaterialHitFlags;
    }
};

bool IsAJMaterialHitBackendEnabled_AnyThread();

#if RHI_RAYTRACING
/** Called by Renderer before creating its native material RT pipeline. */
void GatherAJMaterialRayGenShader(
    const FSceneView& View, TArray<FRHIRayTracingShader*>& OutShaders);

/** Adds primary sampling and a native material-RayGen pass to the current RDG. */
FAJMaterialHitOutputs AddAJMaterialHitPass(const FOpaqueReflectionRenderContext& Context);
#endif
