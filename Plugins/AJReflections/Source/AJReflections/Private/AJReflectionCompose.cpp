// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJReflectionCompose.h"
#include "AJMaterialHitRayTracing.h"
#include "AJReflectionHistory.h"
#include "AJHistoryReprojection.h"
#include "AJTemporalAccumulation.h"
#include "AJReflectionTrace.h"
#include "OpaqueReflectionExtension.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RHIStaticStates.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

namespace
{
    TAutoConsoleVariable<int32> CVarAJReflectionPresentationMode(
        TEXT("r.AJReflections.PresentationMode"), 0,
        TEXT("0: no AJ scene addition (default). 1: add raw AJ radiance. 2: add accumulated AJ radiance. Use DebugMode -1."),
        ECVF_RenderThreadSafe);
}

int32 GetAJReflectionPresentationMode_RenderThread()
{
    return FMath::Clamp(CVarAJReflectionPresentationMode.GetValueOnRenderThread(), 0, 2);
}

#if RHI_RAYTRACING
namespace
{
    class FAJReflectionComposePS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FAJReflectionComposePS);
        SHADER_USE_PARAMETER_STRUCT(FAJReflectionComposePS, FGlobalShader);
        class FHaveInputs : SHADER_PERMUTATION_BOOL("AJ_COMPOSE_HAS_INPUTS");
        class FHaveHistory : SHADER_PERMUTATION_BOOL("AJ_COMPOSE_HAS_HISTORY");
        class FHaveReprojection : SHADER_PERMUTATION_BOOL("AJ_COMPOSE_HAS_REPROJECTION");
        class FHaveAccumulation : SHADER_PERMUTATION_BOOL("AJ_COMPOSE_HAS_ACCUMULATION");
        using FPermutationDomain = TShaderPermutationDomain<
            FHaveInputs, FHaveHistory, FHaveReprojection, FHaveAccumulation>;

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, ViewRectMin)
            SHADER_PARAMETER(int32, DebugMode)
            SHADER_PARAMETER(int32, PresentationMode)
            SHADER_PARAMETER(float, TemporalMaxHistoryFrames)
            SHADER_PARAMETER(float, HistoryExposureRatio)
            SHADER_PARAMETER(float, DebugTraceDistanceMax)
            SHADER_PARAMETER(uint32, HistoryResetReason)
            SHADER_PARAMETER(float, TemporalDepthThreshold)
            SHADER_PARAMETER(float, TemporalRoughnessThreshold)
            SHADER_PARAMETER(float, TemporalF0Threshold)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReprojectedRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryAcceptance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HistoryRejectionFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryConsistency)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AccumulatedRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, AccumulatedHistoryLength)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TemporalCurrentWeight)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousReflectionRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousReflectionRadianceFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialHitData0)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialHitData1)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MaterialHitFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrimaryTransport)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReflectionRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SecondaryEmissive)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SecondaryDirect)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SecondaryEnvironment)
            RENDER_TARGET_BINDING_SLOTS()
        END_SHADER_PARAMETER_STRUCT()
    };

    IMPLEMENT_GLOBAL_SHADER(
        FAJReflectionComposePS,
        "/AJReflections/Private/AJReflectionCompose.usf",
        "MainPS", SF_Pixel);
}
#endif

