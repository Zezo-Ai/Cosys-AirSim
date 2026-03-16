// This class and its functions are derivatives of the work of UnrealCV, https://unrealcv.org/
// Licensed under the MIT License.


#include "AnnotationComponent.h"
// Overwrite the material

#include "Runtime/CoreUObject/Public/UObject/ConstructorHelpers.h"
#include "Runtime/Engine/Public/Materials/Material.h"
#include "Runtime/Engine/Public/Materials/MaterialInstanceDynamic.h"
#include "Runtime/Engine/Classes/Engine/StaticMesh.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Runtime/Engine/Public/MaterialShared.h"
#include "Runtime/Engine/Classes/Engine/Engine.h"
#include "AirBlueprintLib.h"

#if ENGINE_MAJOR_VERSION >= 5
//different header files in UE
#include "Runtime/Engine/Public/StaticMeshSceneProxy.h"
#include "Runtime/Engine/Public/SkeletalMeshSceneProxy.h"
#include "Runtime/Engine/Public/SkinnedMeshSceneProxyDesc.h"
#endif
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"

// Console variable to enable/disable Nanite skeletal annotation support
static TAutoConsoleVariable<int32> CVarAirSimAnnotationNaniteSkeletal(
	TEXT("airsim.annotation.nanite.skeletal"),
	1,
	TEXT("Enable Nanite skeletal mesh annotation support (default: 1)")
);

/** A proxy class to get mesh data from StaticMesh, should be used together with AnnotationCamSensor.
Inheritance is needed because I need to access protected data
Use `show Material` command to see the effect of this component
Note that some area might be not colored, this is caused by the issue that
both the original mesh and the annotation mesh are rendered, this is not an issue for the AnnotationCamSensor, which will exclude original meshes.
*/
class FStaticAnnotationSceneProxy : public FStaticMeshSceneProxy
{
public:
	FMaterialRenderProxy* MaterialRenderProxy;

	//FStaticMeshSceneProxyDesc::InitializeFrom(UStaticMeshComponent* Component);

	FStaticAnnotationSceneProxy(UStaticMeshComponent* Component, bool bForceLODsShareStaticLighting, UMaterialInterface* AnnotationMID) :
		FStaticMeshSceneProxy(Component, bForceLODsShareStaticLighting)
	{
		MaterialRenderProxy = AnnotationMID->GetRenderProxy();
		// this->MaterialRelevance = AnnotationMID->GetRelevance(GetScene().GetFeatureLevel());
		// Note: This MaterailRelevance makes no difference?

		this->bVerifyUsedMaterials = false;
		// This is required, otherwise the code will fail

		bCastShadow = false;
	}

	virtual void GetDynamicMeshElements(
		const TArray < const FSceneView * > & Views,
		const FSceneViewFamily & ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector & Collector) const override;

	virtual bool GetMeshElement
	(
		int32 LODIndex,
		int32 BatchIndex,
		int32 ElementIndex,
		uint8 InDepthPriorityGroup,
		bool bUseSelectedMaterial,
		bool bAllowPreCulledIndices,
		FMeshBatch & OutMeshBatch
	) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView * View) const override;
};

FPrimitiveViewRelevance FStaticAnnotationSceneProxy::GetViewRelevance(const FSceneView * View) const
{
	if (View->Family->EngineShowFlags.Materials)
	{
		FPrimitiveViewRelevance ViewRelevance;
		ViewRelevance.bDrawRelevance = 0; 
		return ViewRelevance;
	}
	else
	{
		return FStaticMeshSceneProxy::GetViewRelevance(View);
	}
}


void FStaticAnnotationSceneProxy::GetDynamicMeshElements(
	const TArray < const FSceneView * > & Views,
	const FSceneViewFamily & ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector & Collector) const
{
	//if (MaterialRenderProxy->GetMaterialName().Contains("AnnotationMaterialMID")) {
	//	FStaticMeshSceneProxy::GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector);
	//}	
	FStaticMeshSceneProxy::GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector);

}

