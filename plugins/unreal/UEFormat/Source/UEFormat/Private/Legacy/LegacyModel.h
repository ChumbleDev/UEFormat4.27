#pragma once

#include "Archive/UEFormatReader.h"
#include "Data/UEModelData.h"

namespace UEFormat
{
namespace Legacy
{
	void ReadModel(FUEFormatReader& Ar, TArray<FLODData>& OutLODs, FSkeletonData& OutSkeleton);
}
}
