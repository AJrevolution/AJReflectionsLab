// Copyright Epic Games, Inc. All Rights Reserved.

#include "AJReflections.h"
#include "AJReflectionsViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "AJReflectionsModule"

namespace
{
	TSharedPtr<
		FAJReflectionsViewExtension,
		ESPMode::ThreadSafe> GAJReflectionsViewExtension;

	FDelegateHandle GAJPostEngineInitHandle;
}

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

	GAJPostEngineInitHandle =
		FCoreDelegates::OnPostEngineInit.AddLambda([]()
	{
		GAJReflectionsViewExtension =
			FSceneViewExtensions::NewExtension<
			FAJReflectionsViewExtension>();
	});
}

void FAJReflectionsModule::ShutdownModule()
{
	if (GAJPostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(
			GAJPostEngineInitHandle);
	}

	GAJReflectionsViewExtension.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAJReflectionsModule, AJReflections)