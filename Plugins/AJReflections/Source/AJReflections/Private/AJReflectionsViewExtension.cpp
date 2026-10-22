#include "AJReflectionsViewExtension.h"

#include "AJDebugShader.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"
#include "ScreenPass.h"

FAJReflectionsViewExtension::FAJReflectionsViewExtension(
    const FAutoRegister& AutoRegister)
    : FSceneViewExtensionBase(AutoRegister)
{
}

void FAJReflectionsViewExtension::SetupViewFamily(
    FSceneViewFamily& InViewFamily)
{
}

void FAJReflectionsViewExtension::SetupView(
    FSceneViewFamily& InViewFamily,
    FSceneView& InView)
{
}

void FAJReflectionsViewExtension::BeginRenderViewFamily(
    FSceneViewFamily& InViewFamily)
{
}

bool FAJReflectionsViewExtension::IsActiveThisFrame_Internal(
    const FSceneViewExtensionContext& Context) const
{
    return true;
}

void FAJReflectionsViewExtension::SubscribeToPostProcessingPass(
    EPostProcessingPass Pass,
    const FSceneView& InView,
    FPostProcessingPassDelegateArray& InOutPassCallbacks,
    bool bIsPassEnabled)
{
    (void)bIsPassEnabled;

    if (Pass != EPostProcessingPass::AfterDOF)
    {
        return;
    }

    if (InView.Family == nullptr ||
        InView.Family->Scene == nullptr)
    {
        return;
    }

    InOutPassCallbacks.Add(
        FPostProcessingPassDelegate::CreateRaw(
            this,
            &FAJReflectionsViewExtension::
            PostProcessAfterDOF_RenderThread));
}

FScreenPassTexture
FAJReflectionsViewExtension::PostProcessAfterDOF_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs)
{
    const FScreenPassTextureSlice SceneColor =
        Inputs.GetInput(EPostProcessMaterialInput::SceneColor);

    if (!SceneColor.IsValid())
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder);
    }

    // This project targets the deferred renderer. The shader below consumes
    // FSceneTextureUniformParameters via SceneTexturesStruct.
    if (!Inputs.SceneTextures.SceneTextures)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder);
    }

    const FIntPoint ViewSize =
        SceneColor.ViewRect.Size();

    const FIntPoint ViewRectMin =
        SceneColor.ViewRect.Min;

    const FScreenPassTexture DebugOutput =
        AddAJDebugPass(
            GraphBuilder,
            View,
            ViewSize,
            ViewRectMin,
            Inputs.SceneTextures);

    if (!DebugOutput.IsValid())
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder);
    }

    if (Inputs.OverrideOutput.IsValid())
    {
        AddDrawTexturePass(
            GraphBuilder,
            FScreenPassViewInfo(View),
            DebugOutput,
            Inputs.OverrideOutput,
            View.GetMultiViewCount());

        return FScreenPassTexture(
            Inputs.OverrideOutput.Texture,
            Inputs.OverrideOutput.ViewRect);
    }

    return DebugOutput;
}