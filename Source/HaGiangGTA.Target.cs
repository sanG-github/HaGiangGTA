using UnrealBuildTool;

public class HaGiangGTATarget : TargetRules
{
	public HaGiangGTATarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HaGiangGTA");
	}
}