void AddAJReflectionComposePass(
    const FOpaqueReflectionRenderContext& Context,
    const FAJMaterialHitOutputs& Hit,
    const FAJPreviousReflectionHistory& Previous,
    const FAJHistoryReprojectionOutputs& Reprojected,
    const FAJTemporalAccumulationOutputs& Accumulated,
    int32 DebugMode,
    int32 PresentationMode)
{
#if RHI_RAYTRACING
    if (Context.SceneColor == nullptr)
    {
        return;
    }
    const bool bDiagnostic = DebugMode >= 16 && DebugMode <= 38;
    const bool bPresent = DebugMode == -1 && (PresentationMode == 1 || PresentationMode == 2);
    if (!bDiagnostic && !bPresent)
    {
        return;
    }
    const bool bHaveInputs = Hit.IsValid() && Hit.HasRadiance();
    const bool bHaveAccumulation = Accumulated.IsValid();
    if (bPresent && ((PresentationMode == 1 && !bHaveInputs)
        || (PresentationMode == 2 && !bHaveAccumulation)))
    {
        return; // No source signal; never issue an additive pass with unbound input.
    }

    auto* Parameters = Context.GraphBuilder.AllocParameters<FAJReflectionComposePS::FParameters>();
    Parameters->ViewRectMin = Context.ViewRect.Min;
    Parameters->DebugMode = DebugMode;
    Parameters->PresentationMode = bPresent ? PresentationMode : 0;
    Parameters->TemporalMaxHistoryFrames = float(GetAJTemporalMaxHistoryFrames_RenderThread());
    Parameters->DebugTraceDistanceMax = FMath::Max(
        GetAJReflectionMaxTraceDistance_RenderThread(), 1.0f);
    Parameters->HistoryResetReason = static_cast<uint32>(Previous.ResetReason);
    const bool bHaveHistory = Previous.bValid
        && Previous.Radiance != nullptr && Previous.Flags != nullptr;
    Parameters->HistoryExposureRatio = bHaveHistory
        ? Context.View.State->GetPreExposure() / FMath::Max(Previous.PreviousPreExposure, UE_SMALL_NUMBER)
        : 1.0f;
    Parameters->PreviousReflectionRadiance = bHaveHistory ? Previous.Radiance : nullptr;
    Parameters->PreviousReflectionRadianceFlags = bHaveHistory ? Previous.Flags : nullptr;
    const bool bHaveReprojection = Reprojected.IsValid();
    const FAJHistoryReprojectionThresholds Thresholds = GetAJHistoryReprojectionThresholds_RenderThread();
    Parameters->TemporalDepthThreshold = Thresholds.Depth;
    Parameters->TemporalRoughnessThreshold = Thresholds.Roughness;
    Parameters->TemporalF0Threshold = Thresholds.F0;
    Parameters->ReprojectedRadiance = bHaveReprojection ? Reprojected.ReprojectedRadiance : nullptr;
    Parameters->HistoryAcceptance = bHaveReprojection ? Reprojected.HistoryAcceptance : nullptr;
    Parameters->HistoryRejectionFlags = bHaveReprojection ? Reprojected.HistoryRejectionFlags : nullptr;
    Parameters->HistoryConsistency = bHaveReprojection ? Reprojected.HistoryConsistency : nullptr;
    Parameters->AccumulatedRadiance = bHaveAccumulation ? Accumulated.AccumulatedRadiance : nullptr;
    Parameters->AccumulatedHistoryLength = bHaveAccumulation ? Accumulated.AccumulatedHistoryLength : nullptr;
    Parameters->TemporalCurrentWeight = bHaveAccumulation ? Accumulated.TemporalCurrentWeight : nullptr;
    Parameters->MaterialHitData0 = bHaveInputs ? Hit.MaterialHitData0 : nullptr;
    Parameters->MaterialHitData0 = bHaveInputs ? Hit.MaterialHitData0 : nullptr;
    Parameters->MaterialHitData1 = bHaveInputs ? Hit.MaterialHitData1 : nullptr;
    Parameters->MaterialHitFlags = bHaveInputs ? Hit.MaterialHitFlags : nullptr;
    Parameters->PrimaryTransport = bHaveInputs ? Hit.PrimaryTransport : nullptr;
    Parameters->ReflectionRadiance = bHaveInputs ? Hit.ReflectionRadiance : nullptr;
    Parameters->SecondaryEmissive = bHaveInputs ? Hit.SecondaryEmissive : nullptr;
    Parameters->SecondaryDirect = bHaveInputs ? Hit.SecondaryDirect : nullptr;
    Parameters->SecondaryEnvironment = bHaveInputs ? Hit.SecondaryEnvironment : nullptr;
    Parameters->RenderTargets[0] = FRenderTargetBinding(Context.SceneColor, ERenderTargetLoadAction::ELoad);
    FAJReflectionComposePS::FPermutationDomain Permutation;
    Permutation.Set<FAJReflectionComposePS::FHaveInputs>(bHaveInputs);
    Permutation.Set<FAJReflectionComposePS::FHaveHistory>(bHaveHistory);
    Permutation.Set<FAJReflectionComposePS::FHaveReprojection>(bHaveReprojection);
    Permutation.Set<FAJReflectionComposePS::FHaveAccumulation>(bHaveAccumulation);
    const TShaderMapRef<FAJReflectionComposePS> Shader(
        GetGlobalShaderMap(Context.View.GetShaderPlatform()), Permutation);
    // Debug overwrites the view; beauty performs source RGB + destination RGB,
    // leaving SceneColor alpha alone. Never sample the render target as SRV.
    FRHIBlendState* BlendState = bPresent
        ? TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI()
        : TStaticBlendState<>::GetRHI();
    if (bPresent)
    {
        FPixelShaderUtils::AddFullscreenPass(
            Context.GraphBuilder,
            GetGlobalShaderMap(Context.View.GetShaderPlatform()),
            RDG_EVENT_NAME("AJReflections.Present"),
            Shader, Parameters, Context.ViewRect, BlendState);
    }
    else
    {
        FPixelShaderUtils::AddFullscreenPass(
            Context.GraphBuilder,
            GetGlobalShaderMap(Context.View.GetShaderPlatform()),
            RDG_EVENT_NAME("AJReflections.Compose"),
            Shader, Parameters, Context.ViewRect, BlendState);
    }
#else
    (void)Context;
    (void)Hit;
    (void)Previous;
    (void)Reprojected;
    (void)Accumulated;
    (void)DebugMode;
    (void)PresentationMode;
#endif
}
