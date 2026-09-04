#pragma once

#include "CoreMinimal.h"

// UE4.27 uses single-precision math types. Alias the UE5 float names so
// the binary reader can keep the same layout and call sites.
using FVector2f = FVector2D;
using FVector3f = FVector;
using FVector4f = FVector4;
using FQuat4f = FQuat;
