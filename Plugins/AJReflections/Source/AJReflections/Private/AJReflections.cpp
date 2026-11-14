// Copyright Epic Games, Inc. All Rights Reserved.

#include "AJReflections.h"
#include "AJReflectionsViewExtension.h"
#include "AJRendererExtension.h"
#include "OpaqueReflectionExtension.h"
#include "RenderingThread.h"
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
	TSharedPtr<FAJRendererExtension, ESPMode::ThreadSafe> GAJRendererExtension;
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

		// Generic Renderer registry; never register before OnPostEngineInit.
		GAJRendererExtension = MakeShared<FAJRendererExtension, ESPMode::ThreadSafe>();
		RegisterOpaqueReflectionExtension(GAJRendererExtension.ToSharedRef());
	});
}

void FAJReflectionsModule::ShutdownModule()
{
	if (GAJPostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(
			GAJPostEngineInitHandle);
	}

	if (GAJRendererExtension.IsValid())
	{
		UnregisterOpaqueReflectionExtension(GAJRendererExtension.Get());
		// The Renderer may still have RDG work queued on its rendering thread.
		FlushRenderingCommands();
		GAJRendererExtension.Reset();
	}

	GAJReflectionsViewExtension.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAJReflectionsModule, AJReflections)