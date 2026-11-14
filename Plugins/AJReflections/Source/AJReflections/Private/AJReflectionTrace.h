#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FRDGBuilder;
class FSceneView;
class FSceneTextureShaderParameters;

// Per-callback RDG resources. Never cache these across graphs or frames.
struct FAJReflectionTraceOutputs
{
    // Existing Week 3A material-independent intersection ABI.
    FRDGTextureRef TraceData = nullptr;
    FRDGTextureRef TraceFlags = nullptr;

    // Week 3C independent, current-graph sampling diagnostics.
    FRDGTextureRef SampleData = nullptr;
    FRDGTextureRef SampleFlags = nullptr;

    bool IsValid() const
    {
        return TraceData != nullptr && TraceFlags != nullptr
            && SampleData != nullptr && SampleFlags != nullptr;
    }
};

bool IsAJReflectionTracingEnabled_AnyThread();
float GetAJReflectionMaxTraceDistance_RenderThread();
bool GetAJReflectionStochasticSampling_RenderThread();
int32 GetAJReflectionSampleFrameOverride_RenderThread();
bool CanAJReflectionTrace_RenderThread(const FSceneView& View);

FAJReflectionTraceOutputs AddAJReflectionTracePass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FIntRect SceneColorViewRect,
    const FSceneTextureShaderParameters& SceneTextures,
    FRDGBufferSRVRef TLAS);
