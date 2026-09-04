#pragma once
#include "CoreMinimal.h"
#include "Animation/Skeleton.h"
#include "UEFAnimImportOptions.generated.h"

UCLASS(config = Engine, defaultconfig, transient)
class UEFORMAT_API UEFAnimImportOptions : public UObject
{
	GENERATED_BODY()
public:
	UEFAnimImportOptions();

	UPROPERTY(EditAnywhere, Category = "Import Settings")
	USkeleton* Skeleton;

	bool bInitialized;
};
