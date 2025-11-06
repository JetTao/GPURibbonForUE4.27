// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRenderer.h: Base class for Niagara render modules
==============================================================================*/
#pragma once

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraGPURibbonVertexFactory.h"
#include "NiagaraRibbonCompute.h"
#include "NiagaraRenderer.h"
#include "NiagaraCommon.h"
#include "NiagaraGPURibbonRendererProperties.h"

class FNiagaraDataSet;

struct FNiagaraDynamicDataGPURibbon;
struct FNiagaraRibbonRenderingFrameResources;
struct FNiagaraRibbonRenderingFrameViewResources;


struct FNiagaraIndexGenerationInput
{
	float ViewDistance;
	int32 LODDistanceFactor;
	
	uint32 MaxSegmentCount;
	uint32 SubSegmentCount;	

	uint32 SegmentBitShift;
	uint32 SegmentBitMask;

	uint32 SubSegmentBitShift;
	uint32 SubSegmentBitMask;

	uint32 ShapeBitMask;
	
	uint32 TotalBitCount;
	uint32 TotalNumIndices;
};

struct FNiagaraRibbonGenerationConfig
{
public:
	FNiagaraRibbonGenerationConfig(const UNiagaraGPURibbonRendererProperties* Properties)
		: MaterialParamValidMask(Properties->MaterialParamValidMask)
		, CurveTension(Properties->CurveTension)
		, MaxNumRibbons(Properties->MaxNumRibbons)
		, bHasFullRibbonIDs(Properties->RibbonFullIDDataSetAccessor.IsValid())
		, bHasSimpleRibbonIDs(Properties->RibbonIdDataSetAccessor.IsValid())
		, bHasCustomLinkOrder(Properties->RibbonLinkOrderDataSetAccessor.IsValid())
		, bHasTwist(Properties->TwistDataSetAccessor.IsValid())
		, bHasCustomU0Data(Properties->UV0Settings.bEnablePerParticleUOverride && Properties->U0OverrideIsBound)
		, bHasCustomU1Data(Properties->UV1Settings.bEnablePerParticleUOverride && Properties->U1OverrideIsBound)
		, bWantsConstantTessellation(Properties->TessellationMode == ENiagaraGPURibbonTessellationMode::Custom && Properties->bUseConstantFactor)
		, bWantsAutomaticTessellation(Properties->TessellationMode != ENiagaraGPURibbonTessellationMode::Disabled && !bWantsConstantTessellation)
		, bNeedsPreciseMotionVectors(Properties->NeedsPreciseMotionVectors())
	{
		
	}
	
	uint32 GetMaterialParamValidMask() const { return MaterialParamValidMask; }
	float GetCurveTension() const { return FMath::Min(CurveTension, 0.99f); }
	int32 GetMaxNumRibbons() const { return MaxNumRibbons; }
	
	bool HasFullRibbonIDs() const { return bHasFullRibbonIDs; }
	bool HasSimpleRibbonIDs() const { return bHasSimpleRibbonIDs; }
	bool HasRibbonIDs() const { return HasFullRibbonIDs() || HasSimpleRibbonIDs(); };
	
	bool HasCustomLinkOrder() const { return bHasCustomLinkOrder; }
	bool HasTwist() const { return bHasTwist; }
	
	bool HasCustomU0Data() const { return bHasCustomU0Data; }
	bool HasCustomU1Data() const { return bHasCustomU1Data; }
	
	bool WantsConstantTessellation () const { return bWantsConstantTessellation; }
	bool WantsAutomaticTessellation() const { return bWantsAutomaticTessellation; }
	
	bool NeedsPreciseMotionVectors() const { return bNeedsPreciseMotionVectors; }

private:
	const uint32 MaterialParamValidMask;
	const float CurveTension;
	const int32 MaxNumRibbons;
	
	const uint32 bHasFullRibbonIDs : 1;
	const uint32 bHasSimpleRibbonIDs : 1;
	const uint32 bHasCustomLinkOrder : 1;
	const uint32 bHasTwist : 1;
	
	const uint32 bHasCustomU0Data : 1;
	const uint32 bHasCustomU1Data : 1;
	const uint32 bWantsConstantTessellation : 1;
	const uint32 bWantsAutomaticTessellation : 1;	
	const uint32 bNeedsPreciseMotionVectors : 1;
};

struct FRibbonMultiRibbonInfoBufferEntry
{
	static constexpr int32 NumElements = 8;
	
	float U0Scale = 1.0;
	float U0Offset = 0.0;
	float U0DistributionScaler = 1.0;
	float U1Scale = 1.0;
	float U1Offset = 0.0;
	float U1DistributionScaler = 1.0;
	int32 FirstParticleId = INDEX_NONE;
	int32 LastParticleId = INDEX_NONE;
};

