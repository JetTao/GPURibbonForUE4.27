// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererGPURibbons.h"

#include "GPUSortManager.h"
#include "ParticleResources.h"
#include "NiagaraGPURibbonVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraStats.h"
#include "NiagaraComponent.h"
#include "RayTracingDefinitions.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingInstance.h"
#include "Math/NumericLimits.h"
#include "NiagaraRibbonCompute.h"
#include "Misc/LazySingleton.h"

DECLARE_CYCLE_STAT(TEXT("Render GPU Ribbons [RT]"), STAT_NiagaraRenderGPURibbons, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("GPU Ribbons GenerateUniformBuffer [RT]"), STAT_NiagaraGPURibbonsGenerateUniformBuffer, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Ribbons GenerateVFLooseParameter [RT]"), STAT_NiagaraGPURibbonsGenerateVFLooseParameter, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Generate Indices GPU [RT]"), STAT_NiagaraRenderGPURibbonsGenIndicies, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU [RT]"), STAT_NiagaraRenderGPURibbonsGenVertices, STATGROUP_Niagara);


DECLARE_STATS_GROUP(TEXT("NiagaraRibbons"), STATGROUP_NiagaraRibbons, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - Sort [RT]"), STAT_NiagaraRenderRibbonsGenVerticesSortGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - InitialSort [RT]"), STAT_NiagaraRenderRibbonsGenVerticesInitialSortGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - FinalSort [RT]"), STAT_NiagaraRenderRibbonsGenVerticesFinalSortGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - Reduction Phase 1 [RT]"), STAT_NiagaraRenderRibbonsGenVerticesReductionPhase1GPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - Reduction Init [RT]"), STAT_NiagaraRenderRibbonsGenVerticesReductionInitGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - Reduction Propagate [RT]"), STAT_NiagaraRenderRibbonsGenVerticesReductionPropagateGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - Reduction Tessellation [RT]"), STAT_NiagaraRenderRibbonsGenVerticesReductionTessellationGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - Reduction Phase 2 [RT]"), STAT_NiagaraRenderRibbonsGenVerticesReductionPhase2GPU, STATGROUP_NiagaraRibbons);

DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - Reduction Finalize [RT]"), STAT_NiagaraRenderRibbonsGenVerticesReductionFinalizeGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - MultiRibbon Init [RT]"), STAT_NiagaraRenderRibbonsGenVerticesMultiRibbonInitGPU, STATGROUP_NiagaraRibbons);
DECLARE_CYCLE_STAT(TEXT("Generate Vertices GPU - MultiRibbon Init Compute [RT]"), STAT_NiagaraRenderRibbonsGenVerticesMultiRibbonInitComputeGPU, STATGROUP_NiagaraRibbons);

DECLARE_GPU_STAT_NAMED(NiagaraGPURibbonRenderers, TEXT("Niagara GPU Ribbons"));

int32 GNiagaraGPURibbonTessellationEnabled = 1;
static FAutoConsoleVariableRef CVarNiagaraGPURibbonTessellationEnabled(
	TEXT("Niagara.GPURibbon.Tessellation.Enabled"),
	GNiagaraGPURibbonTessellationEnabled,
	TEXT("Determine if we allow tesellation on this platform or not."),
	ECVF_Scalability
);

float GNiagaraGPURibbonTessellationAngle = 15.f * (2.f * PI) / 360.f; // Every 15 degrees
static FAutoConsoleVariableRef CVarNiagaraGPURibbonTessellationAngle(
	TEXT("Niagara.GPURibbon.Tessellation.MinAngle"),
	GNiagaraGPURibbonTessellationAngle,
	TEXT("Ribbon segment angle to tesselate in radian. (default=15 degrees)"),
	ECVF_Scalability
);

int32 GNiagaraGPURibbonMaxTessellation = 16;
static FAutoConsoleVariableRef CVarNiagaraGPURibbonMaxTessellation(
	TEXT("Niagara.GPURibbon.Tessellation.MaxInterp"),
	GNiagaraGPURibbonMaxTessellation,
	TEXT("When TessellationAngle is > 0, this is the maximum tesselation factor. \n")
	TEXT("Higher values allow more evenly divided tesselation. \n")
	TEXT("When TessellationAngle is 0, this is the actually tesselation factor (default=16)."),
	ECVF_Scalability
);

float GNiagaraGPURibbonTessellationScreenPercentage = 0.002f;
static FAutoConsoleVariableRef CVarNiagaraGPURibbonTessellationScreenPercentage(
	TEXT("Niagara.GPURibbon.Tessellation.MaxErrorScreenPercentage"),
	GNiagaraGPURibbonTessellationScreenPercentage,
	TEXT("Screen percentage used to compute the tessellation factor. \n")
	TEXT("Smaller values will generate more tessellation, up to max tesselltion. (default=0.002)"),
	ECVF_Scalability
);

float GNiagaraGPURibbonTessellationMinDisplacementError = 0.5f;
static FAutoConsoleVariableRef CVarNiagaraGPURibbonTessellationMinDisplacementError(
	TEXT("Niagara.GPURibbon.Tessellation.MinAbsoluteError"),
	GNiagaraGPURibbonTessellationMinDisplacementError,
	TEXT("Minimum absolute world size error when tessellating. \n")
	TEXT("Prevent over tessellating when distance gets really small. (default=0.5)"),
	ECVF_Scalability
);

float GNiagaraGPURibbonMinSegmentLength = 1.f;
static FAutoConsoleVariableRef CVarNiagaraGPURibbonMinSegmentLength(
	TEXT("Niagara.GPURibbon.MinSegmentLength"),
	GNiagaraGPURibbonMinSegmentLength,
	TEXT("Min length of niagara ribbon segments. (default=1)"),
	ECVF_Scalability
);

static int32 GbEnableNiagaraGPURibbonRendering = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraGPURibbonRendering(
	TEXT("fx.EnableNiagaraGPURibbonRendering"),
	GbEnableNiagaraGPURibbonRendering,
	TEXT("If == 0, Niagara Ribbon Renderers are disabled. \n"),
	ECVF_Default
);

static TAutoConsoleVariable<int32> CVarRayTracingNiagaraGPURibbons(
	TEXT("r.RayTracing.Geometry.GPUNiagaraRibbons"),
	1,
	TEXT("Include Niagara ribbons in ray tracing effects (default = 1 (Niagara ribbons enabled in ray tracing))"));


static int32 GbEnableNiagaraRibbonComputeShaderGen = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraRibbonComputeShaderGen(
	TEXT("fx.EnableNiagaraURibbonComputeShaderGen"),
	GbEnableNiagaraRibbonComputeShaderGen,
	TEXT("If == 1, Niagara Ribbon Renderer will use compute shaders if available to generate intermediate data. \n"),
	ECVF_Default
);

static int32 GbForceNiagaraRibbonGPUInit = 0;
static FAutoConsoleVariableRef CVarForceNiagaraRibbonGPUInit(
	TEXT("fx.ForceNiagaraRibbonGPUInit"),
	GbForceNiagaraRibbonGPUInit,
	TEXT("If == 1, Niagara Ribbon Renderer will use compute shader based initialization for all CPU systems. \n"),
	ECVF_Default
);


// max absolute error 9.0x10^-3
// Eberly's polynomial degree 1 - respect bounds
// input [-1, 1] and output [0, PI]
FORCEINLINE float AcosFast(float InX)
{
	float X = FMath::Abs(InX);
	float Res = -0.156583f * X + (0.5 * PI);
	Res *= sqrt(FMath::Max(0.f, 1.0f - X));
	return (InX >= 0) ? Res : PI - Res;
}

// Calculates the number of bits needed to store a maximum value
FORCEINLINE uint32 CalculateBitsForRange(uint32 Range)
{
	return FMath::CeilToInt(FMath::Loge(static_cast<float>(Range)) / FMath::Loge(static_cast<float>(2)));
}

// Generates the mask to remove unecessary bits after a range of bits
FORCEINLINE uint32 CalculateBitMask(uint32 NumBits)
{
	return static_cast<uint32>(static_cast<uint64>(0xFFFFFFFF) >> (32 - NumBits));
}

struct FTessellationStatsEntry
{
	static constexpr int32 NumElements = 5;
	
	float TotalLength;
	float AverageSegmentLength;
	float AverageSegmentAngle;
	float AverageTwistAngle;
	float AverageWidth;
};

struct FTessellationStatsEntryNoTwist
{
	static constexpr int32 NumElements = 3;
	
	float TotalLength;
	float AverageSegmentLength;
	float AverageSegmentAngle;
};

struct FNiagaraRibbonCommandBufferLayout
{
	static constexpr int32 NumElements = 15;
	
	uint32 FinalizationIndirectArgsXDim;
	uint32 FinalizationIndirectArgsYDim;
	uint32 FinalizationIndirectArgsZDim;
	uint32 NumSegments;
	uint32 NumRibbons;
	
	float Tessellation_Angle;
	float Tessellation_Curvature;
	float Tessellation_TwistAngle;
	float Tessellation_TwistCurvature;
	float Tessellation_TotalLength;

	float TessCurrentFrame_TotalLength;
	float TessCurrentFrame_AverageSegmentLength;
	float TessCurrentFrame_AverageSegmentAngle;
	float TessCurrentFrame_AverageTwistAngle;
	float TessCurrentFrame_AverageWidth;
};


struct FNiagaraRibbonIndirectDrawBufferLayout
{
	static constexpr int32 NumElements = 12;
	static constexpr int32 GenerateIndicesCommandOffset = 0;
	static constexpr int32 IndirectDrawCommandIndex = 4;
	static constexpr int32 IndirectDrawCommandByteOffset = IndirectDrawCommandIndex * sizeof(uint32);

	// This is passed from InitializeIndices to GenerateIndices
	uint32 IndexGenIndirectArgsXDim;
	uint32 IndexGenIndirectArgsYDim;
	uint32 IndexGenIndirectArgsZDim;
	uint32 TessellationFactor;

	// This is the indirect draw args and then resulting information for the vertex shader	
	uint32 IndexCount;
	uint32 NumInstances;
	uint32 FirstIndexOffset;
	uint32 FirstVertexOffset;
	uint32 FirstInstanceOffset;
	
	uint32 NumSegments;
	uint32 NumSubSegments;
	float OneOverSubSegmentCount;	
};


struct FRWIndexBuffer final : FIndexBuffer
{
	FUnorderedAccessViewRHIRef UAV;

	FRWIndexBuffer()
	{}

	virtual ~FRWIndexBuffer() override
	{
		FRenderResource::ReleaseResource();
	}

	void Initialize(const TCHAR* InDebugName, const uint32 BytesPerElement, const uint32 NumElements, const EPixelFormat Format, const ERHIAccess InResourceState, const EBufferUsageFlags AdditionalUsage = BUF_None, FResourceArrayInterface *InResourceArray = nullptr)
	{
		InitResource();
		
		// Provide a debug name if using Fast VRAM so the allocators diagnostics will work
		ensure(!(EnumHasAnyFlags(AdditionalUsage, BUF_FastVRAM) && !InDebugName));
		const int32 NumBytes = BytesPerElement * NumElements;
		FRHIResourceCreateInfo CreateInfo(InDebugName);
		CreateInfo.ResourceArray = InResourceArray;
		IndexBufferRHI = RHICreateIndexBuffer(BytesPerElement, NumBytes, AdditionalUsage, InResourceState, CreateInfo);
		if (EnumHasAnyFlags(AdditionalUsage, BUF_UnorderedAccess))
		{
			UAV = RHICreateUnorderedAccessView(IndexBufferRHI, static_cast<uint8>(Format));
		}
	}

	virtual void ReleaseRHI() override
	{
		UAV.SafeRelease();

		FIndexBuffer::ReleaseRHI();
	}
};


struct FNiagaraDynamicDataGPURibbon : public FNiagaraDynamicDataBase
{
	FNiagaraDynamicDataGPURibbon(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
		, Material(nullptr)
		, MaxAllocatedParticleCount(0)
	{
	}

	// virtual void ApplyMaterialOverride(int32 MaterialIndex, UMaterialInterface* MaterialOverride) override
	// {
	// 	if (MaterialIndex == 0 && MaterialOverride)
	// 	{
	// 		Material = MaterialOverride->GetRenderProxy();
	// 	}
	// }

	
	/** Material to use passed to the Renderer. */
	FMaterialRenderProxy* Material;
	int32 MaxAllocatedParticleCount;
};


struct FNiagaraRibbonRenderingFrameViewResources
{
	FNiagaraGPURibbonVertexFactory VertexFactory;
	FNiagaraGPURibbonUniformBufferRef UniformBuffer;

	FRWIndexBuffer IndexBuffer;
	FRWBuffer IndirectDrawBuffer;

	FNiagaraIndexGenerationInput IndexGenerationSettings;

	int32 IndirectDrawBufferStartOffset = FNiagaraRibbonIndirectDrawBufferLayout::IndirectDrawCommandIndex;
	int32 IndirectDrawBufferStartByteOffset = FNiagaraRibbonIndirectDrawBufferLayout::IndirectDrawCommandByteOffset;

