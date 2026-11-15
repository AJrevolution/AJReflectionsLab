// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJMaterialHitRayTracing.h"
#include "AJReflectionTrace.h"
#include "OpaqueReflectionExtension.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "RHI.h"
#include "SceneView.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "RayTracing/RayTracingHitLightingParameters.h"
#include "Substrate/SubstrateViewParameters.h"

#if RHI_RAYTRACING
#include "RayTracingPayloadType.h"
#include "RayTracingShaderBindingLayout.h"
#include "SceneUniformBuffer.h"
#endif

namespace
{
    TAutoConsoleVariable<int32> CVarAJMaterialHitBackend(
        TEXT("r.AJReflections.MaterialHitBackend"),
        1,
        TEXT("0: disable AJ programmable material-hit dispatch. 1: enable (default)."),
        ECVF_RenderThreadSafe);
}

bool IsAJMaterialHitBackendEnabled_AnyThread()
{
    return CVarAJMaterialHitBackend.GetValueOnAnyThread() != 0;
}

#if RHI_RAYTRACING
namespace
{
    class FAJPrimaryReflectionSampleCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FAJPrimaryReflectionSampleCS);
        SHADER_USE_PARAMETER_STRUCT(FAJPrimaryReflectionSampleCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, OutputSize)
            SHADER_PARAMETER(FIntPoint, ViewRectMin)
            SHADER_PARAMETER(int32, StochasticSampling)
            SHADER_PARAMETER(int32, SampleFrameOverride)
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
            SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, PrimaryDirection)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, PrimarySurface)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, PrimaryTransport)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PrimarySamplingClass)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(
        FAJPrimaryReflectionSampleCS,
        "/AJReflections/Private/AJPrimaryReflectionSample.usf",
        "MainCS", SF_Compute);

    class FAJMaterialHitRGS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FAJMaterialHitRGS);
        SHADER_USE_ROOT_PARAMETER_STRUCT(FAJMaterialHitRGS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, OutputSize)
            SHADER_PARAMETER(FIntPoint, ViewRectMin)
            SHADER_PARAMETER(float, MaxTraceDistance)
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
            SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
            SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
            SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRayTracingLightGrid, LightGrid)
            SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrimaryDirection)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrimarySurface)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrimaryTransport)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PrimarySamplingClass)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MaterialHitData0)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MaterialHitData1)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, MaterialHitFlags)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ReflectionRadiance)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SecondaryEmissive)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SecondaryDirect)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ReflectionRadianceFlags)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
        }

        static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
        {
            return ERayTracingPayloadType::RayTracingMaterial;
        }

        static const FShaderBindingLayout* GetShaderBindingLayout(
            const FShaderPermutationParameters& Parameters)
        {
            return RayTracing::GetShaderBindingLayout(Parameters.Platform);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(
        FAJMaterialHitRGS,
        "/AJReflections/Private/AJMaterialHitRayGen.usf",
        "MainRGS", SF_RayGen);
}

void GatherAJMaterialRayGenShader(
    const FSceneView& View, TArray<FRHIRayTracingShader*>& OutShaders)
{
    if (IsAJMaterialHitBackendEnabled_AnyThread()
        && GRHISupportsRayTracingShaders
        && View.IsRayTracingAllowedForView())
    {
        const TShaderMapRef<FAJMaterialHitRGS> Shader(
            GetGlobalShaderMap(View.GetShaderPlatform()));
        OutShaders.Add(Shader.GetRayTracingShader());
    }
}

