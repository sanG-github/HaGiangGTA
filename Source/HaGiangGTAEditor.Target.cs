using UnrealBuildTool;

public class HaGiangGTAEditorTarget : TargetRules
{
	public HaGiangGTAEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HaGiangGTA");
		ExtraModuleNames.Add("HaGiangGTAEditor");
	}
}
