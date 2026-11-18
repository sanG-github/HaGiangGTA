using UnrealBuildTool;

public class HaGiangGTAEditorTarget : TargetRules
{
	public HaGiangGTAEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.Add("HaGiangGTA");
		ExtraModuleNames.Add("HaGiangGTAEditor");
	}
}
