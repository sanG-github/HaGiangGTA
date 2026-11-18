using UnrealBuildTool;

public class HaGiangGTAEditor : ModuleRules
{
	public HaGiangGTAEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HaGiangGTA"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Landscape"
		});
	}
}
