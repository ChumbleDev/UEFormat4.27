#pragma once

#include "Archive/UEFormatReader.h"
#include "Data/UEModelData.h"

namespace UEFormat
{
namespace Deserialize
{
	void ReadSkeleton(FUEFormatReader& Ar, FSkeletonData& OutSkeleton);
}
}
