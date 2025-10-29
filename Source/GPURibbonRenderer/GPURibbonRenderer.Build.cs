// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GPURibbonRenderer : ModuleRules
{
	public GPURibbonRenderer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		var NiagaraPluginPath = Path.GetFullPath(Target.RelativeEnginePath + "Plugins/FX/Niagara/");
		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
				Path.Combine(EngineDir, "Shaders/Shared"),
				Path.Combine(NiagaraPluginPath, "Source/Niagara/Private")
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",	
				"Niagara",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"RHI",
				"RenderCore",
				"NiagaraCore",
				"Niagara",
				"NiagaraShader",
				"GPURibbonShaders",
				"GPURibbonVertexFactories"
				// ... add private dependencies that you statically link with here ...	
			}
			);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Slate",
					"SlateCore",
					"NiagaraEditor",
					"UnrealEd"
				});
		}
		
		OptimizeCode = CodeOptimization.Never;
	}
}
