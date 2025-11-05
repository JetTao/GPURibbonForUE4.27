#include "GPURibbonVertexFactories.h"


#define LOCTEXT_NAMESPACE "FGPURibbonVertexFactoriesModule"

void FGPURibbonVertexFactoriesModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("NiagaraGPURibbonRenderer"), TEXT("Shaders"));
	auto ShaderSourceDirectoryMappings = AllShaderSourceDirectoryMappings();
	if (!ShaderSourceDirectoryMappings.Find(TEXT("/Plugin/NiagaraGPURibbonRenderer")))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/NiagaraGPURibbonRenderer"), PluginShaderDir);
	}
}

void FGPURibbonVertexFactoriesModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FGPURibbonVertexFactoriesModule, GPURibbonVertexFactories)