#include "Factories/UEFModelFactory.h"
#include "StaticMeshAttributes.h"
#include "Animation/Skeleton.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistryModule.h"
#include "Engine/SkeletalMeshSocket.h"
#include "ReferenceSkeleton.h"
#include "IMeshBuilderModule.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "LODUtilities.h"
#include "MeshBuild.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"

UEFModelFactory::UEFModelFactory(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Formats.Add(TEXT("uemodel; UEMODEL Mesh File"));
	SupportedClass = UObject::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
}

UObject* UEFModelFactory::FactoryCreateFile(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UEFModelReader Data = UEFModelReader(Filename);
	if (!Data.Read() || Data.LODs.Num() == 0)
	{
		return nullptr;
	}

	if (Data.Skeleton.Bones.Num() > 0)
	{
		USkeletalMesh* SkeletalMesh = CreateSkeletalMesh(Data.LODs, Data.Skeleton, Parent, Name, Flags);
		if (!SkeletalMesh)
		{
			return nullptr;
		}

		SkeletalMesh->PostEditChange();
		FAssetRegistryModule::AssetCreated(SkeletalMesh);
		return SkeletalMesh;
	}

	UStaticMesh* StaticMesh = CreateStaticMesh(Data.LODs, Parent, Name, Flags);
	if (!StaticMesh)
	{
		return nullptr;
	}

	StaticMesh->PostEditChange();
	FAssetRegistryModule::AssetCreated(StaticMesh);
	return StaticMesh;
}

void UEFModelFactory::PopulateMeshDescription(FMeshDescription& MeshDesc, FLODData& Data)
{
	MeshDesc.ReserveNewVertices(Data.Vertices.Num());
	MeshDesc.ReserveNewVertexInstances(Data.Vertices.Num());
	MeshDesc.ReserveNewPolygons(Data.Indices.Num() / 3);
	MeshDesc.ReserveNewPolygonGroups(Data.Materials.Num());

	for (int32 i = 0; i < Data.Vertices.Num(); ++i)
	{
		MeshDesc.CreateVertex();
	}

	for (const int32 Index : Data.Indices)
	{
		MeshDesc.CreateVertexInstance(FVertexID(Index));
	}
}

void UEFModelFactory::SetMeshAttributes(FMeshDescription& MeshDesc, FLODData& Data)
{
	FStaticMeshAttributes Attributes(MeshDesc);
	Attributes.Register();

	const auto VertexPositions = Attributes.GetVertexPositions();
	const auto VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	const auto VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	const auto VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	const auto VertexInstanceColors = Attributes.GetVertexInstanceColors();
	const auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	VertexInstanceUVs.SetNumIndices(FMath::Max(1, Data.TextureCoordinates.Num()));

	for (int32 i = 0; i < Data.Vertices.Num(); ++i)
	{
		VertexPositions.Set(FVertexID(i), Data.Vertices[i]);
	}

	for (int32 i = 0; i < Data.Indices.Num(); ++i)
	{
		const int32 Index = Data.Indices[i];
		const FVertexInstanceID InstanceID(i);

		if (Data.Normals.IsValidIndex(Index))
		{
			VertexInstanceBinormalSigns.Set(InstanceID, Data.Normals[Index].X);
			VertexInstanceNormals.Set(InstanceID, FVector(Data.Normals[Index].Y, Data.Normals[Index].Z, Data.Normals[Index].W));
		}

		if (Data.Tangents.IsValidIndex(Index))
		{
			VertexInstanceTangents.Set(InstanceID, Data.Tangents[Index]);
		}

		if (Data.VertexColors.Num() > 0 && Data.VertexColors[0].Data.IsValidIndex(Index))
		{
			VertexInstanceColors.Set(InstanceID, FLinearColor(Data.VertexColors[0].Data[Index]));
		}

		for (int32 UVChannel = 0; UVChannel < Data.TextureCoordinates.Num(); ++UVChannel)
		{
			if (Data.TextureCoordinates[UVChannel].IsValidIndex(Index))
			{
				VertexInstanceUVs.Set(InstanceID, UVChannel, Data.TextureCoordinates[UVChannel][Index]);
			}
		}
	}
}

