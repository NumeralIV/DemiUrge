using UnrealBuildTool;
public class PlanetLab : ModuleRules {
  public PlanetLab(ReadOnlyTargetRules Target) : base(Target) {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    bUseUnity = false;
    PublicIncludePaths.Add(ModuleDirectory);
    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ProceduralMeshComponent", "RenderCore", "RHI", "MeshDescription", "StaticMeshDescription" });
  }
}
