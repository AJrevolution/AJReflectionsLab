// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJReflectionCompose.h"
#include "AJMaterialHitRayTracing.h"
#include "OpaqueReflectionExtension.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

#if RHI_RAYTRACING
namespace
{
    class FAJReflectionComposePS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FAJReflectionComposePS);
        SHADER_USE_PARAMETER_STRUCT(FAJReflectionComposePS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, ViewRectMin)
            SHADER_PARAMETER(int32, DebugMode)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialHitData0)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialHitData1)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MaterialHitFlags)
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
    int32 DebugMode)
{
#if RHI_RAYTRACING
    if (!Hit.IsValid() || Context.SceneColor == nullptr
        || DebugMode < 16 || DebugMode > 19)
    {
        return;
    }

    auto* Parameters = Context.GraphBuilder.AllocParameters<FAJReflectionComposePS::FParameters>();
    Parameters->ViewRectMin = Context.ViewRect.Min;
    Parameters->DebugMode = DebugMode;
    Parameters->MaterialHitData0 = Hit.MaterialHitData0;
    Parameters->MaterialHitData1 = Hit.MaterialHitData1;
    Parameters->MaterialHitFlags = Hit.MaterialHitFlags;
    Parameters->RenderTargets[0] = FRenderTargetBinding(
        Context.SceneColor, ERenderTargetLoadAction::ELoad);

    const TShaderMapRef<FAJReflectionComposePS> Shader(
        GetGlobalShaderMap(Context.View.GetShaderPlatform()));
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
    (void)DebugMode;
#endif
}
