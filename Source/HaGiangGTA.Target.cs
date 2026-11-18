using UnrealBuildTool;

public class HaGiangGTATarget : TargetRules
{
	public HaGiangGTATarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.Add("HaGiangGTA");
	}
}
