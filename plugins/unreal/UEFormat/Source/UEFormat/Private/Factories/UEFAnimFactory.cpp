#include "Factories/UEFAnimFactory.h"
#include "ComponentReregisterContext.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/Skeleton.h"
#include "Widgets/Anim/UEFAnimImportOptions.h"
#include "Widgets/Anim/UEFAnimWidget.h"
#include "AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Readers/UEFAnimReader.h"
#include "Widgets/SWindow.h"

UEFAnimFactory::UEFAnimFactory(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Formats.Add(TEXT("ueanim; UEANIM Animation File"));
	SupportedClass = UAnimSequence::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	SettingsImporter = CreateDefaultSubobject<UEFAnimImportOptions>(TEXT("Anim Options"));
}

UObject* UEFAnimFactory::FactoryCreateFile(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	FScopedSlowTask SlowTask(5, NSLOCTEXT("UEFAnimFactory", "BeginReadUEAnimFile", "Reading UEAnim file"), true);
	if (Warn->GetScopeStack().Num() == 0)
	{
		SlowTask.MakeDialog(true);
	}

	SlowTask.EnterProgressFrame(0);

	UEFAnimReader Data = UEFAnimReader(Filename);
	if (!Data.Read())
	{
		return nullptr;
	}

	if (SettingsImporter->bInitialized == false)
	{
		TSharedPtr<UEFAnimWidget> ImportOptionsWindow;
		TSharedPtr<SWindow> ParentWindow;
		if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
		{
			IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
			ParentWindow = MainFrame.GetParentWindow();
		}

		TSharedRef<SWindow> Window = SNew(SWindow).Title(FText::FromString(TEXT("Animation Import Options"))).SizingRule(ESizingRule::Autosized);
		Window->SetContent(SAssignNew(ImportOptionsWindow, UEFAnimWidget).WidgetWindow(Window));
		SettingsImporter = ImportOptionsWindow.Get()->Stun;
		FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
		bImport = ImportOptionsWindow.Get()->ShouldImport();
		bImportAll = ImportOptionsWindow.Get()->ShouldImportAll();
		SettingsImporter->bInitialized = true;
	}

	if (!bImport && !bImportAll)
	{
		bOutOperationCanceled = true;
		SettingsImporter->bInitialized = false;
		return nullptr;
	}

	USkeleton* Skeleton = SettingsImporter->Skeleton;
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Error, TEXT("UEFormat: no skeleton selected for animation import"));
		return nullptr;
	}

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(Parent, Name, Flags);
	AnimSequence->SetSkeleton(Skeleton);

	const int32 NumFrames = FMath::Max(Data.NumFrames, 1);
	const float FrameRate = Data.FramesPerSecond > 0.f ? Data.FramesPerSecond : 30.f;
	AnimSequence->SetRawNumberOfFrame(NumFrames);
	AnimSequence->SequenceLength = (NumFrames > 1) ? static_cast<float>(NumFrames - 1) / FrameRate : 1.f / FrameRate;

	FScopedSlowTask ImportTask(Data.Tracks.Num() + Data.Curves.Num(), FText::FromString(TEXT("Importing UEAnim Animation")));
	ImportTask.MakeDialog(false);

	for (const FTrack& Track : Data.Tracks)
	{
		ImportTask.EnterProgressFrame();

		const FName BoneName(UTF8_TO_TCHAR(Track.TrackName.c_str()));
		const TArray<FVectorKey>& PosKeys = Track.TrackPosKeys;
		const TArray<FQuatKey>& RotKeys = Track.TrackRotKeys;
		const TArray<FVectorKey>& ScaleKeys = Track.TrackScaleKeys;

		TArray<FVector> FinalPosKeys;
		TArray<FQuat> FinalRotKeys;
		TArray<FVector> FinalScaleKeys;
		FinalPosKeys.SetNum(NumFrames);
		FinalRotKeys.SetNum(NumFrames);
		FinalScaleKeys.SetNum(NumFrames);

		FVector PrevPos = FVector::ZeroVector;
		FQuat PrevRot = FQuat::Identity;
		FVector PrevScale = FVector::OneVector;

		int32 PosIndex = 0;
		int32 RotIndex = 0;
		int32 ScaleIndex = 0;
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			if (PosIndex < PosKeys.Num() && PosKeys[PosIndex].Frame == FrameIndex)
			{
				FinalPosKeys[FrameIndex] = PosKeys[PosIndex].VectorValue;
				PrevPos = PosKeys[PosIndex].VectorValue;
				++PosIndex;
			}
			else
			{
				FinalPosKeys[FrameIndex] = PrevPos;
			}

			if (RotIndex < RotKeys.Num() && RotKeys[RotIndex].Frame == FrameIndex)
			{
				FinalRotKeys[FrameIndex] = RotKeys[RotIndex].QuatValue;
				PrevRot = RotKeys[RotIndex].QuatValue;
				++RotIndex;
			}
			else
			{
				FinalRotKeys[FrameIndex] = PrevRot;
			}

			if (ScaleIndex < ScaleKeys.Num() && ScaleKeys[ScaleIndex].Frame == FrameIndex)
			{
				FinalScaleKeys[FrameIndex] = ScaleKeys[ScaleIndex].VectorValue;
				PrevScale = ScaleKeys[ScaleIndex].VectorValue;
				++ScaleIndex;
			}
			else
			{
				FinalScaleKeys[FrameIndex] = PrevScale;
			}
		}

		FRawAnimSequenceTrack RawTrack;
		RawTrack.PosKeys = MoveTemp(FinalPosKeys);
		RawTrack.RotKeys = MoveTemp(FinalRotKeys);
		RawTrack.ScaleKeys = MoveTemp(FinalScaleKeys);
		AnimSequence->AddNewRawTrack(BoneName, &RawTrack);
	}

	for (int32 CurveIndex = 0; CurveIndex < Data.Curves.Num(); ++CurveIndex)
	{
		ImportTask.EnterProgressFrame();

		const FName CurveName(UTF8_TO_TCHAR(Data.Curves[CurveIndex].CurveName.c_str()));
		FSmartName CurveSmartName;
		if (!Skeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, CurveName, CurveSmartName))
		{
			Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, CurveName, CurveSmartName);
		}

		AnimSequence->RawCurveData.AddCurveData(CurveSmartName);
		FFloatCurve* FloatCurve = static_cast<FFloatCurve*>(AnimSequence->RawCurveData.GetCurveData(CurveSmartName.UID));
		if (FloatCurve)
		{
			for (const FFloatKey& Key : Data.Curves[CurveIndex].CurveKeys)
			{
				FloatCurve->FloatCurve.AddKey(static_cast<float>(Key.Frame) / FrameRate, Key.FloatValue);
			}
		}
	}

	if (!bImportAll)
	{
		SettingsImporter->bInitialized = false;
	}

	AnimSequence->MarkRawDataAsModified();
	AnimSequence->PostProcessSequence();
	AnimSequence->PostEditChange();

	FAssetRegistryModule::AssetCreated(AnimSequence);
	FGlobalComponentReregisterContext RecreateComponents;

	return AnimSequence;
}
