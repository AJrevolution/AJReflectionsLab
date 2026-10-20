#pragma once

#include "SceneViewExtension.h"
#include "ScreenPass.h"

struct FPostProcessMaterialInputs;

class FAJReflectionsViewExtension final
    : public FSceneViewExtensionBase
{
public:
    explicit FAJReflectionsViewExtension(
        const FAutoRegister& AutoRegister);

    virtual void SetupViewFamily(
        FSceneViewFamily& InViewFamily) override;

    virtual void SetupView(
        FSceneViewFamily& InViewFamily,
        FSceneView& InView) override;

    virtual void BeginRenderViewFamily(
        FSceneViewFamily& InViewFamily) override;

    virtual bool IsActiveThisFrame_Internal(
        const FSceneViewExtensionContext& Context) const override;

    virtual void SubscribeToPostProcessingPass(
        EPostProcessingPass Pass,
        const FSceneView& InView,
        FPostProcessingPassDelegateArray& InOutPassCallbacks,
        bool bIsPassEnabled) override;

private:
    FScreenPassTexture PostProcessAfterDOF_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs);
};
