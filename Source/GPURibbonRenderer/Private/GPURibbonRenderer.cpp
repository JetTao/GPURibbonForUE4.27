// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPURibbonRenderer.h"

#define LOCTEXT_NAMESPACE "FGPURibbonRendererModule"

void FGPURibbonRendererModule::StartupModule()
{
}

void FGPURibbonRendererModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGPURibbonRendererModule, GPURibbonRenderer)