FAJMaterialHitOutputs AddAJMaterialHitPass(const FOpaqueReflectionRenderContext& Context)
{
    FAJMaterialHitOutputs Outputs;
    const FIntPoint Extent = Context.ViewRect.Size();
    if (!IsAJMaterialHitBackendEnabled_AnyThread()
        || !Context.MaterialDispatch.IsValid()
        || Context.TLAS == nullptr || Context.Substrate == nullptr
        || Context.Scene == nullptr || Context.LightGrid == nullptr
        || !Context.SceneTextures.SceneTextures
        || Extent.X <= 0 || Extent.Y <= 0)
    {
        return Outputs;
    }

    FRDGBuilder& GraphBuilder = Context.GraphBuilder;
    const FRDGTextureDesc FloatDesc = FRDGTextureDesc::Create2D(
        Extent, PF_A32B32G32R32F, FClearValueBinding::None,
        TexCreate_ShaderResource | TexCreate_UAV);
    const FRDGTextureDesc FlagsDesc = FRDGTextureDesc::Create2D(
        Extent, PF_R32_UINT, FClearValueBinding::None,
        TexCreate_ShaderResource | TexCreate_UAV);

    // Independent from inline intersection: same sampling helper; no RayQuery.
    FRDGTextureRef PrimaryDirection = GraphBuilder.CreateTexture(
        FloatDesc, TEXT("AJReflections.MaterialHitDirection"));
    FRDGTextureRef PrimarySurface = GraphBuilder.CreateTexture(
        FloatDesc, TEXT("AJReflections.MaterialHitPrimarySurface"));
    Outputs.PrimaryTransport = GraphBuilder.CreateTexture(
        FloatDesc, TEXT("AJReflections.PrimaryTransport"));
    // One graph-local classification channel to signal complex primary fallback
    // without altering Week 4A's Direction/Surface output ABI.
    FRDGTextureRef PrimarySamplingClass = GraphBuilder.CreateTexture(
        FlagsDesc, TEXT("AJReflections.PrimarySamplingClass"));

    auto* SampleParameters = GraphBuilder.AllocParameters<FAJPrimaryReflectionSampleCS::FParameters>();
    SampleParameters->OutputSize = Extent;
    SampleParameters->ViewRectMin = Context.ViewRect.Min;
    SampleParameters->StochasticSampling = GetAJReflectionStochasticSampling_RenderThread() ? 1 : 0;
    SampleParameters->SampleFrameOverride = GetAJReflectionSampleFrameOverride_RenderThread();
    SampleParameters->View = Context.View.ViewUniformBuffer;
    SampleParameters->SceneTextures = Context.SceneTextures;
    SampleParameters->Substrate = Context.Substrate;
    SampleParameters->PrimaryDirection = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PrimaryDirection));
    SampleParameters->PrimarySurface = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PrimarySurface));
    SampleParameters->PrimaryTransport = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.PrimaryTransport));
    SampleParameters->PrimarySamplingClass = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PrimarySamplingClass));

    const TShaderMapRef<FAJPrimaryReflectionSampleCS> SamplingShader(
        GetGlobalShaderMap(Context.View.GetShaderPlatform()));
    FComputeShaderUtils::AddPass(
        GraphBuilder, RDG_EVENT_NAME("AJReflections.PrimarySample"),
        SamplingShader, SampleParameters,
        FComputeShaderUtils::GetGroupCount(Extent, FIntPoint(8, 8)));

    Outputs.MaterialHitData0 = GraphBuilder.CreateTexture(FloatDesc, TEXT("AJReflections.MaterialHitData0"));
    Outputs.MaterialHitData1 = GraphBuilder.CreateTexture(FloatDesc, TEXT("AJReflections.MaterialHitData1"));
    Outputs.MaterialHitFlags = GraphBuilder.CreateTexture(FlagsDesc, TEXT("AJReflections.MaterialHitFlags"));
    Outputs.ReflectionRadiance = GraphBuilder.CreateTexture(FloatDesc, TEXT("AJReflections.ReflectionRadiance"));
    Outputs.SecondaryEmissive = GraphBuilder.CreateTexture(FloatDesc, TEXT("AJReflections.SecondaryEmissive"));
    Outputs.SecondaryDirect = GraphBuilder.CreateTexture(FloatDesc, TEXT("AJReflections.SecondaryDirect"));
    Outputs.ReflectionRadianceFlags = GraphBuilder.CreateTexture(FlagsDesc, TEXT("AJReflections.ReflectionRadianceFlags"));

    auto* Parameters = GraphBuilder.AllocParameters<FAJMaterialHitRGS::FParameters>();
    Parameters->OutputSize = Extent;
    Parameters->ViewRectMin = Context.ViewRect.Min;
    Parameters->MaxTraceDistance = GetAJReflectionMaxTraceDistance_RenderThread();
    Parameters->View = Context.View.ViewUniformBuffer;
    Parameters->SceneTextures = Context.SceneTextures;
    Parameters->Scene = Context.Scene;
    Parameters->Substrate = Context.Substrate;
    Parameters->LightGrid = Context.LightGrid;
    Parameters->TLAS = Context.TLAS;
    Parameters->PrimaryDirection = PrimaryDirection;
    Parameters->PrimarySurface = PrimarySurface;
    Parameters->PrimaryTransport = Outputs.PrimaryTransport;
    Parameters->PrimarySamplingClass = PrimarySamplingClass;
    Parameters->MaterialHitData0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.MaterialHitData0));
    Parameters->MaterialHitData1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.MaterialHitData1));
    Parameters->MaterialHitFlags = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.MaterialHitFlags));
    Parameters->ReflectionRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.ReflectionRadiance));
    Parameters->SecondaryEmissive = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.SecondaryEmissive));
    Parameters->SecondaryDirect = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.SecondaryDirect));
    Parameters->ReflectionRadianceFlags = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.ReflectionRadianceFlags));

    const TShaderMapRef<FAJMaterialHitRGS> RayGenShader(
        GetGlobalShaderMap(Context.View.GetShaderPlatform()));
    ClearUnusedGraphResources(RayGenShader, Parameters);
    const TSharedPtr<FOpaqueReflectionMaterialRayDispatch, ESPMode::ThreadSafe> Dispatch =
        Context.MaterialDispatch;

    // NeverCull: the permanent feature must execute even in DebugMode 0;
    // diagnostic textures are not necessarily consumed by a presentation pass.
    GraphBuilder.AddPass(
        RDG_EVENT_NAME("AJReflections.MaterialHit"), Parameters,
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        [Parameters, RayGenShader, Dispatch, Extent](FRDGAsyncTask, FRHICommandList& RHICmdList)
        {
            FRHIBatchedShaderParameters& Globals = RHICmdList.GetScratchShaderParameters();
            SetShaderParameters(Globals, RayGenShader, *Parameters);
            Dispatch->Dispatch(RHICmdList, RayGenShader.GetRayTracingShader(), Globals, Extent);
        });

    return Outputs;
}
#endif // RHI_RAYTRACING
