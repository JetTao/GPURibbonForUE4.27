// 用于实现需要但引擎未导出的符号
#include "NiagaraRendererProperties.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraDataSet.h"
#include "NiagaraRenderer.h"


#if WITH_EDITOR

bool FNiagaraUtilities::AllowComputeShaders(EShaderPlatform ShaderPlatform)
{
	return RHISupportsComputeShaders(ShaderPlatform) && GRHISupportsDrawIndirect;
}

void FNiagaraRendererLayout::Initialize(int32 NumVariables)
{
	VFVariables_GT.Reset(NumVariables);
	VFVariables_GT.AddDefaulted(NumVariables);
	TotalFloatComponents_GT = 0;
	TotalHalfComponents_GT = 0;
}

bool FNiagaraRendererLayout::SetVariable(const FNiagaraDataSetCompiledData* CompiledData, const FNiagaraVariableBase& Variable, int32 VFVarOffset)
{
	// No compiled data, nothing to bind
	if (CompiledData == nullptr)
	{
		return false;
	}

	// use the DataSetVariable to figure out the information about the data that we'll be sending to the renderer
	const int32 VariableIndex = CompiledData->Variables.IndexOfByPredicate(
		[&](const FNiagaraVariable& InVariable)
		{
			return InVariable.GetName() == Variable.GetName();
		}
	);
	if (VariableIndex == INDEX_NONE)
	{
		VFVariables_GT[VFVarOffset] = FNiagaraRendererVariableInfo();
		return false;
	}

	const FNiagaraVariable& DataSetVariable = CompiledData->Variables[VariableIndex];
	const FNiagaraTypeDefinition& VarType = DataSetVariable.GetType();

	const bool bHalfVariable = VarType == FNiagaraTypeDefinition::GetHalfDef()
		|| VarType == FNiagaraTypeDefinition::GetHalfVec2Def()
		|| VarType == FNiagaraTypeDefinition::GetHalfVec3Def()
		|| VarType == FNiagaraTypeDefinition::GetHalfVec4Def();


	const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData->VariableLayouts[VariableIndex];
	const int32 VarSize = bHalfVariable ? sizeof(FFloat16) : sizeof(float);
	const int32 NumComponents = DataSetVariable.GetSizeInBytes() / VarSize;
	const int32 Offset = bHalfVariable ? DataSetVariableLayout.HalfComponentStart : DataSetVariableLayout.FloatComponentStart;
	int32& TotalVFComponents = bHalfVariable ? TotalHalfComponents_GT : TotalFloatComponents_GT;

	int32 GPULocation = INDEX_NONE;
	bool bUpload = true;
	if (Offset != INDEX_NONE)
	{
		if (FNiagaraRendererVariableInfo* ExistingVarInfo = VFVariables_GT.FindByPredicate([&](const FNiagaraRendererVariableInfo& VarInfo) { return VarInfo.DatasetOffset == Offset && VarInfo.bHalfType == bHalfVariable; }))
		{
			//Don't need to upload this var again if it's already been uploaded for another var info. Just point to that.
			//E.g. when custom sorting uses age.
			GPULocation = ExistingVarInfo->GPUBufferOffset;
			bUpload = false;
		}
		else
		{
			//For CPU Sims we pack just the required data tightly in a GPU buffer we upload. For GPU sims the data is there already so we just provide the real data location.
			GPULocation = CompiledData->SimTarget == ENiagaraSimTarget::CPUSim ? TotalVFComponents : Offset;
			TotalVFComponents += NumComponents;
		}
	}

	VFVariables_GT[VFVarOffset] = FNiagaraRendererVariableInfo(Offset, GPULocation, NumComponents, bUpload, bHalfVariable);

	return Offset != INDEX_NONE;
}


bool FNiagaraRendererLayout::SetVariableFromBinding(const FNiagaraDataSetCompiledData* CompiledData, const FNiagaraVariableAttributeBinding& VariableBinding, int32 VFVarOffset)
{
	if (VariableBinding.IsParticleBinding())
		return SetVariable(CompiledData, VariableBinding.GetDataSetBindableVariable(), VFVarOffset);
	return false;
}