void UEFModelFactory::CreatePolygonGroups(FMeshDescription& MeshDesc, FLODData& Data)
{
	FStaticMeshAttributes Attributes(MeshDesc);
	Attributes.Register();

	for (const FMaterialChunk& Material : Data.Materials)
	{
		const FPolygonGroupID PolygonGroup = MeshDesc.CreatePolygonGroup();
		for (int32 i = Material.FirstIndex; i < Material.FirstIndex + (Material.NumFaces * 3); i += 3)
		{
			TArray<FVertexInstanceID> VertexInstanceIDs;
			VertexInstanceIDs.Reserve(3);
			VertexInstanceIDs.Add(FVertexInstanceID(i));
			VertexInstanceIDs.Add(FVertexInstanceID(i + 1));
			VertexInstanceIDs.Add(FVertexInstanceID(i + 2));
			MeshDesc.CreatePolygon(PolygonGroup, VertexInstanceIDs);
		}

		Attributes.GetPolygonGroupMaterialSlotNames()[PolygonGroup] = FName(UTF8_TO_TCHAR(Material.Name.c_str()));
	}

	if (Data.Materials.Num() == 0 && Data.Indices.Num() >= 3)
	{
		const FPolygonGroupID PolygonGroup = MeshDesc.CreatePolygonGroup();
		for (int32 i = 0; i + 2 < Data.Indices.Num(); i += 3)
		{
			TArray<FVertexInstanceID> VertexInstanceIDs;
			VertexInstanceIDs.Reserve(3);
			VertexInstanceIDs.Add(FVertexInstanceID(i));
			VertexInstanceIDs.Add(FVertexInstanceID(i + 1));
			VertexInstanceIDs.Add(FVertexInstanceID(i + 2));
			MeshDesc.CreatePolygon(PolygonGroup, VertexInstanceIDs);
		}

		Attributes.GetPolygonGroupMaterialSlotNames()[PolygonGroup] = FName(TEXT("DefaultMaterial"));
	}
}

TArray<FStaticMaterial> UEFModelFactory::CreateStaticMaterials(TArray<FMaterialChunk> MaterialInfos)
{
	TArray<FStaticMaterial> Materials;
	for (const FMaterialChunk& MaterialInfo : MaterialInfos)
	{
		FStaticMaterial Material;
		Material.MaterialSlotName = UTF8_TO_TCHAR(MaterialInfo.Name.c_str());
		Material.ImportedMaterialSlotName = UTF8_TO_TCHAR(MaterialInfo.Name.c_str());
		Material.MaterialInterface = nullptr;
		Materials.Add(Material);
	}

	if (Materials.Num() == 0)
	{
		FStaticMaterial Material;
		Material.MaterialSlotName = TEXT("DefaultMaterial");
		Material.ImportedMaterialSlotName = TEXT("DefaultMaterial");
		Materials.Add(Material);
	}

	return Materials;
}

TArray<FSkeletalMaterial> UEFModelFactory::CreateSkeletalMaterials(TArray<FMaterialChunk> MaterialInfos)
{
	TArray<FSkeletalMaterial> Materials;
	for (const FMaterialChunk& MaterialInfo : MaterialInfos)
	{
		FSkeletalMaterial Material;
		Material.MaterialSlotName = UTF8_TO_TCHAR(MaterialInfo.Name.c_str());
		Material.ImportedMaterialSlotName = UTF8_TO_TCHAR(MaterialInfo.Name.c_str());
		Material.MaterialInterface = nullptr;
		Materials.Add(Material);
	}

	if (Materials.Num() == 0)
	{
		FSkeletalMaterial Material;
		Material.MaterialSlotName = TEXT("DefaultMaterial");
		Material.ImportedMaterialSlotName = TEXT("DefaultMaterial");
		Materials.Add(Material);
	}

	return Materials;
}

