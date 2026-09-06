using UnrealBuildTool;

public class PRTGIEditor : ModuleRules
{
    public PRTGIEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "AssetTools",
            "CoreUObject",
            "Engine",
            "InputCore",
            "LevelEditor",
            "PRTGI",
            "PropertyEditor",
            "Slate",
            "SlateCore",
            "UnrealEd"
        });
    }
}
