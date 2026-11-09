#pragma once

#include "ScreenPass.h"

class FSceneView;
class FRDGBuilder;
class FSceneTextureShaderParameters;
struct FAJReflectionTraceOutputs;

int32 GetAJDebugMode_AnyThread();

FScreenPassTexture AddAJDebugPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FScreenPassTextureSlice& SceneColor,
    const FSceneTextureShaderParameters& SceneTextures,
    const FAJReflectionTraceOutputs* TraceOutputs);
