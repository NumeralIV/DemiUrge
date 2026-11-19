using UnrealBuildTool;
public class PlanetLabEditorTarget : TargetRules {
  public PlanetLabEditorTarget(TargetInfo Target) : base(Target) {
    Type = TargetType.Editor;
    DefaultBuildSettings = BuildSettingsVersion.Latest;
    IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
    ExtraModuleNames.Add("PlanetLab");
  }
}
