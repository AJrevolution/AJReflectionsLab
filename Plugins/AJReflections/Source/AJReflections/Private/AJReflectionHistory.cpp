// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJReflectionHistory.h"

#include "CoreGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "SceneManagement.h"

namespace
{
    // Hundreds of consecutive render frames without a view: conservative pool reclamation.
    // Never evict an entry queued by the currently constructed render graph.
    constexpr uint32 HistoryStaleFrameThreshold = 180;

    bool IsUsablePreExposure(float PreExposure)
    {
        return FMath::IsFinite(PreExposure) && PreExposure > UE_SMALL_NUMBER;
    }

    bool IsCurrentHistoryTexture(FRDGTextureRef Texture, const FIntPoint Extent, EPixelFormat Format)
    {
        return Texture && Texture->Desc.Extent == Extent && Texture->Desc.Format == Format;
    }
}

struct FAJReflectionHistory::FEntry
{
    // Address-stable because each entry is separately heap-allocated by MakeShared.
    // QueueTextureExtraction writes these fields only at end-of-graph execution.
    TRefCountPtr<IPooledRenderTarget> Radiance;
    TRefCountPtr<IPooledRenderTarget> Flags;
    float PreExposure = 1.0f;
    FIntRect ViewRect;
    uint32 LastUpdatedFrame = MAX_uint32;
    uint32 LastSeenFrame = MAX_uint32;
    bool bSeeded = false;

    void Clear()
    {
        Radiance.SafeRelease();
        Flags.SafeRelease();
        PreExposure = 1.0f;
        LastUpdatedFrame = MAX_uint32;
        bSeeded = false;
    }
};

FAJReflectionHistory::~FAJReflectionHistory()
{
    // The owner unregisters its renderer extension and flushes rendering commands
    // before destruction, so no executing RDG can still target these fields.
    Entries.Empty();
}

bool FAJReflectionHistory::TryMakeKey(const FSceneView& View, FAJReflectionHistoryKey& OutKey)
{
    if (!View.State)
    {
        return false;
    }
    const uint32 PublicViewKey = View.GetViewKey();
    if (PublicViewKey == 0)
    {
        return false;
    }
    OutKey.ViewKey = PublicViewKey;
    // If two eyes share a persistent state key, their history is still isolated.
    OutKey.StereoViewIndex = View.StereoViewIndex;
    return true;
}

void FAJReflectionHistory::ReclaimStaleEntries(uint32 FrameNumber)
{
    check(IsInRenderingThread());
    for (auto It = Entries.CreateIterator(); It; ++It)
    {
        const TSharedPtr<FEntry, ESPMode::ThreadSafe>& Entry = It.Value();
        if (Entry->LastSeenFrame != MAX_uint32
            && uint32(FrameNumber - Entry->LastSeenFrame) > HistoryStaleFrameThreshold
            && Entry->LastUpdatedFrame != FrameNumber)
        {
            // More than 180 frames have elapsed since this entry participated.
            // The original graph has executed long before releasing this memory.
            It.RemoveCurrent();
        }
    }
}

