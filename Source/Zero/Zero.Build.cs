// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Zero : ModuleRules
{
	public Zero(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
				// ... add public include paths required here ...
			}
		);


		PrivateIncludePaths.AddRange(
			new string[]
			{
				// ... add other private include paths required here ...
			}
		);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Projects",
				"RHI",
				"RHICore",           // Added RHICore here
				"Renderer",          // Critical for vertex factory base classes
				"RenderCore",
				
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Projects",
				"RHI",
				"RHICore",           // Added RHICore here
				"Renderer",          // Critical for vertex factory base classes
				"RenderCore",
				"PhysicsCore",
			}
		);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
		
	}
}