void UEFModelFactory::ProcessLOD(FMeshDescription& MeshDesc, FLODData& LODData)
{
	PopulateMeshDescription(MeshDesc, LODData);
	SetMeshAttributes(MeshDesc, LODData);
	CreatePolygonGroups(MeshDesc, LODData);
}

UStaticMesh* UEFModelFactory::CreateStaticMesh(TArray<FLODData>& LODData, UObject* Parent, FName Name, EObjectFlags Flags)
{
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Parent, Name, Flags);

	TArray<FMeshDescription> MeshDescriptions;
	MeshDescriptions.Reserve(LODData.Num());
	TArray<const FMeshDescription*> MeshDescriptionPtrs;
	MeshDescriptionPtrs.Reserve(LODData.Num());

	for (int32 i = 0; i < LODData.Num(); ++i)
	{
		MeshDescriptions.Emplace();
		MeshDescriptionPtrs.Add(&MeshDescriptions[i]);
		ProcessLOD(MeshDescriptions[i], LODData[i]);
	}

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	StaticMesh->BuildFromMeshDescriptions(MeshDescriptionPtrs, BuildParams);
	StaticMesh->GetStaticMaterials() = CreateStaticMaterials(LODData[0].Materials);

	for (int32 i = 0; i < StaticMesh->GetNumSourceModels(); ++i)
	{
		FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(i);
		SourceModel.BuildSettings.bRecomputeNormals = false;
		SourceModel.BuildSettings.bRecomputeTangents = false;
		SourceModel.BuildSettings.bRemoveDegenerates = false;
		SourceModel.BuildSettings.bGenerateLightmapUVs = false;
	}

	StaticMesh->Modify();
	return StaticMesh;
}