bool FStaticAnnotationSceneProxy::GetMeshElement(
	int32 LODIndex,
	int32 BatchIndex,
	int32 ElementIndex,
	uint8 InDepthPriorityGroup,
	bool bUseSelectedMaterial,
	bool bAllowPreCulledIndices,
	FMeshBatch & OutMeshBatch) const
{
	bool Ret = FStaticMeshSceneProxy::GetMeshElement(LODIndex, BatchIndex, ElementIndex, InDepthPriorityGroup,
		bUseSelectedMaterial, bAllowPreCulledIndices, OutMeshBatch);
	OutMeshBatch.MaterialRenderProxy = this->MaterialRenderProxy;
	return Ret;
}

class FSkeletalAnnotationSceneProxy : public FSkeletalMeshSceneProxy
{
public:
	FSkeletalAnnotationSceneProxy(const USkinnedMeshComponent* Component, FSkeletalMeshRenderData* InSkeletalMeshRenderData, UMaterialInterface* AnnotationMID)
	: FSkeletalMeshSceneProxy(Component, InSkeletalMeshRenderData)
	{
		this->bVerifyUsedMaterials = false;
		this->bCastDynamicShadow = false;
		for(int32 LODIdx=0; LODIdx < LODSections.Num(); LODIdx++)
		{
			FLODSectionElements& LODSection = LODSections[LODIdx];
			for(int32 SectionIndex = 0; SectionIndex < LODSection.SectionElements.Num(); SectionIndex++)
			{
				if (IsValid(AnnotationMID))
				{
					LODSection.SectionElements[SectionIndex].Material = AnnotationMID;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: AnnotationMaterial is Invalid in FSkeletalSceneProxy"));
				}
			}
		}
	}
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView * View) const override;

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const;
};

// Custom Nanite skeletal annotation proxy that uses Nanite rendering pipeline
// but overrides material rendering to apply annotation colors
class FNaniteSkeletalAnnotationSceneProxy : public Nanite::FSkinnedSceneProxy
{
public:
	FNaniteSkeletalAnnotationSceneProxy(
		const USkinnedMeshComponent* Component,
		FSkeletalMeshRenderData* InRenderData,
		UMaterialInterface* AnnotationMID
	)
		: Nanite::FSkinnedSceneProxy(CreateNaniteMaterialAudit(Component), Component, InRenderData, true)
		, AnnotationMaterial(AnnotationMID)
	{

		if (!IsValid(AnnotationMID))
		{
			UE_LOG(LogTemp, Warning, TEXT("Airsim Annotation: Nanite skeletal annotation material is invalid"));
			return;
		}

		FMaterialRenderProxy* AnnotationRenderProxy = AnnotationMID->GetRenderProxy();
		const FMaterialRelevance AnnotationRelevance = AnnotationMID->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
		int32 UpdatedSectionCount = 0;
		for (Nanite::FSceneProxyBase::FMaterialSection& MaterialSection : GetMaterialSections())
		{
			MaterialSection.ShadingMaterialProxy = AnnotationRenderProxy;
			MaterialSection.RasterMaterialProxy = AnnotationRenderProxy;
			MaterialSection.MaterialRelevance = AnnotationRelevance;
			MaterialSection.LocalUVDensities = FVector4f(1.0f);
			MaterialSection.bHasPerInstanceRandomID = false;
			MaterialSection.bHasPerInstanceCustomData = false;
			++UpdatedSectionCount;
		}

		OnMaterialsUpdated(true);
		UE_LOG(LogTemp, VeryVerbose, TEXT("Airsim Annotation: Nanite skeletal proxy remapped %d material sections to %s"), UpdatedSectionCount, *AnnotationMID->GetName());
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		// Hide from main pass (regular scene/lighting cameras) - only show in annotation scene captures
		if (View->Family->EngineShowFlags.Materials)
		{
			FPrimitiveViewRelevance ViewRelevance;
			ViewRelevance.bDrawRelevance = 0;
			return ViewRelevance;
		}
		
		// Show in annotation passes (where Materials flag is OFF)
		FPrimitiveViewRelevance ViewRelevance = Nanite::FSkinnedSceneProxy::GetViewRelevance(View);
		// Ensure it's marked as relevant for rendering in annotation-only context
		ViewRelevance.bDrawRelevance = 1;
		return ViewRelevance;
	}

private:
	static Nanite::FMaterialAudit CreateNaniteMaterialAudit(const USkinnedMeshComponent* Component)
	{
		Nanite::FMaterialAudit Audit;
		if (Component)
		{
			Nanite::AuditMaterials(Component, Audit, false);
		}

		UE_LOG(LogTemp, VeryVerbose, TEXT("Airsim Annotation: Created Nanite material audit with %d entries"), Audit.Entries.Num());

		return Audit;
	}

