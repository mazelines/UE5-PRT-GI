#include "PRTGISettings.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

bool FPRTGILocalVolume::IsValid() const
{
    return !DataFile.IsEmpty() && FPaths::IsRelative(DataFile) && !DataFile.Contains(TEXT(".."))
        && !BoundsMin.ContainsNaN() && !BoundsMax.ContainsNaN()
        && !ReceiverMin.ContainsNaN() && !ReceiverMax.ContainsNaN()
        && (BoundsMax - BoundsMin).GetMin() > 0.0
        && (ReceiverMax - ReceiverMin).GetMin() > 0.0
        && GridCounts.X >= 2 && GridCounts.Y >= 2 && GridCounts.Z >= 2
        && GridCounts.X <= 32 && GridCounts.Y <= 32 && GridCounts.Z <= 32;
}

UPRTGISettings::UPRTGISettings()
{
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("PRT GI");
}

const FPRTGIMapProfile* UPRTGISettings::FindMapProfile(const UWorld* World) const
{
    if (!World) return nullptr;
    const FString Package = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
    const auto* Configured=MapProfiles.FindByPredicate([&Package](const FPRTGIMapProfile& Profile)
    {
        return Profile.MapPackage == Package;
    });
    if(Configured)return Configured;
    // The bundled map remains usable when the plugin is copied to another project.
    static const FPRTGIMapProfile Showcase=[]()
    {
        FPRTGIMapProfile P;P.MapPackage=TEXT("/PRTGI/Maps/PRTGI_Showcase");
        P.BoundsMin=FVector(-1020,-1190,200);P.BoundsMax=FVector(1160,1290,1180);
        P.GridCounts=FIntVector(11,13,5);P.DataFile=TEXT("PRTGI_Showcase.prtgi");
        P.bHardwareLumenComparison=true;P.ReceiverBoundsPadding=10.5f;return P;
    }();
    return Package==Showcase.MapPackage?&Showcase:nullptr;
}

FIntVector UPRTGISettings::GetGridCounts(const UWorld* World) const
{
    const FPRTGIMapProfile* Profile = FindMapProfile(World);
    const FIntVector Counts = Profile ? Profile->GridCounts : GridCounts;
    return FIntVector(FMath::Clamp(Counts.X, 2, 32),
        FMath::Clamp(Counts.Y, 2, 32), FMath::Clamp(Counts.Z, 2, 32));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGIMapProfileTest, "PRTGI.Settings.MapProfileIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPRTGIMapProfileTest::RunTest(const FString& Parameters)
{
    UPRTGISettings* Settings = NewObject<UPRTGISettings>();
    Settings->MapProfiles.Reset();
    FPRTGIMapProfile& Profile = Settings->MapProfiles.AddDefaulted_GetRef();
    Profile.MapPackage = TEXT("/Temp/PRTGIProfileTest");
    Profile.GridCounts = FIntVector(5, 6, 7);
    Profile.DataFile = TEXT("PRTGI/Test.prtgi");
    UWorld* Editor = NewObject<UWorld>(CreatePackage(TEXT("/Temp/PRTGIProfileTest")));
    UWorld* PIE = NewObject<UWorld>(CreatePackage(TEXT("/Temp/UEDPIE_0_PRTGIProfileTest")));
    UWorld* Other = NewObject<UWorld>(CreatePackage(TEXT("/Temp/PRTGIOtherMap")));
    TestTrue(TEXT("Editor and PIE choose the same profile"),
        Settings->FindMapProfile(Editor) == Settings->FindMapProfile(PIE));
    TestEqual(TEXT("Profile grid is applied"), Settings->GetGridCounts(PIE), FIntVector(5, 6, 7));
    TestNull(TEXT("Other maps keep project defaults"), Settings->FindMapProfile(Other));
    TestEqual(TEXT("Other map grid is unchanged"), Settings->GetGridCounts(Other), Settings->GridCounts);
    return true;
}
#endif