namespace
{
FSkeletalMeshImportData CreateSkeletalImportData(const FLODData& Data, const FSkeletonData& SkeletonData)
{
	FSkeletalMeshImportData ImportData;

	ImportData.Points = Data.Vertices;
	ImportData.PointToRawMap.SetNum(Data.Vertices.Num());
	for (int32 PointIndex = 0; PointIndex < Data.Vertices.Num(); ++PointIndex)
	{
		ImportData.PointToRawMap[PointIndex] = PointIndex;
	}

	for (const FMaterialChunk& Material : Data.Materials)
	{
		SkeletalMeshImportData::FMaterial ImportMaterial;
		ImportMaterial.MaterialImportName = UTF8_TO_TCHAR(Material.Name.c_str());
		ImportData.Materials.Add(ImportMaterial);
	}

	if (ImportData.Materials.Num() == 0)
	{
		SkeletalMeshImportData::FMaterial ImportMaterial;
		ImportMaterial.MaterialImportName = TEXT("DefaultMaterial");
		ImportData.Materials.Add(ImportMaterial);
	}

	const int32 NumFaces = Data.Indices.Num() / 3;
	TArray<int32> FaceMaterials;
	FaceMaterials.Init(0, NumFaces);
	for (int32 MaterialIndex = 0; MaterialIndex < Data.Materials.Num(); ++MaterialIndex)
	{
		const FMaterialChunk& Material = Data.Materials[MaterialIndex];
		const int32 FirstFace = Material.FirstIndex / 3;
		for (int32 FaceOffset = 0; FaceOffset < Material.NumFaces; ++FaceOffset)
		{
			const int32 FaceIndex = FirstFace + FaceOffset;
			if (FaceMaterials.IsValidIndex(FaceIndex))
			{
				FaceMaterials[FaceIndex] = MaterialIndex;
			}
		}
	}

	ImportData.Wedges.SetNum(Data.Indices.Num());
	ImportData.Faces.SetNum(NumFaces);
	ImportData.NumTexCoords = FMath::Max<uint32>(1, static_cast<uint32>(Data.TextureCoordinates.Num()));
	ImportData.MaxMaterialIndex = FMath::Max(0, ImportData.Materials.Num() - 1);
	ImportData.bHasVertexColors = Data.VertexColors.Num() > 0;
	ImportData.bHasNormals = Data.Normals.Num() > 0;
	ImportData.bHasTangents = Data.Tangents.Num() > 0;

	for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
	{
		SkeletalMeshImportData::FTriangle& Face = ImportData.Faces[FaceIndex];
		Face.MatIndex = FaceMaterials.IsValidIndex(FaceIndex) ? static_cast<uint16>(FaceMaterials[FaceIndex]) : 0;
		Face.SmoothingGroups = 1;

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 WedgeIndex = FaceIndex * 3 + Corner;
			const int32 VertexIndex = Data.Indices[WedgeIndex];

			Face.WedgeIndex[Corner] = WedgeIndex;

			SkeletalMeshImportData::FVertex& Wedge = ImportData.Wedges[WedgeIndex];
			Wedge.VertexIndex = VertexIndex;
			Wedge.MatIndex = static_cast<uint8>(Face.MatIndex);

			if (ImportData.bHasVertexColors && Data.VertexColors[0].Data.IsValidIndex(VertexIndex))
			{
				Wedge.Color = Data.VertexColors[0].Data[VertexIndex];
			}

			const int32 UVCount = FMath::Min(Data.TextureCoordinates.Num(), static_cast<int32>(MAX_TEXCOORDS));
			for (int32 UVChannel = 0; UVChannel < UVCount; ++UVChannel)
			{
				if (Data.TextureCoordinates[UVChannel].IsValidIndex(VertexIndex))
				{
					Wedge.UVs[UVChannel] = Data.TextureCoordinates[UVChannel][VertexIndex];
				}
			}

			if (Data.Tangents.IsValidIndex(VertexIndex))
			{
				Face.TangentX[Corner] = Data.Tangents[VertexIndex];
			}

			if (Data.Normals.IsValidIndex(VertexIndex))
			{
				Face.TangentZ[Corner] = FVector(Data.Normals[VertexIndex].Y, Data.Normals[VertexIndex].Z, Data.Normals[VertexIndex].W);
				const float BinormalSign = Data.Normals[VertexIndex].X;
				Face.TangentY[Corner] = (Face.TangentZ[Corner] ^ Face.TangentX[Corner]) * BinormalSign;
			}
		}
	}

	TSet<int32> WeightedVertices;
	for (const FWeightChunk& Weight : Data.Weights)
	{
		SkeletalMeshImportData::FRawBoneInfluence Influence;
		Influence.BoneIndex = Weight.WeightBoneIndex;
		Influence.VertexIndex = Weight.WeightVertexIndex;
		Influence.Weight = Weight.WeightAmount;
		ImportData.Influences.Add(Influence);
		WeightedVertices.Add(Weight.WeightVertexIndex);
	}

	for (int32 VertexIndex = 0; VertexIndex < ImportData.Points.Num(); ++VertexIndex)
	{
		if (!WeightedVertices.Contains(VertexIndex))
		{
			SkeletalMeshImportData::FRawBoneInfluence Influence;
			Influence.BoneIndex = 0;
			Influence.VertexIndex = VertexIndex;
			Influence.Weight = 1.0f;
			ImportData.Influences.Add(Influence);
		}
	}

	TArray<int32> ChildCounts;
	ChildCounts.Init(0, SkeletonData.Bones.Num());
	for (int32 BoneIndex = 0; BoneIndex < SkeletonData.Bones.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = SkeletonData.Bones[BoneIndex].BoneParentIndex;
		if (ChildCounts.IsValidIndex(ParentIndex))
		{
			++ChildCounts[ParentIndex];
		}
	}

