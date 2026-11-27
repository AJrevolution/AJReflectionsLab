// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJHistoryReprojection.h"
#include "AJReflectionHistory.h"
#include "AJMaterialHitRayTracing.h"
#include "AJTemporalInputs.h"
#include "OpaqueReflectionExtension.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"

#if RHI_RAYTRACING
namespace
{
    // First implementation's relative linear-depth metric; depth floor 100 UE units
    // (one meter in the default centimeter world scale). Never compare raw DeviceZ.
    TAutoConsoleVariable<float> CVarAJTemporalDepthThreshold(
        TEXT("r.AJReflections.TemporalDepthThreshold"), 0.02f,
        TEXT("Maximum abs(previous linear ViewZ - expected previous ViewZ) / max(abs(expected ViewZ), 100 UE units). Default 0.02."),
        ECVF_RenderThreadSafe);
    TAutoConsoleVariable<float> CVarAJTemporalNormalThreshold(
        TEXT("r.AJReflections.TemporalNormalThreshold"), 0.90f,
        TEXT("Minimum dot(current deterministic world normal, previous world normal). Default 0.90."),
        ECVF_RenderThreadSafe);
    TAutoConsoleVariable<float> CVarAJTemporalRoughnessThreshold(
        TEXT("r.AJReflections.TemporalRoughnessThreshold"), 0.12f,
        TEXT("Maximum absolute difference in deterministic primary/top roughness [0,1]. Default 0.12."),
        ECVF_RenderThreadSafe);
    TAutoConsoleVariable<float> CVarAJTemporalF0Threshold(
        TEXT("r.AJReflections.TemporalF0Threshold"), 0.12f,
        TEXT("Maximum RGB component-wise difference in native deterministic top-closure F0. Default 0.12."),
        ECVF_RenderThreadSafe);

    class FAJHistoryReprojectionCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FAJHistoryReprojectionCS);
        SHADER_USE_PARAMETER_STRUCT(FAJHistoryReprojectionCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, OutputSize)
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER(float, HistoryExposureRatio)
            SHADER_PARAMETER(float, DepthThreshold)
            SHADER_PARAMETER(float, NormalThreshold)
            SHADER_PARAMETER(float, RoughnessThreshold)
            SHADER_PARAMETER(float, F0Threshold)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, CurrentRadianceFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentSurface0)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentSurface1)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MotionData)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRadianceFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousSurface0)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousSurface1)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousAccumulatedRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousAccumulatedHistoryLength)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ReprojectedRadiance)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ReprojectedAccumulatedRadiance)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ReprojectedAccumulatedHistoryLength)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ReprojectedHistoryFlags)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, HistoryAcceptance)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, HistoryRejectionFlags)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HistoryConsistency)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FAJHistoryReprojectionCS,
        "/AJReflections/Private/AJHistoryReprojection.usf", "MainCS", SF_Compute);
}
#endif

FAJHistoryReprojectionThresholds GetAJHistoryReprojectionThresholds_RenderThread()
{
    FAJHistoryReprojectionThresholds Result;
#if RHI_RAYTRACING
    Result.Depth = FMath::Max(0.0f, CVarAJTemporalDepthThreshold.GetValueOnRenderThread());
    Result.Normal = FMath::Clamp(CVarAJTemporalNormalThreshold.GetValueOnRenderThread(), -1.0f, 1.0f);
    Result.Roughness = FMath::Max(0.0f, CVarAJTemporalRoughnessThreshold.GetValueOnRenderThread());
    Result.F0 = FMath::Max(0.0f, CVarAJTemporalF0Threshold.GetValueOnRenderThread());
#endif
    return Result;
}

