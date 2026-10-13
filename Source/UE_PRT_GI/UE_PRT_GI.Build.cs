// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE_PRT_GI : ModuleRules
{
	public UE_PRT_GI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"UE_PRT_GI",
			"UE_PRT_GI/Variant_Horror",
			"UE_PRT_GI/Variant_Horror/UI",
			"UE_PRT_GI/Variant_Shooter",
			"UE_PRT_GI/Variant_Shooter/AI",
			"UE_PRT_GI/Variant_Shooter/UI",
			"UE_PRT_GI/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