struct FNiagaraRibbonTessellationConfig
{
	ENiagaraGPURibbonTessellationMode TessellationMode;
	int32 CustomTessellationFactor;
	bool bCustomUseConstantFactor;
	float CustomTessellationMinAngle;
	bool bCustomUseScreenSpace;
};

struct FNiagaraRibbonVertexBuffers
{
	FRWBuffer SortedIndicesBuffer;
	FRWBuffer TangentsAndDistancesBuffer;
	FRWBuffer MultiRibbonIndicesBuffer;
	FRWBuffer RibbonLookupTableBuffer;
	FRWBuffer SegmentsBuffer;

	FRWBuffer GPUComputeCommandBuffer;

	int32 SortedIndicesLength;
	int32 TangentsLength;
	int32 MultiRibbonIndexLength;
	int32 RibbonLookupTableLength;
	int32 SegmentLength;
	int32 GPUComputeCommandLength;
	bool bJustCreatedCommandBuffer;

	FNiagaraRibbonVertexBuffers()
		: SortedIndicesLength(0)
		, TangentsLength(0)
		, MultiRibbonIndexLength(0)
		, RibbonLookupTableLength(0)
		, SegmentLength(0)
		, GPUComputeCommandLength(0)
		, bJustCreatedCommandBuffer(false)
	{
		
	}

	static bool InitOrUpdateBuffer(bool bEnabled, FRWBuffer& Buffer, int32& CurrentLength, int32 NeededLength, int32 MaxLength, FRWBuffer (*InitFunction)(int32, ERHIAccess), ERHIAccess InitialAccessFlags = ERHIAccess::None);

	void InitializeOrUpdateBuffers(const FNiagaraRibbonGenerationConfig& GenerationConfig, const FNiagaraDataBuffer* SourceParticleData, int32 MaxAllocatedCount);

	void Release()
	{
		SortedIndicesBuffer.Release();
		TangentsAndDistancesBuffer.Release();
		MultiRibbonIndicesBuffer.Release();
		RibbonLookupTableBuffer.Release();
		SegmentsBuffer.Release();
		GPUComputeCommandBuffer.Release();
		
		SortedIndicesLength = 0;
		TangentsLength = 0;
		MultiRibbonIndexLength = 0;
		RibbonLookupTableLength = 0;
		SegmentLength = 0;
		GPUComputeCommandLength = 0;
	}

	static FRWBuffer CreateSortedIndicesBuffer(int32 Size, ERHIAccess InitialAccessFlags = ERHIAccess::None);
	static FRWBuffer CreateTangentsAndDistancesBuffer(int32 Size, ERHIAccess InitialAccessFlags = ERHIAccess::None);
	static FRWBuffer CreateMultiRibbonIndicesBuffer(int32 Size, ERHIAccess InitialAccessFlags = ERHIAccess::None);
	static FRWBuffer CreateRibbonLookupTableBuffer(int32 Size, ERHIAccess InitialAccessFlags = ERHIAccess::None);
	static FRWBuffer CreateSegmentsBuffer(int32 Size, ERHIAccess InitialAccessFlags = ERHIAccess::None);
	static FRWBuffer CreateCommandBuffer(int32 Size, ERHIAccess InitialAccessFlags = ERHIAccess::None);
};

struct FNiagaraRibbonShapeGeometryData
{
	struct FVertex
	{
		static constexpr int32 NumElements = 5;
		
		FVector2D Position;
		FVector2D Normal;
		float TextureV;

		FVertex(const FVector2D& InPosition, const FVector2D& InNormal, float InTextureV)
			: Position(InPosition), Normal(InNormal), TextureV(InTextureV) { }
	};		
	static_assert(sizeof(FVertex) == (sizeof(float) * FVertex::NumElements), "");
	
	// This sets up the first and next vertex for each pair of triangles in the slice.
	// For a plane this will just be a linear set
	// For a multiplane it will be multiple separate linear sets
	// For a tube it will be a linear set that wraps back around to itself,
	// Same with the custom vertices.
	TArray<uint32, TInlineAllocator<32>> SliceTriangleToVertexIds;
	FReadBuffer SliceTriangleToVertexIdsBuffer;
	
	TArray<FVertex> SliceVertexData;		
	FReadBuffer SliceVertexDataBuffer;

	ENiagaraGPURibbonShapeMode Shape;
	
	int32 TrianglesPerSegment;
	int32 NumVerticesInSlice;
	int32 BitsNeededForShape;
	int32 BitMaskForShape;
	bool bDisableBackfaceCulling;
	bool bShouldFlipNormalToView;
};