	~FNiagaraRibbonRenderingFrameViewResources()
	{
		UniformBuffer.SafeRelease();
		VertexFactory.ReleaseResource();
		IndexBuffer.ReleaseResource();
		IndirectDrawBuffer.Release();
	}
};

struct FNiagaraRibbonRenderingFrameResources
{
	TArray<TSharedPtr<FNiagaraRibbonRenderingFrameViewResources>> ViewResources;

	FParticleRenderData ParticleData;
		
	FRHIShaderResourceView*	ParticleFloatSRV;
	FRHIShaderResourceView* ParticleHalfSRV;
	FRHIShaderResourceView* ParticleIntSRV;
	
	int32 ParticleFloatDataStride = INDEX_NONE;
	int32 ParticleHalfDataStride = INDEX_NONE;
	int32 ParticleIntDataStride = INDEX_NONE;
	
	int32 RibbonIdParamOffset = INDEX_NONE;
	
	~FNiagaraRibbonRenderingFrameResources()
	{
		ViewResources.Empty();

		ParticleFloatSRV = nullptr;
		ParticleHalfSRV = nullptr;
		ParticleIntSRV = nullptr;

		ParticleFloatDataStride = INDEX_NONE;
		ParticleHalfDataStride = INDEX_NONE;
		ParticleIntDataStride = INDEX_NONE;
		
		RibbonIdParamOffset = INDEX_NONE;
	}	
};

class FNiagaraRibbonMeshCollectorResources : public FOneFrameResource
{
public:
	TSharedRef<FNiagaraRibbonRenderingFrameResources> RibbonResources;

	FNiagaraRibbonMeshCollectorResources()
		: RibbonResources(new FNiagaraRibbonRenderingFrameResources())
	{
		
	}
};

bool FNiagaraRibbonVertexBuffers::InitOrUpdateBuffer(bool bEnabled, FRWBuffer& Buffer, int32& CurrentLength, int32 NeededLength, int32 MaxLength, FRWBuffer(* InitFunction)(int32, ERHIAccess), ERHIAccess InitialAccessFlags)
{
	static constexpr float UpsizeMultipler = 1.1f;
	static constexpr float DownsizeMultiplier = 1.2f;

	bEnabled &= NeededLength > 0;

	if (bEnabled)
	{
		check(NeededLength <= MaxLength);
		
		// we resize the buffer if it's too small or we're over the allowed free space.
		if (CurrentLength < NeededLength || CurrentLength > (NeededLength * DownsizeMultiplier))
		{
			const int32 NewLength = FMath::Min(MaxLength, FMath::RoundToInt(NeededLength * UpsizeMultipler));

			Buffer = InitFunction(NewLength, InitialAccessFlags);
			CurrentLength = NewLength;

			return true;
		}
	}
	else if (CurrentLength != 0)
	{
		Buffer.Release();
		CurrentLength = 0;
		return true;
	}

	return false;
}

void FNiagaraRibbonVertexBuffers::InitializeOrUpdateBuffers(const FNiagaraRibbonGenerationConfig& GenerationConfig, const FNiagaraDataBuffer* SourceParticleData, int32 MaxAllocatedCount)
{	
	const uint32 MaxAllocatedRibbons = GenerationConfig.HasRibbonIDs()? (GenerationConfig.GetMaxNumRibbons() > 0? GenerationConfig.GetMaxNumRibbons() : MaxAllocatedCount / 2) : 1;
	
	constexpr ERHIAccess InitialBufferAccessFlags = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer;
	
	{		
		const uint32 TotalParticles = SourceParticleData->GetNumInstancesAllocated();

		// We assume to have at least 2 particles in each ribbon, therefor we can't have more than instances/2 ribbons
		const int32 TotalRibbons = FMath::Clamp<int32>(TotalParticles / 2, 1, MaxAllocatedRibbons);
		
		InitOrUpdateBuffer(true,									SortedIndicesBuffer, SortedIndicesLength, TotalParticles, MaxAllocatedCount, &CreateSortedIndicesBuffer, InitialBufferAccessFlags);
		InitOrUpdateBuffer(true,									TangentsAndDistancesBuffer, TangentsLength, TotalParticles, MaxAllocatedCount, &CreateTangentsAndDistancesBuffer, InitialBufferAccessFlags);
		InitOrUpdateBuffer(GenerationConfig.HasRibbonIDs(),		MultiRibbonIndicesBuffer, MultiRibbonIndexLength, TotalParticles, MaxAllocatedCount, &CreateMultiRibbonIndicesBuffer, InitialBufferAccessFlags);
		InitOrUpdateBuffer(true,									RibbonLookupTableBuffer, RibbonLookupTableLength, TotalRibbons, MaxAllocatedRibbons, &CreateRibbonLookupTableBuffer, InitialBufferAccessFlags);
		InitOrUpdateBuffer(true,									SegmentsBuffer, SegmentLength, TotalParticles, MaxAllocatedCount, &CreateSegmentsBuffer, InitialBufferAccessFlags);
		bJustCreatedCommandBuffer |= InitOrUpdateBuffer(true,	GPUComputeCommandBuffer, GPUComputeCommandLength, FNiagaraRibbonCommandBufferLayout::NumElements, FNiagaraRibbonCommandBufferLayout::NumElements, &CreateCommandBuffer, InitialBufferAccessFlags | ERHIAccess::IndirectArgs);
	}	
}


FRWBuffer FNiagaraRibbonVertexBuffers::CreateSortedIndicesBuffer(int32 Size, ERHIAccess InitialAccessFlags)
{
	FRWBuffer NewBuffer;
	NewBuffer.Initialize(sizeof(uint32), Size, EPixelFormat::PF_R32_UINT, InitialAccessFlags, BUF_Static);
	return NewBuffer;
}

FRWBuffer FNiagaraRibbonVertexBuffers::CreateTangentsAndDistancesBuffer(int32 Size, ERHIAccess InitialAccessFlags)
{
	FRWBuffer NewBuffer;
	NewBuffer.Initialize(sizeof(float), Size * 4, EPixelFormat::PF_R32_FLOAT, InitialAccessFlags, BUF_Static);
	return NewBuffer;
}

FRWBuffer FNiagaraRibbonVertexBuffers::CreateMultiRibbonIndicesBuffer(int32 Size, ERHIAccess InitialAccessFlags)
{
	FRWBuffer NewBuffer;
	NewBuffer.Initialize(sizeof(uint32), Size, EPixelFormat::PF_R32_UINT, InitialAccessFlags, BUF_Static);
	return NewBuffer;		
}

FRWBuffer FNiagaraRibbonVertexBuffers::CreateRibbonLookupTableBuffer(int32 Size, ERHIAccess InitialAccessFlags)
{
	FRWBuffer NewBuffer;
	NewBuffer.Initialize(sizeof(uint32), Size * FRibbonMultiRibbonInfoBufferEntry::NumElements, EPixelFormat::PF_R32_UINT, InitialAccessFlags, BUF_Static);
	return NewBuffer;		
}

FRWBuffer FNiagaraRibbonVertexBuffers::CreateSegmentsBuffer(int32 Size, ERHIAccess InitialAccessFlags)
{
	FRWBuffer NewBuffer;
	NewBuffer.Initialize(sizeof(uint32), Size, EPixelFormat::PF_R32_UINT, InitialAccessFlags, BUF_Static);
	return NewBuffer;		
}

FRWBuffer FNiagaraRibbonVertexBuffers::CreateCommandBuffer(int32 Size, ERHIAccess InitialAccessFlags)
{
	FRWBuffer NewBuffer;
	NewBuffer.Initialize(sizeof(uint32), FNiagaraRibbonCommandBufferLayout::NumElements, EPixelFormat::PF_R32_UINT, InitialAccessFlags, BUF_DrawIndirect | BUF_Static);
	return NewBuffer;		
}

struct FNiagaraRibbonGPUInitParameters
{
	const FNiagaraRendererGPURibbons* Renderer;
	NiagaraEmitterInstanceBatcher* Batcher;
	const FNiagaraDataBuffer* SourceParticleData;
	TWeakPtr<FNiagaraRibbonRenderingFrameResources> RenderingResources;
	
	FNiagaraRibbonGPUInitParameters(const FNiagaraRendererGPURibbons* InRenderer, NiagaraEmitterInstanceBatcher* InBatcher,
		const FNiagaraDataBuffer* InSourceParticleData, const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& InRenderingResources)
		: Renderer(InRenderer)
		, Batcher(InBatcher)
		, SourceParticleData(InSourceParticleData)
		, RenderingResources(InRenderingResources)
	{
		
	}	 
};

struct FNiagaraRibbonGPUInitComputeBuffers
{
	FRWBuffer SortBuffer;
	FRWBuffer TempSegments;
	FRWBuffer TempDistances;
	FRWBuffer TempMultiRibbon;
	FRWBuffer TempTessellationStats[2];
	
	FNiagaraRibbonGPUInitComputeBuffers() { }

	void InitOrUpdateBuffers(int32 NeededSize, bool bWantsMultiRibbon, bool bWantsTessellation, bool bWantsTessellationTwist)
	{
		// TODO: Downsize these when we haven't needed the size for a bit

		constexpr ERHIAccess InitialAccess = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer;
		
		if (SortBuffer.NumBytes < NeededSize * sizeof(int32))
		{
			SortBuffer.Initialize(sizeof(uint32), NeededSize,
			                      EPixelFormat::PF_R32_UINT, InitialAccess);
		}

		if (TempSegments.NumBytes < NeededSize * sizeof(int32))
		{
			TempSegments.Initialize(sizeof(uint32), NeededSize,
			                        EPixelFormat::PF_R32_UINT, InitialAccess, BUF_Static);
		}

		if (TempDistances.NumBytes < NeededSize * sizeof(float) * 4)
		{
			TempDistances.Initialize(sizeof(float), NeededSize * 4,
			                         EPixelFormat::PF_R32_FLOAT, InitialAccess, BUF_Static);
		}

		const uint32 MultiRibbonBufferSize = NeededSize * (bWantsMultiRibbon? 1 : 0);
		if (TempMultiRibbon.NumBytes < MultiRibbonBufferSize * sizeof(int32))
		{
			TempMultiRibbon.Initialize(sizeof(uint32), MultiRibbonBufferSize, EPixelFormat::PF_R32_UINT,
				InitialAccess, BUF_Static);
		}

		const uint32 TessellationBufferSize = NeededSize * (bWantsTessellation? (bWantsTessellationTwist? FTessellationStatsEntry::NumElements : FTessellationStatsEntryNoTwist::NumElements) : 0);
		if (TempTessellationStats[0].NumBytes < TessellationBufferSize * sizeof(float))
		{
			TempTessellationStats[0].Initialize(sizeof(float), TessellationBufferSize, EPixelFormat::PF_R32_FLOAT, InitialAccess, BUF_Static);
			TempTessellationStats[1].Initialize(sizeof(float), TessellationBufferSize, EPixelFormat::PF_R32_FLOAT, InitialAccess, BUF_Static);
		}
	}	
};



class FNiagaraRibbonComputeDispatchManager	
{
	friend class FLazySingleton;
private:
	TArray<FNiagaraRibbonGPUInitParameters> RenderersToGenerate;

	FGPUSortManager* SortManager;	
	FDelegateHandle SortManagerEventRef;

	FNiagaraRibbonGPUInitComputeBuffers ComputeBuffers;
	
	void RegisterForEvent(FGPUSortManager* InSortManager)
	{
		if (!SortManagerEventRef.IsValid())
		{
			SortManager = InSortManager;
			SortManagerEventRef = SortManager->PostPreRenderEvent.AddRaw(this, &FNiagaraRibbonComputeDispatchManager::OnPostPreRender);
		}
	}

	void OnPostPreRender(FRHICommandListImmediate& CMDList)
	{
		GenerateAllGPUData(CMDList);
		
		SortManager->PostPreRenderEvent.Remove(SortManagerEventRef);
		SortManagerEventRef.Reset();
		SortManager = nullptr;
	}

	void GenerateAllGPUData(FRHICommandListImmediate& CMDList);

public:
	static FNiagaraRibbonComputeDispatchManager& Get()
	{
		return TLazySingleton<FNiagaraRibbonComputeDispatchManager>::Get();
	}

	void RegisterRenderer(FGPUSortManager* InSortManager, FNiagaraRibbonGPUInitParameters NewRegistration)
	{
		RenderersToGenerate.Add(NewRegistration);
		RegisterForEvent(InSortManager);
	}
	