	UMaterialInterface* AnnotationMaterial;
};


void FSkeletalAnnotationSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	//if (LODSections.Num() > 0){
	//	if (LODSections[0].SectionElements.Num() > 0) {
	//		if (LODSections[0].SectionElements[0].Material->GetName().Contains("AnnotationMaterialMID")) {
	//			FSkeletalMeshSceneProxy::GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector);
	//		}
	//	}
	//}
	FSkeletalMeshSceneProxy::GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector);

}

FPrimitiveViewRelevance FSkeletalAnnotationSceneProxy::GetViewRelevance(const FSceneView * View) const
{
	if (View->Family->EngineShowFlags.Materials)
	{
		FPrimitiveViewRelevance ViewRelevance;
		ViewRelevance.bDrawRelevance = 0;
		return ViewRelevance;
	}
	else
	{
		return FSkeletalMeshSceneProxy::GetViewRelevance(View);
	}
}

// FString MeterialPath = TEXT("MaterialInstanceConstant'/UnrealCV/AnnotationColor_Inst.AnnotationColor_Inst'");
UAnnotationComponent::UAnnotationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	  // , ParentMeshInfo(nullptr)
{
	bSkeletalMesh = false;
	bTexture = false;
	// Initialize from console variable - can be overridden per-component with SetEnableNaniteSkeletalAnnotationPath()
	bEnableNaniteSkeletalAnnotationPath = CVarAirSimAnnotationNaniteSkeletal.GetValueOnGameThread() != 0;
	last_foliage_type_ = EFoliageComponentType::None;

	FString MaterialPath = TEXT("Material'/AirSim/HUDAssets/AnnotationMaterial.AnnotationMaterial'");
	static ConstructorHelpers::FObjectFinder<UMaterial> AnnotationMaterialObject(*MaterialPath);
	if (AnnotationMaterialObject.Object == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: Annotation material is not valid."));
    }
    else
    {
        AnnotationMaterial = AnnotationMaterialObject.Object;
	}

	FString MaterialPathSphere = TEXT("Material'/AirSim/HUDAssets/AnnotationMaterialSphere.AnnotationMaterialSphere'");
	static ConstructorHelpers::FObjectFinder<UMaterial> SphereMaterialObject(*MaterialPathSphere);
	if (SphereMaterialObject.Object == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: Sphere annotation material is not valid."));
	}
	else
	{
		SphereMaterial = SphereMaterialObject.Object;
	}

	// ParentMeshInfo = MakeShareable(new FParentMeshInfo(nullptr));
	// This will be invalid until attached to a MeshComponent
	this->PrimaryComponentTick.bCanEverTick = true;
}

