// Copyright Epic Games, Inc. All Rights Reserved.

#include "AJDebugShader.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

namespace
{
    TAutoConsoleVariable<int32> CVarAJReflectionsDebugMode(
        TEXT("r.AJReflections.DebugMode"),
        1,
        TEXT("Selects the AJReflections debug visualization.\n")
        TEXT("0: UV gradient\n")
        TEXT("1: Raw device depth\n")
        TEXT("2: Linear ViewZ\n")
        TEXT("3: World normal\n")
        TEXT("4: Roughness\n")
        TEXT("5: Scene velocity\n")
        TEXT("6: Scene color\n")
        TEXT("7: Scene color without pre-exposure\n"),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<float> CVarAJReflectionsDebugViewZMax(
        TEXT("r.AJReflections.DebugViewZMax"),
        10000.0f,
        TEXT("Maximum linear ViewZ mapped to white in DebugMode 2, ")
        TEXT("in Unreal world units.\n"),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<float> CVarAJReflectionsDebugVelocityScale(
        TEXT("r.AJReflections.DebugVelocityScale"),
        20.0f,
        TEXT("Display amplification for decoded scene velocity in ")
        TEXT("DebugMode 5.\n")
        TEXT("This affects visualization only and does not modify ")
        TEXT("the renderer velocity semantic.\n"),
        ECVF_RenderThreadSafe);
}

class FAJDebugCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FAJDebugCS);
    SHADER_USE_PARAMETER_STRUCT(FAJDebugCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(FIntPoint, ViewRectMin)
        SHADER_PARAMETER(int32, DebugMode)
        SHADER_PARAMETER(float, DebugViewZMax)
        SHADER_PARAMETER(float, DebugVelocityScale)

        SHADER_PARAMETER_STRUCT_REF(
            FViewUniformShaderParameters,
            View)

        SHADER_PARAMETER_STRUCT_INCLUDE(
            FSceneTextureShaderParameters,
            SceneTextures)

        SHADER_PARAMETER_RDG_TEXTURE_SRV(
            Texture2D,
            SceneColorTexture)

        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float4>,
            OutputTexture)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(
    FAJDebugCS,
    "/AJReflections/Private/AJDebug.usf",
    "MainCS",
    SF_Compute);

FScreenPassTexture AddAJDebugPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FScreenPassTextureSlice& SceneColor,
    const FSceneTextureShaderParameters& SceneTextures)
{
    if (!SceneColor.IsValid())
    {
        return {};
    }

    const FIntPoint ViewSize =
        SceneColor.ViewRect.Size();

    const FIntPoint ViewRectMin =
        SceneColor.ViewRect.Min;

    if (ViewSize.X <= 0 || ViewSize.Y <= 0)
    {
        return {};
    }

    // The debug output remains compact and view-local. Renderer scene textures
    // and callback SceneColor use their original texture coordinates, supplied
    // through the callback SceneColor ViewRect.
    const FRDGTextureDesc OutputDesc =
        FRDGTextureDesc::Create2D(
            ViewSize,
            PF_FloatRGBA,
            FClearValueBinding::None,
            TexCreate_ShaderResource | TexCreate_UAV);

    FRDGTextureRef OutputTexture =
        GraphBuilder.CreateTexture(
            OutputDesc,
            TEXT("AJReflections.DebugOutput"));

    FAJDebugCS::FParameters* PassParameters =
        GraphBuilder.AllocParameters<FAJDebugCS::FParameters>();

    PassParameters->OutputSize = ViewSize;
    PassParameters->ViewRectMin = ViewRectMin;

    PassParameters->DebugMode =
        CVarAJReflectionsDebugMode.GetValueOnRenderThread();

    PassParameters->DebugViewZMax =
        FMath::Max(
            CVarAJReflectionsDebugViewZMax.GetValueOnRenderThread(),
            1.0f);

    PassParameters->DebugVelocityScale =
        FMath::Max(
            CVarAJReflectionsDebugVelocityScale.GetValueOnRenderThread(),
            0.0f);

    PassParameters->View =
        View.ViewUniformBuffer;

    PassParameters->SceneTextures = SceneTextures;

    PassParameters->SceneColorTexture =
        SceneColor.TextureSRV;

    PassParameters->OutputTexture =
        GraphBuilder.CreateUAV(
            FRDGTextureUAVDesc(OutputTexture));

    const TShaderMapRef<FAJDebugCS> ComputeShader(
        GetGlobalShaderMap(View.GetShaderPlatform()));

    const FIntVector GroupCount =
        FComputeShaderUtils::GetGroupCount(
            ViewSize,
            FIntPoint(8, 8));

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("AJReflections.Debug"),
        ComputeShader,
        PassParameters,
        GroupCount);

    return FScreenPassTexture(
        OutputTexture,
        FIntRect(
            FIntPoint::ZeroValue,
            ViewSize));
}