	void UnRegister(FNiagaraRendererGPURibbons* Renderer)
	{		
		RenderersToGenerate.RemoveAll([Renderer](const FNiagaraRibbonGPUInitParameters& Params) { return Params.Renderer == Renderer; });
	}
};



FNiagaraRendererGPURibbons::FNiagaraRendererGPURibbons(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProps, Emitter)
	, GenerationConfig(CastChecked<const UNiagaraGPURibbonRendererProperties>(InProps))
	, FacingMode(ENiagaraGPURibbonFacingMode::Screen)
{
	const UNiagaraGPURibbonRendererProperties* Properties = CastChecked<const UNiagaraGPURibbonRendererProperties>(InProps);

	int32 IgnoredFloatOffset, IgnoredHalfOffset;
	Emitter->GetData().GetVariableComponentOffsets(Properties->RibbonIdBinding.GetDataSetBindableVariable(), IgnoredFloatOffset, RibbonIDParamDataSetOffset, IgnoredHalfOffset);

	// Check we actually have ribbon id if we claim we do
	check(!GenerationConfig.HasRibbonIDs() || RibbonIDParamDataSetOffset != INDEX_NONE);
	
	UV0Settings = Properties->UV0Settings;
	UV1Settings = Properties->UV1Settings;
	FacingMode = Properties->FacingMode;
	DrawDirection = Properties->DrawDirection;
	RendererLayout = &Properties->RendererLayout;
	
	InitializeShape(Properties);
	InitializeTessellation(Properties);	
}

FNiagaraRendererGPURibbons::~FNiagaraRendererGPURibbons()
{
	FNiagaraRibbonComputeDispatchManager::Get().UnRegister(this);
}

// FPrimitiveSceneProxy interface.
void FNiagaraRendererGPURibbons::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::CreateRenderThreadResources(Batcher);


	{
		// Initialize the shape vertex buffer. This doesn't change frame-to-frame, so we can set it up once
		const int32 NumElements = ShapeState.SliceTriangleToVertexIds.Num();
		ShapeState.SliceTriangleToVertexIdsBuffer.Initialize(sizeof(uint32), NumElements, EPixelFormat::PF_R32_UINT, BUF_Static);
		void* SliceTriangleToVertexIdsBufferPtr = RHILockVertexBuffer(ShapeState.SliceTriangleToVertexIdsBuffer.Buffer, 0, sizeof(uint32) * NumElements, RLM_WriteOnly);
		FMemory::Memcpy(SliceTriangleToVertexIdsBufferPtr, ShapeState.SliceTriangleToVertexIds.GetData(), sizeof(uint32) * NumElements);
		RHIUnlockVertexBuffer(ShapeState.SliceTriangleToVertexIdsBuffer.Buffer);
	}

	{
		// Initialize the shape vertex buffer. This doesn't change frame-to-frame, so we can set it up once
		const int32 NumElements = ShapeState.SliceVertexData.Num() * FNiagaraRibbonShapeGeometryData::FVertex::NumElements;
		ShapeState.SliceVertexDataBuffer.Initialize(sizeof(float), NumElements, EPixelFormat::PF_R32_FLOAT, BUF_Static);
		void* SliceVertexDataBufferPtr = RHILockVertexBuffer(ShapeState.SliceVertexDataBuffer.Buffer, 0, sizeof(float) * NumElements, RLM_WriteOnly);
		FMemory::Memcpy(SliceVertexDataBufferPtr, ShapeState.SliceVertexData.GetData(), sizeof(float) * NumElements);
		RHIUnlockVertexBuffer(ShapeState.SliceVertexDataBuffer.Buffer);
	}
	
	
#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		FRayTracingGeometryInitializer Initializer;
		static const FName DebugName("FNiagaraRendererRibbons");
		static int32 DebugNumber = 0;
		Initializer.DebugName = DebugName;
		Initializer.IndexBuffer = nullptr;
		Initializer.TotalPrimitiveCount = 0;
		Initializer.GeometryType = RTGT_Triangles;
		Initializer.bFastBuild = true;
		Initializer.bAllowUpdate = false;
		RayTracingGeometry.SetInitializer(Initializer);
		RayTracingGeometry.InitResource();
	}
#endif
}

void FNiagaraRendererGPURibbons::ReleaseRenderThreadResources()
{
	FNiagaraRenderer::ReleaseRenderThreadResources();

	ShapeState.SliceTriangleToVertexIdsBuffer.Release();
	ShapeState.SliceVertexDataBuffer.Release();
	
#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		RayTracingGeometry.ReleaseResource();
		RayTracingDynamicVertexBuffer.Release();
	}
#endif
}

void FNiagaraRendererGPURibbons::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderGPURibbons);
	PARTICLE_PERF_STAT_CYCLES_RT(SceneProxy->PerfStatsContext, GetDynamicMeshElements);

	FNiagaraDynamicDataGPURibbon* DynamicData = static_cast<FNiagaraDynamicDataGPURibbon*>(DynamicDataRender);
	if (!DynamicData)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicData->GetParticleDataToRender();

	if (GbEnableNiagaraGPURibbonRendering == 0 || SourceParticleData == nullptr)
	{
		return;
	}

	// Bail if we don't have enough particle data to have a valid ribbon
	// or if somehow the sim targets don't match
	if (SimTarget != ENiagaraSimTarget::GPUComputeSim || SourceParticleData->GetNumInstancesAllocated() < 2)
	{
		return;
	}

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif
	
	auto* Batcher = SceneProxy->GetBatcher();

	const FNiagaraRibbonMeshCollectorResources& RenderingResources = Collector.AllocateOneFrameResource<FNiagaraRibbonMeshCollectorResources>();
		
	InitializeVertexBuffersResources(DynamicData, SourceParticleData, Collector.GetDynamicReadBuffer(), RenderingResources.RibbonResources);
	
	// Compute the per-view uniform buffers.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			check(View);

			if (View->bIsInstancedStereoEnabled && IStereoRendering::IsStereoEyeView(*View) && !IStereoRendering::IsAPrimaryView(*View))
			{
				// We don't have to generate batches for non-primary views in stereo instance rendering
				continue;
			}
			
			FMeshBatch& MeshBatch = Collector.AllocateMesh();
			
			const FVector ViewOriginForDistanceCulling = View->ViewMatrices.GetViewOrigin();

			auto& RenderingViewResources = RenderingResources.RibbonResources->ViewResources.Add_GetRef(MakeShared<FNiagaraRibbonRenderingFrameViewResources>());
			RenderingViewResources->IndexGenerationSettings = CalculateIndexBufferConfiguration(SourceParticleData, SceneProxy, View, ViewOriginForDistanceCulling);
			
			GenerateIndexBufferForView(RenderingViewResources->IndexGenerationSettings, DynamicData, RenderingViewResources, View, ViewOriginForDistanceCulling);
			
			SetupPerViewUniformBuffer(RenderingViewResources->IndexGenerationSettings, View, ViewFamily, SceneProxy, RenderingViewResources->UniformBuffer);
			
			SetupMeshBatchAndCollectorResourceForView(RenderingViewResources->IndexGenerationSettings, DynamicData, SourceParticleData, View, ViewFamily, SceneProxy, RenderingResources.RibbonResources, RenderingViewResources, MeshBatch);

			Collector.AddMesh(ViewIndex, MeshBatch);
		}
	}

	// Register this renderer for generation this frame if we're a gpu system or using gpu init
	{
		FGPUSortManager* SortManager = Batcher->GetGPUSortManager();
		FNiagaraRibbonComputeDispatchManager::Get().RegisterRenderer(SortManager, FNiagaraRibbonGPUInitParameters(this, Batcher, SourceParticleData, RenderingResources.RibbonResources));
	}
}

FNiagaraDynamicDataBase* FNiagaraRendererGPURibbons::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter)const
{
	check(Emitter && Emitter->GetParentSystemInstance());

	FNiagaraDynamicDataGPURibbon* DynamicData = nullptr;
	const UNiagaraGPURibbonRendererProperties* Properties = CastChecked<const UNiagaraGPURibbonRendererProperties>(InProperties);

	if (Properties)
	{
		// if (!IsRendererEnabled(Properties, Emitter))
		// {
		// 	return nullptr;
		// }

		// if (InProperties->bAllowInCullProxies == false &&
		// 	Cast<UNiagaraCullProxyComponent>(Emitter->GetParentSystemInstance()->GetAttachComponent()) != nullptr)
		// {
		// 	return nullptr;
		// }

		FNiagaraDataBuffer* DataToRender = Emitter->GetData().GetCurrentData();
		if (DataToRender == nullptr || DataToRender->GetNumInstances() < 2 || !Properties->PositionDataSetAccessor.IsValid() || !Properties->SortKeyDataSetAccessor.IsValid())
		{
			return nullptr;
		}
		
		if(SimTarget == ENiagaraSimTarget::GPUComputeSim || (DataToRender != nullptr && DataToRender->GetNumInstances() > 1))
		{
			DynamicData = new FNiagaraDynamicDataGPURibbon(Emitter);
	
			//In preparation for a material override feature, we pass our material(s) and relevance in via dynamic data.
			//The renderer ensures we have the correct usage and relevance for materials in BaseMaterials_GT.
			//Any override feature must also do the same for materials that are set.
			check(BaseMaterials_GT.Num() == 1);
			check(BaseMaterials_GT[0]->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraRibbons));
			DynamicData->Material = BaseMaterials_GT[0]->GetRenderProxy();
			DynamicData->SetMaterialRelevance(BaseMaterialRelevance_GT);
		}

		if (DynamicData)
		{		
			DynamicData->MaxAllocatedParticleCount = Emitter->GetData().GetMaxInstanceCount() + 1;
			
		}

		if (DynamicData && Properties->MaterialParameterBindings.Num() != 0)
		{
			ProcessMaterialParameterBindings(MakeArrayView(Properties->MaterialParameterBindings), Emitter, MakeArrayView(BaseMaterials_GT));
		}
	}
	
	return DynamicData;
}

int FNiagaraRendererGPURibbons::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataGPURibbon);

	Size += ShapeState.SliceVertexData.GetAllocatedSize();

	return Size;
}

bool FNiagaraRendererGPURibbons::IsMaterialValid(const UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraRibbons);
}

#if RHI_RAYTRACING
void FNiagaraRendererGPURibbons::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	return; //Disabling ray trcing for ribbon temperally
	if (!CVarRayTracingNiagaraGPURibbons.GetValueOnRenderThread())
	{
		return;
	}
	
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderGPURibbons);
	check(SceneProxy);
	
	FNiagaraDynamicDataGPURibbon *DynamicDataRibbon = static_cast<FNiagaraDynamicDataGPURibbon*>(DynamicDataRender);
	NiagaraEmitterInstanceBatcher* ComputeDispatchInterface = SceneProxy->GetBatcher();
	
	if (!ComputeDispatchInterface || !DynamicDataRibbon)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataRibbon->GetParticleDataToRender();

	if (GbEnableNiagaraGPURibbonRendering == 0 || SourceParticleData == nullptr)
	{
		return;
	}

	// Bail if we don't have enough particle data to have a valid ribbon
	// or if somehow the sim targets don't match
	if (SimTarget != ENiagaraSimTarget::GPUComputeSim || SourceParticleData->GetNumInstancesAllocated() < 2)
	{
		return;
	}
	
	auto& View = Context.ReferenceView;
	auto& ViewFamily = Context.ReferenceViewFamily;
	// Setup material for our ray tracing instance
	
	const FVector ViewOriginForDistanceCulling = View->ViewMatrices.GetViewOrigin();
	
	FNiagaraRibbonMeshCollectorResources& RenderingResources = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FNiagaraRibbonMeshCollectorResources>();
	auto& RenderingViewResources = RenderingResources.RibbonResources->ViewResources.Add_GetRef(MakeShared<FNiagaraRibbonRenderingFrameViewResources>());
	RenderingViewResources->IndexGenerationSettings = CalculateIndexBufferConfiguration(SourceParticleData, SceneProxy, View, ViewOriginForDistanceCulling);
	
	// if (!RenderingViewResources->VertexFactory.GetType()->SupportsRayTracingDynamicGeometry())
	// {
	// 	return;
	// }
	
	InitializeVertexBuffersResources(DynamicDataRibbon, SourceParticleData, Context.RayTracingMeshResourceCollector.GetDynamicReadBuffer(), RenderingResources.RibbonResources);
	
	GenerateIndexBufferForView(RenderingViewResources->IndexGenerationSettings, DynamicDataRibbon, RenderingViewResources, View, ViewOriginForDistanceCulling);
			
	SetupPerViewUniformBuffer(RenderingViewResources->IndexGenerationSettings, View, ViewFamily, SceneProxy, RenderingViewResources->UniformBuffer);
	
	if (RenderingViewResources->IndexGenerationSettings.TotalNumIndices <= 0)
	{
		return;
	}
	
	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &RayTracingGeometry;
	RayTracingInstance.InstanceTransforms.Add(FMatrix::Identity);
	
	RayTracingGeometry.Initializer.IndexBuffer = RenderingViewResources->IndexBuffer.IndexBufferRHI;// PerViewGeneratedData.IndexAllocation.IndexBuffer->IndexBufferRHI;
	RayTracingGeometry.Initializer.IndexBufferOffset = 0;//PerViewGeneratedData.IndexAllocation.FirstIndex * PerViewGeneratedData.IndexAllocation.IndexStride;
	
	FMeshBatch MeshBatch;
	
	SetupMeshBatchAndCollectorResourceForView(RenderingViewResources->IndexGenerationSettings, DynamicDataRibbon, SourceParticleData, View, ViewFamily, SceneProxy, RenderingResources.RibbonResources, RenderingViewResources, MeshBatch);

	RayTracingInstance.Materials.Add(MeshBatch);
	
	// Use the internal vertex buffer only when initialized otherwise used the shared vertex buffer - needs to be updated every frame
	FRWBuffer* VertexBuffer = RayTracingDynamicVertexBuffer.NumBytes > 0 ? &RayTracingDynamicVertexBuffer : nullptr;
	
	const uint32 VertexCount = SourceParticleData->GetNumInstances();

	
	const int32 MaxTriangleCount = RenderingViewResources->IndexGenerationSettings.MaxSegmentCount * RenderingViewResources->IndexGenerationSettings.SubSegmentCount * ShapeState.TrianglesPerSegment;
	
	Context.DynamicRayTracingGeometriesToUpdate.Add(
		FRayTracingDynamicGeometryUpdateParams
		{
			RayTracingInstance.Materials,
			true,
			VertexCount,
			VertexCount * static_cast<uint32>(sizeof(FVector)),
			static_cast<uint32>(MaxTriangleCount),
			&RayTracingGeometry,
			VertexBuffer,
			true
		}
	);
	
	// RayTracingInstance.BuildInstanceMaskAndFlags(FeatureLevel);
	
	OutRayTracingInstances.Add(RayTracingInstance);
}
#endif

