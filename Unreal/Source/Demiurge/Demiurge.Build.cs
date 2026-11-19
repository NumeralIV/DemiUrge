// DEMIURGE runtime module.

using UnrealBuildTool;

public class Demiurge : ModuleRules
{
	public Demiurge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		CppStandard = CppStandardVersion.Cpp20;

		// The simulation must compute exactly what the standalone tests (SimTests/run.ps1, cl /fp:precise)
		// compute. Unreal's default is /fp:fast, under which MSVC folds NaN tests such as `x != x` and
		// `!(x > 0)` away and reassociates arithmetic, so NaN guards stop working and the game would
		// diverge from simrun. Overriding the FP mode needs a private PCH (otherwise UBT drops PCH use
		// for the whole module); DemiurgePCH.h only includes CoreMinimal.h.
		FPSemantics = FPSemanticsMode.Precise;
		PrivatePCHHeaderFile = "DemiurgePCH.h";

		// Sim/ is pure C++20 with no Unreal headers; expose it so Game/ code can include its headers by name.
		PublicIncludePaths.Add(ModuleDirectory + "/Sim");
		// The module root, for DemiurgePCH.h and for the unambiguous spelling #include "Sim/Civ.h".
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ProceduralMeshComponent",
			"GeometryScriptingCore",
			"GeometryFramework",
			"UMG",
			"Slate",
			"SlateCore",
		});

		// (integration) the engine's unit timers (GGameThreadTime, GRenderThreadTime, RHIGetGPUFrameCycles) for the
		// harness's DmStats and the perf scenarios, also in the monolithic packaged game (the DLL lookup the bridge used
		// as a stopgap only works in the editor build)
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });
	}
}