FAJPreviousReflectionHistory FAJReflectionHistory::BeginViewHistory(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FIntRect& ViewRect,
    bool bMaterialBackendAvailable)
{
    check(IsInRenderingThread());
    const uint32 FrameNumber = GFrameNumberRenderThread;
    ReclaimStaleEntries(FrameNumber);

    FAJPreviousReflectionHistory Previous;
    FAJReflectionHistoryKey Key;
    if (!TryMakeKey(View, Key))
    {
        Previous.ResetReason = EAJReflectionHistoryResetReason::NoViewState;
        return Previous;
    }

    TSharedPtr<FEntry, ESPMode::ThreadSafe>& EntrySlot = Entries.FindOrAdd(Key);
    if (!EntrySlot.IsValid())
    {
        EntrySlot = MakeShared<FEntry, ESPMode::ThreadSafe>();
    }
    const TSharedPtr<FEntry, ESPMode::ThreadSafe> Entry = EntrySlot;
    Entry->LastSeenFrame = FrameNumber;

    if (!bMaterialBackendAvailable)
    {
        Entry->Clear();
        Previous.ResetReason = EAJReflectionHistoryResetReason::BackendUnavailable;
        return Previous;
    }
    if (!Entry->bSeeded)
    {
        // A newly created state always reports FirstFrame, even when Unreal
        // also flags its first frame as a camera cut.
        Previous.ResetReason = EAJReflectionHistoryResetReason::FirstFrame;
        return Previous;
    }
    if (View.bCameraCut)
    {
        Entry->Clear();
        Previous.ResetReason = EAJReflectionHistoryResetReason::CameraCut;
        return Previous;
    }
    if (Entry->ViewRect != ViewRect)
    {
        Entry->Clear();
        Previous.ResetReason = EAJReflectionHistoryResetReason::ViewRectChanged;
        return Previous;
    }
    if (Entry->LastUpdatedFrame == FrameNumber)
    {
        // One view was submitted again within the same render-thread frame.
        // Never import its as-yet unfilled extraction destination.
        Previous.ResetReason = EAJReflectionHistoryResetReason::RepeatedFrame;
        return Previous;
    }
    if (uint32(FrameNumber - Entry->LastUpdatedFrame) != 1u)
    {
        Entry->Clear();
        Previous.ResetReason = EAJReflectionHistoryResetReason::FrameGap;
        return Previous;
    }
    if (!Entry->Radiance.IsValid() || !Entry->Flags.IsValid()
        || !IsUsablePreExposure(Entry->PreExposure))
    {
        Entry->Clear();
        Previous.ResetReason = EAJReflectionHistoryResetReason::MissingResource;
        return Previous;
    }

    // Copy ref-counted OLD resources before QueueTextureExtraction later replaces
    // the stable per-entry destinations with this frame's newly extracted outputs.
    const TRefCountPtr<IPooledRenderTarget> PreviousRadiancePooled = Entry->Radiance;
    const TRefCountPtr<IPooledRenderTarget> PreviousFlagsPooled = Entry->Flags;
    Previous.Radiance = GraphBuilder.RegisterExternalTexture(
        PreviousRadiancePooled, TEXT("AJReflections.History.Radiance"));
    Previous.Flags = GraphBuilder.RegisterExternalTexture(
        PreviousFlagsPooled, TEXT("AJReflections.History.Flags"));
    Previous.PreviousPreExposure = Entry->PreExposure;
    Previous.bValid = true;
    Previous.ResetReason = EAJReflectionHistoryResetReason::Valid;
    return Previous;
}

void FAJReflectionHistory::InvalidateViewHistory(const FSceneView& View)
{
    check(IsInRenderingThread());
    FAJReflectionHistoryKey Key;
    if (TryMakeKey(View, Key))
    {
        if (TSharedPtr<FEntry, ESPMode::ThreadSafe>* Slot = Entries.Find(Key))
        {
            (*Slot)->Clear();
            (*Slot)->LastSeenFrame = GFrameNumberRenderThread;
        }
    }
}

void FAJReflectionHistory::CommitViewHistory(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FIntRect& ViewRect,
    FRDGTextureRef CurrentReflectionRadiance,
    FRDGTextureRef CurrentReflectionRadianceFlags)
{
    check(IsInRenderingThread());
    FAJReflectionHistoryKey Key;
    if (!TryMakeKey(View, Key))
    {
        return; // Stateless views render normally, but NEVER share history.
    }

    const FIntPoint Extent = ViewRect.Size();
    if (!IsCurrentHistoryTexture(CurrentReflectionRadiance, Extent, PF_A32B32G32R32F)
        || !IsCurrentHistoryTexture(CurrentReflectionRadianceFlags, Extent, PF_R32_UINT)
        || !IsUsablePreExposure(View.State->GetPreExposure()))
    {
        InvalidateViewHistory(View);
        return;
    }

    // BeginViewHistory always creates this stable entry for stateful views.
    const TSharedPtr<FEntry, ESPMode::ThreadSafe>* Slot = Entries.Find(Key);
    check(Slot && Slot->IsValid());
    FEntry& Entry = **Slot;
    const uint32 FrameNumber = GFrameNumberRenderThread;
    if (Entry.bSeeded && Entry.LastUpdatedFrame == FrameNumber)
    {
        return; // Do not queue multiple extractions to the same destination.
    }

    // The previous pooled targets are already imported into this graph; RDG
    // owns reference-counted allocations until graph completion. UE's
    // QueueTextureExtraction clears each destination immediately, then fills
    // that stable destination with the new pooled reference at graph execution.
    GraphBuilder.QueueTextureExtraction(CurrentReflectionRadiance, &Entry.Radiance);
    GraphBuilder.QueueTextureExtraction(CurrentReflectionRadianceFlags, &Entry.Flags);
    Entry.PreExposure = View.State->GetPreExposure();
    Entry.ViewRect = ViewRect;
    Entry.LastUpdatedFrame = FrameNumber;
    Entry.LastSeenFrame = FrameNumber;
    Entry.bSeeded = true;
}
