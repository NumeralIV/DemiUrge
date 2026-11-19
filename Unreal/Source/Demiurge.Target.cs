// DEMIURGE game target.

using UnrealBuildTool;
using System.Collections.Generic;

public class DemiurgeTarget : TargetRules
{
	public DemiurgeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Demiurge");
	}
}
