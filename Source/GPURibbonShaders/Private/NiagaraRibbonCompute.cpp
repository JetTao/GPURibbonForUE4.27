// Copyright Epic Games, Inc. All Rights Reserved.


#include "NiagaraRibbonCompute.h"

constexpr uint32 FNiagaraRibbonComputeCommon::VertexGenReductionInitializationThreadSize;
constexpr uint32 FNiagaraRibbonComputeCommon::VertexGenReductionPropagationThreadSize;
constexpr uint32 FNiagaraRibbonComputeCommon::VertexGenReductionFinalizationThreadSize;
constexpr uint32 FNiagaraRibbonComputeCommon::VertexGenFinalizationThreadSize;
constexpr uint32 FNiagaraRibbonComputeCommon::IndexGenThreadSize;


void FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment, int32 ThreadGroupSize)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	//OutEnvironment.SetDefine(TEXT("NiagaraVFLooseParameters"), TEXT("NiagaraGPURibbonVFLooseParameters"));
	if (ThreadGroupSize > 0)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
}

bool FNiagaraRibbonComputeCommon::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return RHISupportsComputeShaders(Parameters.Platform);		
}


void FNiagaraRibbonSortPhase1CS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, BubbleSortGroupWidth);
}

void FNiagaraRibbonSortPhase2CS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, ThreadGroupSize);
}


void FNiagaraRibbonVertexReductionInitializationCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, FNiagaraRibbonComputeCommon::VertexGenReductionInitializationThreadSize);
}

void FNiagaraRibbonVertexReductionPropagateCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, FNiagaraRibbonComputeCommon::VertexGenReductionPropagationThreadSize);
}

void FNiagaraRibbonVertexReductionFinalizeCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, FNiagaraRibbonComputeCommon::VertexGenReductionFinalizationThreadSize);
}

void FNiagaraRibbonUVParamCalculationCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, FNiagaraRibbonComputeCommon::VertexGenFinalizationThreadSize);
}

void FNiagaraRibbonCreateIndexBufferParamsCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, 1);
}

void FNiagaraRibbonCreateIndexBufferCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	FNiagaraRibbonComputeCommon::ModifyCompilationEnvironment(Parameters, OutEnvironment, FNiagaraRibbonComputeCommon::IndexGenThreadSize);


	// The optimal size is split in half for half above/half below geometry center using two
	// separate loops, and then in half again as the loops handle 2 vertices per slice
	
	const uint32 LoopUnrollSize = FNiagaraRibbonComputeCommon::IndexGenOptimalLoopVertexLimit / 2 / 2;
	
	OutEnvironment.SetDefine(TEXT("LOOP_UNROLL_SIZE"), LoopUnrollSize);
	
}


IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonSortPhase1CS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonSortParticles.usf", "InitialSortList", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonSortPhase2CS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonSortParticles.usf", "MergeSort", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonVertexReductionInitializationCS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonVertexReductionInitialization.usf", "VertexGenInitialize", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonVertexReductionPropagateCS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonVertexReductionPropagation.usf", "VertexGenPrefixSumPropagation", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonVertexReductionFinalizeCS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonVertexReductionFinalization.usf", "OutputRibbonStats", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonUVParamCalculationCS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonRibbonUVParamCalculation.usf", "GenerateRibbonUVParams", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonCreateIndexBufferParamsCS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonInitializeIndices.usf", "InitializeIndices", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FNiagaraRibbonCreateIndexBufferCS, "/Plugin/NiagaraGPURibbonRenderer/Private/Ribbons/NiagaraRibbonGenerateIndices.usf", "GenerateIndices", SF_Compute);

