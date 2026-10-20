// Copyright Epic Games, Inc. All Rights Reserved.

#include "AJDebugShader.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

class FAJDebugCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FAJDebugCS);
    SHADER_USE_PARAMETER_STRUCT(FAJDebugCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)
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
    FIntPoint ViewSize)
{
    if (ViewSize.X <= 0 || ViewSize.Y <= 0)
    {
        return {};
    }

    // Compact per-view output: shader coordinates are local 0..ViewSize-1.
    // Future renderer work must not assume View.ViewRect.Min is zero.
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
