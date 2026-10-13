using UnrealBuildTool;
using System.IO;

public class PRTGI : ModuleRules
{
    public PRTGI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "DeveloperSettings",
            "InputCore",
            "Projects",
            "RenderCore",
            "Renderer",
            "RHI",
            "Slate",
            "SlateCore"
        });

        // UE 5.8 的 GI Plugin delegate 仍声明在 Renderer/Private。
        // 此路径让插件保持 project-local，但会明确绑定当前引擎版本。
        PrivateIncludePaths.AddRange(new[]
        {
            Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"),
            // SceneRendering.h 在 UE 5.8 中直接包含 Renderer/Internal 下的头。
            Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal")
        });

        // Surfel/path caches are raw binary files, not .uassets. Stage them with
        // the plugin so packaged builds can resolve the same plugin-local paths.
        string PrecomputedDir = Path.Combine(PluginDirectory, "Content", "Precomputed");
        if (Directory.Exists(PrecomputedDir))
            foreach (string CacheFile in Directory.EnumerateFiles(PrecomputedDir, "*", SearchOption.AllDirectories))
                RuntimeDependencies.Add(CacheFile, StagedFileType.NonUFS);
    }
}