void UAnnotationComponent::OnRegister()
{
	Super::OnRegister();

	if (this->GetFName().ToString().Contains("annotation_sphere")) {
		AnnotationMID = UMaterialInstanceDynamic::Create(SphereMaterial, this, TEXT("AnnotationMaterialMID"));
		if (!IsValid(AnnotationMID))
		{
			UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: SphereMaterial is not correctly initialized"));
			return;
		}
		FLinearColor LinearAnnotationColor = FLinearColor(0, 0, 0, 1.0);
		AnnotationMID->SetVectorParameterValue("AnnotationColor", LinearAnnotationColor);
	}
	else {
		// Note: This can not be placed in the constructor, MID means material instance dynamic
		AnnotationMID = UMaterialInstanceDynamic::Create(AnnotationMaterial, this, TEXT("AnnotationMaterialMID"));
		if (!IsValid(AnnotationMID))
		{
			UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: ColorAnnotationMaterial is not correctly initialized"));
			return;
		}
		const float OneOver255 = 1.0f / 255.0f;
		FLinearColor LinearAnnotationColor = FLinearColor(
			this->AnnotationColor.R * OneOver255,
			this->AnnotationColor.G * OneOver255,
			this->AnnotationColor.B * OneOver255,
			1.0
		);
		AnnotationMID->SetVectorParameterValue("AnnotationColor", LinearAnnotationColor);
	}
}

/** 
 * Note: The "exposure compensation" in "PostProcessVolume3" in the RR map will destroy the color
 * Saturate the color to 1. This is a mysterious behavior after tedious debug.
 */
void UAnnotationComponent::SetAnnotationColor(FColor NewAnnotationColor)
{
	if (NewAnnotationColor.R == 27)NewAnnotationColor.R = 26;
	if (NewAnnotationColor.G == 27)NewAnnotationColor.G = 26;
	if (NewAnnotationColor.B == 27)NewAnnotationColor.B = 26;
	if (NewAnnotationColor.R == 32)NewAnnotationColor.R = 31;
	if (NewAnnotationColor.G == 32)NewAnnotationColor.G = 31;
	if (NewAnnotationColor.B == 32)NewAnnotationColor.B = 31;
	if (NewAnnotationColor.R == 35)NewAnnotationColor.R = 34;
	if (NewAnnotationColor.G == 35)NewAnnotationColor.G = 34;
	if (NewAnnotationColor.B == 35)NewAnnotationColor.B = 34;
	if (NewAnnotationColor.R == 41)NewAnnotationColor.R = 40;
	if (NewAnnotationColor.G == 41)NewAnnotationColor.G = 40;
	if (NewAnnotationColor.B == 41)NewAnnotationColor.B = 40;
	if (NewAnnotationColor.R == 44)NewAnnotationColor.R = 43;
	if (NewAnnotationColor.G == 44)NewAnnotationColor.G = 43;
	if (NewAnnotationColor.B == 44)NewAnnotationColor.B = 43;
	if (NewAnnotationColor.R == 49)NewAnnotationColor.R = 48;
	if (NewAnnotationColor.G == 49)NewAnnotationColor.G = 48;
	if (NewAnnotationColor.B == 49)NewAnnotationColor.B = 48;
	if (NewAnnotationColor.R == 51)NewAnnotationColor.R = 50;
	if (NewAnnotationColor.G == 51)NewAnnotationColor.G = 50;
	if (NewAnnotationColor.B == 51)NewAnnotationColor.B = 50;
	this->AnnotationColor = NewAnnotationColor;
	const float OneOver255 = 1.0f / 255.0f; // TODO: Check 255 or 256?

	FLinearColor LinearAnnotationColor = FLinearColor(
		AnnotationColor.R * OneOver255,
		AnnotationColor.G * OneOver255,
		AnnotationColor.B * OneOver255,
		1.0
	);

	if (IsValid(AnnotationMID))
	{
		AnnotationMID->SetVectorParameterValue("AnnotationColor", LinearAnnotationColor);
	}
}