void FNiagaraRendererGPURibbons::GenerateShapeStateMultiPlane(FNiagaraRibbonShapeGeometryData& State, int32 MultiPlaneCount, int32 WidthSegmentationCount, bool bEnableAccurateGeometry)
{
	State.Shape = ENiagaraGPURibbonShapeMode::MultiPlane;
	State.bDisableBackfaceCulling = !bEnableAccurateGeometry;
	State.bShouldFlipNormalToView = !bEnableAccurateGeometry;
	State.TrianglesPerSegment = 2 * MultiPlaneCount * WidthSegmentationCount * (bEnableAccurateGeometry? 2 : 1);
	State.NumVerticesInSlice = MultiPlaneCount * (WidthSegmentationCount + 1) * (bEnableAccurateGeometry ? 2 : 1);
	State.BitsNeededForShape = CalculateBitsForRange(State.NumVerticesInSlice);
	State.BitMaskForShape = CalculateBitMask(State.BitsNeededForShape);
	
	for (int32 PlaneIndex = 0; PlaneIndex < MultiPlaneCount; PlaneIndex++)
	{
		const float RotationAngle = (static_cast<float>(PlaneIndex) / MultiPlaneCount) * 180.0f;

		for (int32 VertexId = 0; VertexId <= WidthSegmentationCount; VertexId++)
		{
			const FVector2D Position = FVector2D((static_cast<float>(VertexId) / WidthSegmentationCount) - 0.5f, 0).GetRotated(RotationAngle);
			const FVector2D Normal = FVector2D(0, 1).GetRotated(RotationAngle);
			const float TextureV = static_cast<float>(VertexId) / WidthSegmentationCount;

			State.SliceVertexData.Emplace(Position, Normal, TextureV);
		}
	}

	if (bEnableAccurateGeometry)
	{
		for (int32 PlaneIndex = 0; PlaneIndex < MultiPlaneCount; PlaneIndex++)
		{
			const float RotationAngle = (static_cast<float>(PlaneIndex) / MultiPlaneCount) * 180.0f;

			for (int32 VertexId = 0; VertexId <= WidthSegmentationCount; VertexId++)
			{
				const FVector2D Position = FVector2D((static_cast<float>(VertexId) / WidthSegmentationCount) - 0.5f, 0).GetRotated(RotationAngle);
				const FVector2D Normal = FVector2D(0, -1).GetRotated(RotationAngle);
				const float TextureV = static_cast<float>(VertexId) / WidthSegmentationCount;

				State.SliceVertexData.Emplace(Position, Normal, TextureV);
			}
		}
	}


	const int32 FrontFaceVertexCount = MultiPlaneCount * (WidthSegmentationCount + 1);

	State.SliceTriangleToVertexIds.Reserve(WidthSegmentationCount * MultiPlaneCount * (bEnableAccurateGeometry ? 2 : 1));
	for (int32 PlaneIndex = 0; PlaneIndex < MultiPlaneCount; PlaneIndex++)
	{
		const int32 BaseVertexId = (PlaneIndex * (WidthSegmentationCount + 1));

		for (int32 VertexIdx = 0; VertexIdx < WidthSegmentationCount; VertexIdx++)
		{
			State.SliceTriangleToVertexIds.Add(BaseVertexId + VertexIdx);
			State.SliceTriangleToVertexIds.Add(BaseVertexId + VertexIdx + 1);
		}

		if (bEnableAccurateGeometry)
		{
			for (int32 VertexIdx = 0; VertexIdx < WidthSegmentationCount; VertexIdx++)
			{
				State.SliceTriangleToVertexIds.Add(FrontFaceVertexCount + BaseVertexId + VertexIdx + 1);
				State.SliceTriangleToVertexIds.Add(FrontFaceVertexCount + BaseVertexId + VertexIdx);
			}
		}
	}
}

void FNiagaraRendererGPURibbons::GenerateShapeStateTube(FNiagaraRibbonShapeGeometryData& State, int32 TubeSubdivisions)
{
	State.Shape = ENiagaraGPURibbonShapeMode::Tube;
	State.bDisableBackfaceCulling = false;
	State.bShouldFlipNormalToView = true;
	State.TrianglesPerSegment = 2 * TubeSubdivisions;
	State.NumVerticesInSlice = TubeSubdivisions + 1;
	State.BitsNeededForShape = CalculateBitsForRange(State.NumVerticesInSlice);
	State.BitMaskForShape = CalculateBitMask(State.BitsNeededForShape);
	
	for (int32 VertexId = 0; VertexId <= TubeSubdivisions; VertexId++)
	{
		const float RotationAngle = (static_cast<float>(VertexId) / TubeSubdivisions) * -360.0f;
		const FVector2D Position = FVector2D(-0.5f, 0.0f).GetRotated(RotationAngle);
		const FVector2D Normal = FVector2D(-1, 0).GetRotated(RotationAngle);
		const float TextureV = static_cast<float>(VertexId) / TubeSubdivisions;

		State.SliceVertexData.Emplace(Position, Normal, TextureV);
	}
	
	State.SliceTriangleToVertexIds.Reserve(TubeSubdivisions);
	for (int32 VertexIdx = 0; VertexIdx < TubeSubdivisions; VertexIdx++)
	{
		State.SliceTriangleToVertexIds.Add(VertexIdx);
		State.SliceTriangleToVertexIds.Add(VertexIdx + 1);
	}
}

void FNiagaraRendererGPURibbons::GenerateShapeStateCustom(FNiagaraRibbonShapeGeometryData& State, const TArray<FNiagaraGPURibbonShapeCustomVertex>& CustomVertices)
{
	State.Shape = ENiagaraGPURibbonShapeMode::Custom;
	State.bDisableBackfaceCulling = false;
	State.bShouldFlipNormalToView = true;
	State.TrianglesPerSegment = 2 * CustomVertices.Num();
	State.NumVerticesInSlice = CustomVertices.Num() + 1;
	State.BitsNeededForShape = CalculateBitsForRange(State.NumVerticesInSlice);
	State.BitMaskForShape = CalculateBitMask(State.BitsNeededForShape);
	
	bool bHasCustomUVs = false;
	for (int32 VertexId = 0; VertexId < CustomVertices.Num(); VertexId++)
	{
		if (!FMath::IsNearlyZero(CustomVertices[VertexId].TextureV))
		{
			bHasCustomUVs = true;
			break;
		}
	}

	for (int32 VertexId = 0; VertexId <= CustomVertices.Num(); VertexId++)
	{
		const auto& CustomVert = CustomVertices[VertexId % CustomVertices.Num()];

		const FVector2D Position = CustomVert.Position;
		const FVector2D Normal = CustomVert.Normal.IsNearlyZero() ? Position.GetSafeNormal() : CustomVert.Normal;
		const float TextureV = bHasCustomUVs ? CustomVert.TextureV : static_cast<float>(VertexId) / CustomVertices.Num();

		State.SliceVertexData.Emplace(Position, Normal, TextureV);
	}

	State.SliceTriangleToVertexIds.Reserve(CustomVertices.Num());
	for (int32 VertexIdx = 0; VertexIdx < CustomVertices.Num(); VertexIdx++)
	{
		State.SliceTriangleToVertexIds.Add(VertexIdx);
		State.SliceTriangleToVertexIds.Add(VertexIdx + 1);
	}
}

void FNiagaraRendererGPURibbons::GenerateShapeStatePlane(FNiagaraRibbonShapeGeometryData& State, int32 WidthSegmentationCount)
{
	State.Shape = ENiagaraGPURibbonShapeMode::Plane;
	State.bDisableBackfaceCulling = true;
	State.bShouldFlipNormalToView = true;
	State.TrianglesPerSegment = 2 * WidthSegmentationCount;
	State.NumVerticesInSlice = WidthSegmentationCount + 1;
	State.BitsNeededForShape = CalculateBitsForRange(State.NumVerticesInSlice);
	State.BitMaskForShape = CalculateBitMask(State.BitsNeededForShape);
	
	for (int32 VertexId = 0; VertexId <= WidthSegmentationCount; VertexId++)
	{
		const FVector2D Position = FVector2D((static_cast<float>(VertexId) / WidthSegmentationCount) - 0.5f, 0);
		const FVector2D Normal = FVector2D(0, 1);
		const float TextureV = static_cast<float>(VertexId) / WidthSegmentationCount;

		State.SliceVertexData.Emplace(Position, Normal, TextureV);
	}
	
	State.SliceTriangleToVertexIds.Reserve(WidthSegmentationCount);
	for (int32 VertexIdx = 0; VertexIdx < WidthSegmentationCount; VertexIdx++)
	{
		State.SliceTriangleToVertexIds.Add(VertexIdx);
		State.SliceTriangleToVertexIds.Add(VertexIdx + 1);
	}
}

void FNiagaraRendererGPURibbons::InitializeShape(const UNiagaraGPURibbonRendererProperties* Properties)
{
	if (Properties->Shape == ENiagaraGPURibbonShapeMode::Custom && Properties->CustomVertices.Num() > 2)
	{
		GenerateShapeStateCustom(ShapeState, Properties->CustomVertices);
	}
	else if (Properties->Shape == ENiagaraGPURibbonShapeMode::Tube && Properties->TubeSubdivisions > 2 && Properties->TubeSubdivisions <= 16)
	{
		GenerateShapeStateTube(ShapeState, Properties->TubeSubdivisions);
	}
	else if (Properties->Shape == ENiagaraGPURibbonShapeMode::MultiPlane && Properties->MultiPlaneCount > 1 && Properties->MultiPlaneCount <= 16)
	{
		GenerateShapeStateMultiPlane(ShapeState, Properties->MultiPlaneCount, Properties->WidthSegmentationCount, Properties->bEnableAccurateGeometry);
	}
	else
	{
		GenerateShapeStatePlane(ShapeState, Properties->WidthSegmentationCount);
	}	
}

void FNiagaraRendererGPURibbons::InitializeTessellation(const UNiagaraGPURibbonRendererProperties* Properties)
{
	TessellationConfig.TessellationMode = Properties->TessellationMode;
	TessellationConfig.CustomTessellationFactor = Properties->TessellationFactor;
	TessellationConfig.bCustomUseConstantFactor = Properties->bUseConstantFactor;
	TessellationConfig.CustomTessellationMinAngle = Properties->TessellationAngle > 0.f && Properties->TessellationAngle < 1.f ? 1.f : Properties->TessellationAngle;
	TessellationConfig.CustomTessellationMinAngle *= PI / 180.f;
	TessellationConfig.bCustomUseScreenSpace = Properties->bScreenSpaceTessellation;
}


FNiagaraIndexGenerationInput FNiagaraRendererGPURibbons::CalculateIndexBufferConfiguration(const FNiagaraDataBuffer* SourceParticleData,
	const FNiagaraSceneProxy* SceneProxy, const FSceneView* View, const FVector& ViewOriginForDistanceCulling) const
{
	FNiagaraIndexGenerationInput IndexGenInput;

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
	IndexGenInput.ViewDistance  = SceneProxy->PreviewLODDistance >= 0.0f ? SceneProxy->PreviewLODDistance : SceneProxy->GetBounds().ComputeSquaredDistanceFromBoxToPoint(ViewOriginForDistanceCulling);
#else
	cIndexGenInput.ViewDistance = SceneProxy->GetBounds().ComputeSquaredDistanceFromBoxToPoint(ViewOriginForDistanceCulling);
#endif
	
	// If we are a gpu sim, we rely on num instances allocated since between now and render we could have a different number of particles living
	// If we're not a gpu sim, we're a cpu sim with gpu init, we can rely on the num particles - 1 being the max cap for segments
	IndexGenInput.MaxSegmentCount = SourceParticleData->GetNumInstancesAllocated();
	

	IndexGenInput.SubSegmentCount = 1;
	if (GenerationConfig.WantsAutomaticTessellation() || GenerationConfig.WantsConstantTessellation())
	{
		// if we have a constant factor, use it, if not set it to the max allowed since we won't know what we need exactly until later on.
		IndexGenInput.SubSegmentCount = (TessellationConfig.TessellationMode == ENiagaraGPURibbonTessellationMode::Custom && TessellationConfig.bCustomUseConstantFactor)?
			TessellationConfig.CustomTessellationFactor : GNiagaraGPURibbonMaxTessellation;
	}	
	const uint32 NumSegmentBits = CalculateBitsForRange(IndexGenInput.MaxSegmentCount);
	const uint32 NumSubSegmentBits = CalculateBitsForRange(IndexGenInput.SubSegmentCount);
	
	IndexGenInput.SegmentBitShift = NumSubSegmentBits + ShapeState.BitsNeededForShape;
	IndexGenInput.SubSegmentBitShift = ShapeState.BitsNeededForShape;

	IndexGenInput.SegmentBitMask = CalculateBitMask(NumSegmentBits);
	IndexGenInput.SubSegmentBitMask = CalculateBitMask(NumSubSegmentBits);

	IndexGenInput.ShapeBitMask = ShapeState.BitMaskForShape;
	
	IndexGenInput.TotalBitCount = NumSegmentBits + NumSubSegmentBits + ShapeState.BitsNeededForShape;
	IndexGenInput.TotalNumIndices = IndexGenInput.MaxSegmentCount * IndexGenInput.SubSegmentCount * ShapeState.TrianglesPerSegment * 3;

	return IndexGenInput;
}

