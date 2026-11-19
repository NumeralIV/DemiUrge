using UnrealBuildTool;
public class PlanetLabTarget : TargetRules {
  public PlanetLabTarget(TargetInfo Target) : base(Target) {
    Type = TargetType.Game;
    DefaultBuildSettings = BuildSettingsVersion.Latest;
    IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
    ExtraModuleNames.Add("PlanetLab");
  }
}