void UAnnotationComponent::SetAnnotationTexture(FString NewAnnotationTexturePath)
{
    bTexture = true;
	AnnotationMID->SetScalarParameterValue("TextureEnabled", 1);
    this->AnnotationTexturePath = NewAnnotationTexturePath;
    TArray<FString> splitPath;
    NewAnnotationTexturePath.ParseIntoArray(splitPath, TEXT("/"), true);
    FString TextureFileName = splitPath.Last();
	FString FullPath = FString::Printf(TEXT("%s.%s"), *NewAnnotationTexturePath, *TextureFileName);
	UTexture* AnnotationTexture = LoadObject<UTexture>(NULL, *FullPath);

    if (AnnotationTexture != nullptr)
    {       
        if (IsValid(AnnotationMID))
        {
			AnnotationMID->SetTextureParameterValue("AnnotationTexture", AnnotationTexture);
		}else
		{
			UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: Could not set annotation texture to %s cause something wrong with MID."), *FullPath);
		}
    }
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: Could not set annotation texture to %s."), *FullPath);
	}
}

void UAnnotationComponent::SetAnnotationTexture(UTexture* NewAnnotationTexture)
{
	bTexture = true;
	AnnotationMID->SetScalarParameterValue("TextureEnabled", 1);
	TArray<FString> splitPath;
	NewAnnotationTexture->GetPathName().ParseIntoArray(splitPath, TEXT("."), true);
	FString TextureFilePath = splitPath[0];
	this->AnnotationTexturePath = TextureFilePath;
	if (IsValid(AnnotationMID))
	{
		AnnotationMID->SetTextureParameterValue("AnnotationTexture", NewAnnotationTexture);
	}
}

FColor UAnnotationComponent::GetAnnotationColor()
{
	return AnnotationColor;
}

FString UAnnotationComponent::GetAnnotationTexturePath()
{
	return AnnotationTexturePath;
}

UAnnotationComponent::EFoliageComponentType UAnnotationComponent::GetLastDetectedFoliageType() const
{
	return last_foliage_type_;
}

void UAnnotationComponent::SetEnableNaniteSkeletalAnnotationPath(bool bEnable)
{
	bEnableNaniteSkeletalAnnotationPath = bEnable;
}

bool UAnnotationComponent::IsNaniteSkeletalAnnotationPathEnabled() const
{
	return bEnableNaniteSkeletalAnnotationPath;
}

UAnnotationComponent::EFoliageComponentType UAnnotationComponent::ClassifyFoliageType(const USceneComponent* Component)
{
	const UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component);
	if (!IsValid(MeshComponent))
	{
		return EFoliageComponentType::None;
	}

	const FString ClassName = MeshComponent->GetClass()->GetName().ToLower();
	const FString ComponentName = MeshComponent->GetName().ToLower();
	const AActor* Owner = MeshComponent->GetOwner();
	const FString OwnerName = Owner ? Owner->GetName().ToLower() : TEXT("");
	const FString OwnerClassName = Owner ? Owner->GetClass()->GetName().ToLower() : TEXT("");

	const bool bFoliageLike =
		ClassName.Contains(TEXT("foliage")) ||
		ClassName.Contains(TEXT("vegetation")) ||
		ComponentName.Contains(TEXT("foliage")) ||
		ComponentName.Contains(TEXT("vegetation")) ||
		OwnerName.Contains(TEXT("foliage")) ||
		OwnerName.Contains(TEXT("vegetation")) ||
		OwnerClassName.Contains(TEXT("foliage")) ||
		OwnerClassName.Contains(TEXT("vegetation"));

	if (!bFoliageLike)
	{
		return EFoliageComponentType::None;
	}

	if (Cast<UStaticMeshComponent>(MeshComponent))
	{
		return EFoliageComponentType::StaticFoliage;
	}

	if (Cast<USkeletalMeshComponent>(MeshComponent))
	{
		return EFoliageComponentType::SkeletalFoliage;
	}

	const bool bInstancedSkeletalLike =
		ClassName.Contains(TEXT("instancedskeletal")) ||
		ClassName.Contains(TEXT("instancedskinned")) ||
		ClassName.Contains(TEXT("instanced")) && (ClassName.Contains(TEXT("skeletal")) || ClassName.Contains(TEXT("skinned"))) ||
		ComponentName.Contains(TEXT("instancedskeletal")) ||
		ComponentName.Contains(TEXT("instancedskinned"));

	if (bInstancedSkeletalLike)
	{
		return EFoliageComponentType::InstancedSkeletalFoliage;
	}

	return EFoliageComponentType::None;
}

