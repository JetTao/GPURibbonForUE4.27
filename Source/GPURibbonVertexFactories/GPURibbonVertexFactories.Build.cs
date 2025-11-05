using UnrealBuildTool;
using System.IO;
public class GPURibbonVertexFactories : ModuleRules
{
    public GPURibbonVertexFactories(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
        PrivateIncludePaths.AddRange(
            new string[] {
                // ... add other private include paths required here ...
                Path.Combine(EngineDir, "Shaders/Shared"),
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "RHI",
                "RenderCore",
                "NiagaraVertexFactories"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
            }
        );
    }
}