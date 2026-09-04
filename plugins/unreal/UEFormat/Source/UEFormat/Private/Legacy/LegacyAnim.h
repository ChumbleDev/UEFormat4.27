#pragma once

#include "Archive/UEFormatReader.h"
#include "Data/UEAnimData.h"

namespace UEFormat
{
namespace Legacy
{
	void ReadAnim(FUEFormatReader& Ar, FAnimData& OutAnim);
}
}
