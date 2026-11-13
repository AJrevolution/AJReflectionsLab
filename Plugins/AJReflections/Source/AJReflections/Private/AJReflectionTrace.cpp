#include "AJReflectionTrace.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "RHI.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "Substrate/SubstrateViewParameters.h"

namespace
{
    TAutoConsoleVariable<int32> CVarAJReflectionsEnable(
        TEXT("r.AJReflections.Enable"),
        1,
        TEXT("Enable AJReflections hardware ray tracing for debug modes 8-10 and 13-15.\n")        
        TEXT("0: Skip tracing  1: Enable tracing when supported\n"),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<float> CVarAJReflectionsMaxTraceDistance(
        TEXT("r.AJReflections.MaxTraceDistance"),
        20000.0f,
        TEXT("Maximum AJ reflection ray TMax, in Unreal world units.\n"),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<int32> CVarAJReflectionsStochasticSampling(
        TEXT("r.AJReflections.StochasticSampling"),
        1,
        TEXT("0: Week 3A deterministic mirror ray. 1: Substrate-aware one-ray GGX sampling.\n"),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<int32> CVarAJReflectionsSampleFrameOverride(
        TEXT("r.AJReflections.SampleFrameOverride"),
        -1,
        TEXT("-1: View.StateFrameIndex. >=0: fixed shader sample-frame index for reproducible diagnostics.\n"),
        ECVF_RenderThreadSafe);
}

bool IsAJReflectionTracingEnabled_AnyThread()
{
    return CVarAJReflectionsEnable.GetValueOnAnyThread() != 0;
}

float GetAJReflectionMaxTraceDistance_RenderThread()
{
    return FMath::Max(CVarAJReflectionsMaxTraceDistance.GetValueOnRenderThread(), 0.0f);
}

bool CanAJReflectionTrace_RenderThread(const FSceneView& View)
{
#if RHI_RAYTRACING
    return View.IsRayTracingAllowedForView()
        && IsRayTracingEnabled(View.GetShaderPlatform())
        && GRHISupportsRayTracing
        && GRHISupportsInlineRayTracing;
#else
    (void)View;
    return false;
#endif
}

#if RHI_RAYTRACING
class FAJReflectionTraceCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FAJReflectionTraceCS);
    SHADER_USE_PARAMETER_STRUCT(FAJReflectionTraceCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(FIntPoint, ViewRectMin)
        SHADER_PARAMETER(float, MaxTraceDistance)
        SHADER_PARAMETER(int32, StochasticSampling)
        SHADER_PARAMETER(int32, SampleFrameOverride)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
        SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TraceData)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, TraceFlags)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SampleData)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SampleFlags)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsRayTracingEnabledForProject(Parameters.Platform)
            && RHISupportsRayTracing(Parameters.Platform)
            && RHISupportsInlineRayTracing(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        // Match UE 5.8.1's one-wave-per-group inline traversal precedent.
        OutEnvironment.CompilerFlags.Add(CFLAG_InlineRayTracing);
        OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
    }
};

IMPLEMENT_GLOBAL_SHADER(
    FAJReflectionTraceCS,
    "/AJReflections/Private/AJReflectionTrace.usf",
    "MainCS",
    SF_Compute);
#endif // RHI_RAYTRACING

FAJReflectionTraceOutputs AddAJReflectionTracePass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FIntRect SceneColorViewRect,
    const FSceneTextureShaderParameters& SceneTextures,
    FRDGBufferSRVRef TLAS)
{
    FAJReflectionTraceOutputs Outputs;

#if RHI_RAYTRACING
    const FIntPoint ViewSize = SceneColorViewRect.Size();
    if (TLAS == nullptr || !SceneTextures.SceneTextures
        || View.GetSubstrateGlobalUniformBuffer() == nullptr
        || ViewSize.X <= 0 || ViewSize.Y <= 0
        || !CanAJReflectionTrace_RenderThread(View))
    {
        return Outputs;
    }

    const FRDGTextureDesc TraceDataDesc = FRDGTextureDesc::Create2D(
        ViewSize,
        PF_A32B32G32R32F,
        FClearValueBinding::None,
        TexCreate_ShaderResource | TexCreate_UAV);

    const FRDGTextureDesc TraceFlagsDesc = FRDGTextureDesc::Create2D(
        ViewSize,
        PF_R32_UINT,
        FClearValueBinding::None,
        TexCreate_ShaderResource | TexCreate_UAV);

    Outputs.TraceData = GraphBuilder.CreateTexture(TraceDataDesc, TEXT("AJReflections.TraceData"));
    Outputs.TraceFlags = GraphBuilder.CreateTexture(TraceFlagsDesc, TEXT("AJReflections.TraceFlags"));

    // PDF needs FP32: narrow GGX lobes can produce values well above the
    // maximum finite FP16 value. Neither texture is persistent/extracted.
    Outputs.SampleData = GraphBuilder.CreateTexture(
        TraceDataDesc, TEXT("AJReflections.SampleData"));
    Outputs.SampleFlags = GraphBuilder.CreateTexture(
        TraceFlagsDesc, TEXT("AJReflections.SampleFlags"));

    auto* PassParameters = GraphBuilder.AllocParameters<FAJReflectionTraceCS::FParameters>();
    PassParameters->OutputSize = ViewSize;
    PassParameters->ViewRectMin = SceneColorViewRect.Min;
    PassParameters->MaxTraceDistance = GetAJReflectionMaxTraceDistance_RenderThread();
    PassParameters->StochasticSampling = CVarAJReflectionsStochasticSampling.GetValueOnRenderThread() != 0 ? 1 : 0;
    PassParameters->SampleFrameOverride = FMath::Max(CVarAJReflectionsSampleFrameOverride.GetValueOnRenderThread(), -1);
    PassParameters->View = View.ViewUniformBuffer;
    PassParameters->SceneTextures = SceneTextures;
    PassParameters->Substrate = View.GetSubstrateGlobalUniformBuffer();
    PassParameters->TLAS = TLAS;
    PassParameters->TraceData = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.TraceData));
    PassParameters->TraceFlags = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.TraceFlags));
    PassParameters->SampleData = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.SampleData));
    PassParameters->SampleFlags = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.SampleFlags));

    const TShaderMapRef<FAJReflectionTraceCS> ComputeShader(
        GetGlobalShaderMap(View.GetShaderPlatform()));

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("AJReflections.Trace"),
        ComputeShader,
        PassParameters,
        FComputeShaderUtils::GetGroupCount(ViewSize, FIntPoint(8, 4)));
#else
    (void)GraphBuilder;
    (void)View;
    (void)SceneColorViewRect;
    (void)SceneTextures;
    (void)TLAS;
#endif

    return Outputs;
}