	for (int32 BoneIndex = 0; BoneIndex < SkeletonData.Bones.Num(); ++BoneIndex)
	{
		const FBoneChunk& Bone = SkeletonData.Bones[BoneIndex];
		SkeletalMeshImportData::FBone ImportBone;
		ImportBone.Name = UTF8_TO_TCHAR(Bone.BoneName.c_str());
		ImportBone.Flags = 0x02;
		ImportBone.NumChildren = ChildCounts[BoneIndex];
		ImportBone.ParentIndex = Bone.BoneParentIndex;
		ImportBone.BonePos.Transform.SetLocation(Bone.BonePos);
		ImportBone.BonePos.Transform.SetRotation(Bone.BoneRot);
		ImportBone.BonePos.Transform.SetScale3D(Bone.BoneScale);
		ImportBone.BonePos.Length = 1.0f;
		ImportBone.BonePos.XSize = 100.0f;
		ImportBone.BonePos.YSize = 100.0f;
		ImportBone.BonePos.ZSize = 100.0f;
		ImportData.RefBonesBinary.Add(ImportBone);
	}

	for (const FMorphTargetChunk& Morph : Data.Morphs)
	{
		FSkeletalMeshImportData MorphImport;
		ImportData.CopyDataNeedByMorphTargetImport(MorphImport);

		TSet<uint32> ModifiedPoints;
		for (const FMorphTargetDataChunk& Delta : Morph.MorphDeltas)
		{
			const int32 VertexIndex = Delta.MorphVertexIndex;
			if (!MorphImport.Points.IsValidIndex(VertexIndex))
			{
				continue;
			}

			MorphImport.Points[VertexIndex] += FVector(Delta.MorphPosition.X, -Delta.MorphPosition.Y, Delta.MorphPosition.Z);
			ModifiedPoints.Add(static_cast<uint32>(VertexIndex));
		}

		MorphImport.PointToRawMap.Empty();
		ImportData.MorphTargetNames.Add(UTF8_TO_TCHAR(Morph.MorphName.c_str()));
		ImportData.MorphTargetModifiedPoints.Add(MoveTemp(ModifiedPoints));
		ImportData.MorphTargets.Add(MoveTemp(MorphImport));
	}

	return ImportData;
}
}