void FNiagaraRendererGPURibbons::GenerateIndexBufferForView(FNiagaraIndexGenerationInput& GeneratedData, FNiagaraDynamicDataGPURibbon* DynamicDataRibbon,
                                                         const TSharedPtr<FNiagaraRibbonRenderingFrameViewResources>& RenderingViewResources, const FSceneView* View,
                                                         const FVector& ViewOriginForDistanceCulling) const
{

	if (GeneratedData.MaxSegmentCount > 0)
	{
		RenderingViewResources->IndirectDrawBuffer.Initialize(sizeof(uint32), FNiagaraRibbonIndirectDrawBufferLayout::NumElements, EPixelFormat::PF_R32_UINT, ERHIAccess::IndirectArgs | ERHIAccess::SRVMask, BUF_Static | BUF_DrawIndirect);			

		const EBufferUsageFlags IndexBufferUsage = BUF_Static |  BUF_UnorderedAccess;
		if (GeneratedData.TotalBitCount <= 16/* Number of bits in a ushort*/ )
		{
			RenderingViewResources->IndexBuffer.Initialize(TEXT("NiagaraRibbonIndexBuffer"), sizeof(uint16), GeneratedData.TotalNumIndices, PF_R16_UINT, ERHIAccess::VertexOrIndexBuffer, IndexBufferUsage);
		}
		else
		{		
			RenderingViewResources->IndexBuffer.Initialize(TEXT("NiagaraRibbonIndexBuffer"), sizeof(uint32), GeneratedData.TotalNumIndices, PF_R32_UINT, ERHIAccess::VertexOrIndexBuffer, IndexBufferUsage);
		}
	}
}


void FNiagaraRendererGPURibbons::SetupPerViewUniformBuffer(FNiagaraIndexGenerationInput& GeneratedData, const FSceneView* View,
	const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy* SceneProxy, FNiagaraGPURibbonUniformBufferRef& OutUniformBuffer) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPURibbonsGenerateUniformBuffer);

	FNiagaraGPURibbonUniformParameters PerViewUniformParameters;
	FMemory::Memzero(&PerViewUniformParameters,sizeof(PerViewUniformParameters)); // Clear unset bytes

	PerViewUniformParameters.bLocalSpace = bLocalSpace;
	PerViewUniformParameters.DeltaSeconds = ViewFamily.DeltaWorldTime;
	// PerViewUniformParameters.SystemLWCTile = SceneProxy->GetLWCRenderTile();
	PerViewUniformParameters.CameraUp = static_cast<FVector>(View->GetViewUp()); // FVector4(0.0f, 0.0f, 1.0f, 0.0f);
	PerViewUniformParameters.CameraRight = static_cast<FVector>(View->GetViewRight());//	FVector4(1.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.ScreenAlignment = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.InterpCount = GeneratedData.SubSegmentCount;
	PerViewUniformParameters.OneOverInterpCount = 1.f / static_cast<float>(GeneratedData.SubSegmentCount);
	PerViewUniformParameters.ParticleIdShift = GeneratedData.SegmentBitShift;
	PerViewUniformParameters.ParticleIdMask = GeneratedData.SegmentBitMask;
	PerViewUniformParameters.InterpIdShift = GeneratedData.SubSegmentBitShift;
	PerViewUniformParameters.InterpIdMask = GeneratedData.SubSegmentBitMask;
	PerViewUniformParameters.SliceVertexIdMask = ShapeState.BitMaskForShape;
	PerViewUniformParameters.ShouldFlipNormalToView = ShapeState.bShouldFlipNormalToView;
	PerViewUniformParameters.ShouldUseMultiRibbon = GenerationConfig.HasRibbonIDs()? 1 : 0;

	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
	PerViewUniformParameters.PositionDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Position].GetGPUOffset();
	PerViewUniformParameters.VelocityDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Velocity].GetGPUOffset();
	PerViewUniformParameters.ColorDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Color].GetGPUOffset();
	PerViewUniformParameters.WidthDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Width].GetGPUOffset();
	PerViewUniformParameters.TwistDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Twist].GetGPUOffset();
	PerViewUniformParameters.NormalizedAgeDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::NormalizedAge].GetGPUOffset();
	PerViewUniformParameters.MaterialRandomDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialRandom].GetGPUOffset();
	PerViewUniformParameters.MaterialParamDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam0].GetGPUOffset();
	PerViewUniformParameters.MaterialParam1DataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam1].GetGPUOffset();
	PerViewUniformParameters.MaterialParam2DataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam2].GetGPUOffset();
	PerViewUniformParameters.MaterialParam3DataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam3].GetGPUOffset();
	PerViewUniformParameters.DistanceFromStartOffset =
		(UV0Settings.DistributionMode == ENiagaraGPURibbonUVDistributionMode::TiledFromStartOverRibbonLength ||
		UV1Settings.DistributionMode == ENiagaraGPURibbonUVDistributionMode::TiledFromStartOverRibbonLength)?
		VFVariables[ENiagaraGPURibbonVFLayout::DistanceFromStart].GetGPUOffset() : -1;
	PerViewUniformParameters.U0OverrideDataOffset = UV0Settings.bEnablePerParticleUOverride ? VFVariables[ENiagaraGPURibbonVFLayout::U0Override].GetGPUOffset() : -1;
	PerViewUniformParameters.V0RangeOverrideDataOffset = UV0Settings.bEnablePerParticleVRangeOverride ? VFVariables[ENiagaraGPURibbonVFLayout::V0RangeOverride].GetGPUOffset() : -1;
	PerViewUniformParameters.U1OverrideDataOffset = UV1Settings.bEnablePerParticleUOverride ? VFVariables[ENiagaraGPURibbonVFLayout::U1Override].GetGPUOffset() : -1;
	PerViewUniformParameters.V1RangeOverrideDataOffset = UV1Settings.bEnablePerParticleVRangeOverride ? VFVariables[ENiagaraGPURibbonVFLayout::V1RangeOverride].GetGPUOffset() : -1;

	PerViewUniformParameters.MaterialParamValidMask = GenerationConfig.GetMaterialParamValidMask();

	bool bShouldDoFacing = FacingMode == ENiagaraGPURibbonFacingMode::Custom || FacingMode == ENiagaraGPURibbonFacingMode::CustomSideVector;
	PerViewUniformParameters.FacingDataOffset = bShouldDoFacing ? VFVariables[ENiagaraGPURibbonVFLayout::Facing].GetGPUOffset() : -1;

	PerViewUniformParameters.LinkOrderDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::LinkOrder].GetGPUOffset();

	PerViewUniformParameters.U0DistributionMode = static_cast<int32>(UV0Settings.DistributionMode);
	PerViewUniformParameters.U1DistributionMode = static_cast<int32>(UV1Settings.DistributionMode);
	PerViewUniformParameters.PackedVData = FVector4(UV0Settings.Scale.Y, UV0Settings.Offset.Y, UV1Settings.Scale.Y, UV1Settings.Offset.Y);

	OutUniformBuffer = FNiagaraGPURibbonUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
}

inline void FNiagaraRendererGPURibbons::SetupMeshBatchAndCollectorResourceForView(const FNiagaraIndexGenerationInput& GeneratedData, FNiagaraDynamicDataGPURibbon* DynamicDataRibbon, const FNiagaraDataBuffer* SourceParticleData, const FSceneView* View,
    const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy* SceneProxy, const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources, const TSharedPtr<FNiagaraRibbonRenderingFrameViewResources>& RenderingViewResources,
    FMeshBatch& OutMeshBatch) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPURibbonsGenerateVFLooseParameter);

	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;
	FMaterialRenderProxy* MaterialRenderProxy = DynamicDataRibbon->Material;
	check(MaterialRenderProxy);
	
	// Set common data on vertex factory
	// DynamicDataRibbon->SetVertexFactoryData(RenderingViewResources->VertexFactory); //Empty implementation in UE5.1

	FNiagaraGPURibbonVFLooseParameters VFLooseParams;
	VFLooseParams.SortedIndices = VertexBuffers.SortedIndicesBuffer.SRV;
	VFLooseParams.TangentsAndDistances = VertexBuffers.TangentsAndDistancesBuffer.SRV;
	VFLooseParams.MultiRibbonIndices = FNiagaraRenderer::GetSrvOrDefaultUInt(VertexBuffers.MultiRibbonIndicesBuffer);
	VFLooseParams.PackedPerRibbonDataByIndex = VertexBuffers.RibbonLookupTableBuffer.SRV;
	VFLooseParams.SliceVertexData = ShapeState.SliceVertexDataBuffer.SRV;
	VFLooseParams.NiagaraParticleDataFloat = RenderingResources->ParticleFloatSRV;
	VFLooseParams.NiagaraParticleDataHalf = RenderingResources->ParticleHalfSRV;
	VFLooseParams.NiagaraFloatDataStride = RenderingResources->ParticleFloatDataStride;
	VFLooseParams.FacingMode = static_cast<uint32>(FacingMode);
	VFLooseParams.Shape = static_cast<uint32>(ShapeState.Shape);

	VFLooseParams.IndirectDrawOutput = (FRHIShaderResourceView*)RenderingViewResources->IndirectDrawBuffer.SRV;
	VFLooseParams.IndirectDrawOutputOffset = 0;

	// Collector.AllocateOneFrameResource uses default ctor, initialize the vertex factory
	RenderingViewResources->VertexFactory.SetParticleFactoryType(NVFT_Ribbon);
	RenderingViewResources->VertexFactory.LooseParameterUniformBuffer = FNiagaraGPURibbonVFLooseParametersRef::CreateUniformBufferImmediate(VFLooseParams, UniformBuffer_SingleFrame);
	RenderingViewResources->VertexFactory.InitResource();
	RenderingViewResources->VertexFactory.SetRibbonUniformBuffer(RenderingViewResources->UniformBuffer);


	OutMeshBatch.VertexFactory = &RenderingViewResources->VertexFactory;
	OutMeshBatch.CastShadow = SceneProxy->CastsDynamicShadow();
#if RHI_RAYTRACING
	OutMeshBatch.CastRayTracedShadow = SceneProxy->CastsDynamicShadow();
#endif
	OutMeshBatch.bUseAsOccluder = false;
	OutMeshBatch.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
	OutMeshBatch.bDisableBackfaceCulling = ShapeState.bDisableBackfaceCulling;
	OutMeshBatch.Type = PT_TriangleList;
	OutMeshBatch.DepthPriorityGroup = SceneProxy->GetDepthPriorityGroup(View);
	OutMeshBatch.bCanApplyViewModeOverrides = true;
	OutMeshBatch.bUseWireframeSelectionColoring = SceneProxy->IsSelected();
	OutMeshBatch.SegmentIndex = 0;
	OutMeshBatch.MaterialRenderProxy = bIsWireframe? UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy() : MaterialRenderProxy;
	
	FMeshBatchElement& MeshElement = OutMeshBatch.Elements[0];
	MeshElement.IndexBuffer = &RenderingViewResources->IndexBuffer;
	MeshElement.FirstIndex = 0;
	MeshElement.NumInstances = 1;
	MeshElement.MinVertexIndex = 0;
	MeshElement.MaxVertexIndex = 0;

	MeshElement.NumPrimitives = 0;
	MeshElement.IndirectArgsBuffer = RenderingViewResources->IndirectDrawBuffer.Buffer;
	MeshElement.IndirectArgsOffset = RenderingViewResources->IndirectDrawBufferStartByteOffset;
	
	// TODO: MotionVector/Velocity? Probably need to look into this?
	MeshElement.PrimitiveUniformBuffer = SceneProxy->GetUniformBufferNoVelocity();	// Note: Ribbons don't generate accurate velocities so disabling	
}


