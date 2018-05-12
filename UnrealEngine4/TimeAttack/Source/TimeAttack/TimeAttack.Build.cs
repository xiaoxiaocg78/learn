// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TimeAttack : ModuleRules
{
	public TimeAttack(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "PhysXVehicles", "HeadMountedDisplay", "UMG" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		PublicDefinitions.Add("HMD_MODULE_INCLUDED=1");
	}
}
