using UnrealBuildTool;

public class ChaosClothUniversalSelectionRemapper : ModuleRules
{
    public ChaosClothUniversalSelectionRemapper(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "Chaos",
            "DataflowCore",
            "GeometryCollectionEngine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "ChaosClothAsset",
            "ChaosClothAssetDataflowNodes",
            "DataflowNodes"
        });
    }
}
