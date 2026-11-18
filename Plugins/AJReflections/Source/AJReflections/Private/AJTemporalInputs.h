// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RenderGraphFwd.h"

struct FOpaqueReflectionRenderContext;

/** Compact, deterministic primary-surface metadata and current-frame motion.
 *  All references are graph-local and must not survive FRDGBuilder::Execute(). */
struct FAJTemporalInputs
{
    FRDGTextureRef Surface0 = nullptr; // RGBA32F: world normal / linear ViewZ.
    FRDGTextureRef Surface1 = nullptr; // RGBA16F: top visible F0 / roughness.
    FRDGTextureRef MotionData = nullptr; // RGBA32F: prev local pixel center, prev ViewZ, motion class.

    bool IsValid() const { return Surface0 && Surface1 && MotionData; }
};

FAJTemporalInputs AddAJTemporalInputsPass(const FOpaqueReflectionRenderContext& Context);
