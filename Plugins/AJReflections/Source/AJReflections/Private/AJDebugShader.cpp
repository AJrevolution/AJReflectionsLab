// Copyright Epic Games, Inc. All Rights Reserved.

#include "AJDebugShader.h"
#include "AJReflectionTrace.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "Substrate/SubstrateViewParameters.h"

namespace
{
    TAutoConsoleVariable<int32> CVarAJReflectionsDebugMode(
        TEXT("r.AJReflections.DebugMode"),
        1,
        TEXT("Selects the AJReflections debug visualization.\n")
        TEXT("-1: no AJ diagnostic; normal lit SceneColor\n")
        TEXT("0: UV gradient\n")
        TEXT("1: Raw device depth\n")
        TEXT("2: Linear ViewZ\n")
        TEXT("3: World normal\n")
        TEXT("4: Roughness\n")
        TEXT("5: Scene velocity\n")
        TEXT("6: Scene color\n")
        TEXT("7: Scene color without pre-exposure\n")
        TEXT("8: Hardware reflection hit mask\n")
        TEXT("9: Hardware reflection hit distance\n")
        TEXT("10: Hardware reflection direction\n")
        TEXT("11: Substrate stored closure count\n")
        TEXT("12: Primary/top Substrate closure F0\n")
        TEXT("13: Reflection sampling class and closure index\n")
        TEXT("14: Selected reflection-lobe roughness\n")
        TEXT("15: Combined sampling PDF (display mapping)\n")
        TEXT("16: Secondary-hit native material validity / complexity\n")
        TEXT("17: Secondary-hit top closure F0\n")
        TEXT("18: Secondary-hit top closure roughness\n")
        TEXT("19: Secondary-hit native shading normal\n")
        TEXT("20: Final pre-exposed HDR reflection radiance\n")
        TEXT("21: Secondary material emissive (pre-exposed HDR)\n")
        TEXT("22: Shadowed secondary directional light (pre-exposed HDR)\n")
        TEXT("23: Primary selected-closure transport (display mapped)\n")
        TEXT("24: Previous-frame reflection radiance (exposure-adjusted debug)\n")
        TEXT("25: Previous-frame secondary hit distance\n")
        TEXT("26: Previous-frame reflection radiance flags\n")
        TEXT("27: History validity / reset reason\n")
        TEXT("28: Reprojected accepted previous radiance\n")
        TEXT("29: History acceptance mask\n")
        TEXT("30: History rejection reason\n")
        TEXT("31: Relative linear-depth consistency\n")
        TEXT("32: World-normal consistency\n")
        TEXT("33: Roughness / F0 consistency\n")
        TEXT("34: Reprojected previous hit distance\n")
        TEXT("35: Accumulated reflection radiance (HDR)\n")
        TEXT("36: Effective temporal history length (normalized display)\n")
        TEXT("37: Temporal current sample weight\n"),
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

int32 GetAJDebugMode_AnyThread()
{
    return CVarAJReflectionsDebugMode.GetValueOnAnyThread();
}

class FAJDebugCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FAJDebugCS);
    SHADER_USE_PARAMETER_STRUCT(FAJDebugCS, FGlobalShader);

    class FTraceAvailable : SHADER_PERMUTATION_BOOL("AJ_TRACE_AVAILABLE");
    using FPermutationDomain = TShaderPermutationDomain<FTraceAvailable>;

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(FIntPoint, ViewRectMin)
        SHADER_PARAMETER(int32, DebugMode)
        SHADER_PARAMETER(float, DebugViewZMax)
        SHADER_PARAMETER(float, DebugVelocityScale)
        SHADER_PARAMETER(float, DebugTraceDistanceMax)

        SHADER_PARAMETER_STRUCT_REF(
            FViewUniformShaderParameters,
            View)

        SHADER_PARAMETER_STRUCT_INCLUDE(
            FSceneTextureShaderParameters,
            SceneTextures)

        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)

        SHADER_PARAMETER_RDG_TEXTURE_SRV(
            Texture2D,
            SceneColorTexture)

        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceDataTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, TraceFlagsTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SampleDataTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SampleFlagsTexture)

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
    const FSceneTextureShaderParameters& SceneTextures,
    const FAJReflectionTraceOutputs* TraceOutputs)
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

    PassParameters->DebugTraceDistanceMax =
        FMath::Max(GetAJReflectionMaxTraceDistance_RenderThread(), 1.0f);

    PassParameters->View =
        View.ViewUniformBuffer;

    PassParameters->SceneTextures = SceneTextures;
    
    PassParameters->Substrate = View.GetSubstrateGlobalUniformBuffer();

    PassParameters->SceneColorTexture =
        SceneColor.TextureSRV;

    const bool bTraceAvailable = TraceOutputs != nullptr && TraceOutputs->IsValid();
    PassParameters->TraceDataTexture = bTraceAvailable ? TraceOutputs->TraceData : nullptr;
    PassParameters->TraceFlagsTexture = bTraceAvailable ? TraceOutputs->TraceFlags : nullptr;
    PassParameters->SampleDataTexture = bTraceAvailable ? TraceOutputs->SampleData : nullptr;
    PassParameters->SampleFlagsTexture = bTraceAvailable ? TraceOutputs->SampleFlags : nullptr;

    PassParameters->OutputTexture =
        GraphBuilder.CreateUAV(
            FRDGTextureUAVDesc(OutputTexture));

    FAJDebugCS::FPermutationDomain PermutationVector;
    PermutationVector.Set<FAJDebugCS::FTraceAvailable>(bTraceAvailable);
    const TShaderMapRef<FAJDebugCS> ComputeShader(
        GetGlobalShaderMap(View.GetShaderPlatform()), PermutationVector);

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
