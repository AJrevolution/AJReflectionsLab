// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FAJDebugCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAJDebugCS);
	SHADER_USE_PARAMETER_STRUCT(FAJDebugCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(
	FAJDebugCS,
	"/AJReflections/Private/AJDebug.usf",
	"MainCS",
	SF_Compute
);