void FNiagaraRendererGPURibbons::InitializeViewIndexBuffersGPU(FRHICommandListImmediate& CMDList, NiagaraEmitterInstanceBatcher* Batcher,
	const FNiagaraDataBuffer* SourceParticleData, const TSharedPtr<FNiagaraRibbonRenderingFrameViewResources>& RenderingViewResources) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderGPURibbonsGenIndicies);
	
	const uint32 NumInstances = SourceParticleData->GetNumInstances();
		
	if (!RenderingViewResources->IndirectDrawBuffer.Buffer.IsValid())
	{
		return;
	}
	
	{
		FNiagaraRibbonCreateIndexBufferParamsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRibbonWantsAutomaticTessellation>(GenerationConfig.WantsAutomaticTessellation());
		PermutationVector.Set<FRibbonWantsConstantTessellation>(GenerationConfig.WantsConstantTessellation());
			
		TShaderMapRef<FNiagaraRibbonCreateIndexBufferParamsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

		FNiagaraRibbonInitializeIndices Params;
		FMemory::Memzero(Params);

		Params.IndirectDrawOutput = RenderingViewResources->IndirectDrawBuffer.UAV;
		Params.VertexGenerationResults = VertexBuffers.GPUComputeCommandBuffer.SRV;

		// Total particle Count
		Params.TotalNumParticlesDirect = SourceParticleData->GetNumInstances();

		// Indirect particle Count
		Params.EmitterParticleCountsBuffer = GetSrvOrDefaultUInt(Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer());
		Params.EmitterParticleCountsBufferOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();

		Params.IndirectDrawOutputIndex = 0;
		Params.VertexGenerationResultsIndex = 0; /*Offset into command buffer*/
		Params.IndexGenThreadSize = FNiagaraRibbonComputeCommon::IndexGenThreadSize;
		Params.TrianglesPerSegment = ShapeState.TrianglesPerSegment;

		Params.ViewDistance = RenderingViewResources->IndexGenerationSettings.ViewDistance;
		Params.LODDistanceFactor = RenderingViewResources->IndexGenerationSettings.LODDistanceFactor;
		Params.TessellationMode = static_cast<uint32>(TessellationConfig.TessellationMode);
		Params.bCustomUseConstantFactor = TessellationConfig.bCustomUseConstantFactor ? 1 : 0;
		Params.CustomTessellationFactor = TessellationConfig.CustomTessellationFactor;
		Params.CustomTessellationMinAngle = TessellationConfig.CustomTessellationMinAngle;
		Params.bCustomUseScreenSpace = TessellationConfig.bCustomUseScreenSpace ? 1 : 0;
		Params.GNiagaraGPURibbonMaxTessellation = GNiagaraGPURibbonMaxTessellation;
		Params.GNiagaraGPURibbonTessellationAngle = GNiagaraGPURibbonTessellationAngle;
		Params.GNiagaraGPURibbonTessellationScreenPercentage = GNiagaraGPURibbonTessellationScreenPercentage;
		Params.GNiagaraGPURibbonTessellationEnabled = GNiagaraGPURibbonTessellationEnabled ? 1 : 0;
		Params.GNiagaraGPURibbonTessellationMinDisplacementError = GNiagaraGPURibbonTessellationMinDisplacementError;

		CMDList.Transition(FRHITransitionInfo(RenderingViewResources->IndirectDrawBuffer.UAV, ERHIAccess::SRVMask | ERHIAccess::IndirectArgs, ERHIAccess::UAVCompute));
		SetComputePipelineState(CMDList, ComputeShader.GetComputeShader());
		SetShaderParameters(CMDList, ComputeShader, ComputeShader.GetComputeShader(), Params);
		DispatchComputeShader(CMDList, ComputeShader, 1, 1, 1);
		UnsetShaderUAVs(CMDList, ComputeShader, ComputeShader.GetComputeShader());
		CMDList.Transition(FRHITransitionInfo(RenderingViewResources->IndirectDrawBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::IndirectArgs));
	}
	
	// Not possible to have a valid ribbon with less than 2 particles, abort!
	// but we do need to write out the indirect draw so it will behave correctly.
	// So the initialize call above sets up the indirect draw, but we'll skip the actual index gen below.
	if (NumInstances < 2)
	{
		return;
	}
		
	{
		FNiagaraRibbonCreateIndexBufferCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRibbonHasFullRibbonID>(GenerationConfig.HasFullRibbonIDs());
		PermutationVector.Set<FRibbonHasRibbonID>(GenerationConfig.HasSimpleRibbonIDs());

		// This switches the index gen from a unrolled limited loop for performance to a full loop that can handle anything thrown at it
		PermutationVector.Set<FRibbonHasHighSliceComplexity>(ShapeState.TrianglesPerSegment > 32);
		
		TShaderMapRef<FNiagaraRibbonCreateIndexBufferCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		// const int TotalNumInvocations = Params.TotalNumParticles * IndicesConfig.SubSegmentCount;
		// const uint32 NumThreadGroups = FMath::DivideAndRoundUp<uint32>(TotalNumInvocations, FNiagaraRibbonComputeCommon::IndexGenThreadSize);

		constexpr uint32 IndirectDispatchArgsOffset = 0;

		FNiagaraRibbonGenerateIndices Params;
		FMemory::Memzero(Params);
		
		Params.GeneratedIndicesBuffer = RenderingViewResources->IndexBuffer.UAV;
		Params.SortedIndices = VertexBuffers.SortedIndicesBuffer.SRV;
		Params.MultiRibbonIndices = VertexBuffers.MultiRibbonIndicesBuffer.SRV;
		Params.Segments = VertexBuffers.SegmentsBuffer.SRV;

		Params.IndirectDrawInfo = RenderingViewResources->IndirectDrawBuffer.SRV;
		Params.TriangleToVertexIds = ShapeState.SliceTriangleToVertexIdsBuffer.SRV;

		// Total particle Count
		Params.TotalNumParticlesDirect = SourceParticleData->GetNumInstances();

		// Indirect particle Count
		Params.EmitterParticleCountsBuffer = GetSrvOrDefaultUInt(Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer());
		Params.EmitterParticleCountsBufferOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();

		Params.IndexBufferOffset = 0;
		Params.IndirectDrawInfoIndex = 0;
		Params.TriangleToVertexIdsCount = ShapeState.SliceTriangleToVertexIds.Num();

		Params.TrianglesPerSegment = ShapeState.TrianglesPerSegment;
		Params.NumVerticesInSlice = ShapeState.NumVerticesInSlice;
		Params.BitsNeededForShape = ShapeState.BitsNeededForShape;
		Params.BitMaskForShape = ShapeState.BitMaskForShape;
		Params.SegmentBitShift = RenderingViewResources->IndexGenerationSettings.SegmentBitShift;
		Params.SegmentBitMask = RenderingViewResources->IndexGenerationSettings.SegmentBitMask;
		Params.SubSegmentBitShift = RenderingViewResources->IndexGenerationSettings.SubSegmentBitShift;
		Params.SubSegmentBitMask = RenderingViewResources->IndexGenerationSettings.SubSegmentBitMask;
		
		CMDList.Transition(FRHITransitionInfo(RenderingViewResources->IndexBuffer.UAV, ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute));
		SetComputePipelineState(CMDList, ComputeShader.GetComputeShader());
		SetShaderParameters(CMDList, ComputeShader, ComputeShader.GetComputeShader(), Params);
		DispatchIndirectComputeShader(CMDList, ComputeShader.GetShader(), RenderingViewResources->IndirectDrawBuffer.Buffer, IndirectDispatchArgsOffset);
		UnsetShaderUAVs(CMDList, ComputeShader, ComputeShader.GetComputeShader());
		CMDList.Transition(FRHITransitionInfo(RenderingViewResources->IndexBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::VertexOrIndexBuffer));
	}
}

void FNiagaraRendererGPURibbons::InitializeVertexBuffersResources(const FNiagaraDynamicDataGPURibbon* DynamicDataRibbon, FNiagaraDataBuffer* SourceParticleData,
                                                               FGlobalDynamicReadBuffer& DynamicReadBuffer, const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources) const
{

	// Make sure our ribbon data buffers are setup
	VertexBuffers.InitializeOrUpdateBuffers(GenerationConfig, SourceParticleData, DynamicDataRibbon->MaxAllocatedParticleCount);
	
	// Now we need to bind the source particle data, copying it to the gpu if necessary
	{		
		RenderingResources->ParticleFloatSRV = GetSrvOrDefaultFloat(SourceParticleData->GetGPUBufferFloat());
		RenderingResources->ParticleHalfSRV = GetSrvOrDefaultHalf(SourceParticleData->GetGPUBufferHalf());
		RenderingResources->ParticleIntSRV = GetSrvOrDefaultInt(SourceParticleData->GetGPUBufferInt());
		
		RenderingResources->ParticleFloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
		RenderingResources->ParticleHalfDataStride = SourceParticleData->GetHalfStride() / sizeof(FFloat16);
		RenderingResources->ParticleIntDataStride = SourceParticleData->GetInt32Stride() / sizeof(int32);
		
		RenderingResources->RibbonIdParamOffset = RibbonIDParamDataSetOffset;
	}
}

FRibbonComputeUniformParameters FNiagaraRendererGPURibbons::SetupComputeVertexGenParams(NiagaraEmitterInstanceBatcher* Batcher,
	const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources, const FNiagaraDataBuffer* SourceParticleData) const
{
	FRibbonComputeUniformParameters CommonParams;
	FMemory::Memzero(CommonParams);

	// Total particle Count
	CommonParams.TotalNumParticlesDirect = SourceParticleData->GetNumInstances();

	// Indirect particle Count
	CommonParams.EmitterParticleCountsBuffer = GetSrvOrDefaultUInt(Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer());
	CommonParams.EmitterParticleCountsBufferOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();

	// Niagara sim data
	CommonParams.NiagaraParticleDataFloat = RenderingResources->ParticleFloatSRV->IsValid() ? RenderingResources->ParticleFloatSRV : GetDummyFloatBuffer();
	CommonParams.NiagaraParticleDataHalf = RenderingResources->ParticleHalfSRV? RenderingResources->ParticleHalfSRV : GetDummyHalfBuffer();
	CommonParams.NiagaraParticleDataInt = RenderingResources->ParticleIntSRV->IsValid() ? RenderingResources->ParticleIntSRV : GetDummyIntBuffer();
	CommonParams.NiagaraFloatDataStride = RenderingResources->ParticleFloatDataStride;
	CommonParams.NiagaraIntDataStride = RenderingResources->ParticleIntDataStride;

	
	// Int bindings
	CommonParams.RibbonIdDataOffset = RenderingResources->RibbonIdParamOffset;

	// Float bindings
	const TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
	CommonParams.PositionDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Position].GetGPUOffset();
	// CommonParams.PrevPositionDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::PrevPosition].GetGPUOffset();		//Todo
	CommonParams.VelocityDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Velocity].GetGPUOffset();
	CommonParams.ColorDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Color].GetGPUOffset();
	CommonParams.WidthDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Width].GetGPUOffset();
	// CommonParams.PrevWidthDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::PrevRibbonWidth].GetGPUOffset();		//Todo
	CommonParams.TwistDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::Twist].GetGPUOffset();
	// CommonParams.PrevTwistDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::PrevRibbonTwist].GetGPUOffset();		//Todo
	CommonParams.NormalizedAgeDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::NormalizedAge].GetGPUOffset();
	CommonParams.MaterialRandomDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialRandom].GetGPUOffset();
	CommonParams.MaterialParamDataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam0].GetGPUOffset();
	CommonParams.MaterialParam1DataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam1].GetGPUOffset();
	CommonParams.MaterialParam2DataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam2].GetGPUOffset();
	CommonParams.MaterialParam3DataOffset = VFVariables[ENiagaraGPURibbonVFLayout::MaterialParam3].GetGPUOffset();

	const bool bShouldLinkDistanceFromStart = (UV0Settings.DistributionMode == ENiagaraGPURibbonUVDistributionMode::TiledFromStartOverRibbonLength ||
		UV1Settings.DistributionMode == ENiagaraGPURibbonUVDistributionMode::TiledFromStartOverRibbonLength);
	
	CommonParams.DistanceFromStartOffset = bShouldLinkDistanceFromStart ? VFVariables[ENiagaraGPURibbonVFLayout::DistanceFromStart].GetGPUOffset() : -1;
	CommonParams.U0OverrideDataOffset = UV0Settings.bEnablePerParticleUOverride ? VFVariables[ENiagaraGPURibbonVFLayout::U0Override].GetGPUOffset() : -1;
	CommonParams.V0RangeOverrideDataOffset = UV0Settings.bEnablePerParticleVRangeOverride ? VFVariables[ENiagaraGPURibbonVFLayout::V0RangeOverride].GetGPUOffset() : -1;
	CommonParams.U1OverrideDataOffset = UV1Settings.bEnablePerParticleUOverride ? VFVariables[ENiagaraGPURibbonVFLayout::U1Override].GetGPUOffset() : -1;
	CommonParams.V1RangeOverrideDataOffset = UV1Settings.bEnablePerParticleVRangeOverride ? VFVariables[ENiagaraGPURibbonVFLayout::V1RangeOverride].GetGPUOffset() : -1;

	CommonParams.MaterialParamValidMask = GenerationConfig.GetMaterialParamValidMask();

	const bool bShouldDoFacing = FacingMode == ENiagaraGPURibbonFacingMode::Custom || FacingMode == ENiagaraGPURibbonFacingMode::CustomSideVector;
	CommonParams.FacingDataOffset = bShouldDoFacing ? VFVariables[ENiagaraGPURibbonVFLayout::Facing].GetGPUOffset() : -1;
	// CommonParams.PrevFacingDataOffset = bShouldDoFacing ? VFVariables[ENiagaraGPURibbonVFLayout::PrevRibbonFacing].GetGPUOffset() : -1; //Todo

	CommonParams.RibbonLinkOrderDataOffset = GenerationConfig.HasCustomLinkOrder()? VFVariables[ENiagaraGPURibbonVFLayout::LinkOrder].GetGPUOffset() : -1;

	CommonParams.U0DistributionMode = static_cast<int32>(UV0Settings.DistributionMode);
	CommonParams.U1DistributionMode = static_cast<int32>(UV1Settings.DistributionMode);

	return CommonParams;
}