bool UAnnotationComponent::IsNaniteSkeletalMesh(const USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (!IsValid(SkeletalMeshComponent))
	{
		return false;
	}

	const USkeletalMesh* SkeletalMeshAsset = SkeletalMeshComponent->GetSkeletalMeshAsset();
	return SkeletalMeshAsset && SkeletalMeshAsset->IsNaniteEnabled();
}

FPrimitiveSceneProxy* UAnnotationComponent::CreateSceneProxy(UStaticMeshComponent* StaticMeshComponent)
{
	UMaterialInterface* ProxyMaterial = AnnotationMID; // Material Instance Dynamic
	UStaticMesh* ParentStaticMesh = StaticMeshComponent->GetStaticMesh();
	if (ParentStaticMesh == NULL
		|| ParentStaticMesh->GetRenderData() == NULL
		|| ParentStaticMesh->GetRenderData()->LODResources.Num() == 0)
	{
		return NULL;
	}


	FPrimitiveSceneProxy* Proxy = ::new FStaticAnnotationSceneProxy(StaticMeshComponent, false, ProxyMaterial);
	return Proxy;
}

FPrimitiveSceneProxy* UAnnotationComponent::CreateSceneProxy(USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (IsNaniteSkeletalMesh(SkeletalMeshComponent))
	{

		if (!bEnableNaniteSkeletalAnnotationPath)
		{
			static TSet<FString> LoggedNaniteSkeletalMeshes;
			if (!LoggedNaniteSkeletalMeshes.Contains(SkeletalMeshComponent->GetName()))
			{
				LoggedNaniteSkeletalMeshes.Add(SkeletalMeshComponent->GetName());
				UE_LOG(LogTemp, Log, TEXT("AirSim Annotation: Skipping Nanite skeletal mesh %s - Nanite skeletal annotation path is disabled."), *SkeletalMeshComponent->GetName());
			}
			return nullptr;
		}

		return CreateSceneProxyNaniteSkeletal(SkeletalMeshComponent);
	}

	UMaterialInterface* ProxyMaterial = AnnotationMID;
	FSkeletalMeshRenderData* SkelMeshRenderData = SkeletalMeshComponent->GetSkeletalMeshRenderData();

	if (SkelMeshRenderData
		&& SkelMeshRenderData->LODRenderData.IsValidIndex(SkeletalMeshComponent->GetPredictedLODLevel())
		&& SkeletalMeshComponent->MeshObject)
	{
		return new FSkeletalAnnotationSceneProxy(SkeletalMeshComponent, SkelMeshRenderData, ProxyMaterial);
	}

	return nullptr;
}

