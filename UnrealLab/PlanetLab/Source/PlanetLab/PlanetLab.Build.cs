using UnrealBuildTool;
public class PlanetLab : ModuleRules {
  public PlanetLab(ReadOnlyTargetRules Target) : base(Target) {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    bUseUnity = false;
    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ProceduralMeshComponent" });
  }
}
