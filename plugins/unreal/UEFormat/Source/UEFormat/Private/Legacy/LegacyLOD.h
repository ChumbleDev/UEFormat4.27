#pragma once

#include "Archive/UEFormatReader.h"
#include "Data/UEModelData.h"

namespace UEFormat
{
namespace Legacy
{
	void ReadLOD(FUEFormatReader& Ar, FLODData& OutLOD);
}
}
