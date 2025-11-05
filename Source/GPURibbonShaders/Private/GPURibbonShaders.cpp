#include "GPURibbonShaders.h"

#define LOCTEXT_NAMESPACE "FGPURibbonShadersModule"

void FGPURibbonShadersModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("NiagaraGPURibbonRenderer"), TEXT("Shaders"));
	auto ShaderSourceDirectoryMappings = AllShaderSourceDirectoryMappings();
	if (!ShaderSourceDirectoryMappings.Find(TEXT("/Plugin/NiagaraGPURibbonRenderer")))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/NiagaraGPURibbonRenderer"), PluginShaderDir);
	}
}

void FGPURibbonShadersModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FGPURibbonShadersModule, GPURibbonShaders)