void FNiagaraRendererGPURibbons::InitializeVertexBuffersGPU(FRHICommandListImmediate& CMDList, NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraDataBuffer* SourceParticleData,
	FNiagaraRibbonGPUInitComputeBuffers& TempBuffers, const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources) const
{	
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderGPURibbonsGenVertices);

	FRibbonComputeUniformParameters CommonParams = SetupComputeVertexGenParams(Batcher, RenderingResources, SourceParticleData);

	const int32 NumExecutableInstances = CommonParams.EmitterParticleCountsBufferOffset != INDEX_NONE? SourceParticleData->GetNumInstancesAllocated() : SourceParticleData->GetNumInstances();

	const bool bCanRun = NumExecutableInstances >= 2;
	
	// Clear the command buffer if we just initialized it, or if the sim doesn't have enough data to run
	if ((!bCanRun || VertexBuffers.bJustCreatedCommandBuffer) && VertexBuffers.GPUComputeCommandLength > 0)
	{
		FRHITransitionInfo FirstTransitionInfo;
		FirstTransitionInfo.VertexBuffer = VertexBuffers.GPUComputeCommandBuffer.Buffer;
		FirstTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
		FirstTransitionInfo.AccessBefore = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs;
		FirstTransitionInfo.AccessAfter = ERHIAccess::UAVCompute;

		FRHITransitionInfo SecondTransitionInfo;
		SecondTransitionInfo.VertexBuffer = VertexBuffers.GPUComputeCommandBuffer.Buffer;
		SecondTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
		SecondTransitionInfo.AccessBefore = ERHIAccess::UAVCompute;
		SecondTransitionInfo.AccessAfter = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs;
		
		// CMDList.Transition(FRHITransitionInfo(VertexBuffers.GPUComputeCommandBuffer.Buffer, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs, ERHIAccess::UAVCompute));
		CMDList.Transition(FirstTransitionInfo);
		CMDList.ClearUAVUint(VertexBuffers.GPUComputeCommandBuffer.UAV, FUintVector4(0));
		// CMDList.Transition(FRHITransitionInfo(VertexBuffers.GPUComputeCommandBuffer.Buffer, ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs));
		CMDList.Transition(SecondTransitionInfo);
		VertexBuffers.bJustCreatedCommandBuffer = false;
	}
	
	// Not possible to have a valid ribbon with less than 2 particles, so the remaining work is unnecessary as there's nothing needed here
	if (!bCanRun)
	{
		return;
	}

	{
		SCOPED_DRAW_EVENT(CMDList, NiagaraRenderRibbonsGenVerticesSortGPU);
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesSortGPU);
		
		FNiagaraRibbonSortPhase1CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRibbonHasFullRibbonID>(GenerationConfig.HasFullRibbonIDs());
		PermutationVector.Set<FRibbonHasRibbonID>(GenerationConfig.HasSimpleRibbonIDs());
		PermutationVector.Set<FRibbonHasCustomLinkOrder>(GenerationConfig.HasCustomLinkOrder());
		
		TShaderMapRef<FNiagaraRibbonSortPhase1CS> BubbleSortShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		TShaderMapRef<FNiagaraRibbonSortPhase2CS> MergeSortShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
			
		FRibbonOrderSortParameters SortParams;
		FMemory::Memzero(SortParams);
		SortParams.Common = CommonParams;
		SortParams.DestinationSortedIndices = VertexBuffers.SortedIndicesBuffer.UAV;
		SortParams.SortedIndices = GetSrvOrDefaultUInt(TempBuffers.SortBuffer);
		
		int CurrentBufferOrientation = 0;		
		const auto SwapBuffers = [&]()
		{
			CurrentBufferOrientation ^= 0x1;	
			const bool bComputeOnOutputBuffer = CurrentBufferOrientation == 0;
			
			SortParams.DestinationSortedIndices = bComputeOnOutputBuffer? VertexBuffers.SortedIndicesBuffer.UAV : TempBuffers.SortBuffer.UAV;
			SortParams.SortedIndices = bComputeOnOutputBuffer? TempBuffers.SortBuffer.SRV : VertexBuffers.SortedIndicesBuffer.SRV;			
		};
	
		const uint32 NumInitialThreadGroups = FMath::DivideAndRoundUp<uint32>(NumExecutableInstances, FNiagaraRibbonSortPhase1CS::BubbleSortGroupWidth);	
		const uint32 NumMergeSortThreadGroups = FMath::DivideAndRoundUp<uint32>(NumExecutableInstances, FNiagaraRibbonSortPhase2CS::ThreadGroupSize);
		const uint32 MergeSortPasses = FMath::CeilLogTwo(NumInitialThreadGroups);
		
		// If should do an initial flip so we start with the temp buffer to end in the correct buffer
		if (MergeSortPasses % 2 != 0)
		{
			SwapBuffers();
		}

		{
			SCOPED_DRAW_EVENT(CMDList, NiagaraRenderRibbonsGenVerticesInitialSortGPU);
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesInitialSortGPU);
			
			// Initial sort, sets up the buffer, and runs a parallel bubble sort to create groups of BubbleSortGroupWidth size
			CMDList.Transition(FRHITransitionInfo(SortParams.DestinationSortedIndices, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute));
			SetComputePipelineState(CMDList, BubbleSortShader.GetComputeShader());
			ValidateShaderParameters(BubbleSortShader, SortParams);
			SetShaderParameters(CMDList, BubbleSortShader, BubbleSortShader.GetComputeShader(), SortParams);
			DispatchComputeShader(CMDList, BubbleSortShader, NumInitialThreadGroups, 1, 1);
			UnsetShaderUAVs(CMDList, BubbleSortShader, BubbleSortShader.GetComputeShader());
			CMDList.Transition(FRHITransitionInfo(SortParams.DestinationSortedIndices, ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer));
		}
		
		{
			SCOPED_DRAW_EVENT(CMDList, NiagaraRenderRibbonsGenVerticesFinalSortGPU);
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesFinalSortGPU);
			
			// Repeatedly runs a scatter based merge sort until we have the final buffer
			uint32 SortGroupSize = FNiagaraRibbonSortPhase1CS::BubbleSortGroupWidth;
			for (uint32 Idx = 0; Idx < MergeSortPasses; Idx++)
			{
				SortParams.MergeSortSourceBlockSize = SortGroupSize;
				SortParams.MergeSortDestinationBlockSize = SortGroupSize * 2;
		
				SwapBuffers();
			
				CMDList.Transition(FRHITransitionInfo(SortParams.DestinationSortedIndices, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute));
				SetComputePipelineState(CMDList, MergeSortShader.GetComputeShader());
				SetShaderParameters(CMDList, MergeSortShader, MergeSortShader.GetComputeShader(), SortParams);
				DispatchComputeShader(CMDList, MergeSortShader, NumMergeSortThreadGroups, 1, 1);
				UnsetShaderUAVs(CMDList, MergeSortShader, MergeSortShader.GetComputeShader());
				CMDList.Transition(FRHITransitionInfo(SortParams.DestinationSortedIndices, ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer));
				
				SortGroupSize *= 2;
			}			
		}		
	}
	
	{
		SCOPED_DRAW_EVENT(CMDList, NiagaraRenderRibbonsGenVerticesReductionPhase1GPU);
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesReductionPhase1GPU);
		
		FNiagaraRibbonVertexReductionInitializationCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRibbonHasFullRibbonID>(GenerationConfig.HasFullRibbonIDs());
		PermutationVector.Set<FRibbonHasRibbonID>(GenerationConfig.HasSimpleRibbonIDs());
		PermutationVector.Set<FRibbonWantsAutomaticTessellation>(GenerationConfig.WantsAutomaticTessellation());
		PermutationVector.Set<FRibbonWantsConstantTessellation>(GenerationConfig.WantsConstantTessellation());
		
		TShaderMapRef<FNiagaraRibbonVertexReductionInitializationCS> ReductionInitializationShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		TShaderMapRef<FNiagaraRibbonVertexReductionPropagateCS> ReductionPropgateShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);	
		
		const int32 NumPrefixScanPasses = FMath::CeilLogTwo(NumExecutableInstances);
	
		FNiagaraRibbonVertexReductionParameters Params;
		FMemory::Memzero(Params);
		Params.Common = CommonParams;
		Params.SortedIndices = VertexBuffers.SortedIndicesBuffer.SRV;
		Params.CurveTension = GenerationConfig.GetCurveTension();
	
		uint32 CurrentBufferOrientation = 0x0;
		const auto SwapBuffers = [&]()
		{
			CurrentBufferOrientation ^= 0x1;
			const bool bComputeOnOutputBuffer = CurrentBufferOrientation == 0;
	
			if (bComputeOnOutputBuffer)
			{
				Params.InputTangentsAndDistances = TempBuffers.TempDistances.SRV;
				Params.OutputTangentsAndDistances = VertexBuffers.TangentsAndDistancesBuffer.UAV;
				Params.InputMultiRibbonIndices = TempBuffers.TempMultiRibbon.SRV;
				Params.OutputMultiRibbonIndices = VertexBuffers.MultiRibbonIndicesBuffer.UAV;
				Params.InputSegments = TempBuffers.TempSegments.SRV;
				Params.OutputSegments = VertexBuffers.SegmentsBuffer.UAV;
				Params.InputTessellationStats = TempBuffers.TempTessellationStats[1].SRV;
				Params.OutputTessellationStats = TempBuffers.TempTessellationStats[0].UAV;
			}
			else
			{
				Params.InputTangentsAndDistances = VertexBuffers.TangentsAndDistancesBuffer.SRV;
				Params.OutputTangentsAndDistances = TempBuffers.TempDistances.UAV;
				Params.InputMultiRibbonIndices = VertexBuffers.MultiRibbonIndicesBuffer.SRV;
				Params.OutputMultiRibbonIndices = TempBuffers.TempMultiRibbon.UAV;
				Params.InputSegments = VertexBuffers.SegmentsBuffer.SRV;
				Params.OutputSegments = TempBuffers.TempSegments.UAV;				
				Params.InputTessellationStats = TempBuffers.TempTessellationStats[0].SRV;
				Params.OutputTessellationStats = TempBuffers.TempTessellationStats[1].UAV;
			}				
		};	
	
		// Setup buffers
		if (NumPrefixScanPasses % 2 == 0)
		{
			CurrentBufferOrientation = 0x1;
			SwapBuffers();
		}
		else
		{
			CurrentBufferOrientation = 0x0;
			SwapBuffers();
		}		
		
		const auto TransitionOutputBuffers = [this, &CMDList, &Params, &TempBuffers](ERHIAccess Previous, ERHIAccess Next)
		{
			FRHITransitionInfo DataBufferTransitions[] =
			{
				FRHITransitionInfo(Params.OutputTangentsAndDistances, Previous, Next),
				FRHITransitionInfo(Params.OutputMultiRibbonIndices, Previous, Next),
				FRHITransitionInfo(Params.OutputSegments, Previous, Next),
				FRHITransitionInfo(Params.OutputTessellationStats, Previous, Next),
			};
			CMDList.Transition(MakeArrayView(DataBufferTransitions, UE_ARRAY_COUNT(DataBufferTransitions)));			
		};
		
	
		{			
			
			const uint32 NumThreadGroupsInitialization = FMath::DivideAndRoundUp<uint32>(NumExecutableInstances, FNiagaraRibbonComputeCommon::VertexGenReductionInitializationThreadSize);	
			TransitionOutputBuffers(ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute);
			SetComputePipelineState(CMDList, ReductionInitializationShader.GetComputeShader());
			SetShaderParameters(CMDList, ReductionInitializationShader, ReductionInitializationShader.GetComputeShader(), Params);
			DispatchComputeShader(CMDList, ReductionInitializationShader, NumThreadGroupsInitialization, 1, 1);
			UnsetShaderUAVs(CMDList, ReductionInitializationShader, ReductionInitializationShader.GetComputeShader());
			TransitionOutputBuffers(ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer);
		}
		
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesReductionPropagateGPU);
			
			const uint32 NumThreadGroups = FMath::DivideAndRoundUp<uint32>(NumExecutableInstances, FNiagaraRibbonComputeCommon::VertexGenReductionPropagationThreadSize);
			
			for (Params.PrefixScanStride = 1; Params.PrefixScanStride < NumExecutableInstances; Params.PrefixScanStride *= 2)
			{
				SwapBuffers();
				
				TransitionOutputBuffers(ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute);
			
				SetComputePipelineState(CMDList, ReductionPropgateShader.GetComputeShader());
				SetShaderParameters(CMDList, ReductionPropgateShader, ReductionPropgateShader.GetComputeShader(), Params);
				DispatchComputeShader(CMDList, ReductionPropgateShader, NumThreadGroups, 1, 1);
				UnsetShaderUAVs(CMDList, ReductionPropgateShader, ReductionPropgateShader.GetComputeShader());
				
				TransitionOutputBuffers(ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer);
			}
		}

		check(CurrentBufferOrientation == 0x0);		
	}
	
	static constexpr int32 CommandBufferOffset = 0;
		
	{
		SCOPED_DRAW_EVENT(CMDList, NiagaraRenderRibbonsGenVerticesReductionPhase2GPU);
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesReductionPhase2GPU);
		
		FNiagaraRibbonVertexReductionFinalizationParameters FinalizationParams;
		FMemory::Memzero(FinalizationParams);
		FinalizationParams.Common = CommonParams;
		FinalizationParams.SortedIndices = VertexBuffers.SortedIndicesBuffer.SRV;
		FinalizationParams.TangentsAndDistances = VertexBuffers.TangentsAndDistancesBuffer.SRV;
		FinalizationParams.MultiRibbonIndices = VertexBuffers.MultiRibbonIndicesBuffer.SRV;
		FinalizationParams.Segments = VertexBuffers.SegmentsBuffer.SRV;
		FinalizationParams.TessellationStats = TempBuffers.TempTessellationStats[0].SRV;
		FinalizationParams.PackedPerRibbonData = VertexBuffers.RibbonLookupTableBuffer.UAV;
		FinalizationParams.OutputCommandBuffer = VertexBuffers.GPUComputeCommandBuffer.UAV;
		FinalizationParams.OutputCommandBufferIndex= CommandBufferOffset;
		FinalizationParams.FinalizationThreadBlockSize = FNiagaraRibbonComputeCommon::VertexGenFinalizationThreadSize;
			
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesReductionFinalizeGPU);
			
			FNiagaraRibbonVertexReductionFinalizeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRibbonHasFullRibbonID>(GenerationConfig.HasFullRibbonIDs());
			PermutationVector.Set<FRibbonHasRibbonID>(GenerationConfig.HasSimpleRibbonIDs());
			PermutationVector.Set<FRibbonWantsAutomaticTessellation>(GenerationConfig.WantsAutomaticTessellation());
			PermutationVector.Set<FRibbonWantsConstantTessellation>(GenerationConfig.WantsConstantTessellation());
			PermutationVector.Set<FRibbonHasTwist>(GenerationConfig.HasTwist());
			
			TShaderMapRef<FNiagaraRibbonVertexReductionFinalizeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			// We only run a single threadgroup when we're not running multi-ribbon since we assume start/end is the first/last particle
			const uint32 NumThreadGroups = GenerationConfig.HasRibbonIDs() ?
				FMath::DivideAndRoundUp<uint32>(NumExecutableInstances, FNiagaraRibbonComputeCommon::VertexGenReductionFinalizationThreadSize) :
				1;

			FRHITransitionInfo FirstTransitionInfo;
			FirstTransitionInfo.VertexBuffer = VertexBuffers.RibbonLookupTableBuffer.Buffer;
			FirstTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			FirstTransitionInfo.AccessBefore = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer;
			FirstTransitionInfo.AccessAfter = ERHIAccess::UAVCompute;

			FRHITransitionInfo SecondTransitionInfo;
			SecondTransitionInfo.VertexBuffer = VertexBuffers.GPUComputeCommandBuffer.Buffer;
			SecondTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			SecondTransitionInfo.AccessBefore = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs;
			SecondTransitionInfo.AccessAfter = ERHIAccess::UAVCompute;

			// CMDList.Transition({
			// 	FRHITransitionInfo(VertexBuffers.RibbonLookupTableBuffer.Buffer, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute),
			// 	FRHITransitionInfo(VertexBuffers.GPUComputeCommandBuffer.Buffer, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs, ERHIAccess::UAVCompute)});

			CMDList.Transition( { FirstTransitionInfo, SecondTransitionInfo } );
			
			SetComputePipelineState(CMDList, ComputeShader.GetComputeShader());
			SetShaderParameters(CMDList, ComputeShader, ComputeShader.GetComputeShader(), FinalizationParams);
			DispatchComputeShader(CMDList, ComputeShader, NumThreadGroups, 1, 1);
			UnsetShaderUAVs(CMDList, ComputeShader, ComputeShader.GetComputeShader());

			FirstTransitionInfo.VertexBuffer = VertexBuffers.RibbonLookupTableBuffer.Buffer;
			FirstTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			FirstTransitionInfo.AccessBefore = ERHIAccess::UAVCompute;
			FirstTransitionInfo.AccessAfter = ERHIAccess::UAVCompute;

			SecondTransitionInfo.VertexBuffer = VertexBuffers.GPUComputeCommandBuffer.Buffer;
			SecondTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			SecondTransitionInfo.AccessBefore = ERHIAccess::UAVCompute;
			SecondTransitionInfo.AccessAfter = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs;
			
			// We don't need to transition RibbonLookupTableBuffer as it's still needed for the next shader
			// CMDList.Transition({
			// 	FRHITransitionInfo(VertexBuffers.RibbonLookupTableBuffer.Buffer, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			// 	FRHITransitionInfo(VertexBuffers.GPUComputeCommandBuffer.Buffer, ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer | ERHIAccess::IndirectArgs)});
			
			CMDList.Transition( { FirstTransitionInfo, SecondTransitionInfo } );
		}		
	}
		
	{
		SCOPED_DRAW_EVENT(CMDList, NiagaraRenderRibbonsGenVerticesMultiRibbonInitGPU);
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesMultiRibbonInitGPU);
		
		FNiagaraRibbonVertexFinalizationParameters FinalizeParams;
		FMemory::Memzero(FinalizeParams);
		FinalizeParams.Common = CommonParams;
		FinalizeParams.SortedIndices = VertexBuffers.SortedIndicesBuffer.SRV;
		FinalizeParams.TangentsAndDistances = VertexBuffers.TangentsAndDistancesBuffer.UAV;
		FinalizeParams.PackedPerRibbonData = VertexBuffers.RibbonLookupTableBuffer.UAV;
		FinalizeParams.CommandBuffer = VertexBuffers.GPUComputeCommandBuffer.SRV;
		FinalizeParams.CommandBufferOffset = CommandBufferOffset;
		
		const auto AddUVChannelParams = [](const FNiagaraGPURibbonUVSettings& Input, FNiagaraGPURibbonUVSettingsParams& Output)
		{
			Output.Offset = FVector2D(Input.Offset);
			Output.Scale = FVector2D(Input.Scale);
			Output.TilingLength = Input.TilingLength;
			Output.DistributionMode = static_cast<int32>(Input.DistributionMode);
			Output.LeadingEdgeMode = static_cast<int32>(Input.LeadingEdgeMode);
			Output.TrailingEdgeMode = static_cast<int32>(Input.TrailingEdgeMode);
			Output.bEnablePerParticleUOverride = Input.bEnablePerParticleUOverride ? 1 : 0;
			Output.bEnablePerParticleVRangeOverride = Input.bEnablePerParticleVRangeOverride ? 1 : 0;			
		};
	
		AddUVChannelParams(UV0Settings, FinalizeParams.UV0Settings);
		AddUVChannelParams(UV1Settings, FinalizeParams.UV1Settings);
	
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsGenVerticesMultiRibbonInitComputeGPU);
			
			FNiagaraRibbonUVParamCalculationCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRibbonHasFullRibbonID>(GenerationConfig.HasFullRibbonIDs());
			PermutationVector.Set<FRibbonHasRibbonID>(GenerationConfig.HasSimpleRibbonIDs());
			PermutationVector.Set<FRibbonWantsAutomaticTessellation>(GenerationConfig.WantsAutomaticTessellation());
			PermutationVector.Set<FRibbonWantsConstantTessellation>(GenerationConfig.WantsConstantTessellation());
			PermutationVector.Set<FRibbonHasTwist>(GenerationConfig.HasTwist());

			TShaderMapRef<FNiagaraRibbonUVParamCalculationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FRHITransitionInfo FirstTransitionInfo;
			FirstTransitionInfo.VertexBuffer = VertexBuffers.RibbonLookupTableBuffer.Buffer;
			FirstTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			FirstTransitionInfo.AccessBefore = ERHIAccess::UAVCompute;
			FirstTransitionInfo.AccessAfter = ERHIAccess::UAVCompute;

			FRHITransitionInfo SecondTransitionInfo;
			SecondTransitionInfo.VertexBuffer = VertexBuffers.TangentsAndDistancesBuffer.Buffer;
			SecondTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			SecondTransitionInfo.AccessBefore = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer;
			SecondTransitionInfo.AccessAfter = ERHIAccess::UAVCompute;
			
			// We don't need to transition RibbonLookupTableBuffer as it's still setup for UAV from the last shader
			// CMDList.Transition({
			// 	FRHITransitionInfo(VertexBuffers.RibbonLookupTableBuffer.Buffer, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute),
			// 	FRHITransitionInfo(VertexBuffers.TangentsAndDistancesBuffer.Buffer, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute)});
			CMDList.Transition( { FirstTransitionInfo, SecondTransitionInfo } );
			
			SetComputePipelineState(CMDList, ComputeShader.GetComputeShader());			
			SetShaderParameters(CMDList, ComputeShader, ComputeShader.GetComputeShader(), FinalizeParams);
			DispatchIndirectComputeShader(CMDList, ComputeShader.GetShader(), VertexBuffers.GPUComputeCommandBuffer.Buffer, CommandBufferOffset * FNiagaraRibbonCommandBufferLayout::NumElements);
			UnsetShaderUAVs(CMDList, ComputeShader, ComputeShader.GetComputeShader());

			FirstTransitionInfo.VertexBuffer = VertexBuffers.TangentsAndDistancesBuffer.Buffer;
			FirstTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			FirstTransitionInfo.AccessBefore = ERHIAccess::UAVCompute;
			FirstTransitionInfo.AccessAfter = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer;

			SecondTransitionInfo.VertexBuffer = VertexBuffers.RibbonLookupTableBuffer.Buffer;
			SecondTransitionInfo.Type = FRHITransitionInfo::EType::VertexBuffer;
			SecondTransitionInfo.AccessBefore = ERHIAccess::UAVCompute;
			SecondTransitionInfo.AccessAfter = ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer;
			
			// CMDList.Transition({
			// 	FRHITransitionInfo(VertexBuffers.TangentsAndDistancesBuffer.Buffer, ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer),
			// 	FRHITransitionInfo(VertexBuffers.RibbonLookupTableBuffer.Buffer, ERHIAccess::UAVCompute, ERHIAccess::SRVMask | ERHIAccess::VertexOrIndexBuffer)});
			CMDList.Transition( { FirstTransitionInfo, SecondTransitionInfo } );
		}		
	}
}

