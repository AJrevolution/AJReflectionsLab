// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJReflectionCompose.h"
#include "AJMaterialHitRayTracing.h"
#include "AJReflectionHistory.h"
#include "AJReflectionTrace.h"
#include "OpaqueReflectionExtension.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

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
        using FPermutationDomain = TShaderPermutationDomain<FHaveInputs, FHaveHistory>;

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, ViewRectMin)
            SHADER_PARAMETER(int32, DebugMode)
            SHADER_PARAMETER(float, HistoryExposureRatio)
            SHADER_PARAMETER(float, DebugTraceDistanceMax)
            SHADER_PARAMETER(uint32, HistoryResetReason)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousReflectionRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousReflectionRadianceFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialHitData0)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialHitData1)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MaterialHitFlags)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrimaryTransport)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReflectionRadiance)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SecondaryEmissive)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SecondaryDirect)
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
    int32 DebugMode)
{
#if RHI_RAYTRACING
    if (Context.SceneColor == nullptr || DebugMode < 16 || DebugMode > 27)
    {
        return;
    }

    auto* Parameters = Context.GraphBuilder.AllocParameters<FAJReflectionComposePS::FParameters>();
    Parameters->ViewRectMin = Context.ViewRect.Min;
    Parameters->DebugMode = DebugMode;
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
    const bool bHaveInputs = Hit.IsValid() && Hit.HasRadiance();
    Parameters->MaterialHitData0 = bHaveInputs ? Hit.MaterialHitData0 : nullptr;
    Parameters->MaterialHitData1 = bHaveInputs ? Hit.MaterialHitData1 : nullptr;
    Parameters->MaterialHitFlags = bHaveInputs ? Hit.MaterialHitFlags : nullptr;
    Parameters->PrimaryTransport = bHaveInputs ? Hit.PrimaryTransport : nullptr;
    Parameters->ReflectionRadiance = bHaveInputs ? Hit.ReflectionRadiance : nullptr;
    Parameters->SecondaryEmissive = bHaveInputs ? Hit.SecondaryEmissive : nullptr;
    Parameters->SecondaryDirect = bHaveInputs ? Hit.SecondaryDirect : nullptr;
    Parameters->RenderTargets[0] = FRenderTargetBinding(
        Context.SceneColor, ERenderTargetLoadAction::ELoad);
    FAJReflectionComposePS::FPermutationDomain Permutation;
    Permutation.Set<FAJReflectionComposePS::FHaveInputs>(bHaveInputs);
    Permutation.Set<FAJReflectionComposePS::FHaveHistory>(bHaveHistory);

    const TShaderMapRef<FAJReflectionComposePS> Shader(
        GetGlobalShaderMap(Context.View.GetShaderPlatform()), Permutation);
    // A load-op draw directly into the opaque-stage working SceneColor target:
    // future radiance can use the same composition API with pre-exposed HDR.
    FPixelShaderUtils::AddFullscreenPass(
        Context.GraphBuilder,
        GetGlobalShaderMap(Context.View.GetShaderPlatform()),
        RDG_EVENT_NAME("AJReflections.Compose"),
        Shader, Parameters, Context.ViewRect);
#else
    (void)Context;
    (void)Hit;
    (void)Previous;
    (void)DebugMode;
#endif
}
