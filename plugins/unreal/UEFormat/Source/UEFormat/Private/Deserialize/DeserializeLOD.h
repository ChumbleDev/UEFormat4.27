#pragma once

#include "Archive/UEFormatReader.h"
#include "Data/UEModelData.h"

namespace UEFormat
{
namespace Deserialize
{
	void ReadLOD(FUEFormatReader& Ar, FLODData& OutLOD);
}
}
