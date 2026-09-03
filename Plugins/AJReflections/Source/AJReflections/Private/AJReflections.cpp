// Copyright Epic Games, Inc. All Rights Reserved.

#include "AJReflections.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "AJReflectionsModule"

void FAJReflectionsModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("AJReflections"));

	check(Plugin.IsValid());

	const FString ShaderDirectory =
		FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));

	AddShaderSourceDirectoryMapping(
		TEXT("/AJReflections"),
		ShaderDirectory
	);
}

void FAJReflectionsModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAJReflectionsModule, AJReflections)