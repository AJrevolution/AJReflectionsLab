// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AJReflections : ModuleRules
{
    public AJReflections(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Projects",
                "RenderCore",
                "RHI",
                "Renderer"
            }
        );
    }
}