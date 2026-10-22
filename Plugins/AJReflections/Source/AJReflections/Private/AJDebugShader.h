#pragma once

#include "ScreenPass.h"

class FSceneView;
class FRDGBuilder;
class FSceneTextureShaderParameters;

FScreenPassTexture AddAJDebugPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FIntPoint ViewSize,
    FIntPoint ViewRectMin,
    const FSceneTextureShaderParameters& SceneTextures);
