#pragma once

#include "ScreenPass.h"

class FSceneView;
class FRDGBuilder;

FScreenPassTexture AddAJDebugPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FIntPoint ViewSize);