void FNiagaraRendererLayout::Finalize()
{
	ENQUEUE_RENDER_COMMAND(NiagaraFinalizeLayout)
	(
		[this, VFVariables=VFVariables_GT,TotalFloatComponents=TotalFloatComponents_GT, TotalHalfComponents=TotalHalfComponents_GT](FRHICommandListImmediate& RHICmdList) mutable
		{
			VFVariables_RT = MoveTemp(VFVariables);
			TotalFloatComponents_RT = TotalFloatComponents;
			TotalHalfComponents_RT = TotalHalfComponents;
		}
	);
}




FNiagaraDataBuffer* FNiagaraDynamicDataBase::GetParticleDataToRender(bool bIsLowLatencyTranslucent)const
{
	FNiagaraDataBuffer* Ret = nullptr;

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		Ret = Data.CPUParticleData;
	}
	else
	{
		Ret = Data.GPUExecContext->GetDataToRender(bIsLowLatencyTranslucent);
	}

	checkSlow(Ret == nullptr || Ret->IsBeingRead());
	return Ret;
}

FParticleRenderData FNiagaraRenderer::TransferDataToGPU(FGlobalDynamicReadBuffer& DynamicReadBuffer, const FNiagaraRendererLayout* RendererLayout, TConstArrayView<uint32> IntComponents, const FNiagaraDataBuffer* SrcData)
{
	const int32 NumInstances = SrcData->GetNumInstances();
	const int32 TotalFloatSize = RendererLayout->GetTotalFloatComponents_RenderThread() * NumInstances;
	const int32 TotalHalfSize = RendererLayout->GetTotalHalfComponents_RenderThread() * NumInstances;
	const int32 TotalIntSize = IntComponents.Num() * NumInstances;

	FParticleRenderData Allocation;
	Allocation.FloatData = TotalFloatSize ? DynamicReadBuffer.AllocateFloat(TotalFloatSize) : FGlobalDynamicReadBuffer::FAllocation();
	Allocation.HalfData = TotalHalfSize ? DynamicReadBuffer.AllocateHalf(TotalHalfSize) : FGlobalDynamicReadBuffer::FAllocation();
	Allocation.IntData = TotalIntSize ? DynamicReadBuffer.AllocateInt32(TotalIntSize) : FGlobalDynamicReadBuffer::FAllocation();

	Allocation.FloatStride = TotalFloatSize ? NumInstances * sizeof(float) : 0;
	Allocation.HalfStride = TotalHalfSize ? NumInstances * sizeof(FFloat16) : 0;
	Allocation.IntStride = TotalIntSize ? NumInstances * sizeof(int32) : 0;

	for (const FNiagaraRendererVariableInfo& VarInfo : RendererLayout->GetVFVariables_RenderThread())
	{
		int32 GpuOffset = VarInfo.GetGPUOffset();
		if (GpuOffset != INDEX_NONE && VarInfo.bUpload)
		{
			if (VarInfo.bHalfType)
			{
				GpuOffset &= ~(1 << 31);
				for (int32 CompIdx = 0; CompIdx < VarInfo.NumComponents; ++CompIdx)
				{
					const uint8* SrcComponent = SrcData->GetComponentPtrHalf(VarInfo.DatasetOffset + CompIdx);
					void* Dest = Allocation.HalfData.Buffer + Allocation.HalfStride * (GpuOffset + CompIdx);
					FMemory::Memcpy(Dest, SrcComponent, Allocation.HalfStride);
				}
			}
			else
			{
				for (int32 CompIdx = 0; CompIdx < VarInfo.NumComponents; ++CompIdx)
				{
					const uint8* SrcComponent = SrcData->GetComponentPtrFloat(VarInfo.DatasetOffset + CompIdx);
					void* Dest = Allocation.FloatData.Buffer + Allocation.FloatStride * (GpuOffset + CompIdx);
					FMemory::Memcpy(Dest, SrcComponent, Allocation.FloatStride);
				}
			}
		}
	}

	if (TotalIntSize > 0)
	{
		for (int i=0; i < IntComponents.Num(); ++i )
		{
			uint8* Dst = Allocation.IntData.Buffer + Allocation.IntStride * i;
			const uint8* Src = SrcData->GetComponentPtrInt32(IntComponents[i]);
			FMemory::Memcpy(Dst, Src, Allocation.IntStride);
		}
	}

	return Allocation;
}

#endif