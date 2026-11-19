// DEMIURGE: private precompiled header of the Demiurge module (see Demiurge.Build.cs: it exists so the
// module can be compiled with /fp:precise). Unreal force-includes it into every .cpp of the module,
// Sim/ included, which is why Sim code must avoid the names of Unreal macros (FOUNDATION-CPP 1.2).
#pragma once

#include "CoreMinimal.h"
