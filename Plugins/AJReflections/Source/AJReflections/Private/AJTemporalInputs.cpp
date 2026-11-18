// Copyright Epic Games, Inc. All Rights Reserved.
#include "AJTemporalInputs.h"
#include "OpaqueReflectionExtension.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "Substrate/SubstrateViewParameters.h"

#if RHI_RAYTRACING
namespace
{
    class FAJTemporalInputsCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FAJTemporalInputsCS);
        SHADER_USE_PARAMETER_STRUCT(FAJTemporalInputsCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FIntPoint, OutputSize)
            SHADER_PARAMETER(FIntPoint, ViewRectMin)
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
            SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TemporalSurface0)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TemporalSurface1)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MotionData)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FAJTemporalInputsCS,
        "/AJReflections/Private/AJTemporalInputs.usf", "MainCS", SF_Compute);
}
#endif

FAJTemporalInputs AddAJTemporalInputsPass(const FOpaqueReflectionRenderContext& Context)
{
    FAJTemporalInputs Outputs;
#if RHI_RAYTRACING
    const FIntPoint Extent = Context.ViewRect.Size();
    if (Extent.X <= 0 || Extent.Y <= 0 || !Context.SceneTextures.SceneTextures || !Context.Substrate)
    {
        return Outputs;
    }

    FRDGBuilder& GraphBuilder = Context.GraphBuilder;
    const ETextureCreateFlags Flags = TexCreate_ShaderResource | TexCreate_UAV;
    const FRDGTextureDesc Surface0Desc = FRDGTextureDesc::Create2D(
        Extent, PF_A32B32G32R32F, FClearValueBinding::None, Flags);
    const FRDGTextureDesc Surface1Desc = FRDGTextureDesc::Create2D(
        Extent, PF_FloatRGBA, FClearValueBinding::None, Flags);

    Outputs.Surface0 = GraphBuilder.CreateTexture(Surface0Desc, TEXT("AJReflections.TemporalSurface0"));
    Outputs.Surface1 = GraphBuilder.CreateTexture(Surface1Desc, TEXT("AJReflections.TemporalSurface1"));
    Outputs.MotionData = GraphBuilder.CreateTexture(Surface0Desc, TEXT("AJReflections.MotionData"));

    auto* Parameters = GraphBuilder.AllocParameters<FAJTemporalInputsCS::FParameters>();
    Parameters->OutputSize = Extent;
    Parameters->ViewRectMin = Context.ViewRect.Min;
    Parameters->View = Context.View.ViewUniformBuffer;
    Parameters->SceneTextures = Context.SceneTextures;
    Parameters->Substrate = Context.Substrate;
    Parameters->TemporalSurface0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.Surface0));
    Parameters->TemporalSurface1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.Surface1));
    Parameters->MotionData = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Outputs.MotionData));

    const TShaderMapRef<FAJTemporalInputsCS> Shader(GetGlobalShaderMap(Context.View.GetShaderPlatform()));
    FComputeShaderUtils::AddPass(
        GraphBuilder, RDG_EVENT_NAME("AJReflections.TemporalInputs"), Shader, Parameters,
        FComputeShaderUtils::GetGroupCount(Extent, FIntPoint(8, 8)));
#endif
    return Outputs;
}
