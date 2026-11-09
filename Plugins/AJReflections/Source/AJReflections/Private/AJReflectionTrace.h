#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FRDGBuilder;
class FSceneView;
class FSceneTextureShaderParameters;

// Per-callback RDG resources. Never cache these across graphs or frames.
struct FAJReflectionTraceOutputs
{
    FRDGTextureRef TraceData = nullptr;
    FRDGTextureRef TraceFlags = nullptr;

    bool IsValid() const
    {
        return TraceData != nullptr && TraceFlags != nullptr;
    }
};

bool IsAJReflectionTracingEnabled_AnyThread();
float GetAJReflectionMaxTraceDistance_RenderThread();
bool CanAJReflectionTrace_RenderThread(const FSceneView& View);

FAJReflectionTraceOutputs AddAJReflectionTracePass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FIntRect SceneColorViewRect,
    const FSceneTextureShaderParameters& SceneTextures,
    FRDGBufferSRVRef TLAS);