USkeletalMesh* UEFModelFactory::CreateSkeletalMesh(TArray<FLODData>& LODData, FSkeletonData& SkeletonData, UObject* Parent, FName Name, EObjectFlags Flags)
{
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Parent, Name, Flags);

	FReferenceSkeleton RefSkeleton;
	USkeleton* Skeleton = CreateSkeleton(Name.ToString(), Parent, Flags, SkeletonData, RefSkeleton);
	if (!Skeleton)
	{
		return nullptr;
	}

	SkeletalMesh->PreEditChange(nullptr);
	SkeletalMesh->InvalidateDeriveDataCacheGUID();
	SkeletalMesh->SetRefSkeleton(RefSkeleton);
	SkeletalMesh->SetSkeleton(Skeleton);
	SkeletalMesh->GetMaterials() = CreateSkeletalMaterials(LODData[0].Materials);

	FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
	ImportedModel->LODModels.Empty();
	SkeletalMesh->ResetLODInfo();

	FBox BoundingBox(ForceInit);
	for (const FVector& Vertex : LODData[0].Vertices)
	{
		BoundingBox += Vertex;
	}
	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));
	SkeletalMesh->SetHasVertexColors(LODData[0].VertexColors.Num() > 0);

	IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
	const ITargetPlatform* TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();

	for (int32 LODIndex = 0; LODIndex < LODData.Num(); ++LODIndex)
	{
		FSkeletalMeshImportData ImportData = CreateSkeletalImportData(LODData[LODIndex], SkeletonData);
		SkeletalMeshImportUtils::ProcessImportMeshInfluences(ImportData, SkeletalMesh->GetPathName());

		ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
		FSkeletalMeshLODInfo& LODInfo = SkeletalMesh->AddLODInfo();
		LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
		LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
		LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
		LODInfo.LODHysteresis = 0.02f;
		LODInfo.BuildSettings.bRecomputeNormals = !ImportData.bHasNormals;
		LODInfo.BuildSettings.bRecomputeTangents = !ImportData.bHasTangents;
		LODInfo.BuildSettings.bUseMikkTSpace = false;
		LODInfo.BuildSettings.bRemoveDegenerates = false;

		SkeletalMesh->SaveLODImportedData(LODIndex, ImportData);
		SkeletalMesh->SetLODImportedDataVersions(LODIndex, ESkeletalMeshGeoImportVersions::LatestVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
		ImportedModel->LODModels[LODIndex].NumTexCoords = FMath::Max<uint32>(1, ImportData.NumTexCoords);

		const FSkeletalMeshBuildParameters BuildParameters(SkeletalMesh, TargetPlatform, LODIndex, false);
		if (!MeshBuilderModule.BuildSkeletalMesh(BuildParameters))
		{
			UE_LOG(LogTemp, Error, TEXT("UEFormat: failed to build skeletal mesh LOD %d"), LODIndex);
			return nullptr;
		}

		if (ImportData.MorphTargets.Num() > 0)
		{
			FOverlappingThresholds Thresholds;
			FLODUtilities::BuildMorphTargets(SkeletalMesh, ImportData, LODIndex, ImportData.bHasNormals, ImportData.bHasTangents, false, Thresholds);
		}
	}

	SkeletalMesh->CalculateInvRefMatrices();
	Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	for (const FVirtualBoneChunk& VirtualBone : SkeletonData.VirtualBones)
	{
		const FName SourceName(UTF8_TO_TCHAR(VirtualBone.SourceBoneName.c_str()));
		const FName TargetName(UTF8_TO_TCHAR(VirtualBone.TargetBoneName.c_str()));
		const FName DesiredName(UTF8_TO_TCHAR(VirtualBone.VirtualBoneName.c_str()));

		FName CreatedName;
		if (Skeleton->AddNewVirtualBone(SourceName, TargetName, CreatedName) && CreatedName != DesiredName)
		{
			Skeleton->RenameVirtualBone(CreatedName, DesiredName);
		}
	}

	Skeleton->SetPreviewMesh(SkeletalMesh);
	Skeleton->PostEditChange();
	FAssetRegistryModule::AssetCreated(Skeleton);

	return SkeletalMesh;
}

USkeleton* UEFModelFactory::CreateSkeleton(FString Name, UObject* Parent, EObjectFlags Flags, FSkeletonData& Data, FReferenceSkeleton& RefSkeleton)
{
	const FString SkeletonName = Name + TEXT("_Skeleton");
	UPackage* SkeletonPackage = CreatePackage(*FPaths::Combine(FPaths::GetPath(Parent->GetPathName()), SkeletonName));
	USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, FName(*SkeletonName), Flags);

	FReferenceSkeletonModifier RefSkeletonModifier(RefSkeleton, Skeleton);
	RefSkeleton.Empty();

	for (const FBoneChunk& Bone : Data.Bones)
	{
		FTransform Transform;
		Transform.SetLocation(Bone.BonePos);
		Transform.SetRotation(Bone.BoneRot);
		Transform.SetScale3D(Bone.BoneScale);

		FMeshBoneInfo BoneInfo(UTF8_TO_TCHAR(Bone.BoneName.c_str()), UTF8_TO_TCHAR(Bone.BoneName.c_str()), Bone.BoneParentIndex);
		RefSkeletonModifier.Add(BoneInfo, Transform);
	}

	for (const FSocketChunk& Socket : Data.Sockets)
	{
		USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skeleton);
		NewSocket->SocketName = UTF8_TO_TCHAR(Socket.SocketName.c_str());
		NewSocket->BoneName = UTF8_TO_TCHAR(Socket.SocketParentName.c_str());
		NewSocket->RelativeLocation = Socket.SocketPos;
		NewSocket->RelativeRotation = Socket.SocketRot.Rotator();
		NewSocket->RelativeScale = Socket.SocketScale;
		Skeleton->Sockets.Add(NewSocket);
	}

	return Skeleton;
}