FPrimitiveSceneProxy* UAnnotationComponent::CreateSceneProxyNaniteSkeletal(USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (!IsValid(SkeletalMeshComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateSceneProxyNaniteSkeletal: SkeletalMeshComponent is invalid"));
		return nullptr;
	}


	const USkeletalMesh* SkeletalMeshAsset = SkeletalMeshComponent->GetSkeletalMeshAsset();
	if (!SkeletalMeshAsset || !SkeletalMeshAsset->IsNaniteEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateSceneProxyNaniteSkeletal: SkeletalMeshComponent %s does not have valid Nanite skeletal mesh."), *SkeletalMeshComponent->GetName());
		return nullptr;
	}


	FSkeletalMeshRenderData* SkelMeshRenderData = SkeletalMeshComponent->GetSkeletalMeshRenderData();
	if (!SkelMeshRenderData || !SkelMeshRenderData->LODRenderData.IsValidIndex(SkeletalMeshComponent->GetPredictedLODLevel()))
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateSceneProxyNaniteSkeletal: Nanite skeletal mesh %s has invalid render data. RenderData=%p, LOD=%d, MaxLOD=%d"), 
			*SkeletalMeshComponent->GetName(), 
			SkelMeshRenderData, 
			SkeletalMeshComponent->GetPredictedLODLevel(),
			SkelMeshRenderData ? SkelMeshRenderData->LODRenderData.Num() : -1);
		return nullptr;
	}

	UMaterialInterface* ProxyMaterial = AnnotationMID;
	if (!IsValid(ProxyMaterial))
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateSceneProxyNaniteSkeletal: Annotation material instance is invalid for Nanite skeletal mesh %s"),
			*SkeletalMeshComponent->GetName());
		return nullptr;
	}

	FPrimitiveSceneProxy* Proxy = ::new FNaniteSkeletalAnnotationSceneProxy(
		SkeletalMeshComponent,
		SkelMeshRenderData,
		ProxyMaterial
	);

	static TSet<FString> LoggedNaniteEnabled;
	if (!LoggedNaniteEnabled.Contains(SkeletalMeshComponent->GetName()))
	{
		LoggedNaniteEnabled.Add(SkeletalMeshComponent->GetName());
		UE_LOG(LogTemp, Log, TEXT("Airsim Annotation: Nanite skeletal annotation proxy enabled for %s"), *SkeletalMeshComponent->GetName());
	}

	return Proxy;
}


FPrimitiveSceneProxy* UAnnotationComponent::CreateSceneProxy()
{
	USceneComponent* ParentComponent = this->GetAttachParent();
	if (!IsValid(ParentComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("AirSim Annotation: Parent component is invalid."));
		return nullptr;
	}

	last_foliage_type_ = ClassifyFoliageType(ParentComponent);

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(ParentComponent);
	USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ParentComponent);

	if (IsValid(StaticMeshComponent))
	{
		bSkeletalMesh = false;
		return CreateSceneProxy(StaticMeshComponent);
	}
	else if (IsValid(SkeletalMeshComponent))
	{
		FPrimitiveSceneProxy* Proxy = CreateSceneProxy(SkeletalMeshComponent);
		bSkeletalMesh = (Proxy != nullptr);
		return Proxy;
	}
	else
	{
		return nullptr;
	}
}

FBoxSphereBounds UAnnotationComponent::CalcBounds(const FTransform & LocalToWorld) const
{
	// UMeshComponent* ParentMeshComponent = ParentMeshInfo->GetParentMeshComponent();
	// if (IsValid(ParentMeshComponent))
	// {
	// 	return ParentMeshComponent->CalcBounds(LocalToWorld);
	// }
	// else
	// {
	// 	FBoxSphereBounds DefaultBounds;
	// 	return DefaultBounds;
	// }

	USceneComponent* Parent = this->GetAttachParent();
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Parent);
	if (IsValid(StaticMeshComponent))
	{
		return StaticMeshComponent->CalcBounds(LocalToWorld);
	}

	USkeletalMeshComponent* SkinnedMeshComponent = Cast<USkeletalMeshComponent>(Parent);
	if (IsValid(SkinnedMeshComponent))
	{
		return SkinnedMeshComponent->CalcBounds(LocalToWorld);
	}

	FBoxSphereBounds DefaultBounds;
	DefaultBounds.Origin = LocalToWorld.GetLocation();
	DefaultBounds.BoxExtent = FVector::ZeroVector;
	DefaultBounds.SphereRadius = 0.f;
	return DefaultBounds;
}

// Extra overhead for the game scene
void UAnnotationComponent::TickComponent(
	float DeltaTime,
	enum ELevelTick TickType,
	FActorComponentTickFunction * ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction); 

	if (bSkeletalMesh)
	{
		USceneComponent* ParentComponent = this->GetAttachParent();
		USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ParentComponent);
		if (SkeletalMeshComponent && !IsNaniteSkeletalMesh(SkeletalMeshComponent))
		{
			MarkRenderStateDirty();
		}
	}
}


void UAnnotationComponent::ForceUpdate()
{
	this->MarkRenderStateDirty();
}