FAJHistoryReprojectionOutputs AddAJHistoryReprojectionPass(
    const FOpaqueReflectionRenderContext& Context,
    const FAJMaterialHitOutputs& Current,
    const FAJTemporalInputs& Temporal,
    const FAJPreviousReflectionHistory& Previous)
{
    FAJHistoryReprojectionOutputs Outputs;
#if RHI_RAYTRACING
    if (!Previous.bValid || !Previous.Radiance || !Previous.Flags
        || !Previous.Surface0 || !Previous.Surface1
        || !Previous.AccumulatedRadiance || !Previous.AccumulatedHistoryLength
        || !Temporal.IsValid()
        || !Current.HasRadiance() || !Context.View.State)
    {
        return Outputs;
    }

    const FIntPoint Extent = Context.ViewRect.Size();
    if (Extent.X <= 0 || Extent.Y <= 0
        || !FMath::IsFinite(Previous.PreviousPreExposure)
        || Previous.PreviousPreExposure <= UE_SMALL_NUMBER)
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

    Outputs.ReprojectedRadiance = GraphBuilder.CreateTexture(Float4Desc, TEXT("AJReflections.ReprojectedRadiance"));
    Outputs.ReprojectedHistoryFlags = GraphBuilder.CreateTexture(UintDesc, TEXT("AJReflections.ReprojectedHistoryFlags"));
    Outputs.HistoryAcceptance = GraphBuilder.CreateTexture(FloatDesc, TEXT("AJReflections.HistoryAcceptance"));
    Outputs.HistoryRejectionFlags = GraphBuilder.CreateTexture(UintDesc, TEXT("AJReflections.HistoryRejectionFlags"));
    Outputs.HistoryConsistency = GraphBuilder.CreateTexture(Float4Desc, TEXT("AJReflections.HistoryConsistency"));
    Outputs.ReprojectedAccumulatedRadiance = GraphBuilder.CreateTexture(Float4Desc, TEXT("AJReflections.ReprojectedAccumulatedRadiance"));
    Outputs.ReprojectedAccumulatedHistoryLength = GraphBuilder.CreateTexture(UintDesc, TEXT("AJReflections.ReprojectedAccumulatedHistoryLength"));

    auto* Parameters = GraphBuilder.AllocParameters<FAJHistoryReprojectionCS::FParameters>();
    Parameters->OutputSize = Extent;
    Parameters->View = Context.View.ViewUniformBuffer;
    Parameters->HistoryExposureRatio = Context.View.State->GetPreExposure() / Previous.PreviousPreExposure;
    const FAJHistoryReprojectionThresholds Thresholds = GetAJHistoryReprojectionThresholds_RenderThread();
    Parameters->DepthThreshold = Thresholds.Depth;
    Parameters->NormalThreshold = Thresholds.Normal;
    Parameters->RoughnessThreshold = Thresholds.Roughness;
    Parameters->F0Threshold = Thresholds.F0;
    Parameters->CurrentRadianceFlags = Current.ReflectionRadianceFlags;
    Parameters->CurrentSurface0 = Temporal.Surface0;
    Parameters->CurrentSurface1 = Temporal.Surface1;
    Parameters->MotionData = Temporal.MotionData;
    Parameters->PreviousRadiance = Previous.Radiance;
    Parameters->PreviousRadianceFlags = Previous.Flags;
    Parameters->PreviousSurface0 = Previous.Surface0;
    Parameters->PreviousSurface1 = Previous.Surface1;
    Parameters->PreviousAccumulatedRadiance = Previous.AccumulatedRadiance;
    Parameters->PreviousAccumulatedHistoryLength = Previous.AccumulatedHistoryLength;
    Parameters->ReprojectedRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.ReprojectedRadiance));
    Parameters->ReprojectedAccumulatedRadiance = GraphBuilder.CreateUAV(
        FRDGTextureUAVDesc(Outputs.ReprojectedAccumulatedRadiance));
    Parameters->ReprojectedAccumulatedHistoryLength = GraphBuilder.CreateUAV(
        FRDGTextureUAVDesc(Outputs.ReprojectedAccumulatedHistoryLength));
    Parameters->ReprojectedHistoryFlags = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.ReprojectedHistoryFlags));
    Parameters->HistoryAcceptance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.HistoryAcceptance));
    Parameters->HistoryRejectionFlags = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.HistoryRejectionFlags));
    Parameters->HistoryConsistency = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.HistoryConsistency));

    const TShaderMapRef<FAJHistoryReprojectionCS> Shader(
        GetGlobalShaderMap(Context.View.GetShaderPlatform()));
    // Week 5B defines a real semantic pass even in DebugMode 0. With no
    // current-frame consumer, RDG would otherwise cull this diagnostic pass.
    FComputeShaderUtils::AddPass(
        GraphBuilder, RDG_EVENT_NAME("AJReflections.ReprojectHistory"),
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        Shader, Parameters,
        FComputeShaderUtils::GetGroupCount(Extent, FIntPoint(8, 8)));
#endif
    return Outputs;
}
