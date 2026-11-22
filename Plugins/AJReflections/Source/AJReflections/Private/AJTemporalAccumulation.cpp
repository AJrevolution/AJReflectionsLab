// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJTemporalAccumulation.h"
#include "AJMaterialHitRayTracing.h"
#include "AJHistoryReprojection.h"
#include "OpaqueReflectionExtension.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "SceneView.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

namespace
{
    TAutoConsoleVariable<int32> CVarAJTemporalAccumulation(
        TEXT("r.AJReflections.TemporalAccumulation"), 1,
        TEXT("0: bypass temporal history reuse (valid current sample only). 1: bounded accumulation (default)."),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<int32> CVarAJTemporalMaxHistoryFrames(
        TEXT("r.AJReflections.TemporalMaxHistoryFrames"), 32,
        TEXT("Effective maximum temporal sample count, clamped internally to [1,256]. Default 32."),
        ECVF_RenderThreadSafe);
}

int32 GetAJTemporalMaxHistoryFrames_RenderThread()
{
    return FMath::Clamp(CVarAJTemporalMaxHistoryFrames.GetValueOnRenderThread(), 1, 256);
}

bool IsAJTemporalAccumulationEnabled_RenderThread()
{
    return CVarAJTemporalAccumulation.GetValueOnRenderThread() != 0;
}

#if RHI_RAYTRACING
namespace
{
    class FAJTemporalAccumulationCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FAJTemporalAccumulationCS);
        SHADER_USE_PARAMETER_STRUCT(FAJTemporalAccumulationCS, FGlobalShader);

        class FHaveHistory : SHADER_PERMUTATION_BOOL("AJ_ACCUM_HAS_HISTORY");
        using FPermutationDomain = TShaderPermutationDomain<FHaveHistory>;

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, OutputSize)
            SHADER_PARAMETER(uint32, MaxHistoryFrames)
            SHADER_PARAMETER(int32, AccumulationEnabled)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, CurrentRadianceFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryAcceptance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReprojectedAccumulatedRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ReprojectedAccumulatedHistoryLength)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AccumulatedRadiance)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, AccumulatedHistoryLength)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, TemporalCurrentWeight)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(
        FAJTemporalAccumulationCS,
        "/AJReflections/Private/AJTemporalAccumulation.usf",
        "MainCS", SF_Compute);
}
#endif

FAJTemporalAccumulationOutputs AddAJTemporalAccumulationPass(
    const FOpaqueReflectionRenderContext& Context,
    const FAJMaterialHitOutputs& Current,
    const FAJHistoryReprojectionOutputs& Reprojected)
{
    FAJTemporalAccumulationOutputs Outputs;
#if RHI_RAYTRACING
    const FIntPoint Extent = Context.ViewRect.Size();
    if (!Current.HasRadiance() || Extent.X <= 0 || Extent.Y <= 0)
    {
        return Outputs;
    }

    FRDGBuilder& GraphBuilder = Context.GraphBuilder;
    const ETextureCreateFlags Flags = TexCreate_ShaderResource | TexCreate_UAV;
    const FRDGTextureDesc Float4Desc = FRDGTextureDesc::Create2D(
        Extent, PF_A32B32G32R32F, FClearValueBinding::None, Flags);
    const FRDGTextureDesc UintDesc = FRDGTextureDesc::Create2D(
        Extent, PF_R32_UINT, FClearValueBinding::None, Flags);
    const FRDGTextureDesc FloatDesc = FRDGTextureDesc::Create2D(
        Extent, PF_R32_FLOAT, FClearValueBinding::None, Flags);

    Outputs.AccumulatedRadiance = GraphBuilder.CreateTexture(
        Float4Desc, TEXT("AJReflections.AccumulatedRadiance"));
    Outputs.AccumulatedHistoryLength = GraphBuilder.CreateTexture(
        UintDesc, TEXT("AJReflections.AccumulatedHistoryLength"));
    Outputs.TemporalCurrentWeight = GraphBuilder.CreateTexture(
        FloatDesc, TEXT("AJReflections.TemporalCurrentWeight"));

    const bool bHaveHistory = Reprojected.IsValid()
        && Reprojected.ReprojectedAccumulatedRadiance != nullptr
        && Reprojected.ReprojectedAccumulatedHistoryLength != nullptr;

    auto* Parameters = GraphBuilder.AllocParameters<FAJTemporalAccumulationCS::FParameters>();
    Parameters->OutputSize = Extent;
    Parameters->MaxHistoryFrames = static_cast<uint32>(GetAJTemporalMaxHistoryFrames_RenderThread());
    Parameters->AccumulationEnabled = IsAJTemporalAccumulationEnabled_RenderThread() ? 1 : 0;
    Parameters->CurrentRadiance = Current.ReflectionRadiance;
    Parameters->CurrentRadianceFlags = Current.ReflectionRadianceFlags;
    Parameters->HistoryAcceptance = bHaveHistory ? Reprojected.HistoryAcceptance : nullptr;
    Parameters->ReprojectedAccumulatedRadiance = bHaveHistory
        ? Reprojected.ReprojectedAccumulatedRadiance : nullptr;
    Parameters->ReprojectedAccumulatedHistoryLength = bHaveHistory
        ? Reprojected.ReprojectedAccumulatedHistoryLength : nullptr;
    Parameters->AccumulatedRadiance = GraphBuilder.CreateUAV(
        FRDGTextureUAVDesc(Outputs.AccumulatedRadiance));
    Parameters->AccumulatedHistoryLength = GraphBuilder.CreateUAV(
        FRDGTextureUAVDesc(Outputs.AccumulatedHistoryLength));
    Parameters->TemporalCurrentWeight = GraphBuilder.CreateUAV(
        FRDGTextureUAVDesc(Outputs.TemporalCurrentWeight));

    FAJTemporalAccumulationCS::FPermutationDomain Permutation;
    Permutation.Set<FAJTemporalAccumulationCS::FHaveHistory>(bHaveHistory);
    const TShaderMapRef<FAJTemporalAccumulationCS> Shader(
        GetGlobalShaderMap(Context.View.GetShaderPlatform()), Permutation);

    // Runs and produces extractable semantic outputs even with DebugMode -1/0;
    // the no-history permutation never binds null RDG textures to a reader.
    FComputeShaderUtils::AddPass(
        GraphBuilder, RDG_EVENT_NAME("AJReflections.TemporalAccumulation"),
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        Shader, Parameters,
        FComputeShaderUtils::GetGroupCount(Extent, FIntPoint(8, 8)));
#else
    (void)Context;
    (void)Current;
    (void)Reprojected;
#endif
    return Outputs;
}