void FNiagaraRibbonComputeDispatchManager::GenerateAllGPUData(FRHICommandListImmediate& CMDList)
{
	SCOPED_GPU_STAT(CMDList, NiagaraGPURibbonRenderers);
	
	// Handle all vertex gens first
	for (int32 Index = 0; Index < RenderersToGenerate.Num(); Index++)
	{
		const auto& Params = RenderersToGenerate[Index];

		const auto RenderingResources = Params.RenderingResources.Pin();
		if (RenderingResources.IsValid())
		{
			const bool bIsGPUSim = Params.SourceParticleData->GetGPUInstanceCountBufferOffset() != INDEX_NONE;

			ComputeBuffers.InitOrUpdateBuffers(bIsGPUSim? Params.SourceParticleData->GetNumInstancesAllocated() : Params.SourceParticleData->GetNumInstances(),
				Params.Renderer->GenerationConfig.HasRibbonIDs(),
				Params.Renderer->GenerationConfig.WantsAutomaticTessellation(),
				Params.Renderer->GenerationConfig.HasTwist());
		
		
			Params.Renderer->InitializeVertexBuffersGPU(CMDList, Params.Batcher, Params.SourceParticleData, ComputeBuffers, RenderingResources);
		}
	}
		
	// Now handle all index gens
	for (const auto& RendererToGen : RenderersToGenerate)
	{
		const auto RenderingResources = RendererToGen.RenderingResources.Pin();
		if (RenderingResources.IsValid())
		{
			for (int32 Index = 0; Index < RenderingResources->ViewResources.Num(); Index++)
			{
				const auto& RenderingResourcesView = RenderingResources->ViewResources[Index];
				RendererToGen.Renderer->InitializeViewIndexBuffersGPU(CMDList, RendererToGen.Batcher, RendererToGen.SourceParticleData, RenderingResourcesView);
			}
		}
	}
	
	RenderersToGenerate.Empty();
}