/**
* NiagaraRendererRibbons renders an FNiagaraEmitterInstance as a ribbon connecting all particles
* in order by particle age.
*/
class FNiagaraRendererGPURibbons : public FNiagaraRenderer
{
public:
	FNiagaraRendererGPURibbons(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);	// FNiagaraRenderer Interface 
	~FNiagaraRendererGPURibbons();

	// FNiagaraRenderer Interface 
	virtual void CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) override;
	virtual void ReleaseRenderThreadResources() override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const override;
	virtual FNiagaraDynamicDataBase *GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitteride) const override;
	virtual int32 GetDynamicDataSize()const override;
	virtual bool IsMaterialValid(const UMaterialInterface* Mat)const override;
#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) final override;
#endif

protected:
	
	static void GenerateShapeStateMultiPlane(FNiagaraRibbonShapeGeometryData& State, int32 MultiPlaneCount, int32 WidthSegmentationCount, bool bEnableAccurateGeometry);
	static void GenerateShapeStateTube(FNiagaraRibbonShapeGeometryData& State, int32 TubeSubdivisions);
	static void GenerateShapeStateCustom(FNiagaraRibbonShapeGeometryData& State, const TArray<FNiagaraGPURibbonShapeCustomVertex>& CustomVertices);
	static void GenerateShapeStatePlane(FNiagaraRibbonShapeGeometryData& State, int32 WidthSegmentationCount);	
	void InitializeShape(const UNiagaraGPURibbonRendererProperties* Properties);
	
	void InitializeTessellation(const UNiagaraGPURibbonRendererProperties* Properties);
	
	FNiagaraIndexGenerationInput CalculateIndexBufferConfiguration(const FNiagaraDataBuffer* SourceParticleData,
		const FNiagaraSceneProxy* SceneProxy, const FSceneView* View, const FVector& ViewOriginForDistanceCulling) const;
	
	void GenerateIndexBufferForView(FNiagaraIndexGenerationInput& GeneratedData, FNiagaraDynamicDataGPURibbon* DynamicDataRibbon,
	                                const TSharedPtr<FNiagaraRibbonRenderingFrameViewResources>& RenderingViewResources, const FSceneView* View, const FVector& ViewOriginForDistanceCulling) const;
	
	void SetupPerViewUniformBuffer(FNiagaraIndexGenerationInput& GeneratedData,
	                               const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy* SceneProxy, FNiagaraGPURibbonUniformBufferRef& OutUniformBuffer) const;

	void SetupMeshBatchAndCollectorResourceForView(const FNiagaraIndexGenerationInput& GeneratedData, FNiagaraDynamicDataGPURibbon* DynamicDataRibbon,
	                                               const FNiagaraDataBuffer* SourceParticleData, const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy* SceneProxy,
	                                               const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources, const TSharedPtr<FNiagaraRibbonRenderingFrameViewResources>& RenderingViewResources, FMeshBatch& OutMeshBatch) const;


	void InitializeViewIndexBuffersGPU(FRHICommandListImmediate& CMDList, NiagaraEmitterInstanceBatcher* Bathcer, const FNiagaraDataBuffer* SourceParticleData,
		const TSharedPtr<FNiagaraRibbonRenderingFrameViewResources>& RenderingViewResources) const;

	void InitializeVertexBuffersResources(const FNiagaraDynamicDataGPURibbon* DynamicDataRibbon, FNiagaraDataBuffer* SourceParticleData,
	                                      FGlobalDynamicReadBuffer& DynamicReadBuffer, const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources) const;
	
	void InitializeVertexBuffersGPU(FRHICommandListImmediate& CMDList, NiagaraEmitterInstanceBatcher* Bathcer, const FNiagaraDataBuffer* SourceParticleData,
		struct FNiagaraRibbonGPUInitComputeBuffers& TempBuffers, const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources) const;

	FRibbonComputeUniformParameters SetupComputeVertexGenParams(NiagaraEmitterInstanceBatcher* Bathcer, const TSharedPtr<FNiagaraRibbonRenderingFrameResources>& RenderingResources, const FNiagaraDataBuffer* SourceParticleData) const;

	FNiagaraRibbonGenerationConfig GenerationConfig;
	
	FNiagaraGPURibbonUVSettings UV0Settings;
	FNiagaraGPURibbonUVSettings UV1Settings;
	FNiagaraRibbonShapeGeometryData ShapeState;
	FNiagaraRibbonTessellationConfig TessellationConfig;
	
	ENiagaraGPURibbonFacingMode FacingMode;
	ENiagaraGPURibbonDrawDirection DrawDirection;
	
	const FNiagaraRendererLayout* RendererLayout;

	mutable FNiagaraRibbonVertexBuffers VertexBuffers;
	
	int32 RibbonIDParamDataSetOffset;

	friend class FNiagaraRibbonComputeDispatchManager;
};







