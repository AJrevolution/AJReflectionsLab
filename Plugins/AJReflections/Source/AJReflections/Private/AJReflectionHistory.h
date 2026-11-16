// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "Templates/SharedPointer.h"

class FSceneView;
class FRDGBuilder;

// Diagnostic CPU metadata, not a shader resource or a persistent radiance ABI.
// Keep in sync with AJReflectionCompose.usf's mode-27 palette.
enum class EAJReflectionHistoryResetReason : uint32
{
    Valid = 0,
    NoViewState = 1,
    FirstFrame = 2,
    CameraCut = 3,
    ViewRectChanged = 4,
    FrameGap = 5,
    BackendUnavailable = 6,
    MissingResource = 7,
    RepeatedFrame = 8
};

/** Graph-scoped read-only previous sample; NEVER retain FRDGTextureRef in the manager. */
struct FAJPreviousReflectionHistory
{
    FRDGTextureRef Radiance = nullptr;
    FRDGTextureRef Flags = nullptr;
    float PreviousPreExposure = 1.0f;
    bool bValid = false;
    EAJReflectionHistoryResetReason ResetReason = EAJReflectionHistoryResetReason::FirstFrame;
};

/** Public FSceneView::GetViewKey plus stereo discriminator: no private renderer cast. */
struct FAJReflectionHistoryKey
{
    uint32 ViewKey = 0;
    int32 StereoViewIndex = INDEX_NONE;

    bool operator==(const FAJReflectionHistoryKey& Other) const
    {
        return ViewKey == Other.ViewKey && StereoViewIndex == Other.StereoViewIndex;
    }

    friend uint32 GetTypeHash(const FAJReflectionHistoryKey& Key)
    {
        return HashCombine(::GetTypeHash(Key.ViewKey), ::GetTypeHash(Key.StereoViewIndex));
    }
};

/** Render-thread-only, renderer-extension-owned persistent GPU history. */
class FAJReflectionHistory final
{
public:
    FAJReflectionHistory() = default;
    ~FAJReflectionHistory();

    FAJReflectionHistory(const FAJReflectionHistory&) = delete;
    FAJReflectionHistory& operator=(const FAJReflectionHistory&) = delete;

    FAJPreviousReflectionHistory BeginViewHistory(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FIntRect& ViewRect,
        bool bMaterialBackendAvailable);

    void CommitViewHistory(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FIntRect& ViewRect,
        FRDGTextureRef CurrentReflectionRadiance,
        FRDGTextureRef CurrentReflectionRadianceFlags);

    // Called when material output cannot be produced. No cached resource survives.
    void InvalidateViewHistory(const FSceneView& View);

private:
    struct FEntry;
    TMap<FAJReflectionHistoryKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>> Entries;

    static bool TryMakeKey(const FSceneView& View, FAJReflectionHistoryKey& OutKey);
    void ReclaimStaleEntries(uint32 FrameNumber);
};
