#include "PRTGIAssetMigrationLibrary.h"
#include "PRTGI.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"
#include "UObject/Package.h"
bool UPRTGIAssetMigrationLibrary::CopyPackages(const TMap<FString,FString>& Packages)
{
    if(Packages.IsEmpty())return false;
    TSet<FString> Destinations;
    for(const auto& Pair:Packages)
    {
        if(!Pair.Key.StartsWith(TEXT("/Game/")) || !Pair.Value.StartsWith(TEXT("/PRTGI/"))
            || !FPackageName::IsValidLongPackageName(Pair.Value) || FPackageName::DoesPackageExist(Pair.Value)
            || Destinations.Contains(Pair.Value))return false;
        Destinations.Add(Pair.Value);
    }
    if(!FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().AdvancedCopyPackages(Packages,true,false))return false;
    // AdvancedCopy's autosave flag saves sources, not the duplicated packages.
    // Persist explicitly, then verify the copied world in a fresh editor process.
    TArray<UPackage*> Copies;
    for(const auto& Pair:Packages)
    {auto* Package=FindPackage(nullptr,*Pair.Value);if(!Package)return false;Copies.Add(Package);}
    return UEditorLoadingAndSavingUtils::SavePackages(Copies,false);
}
FString UPRTGIAssetMigrationLibrary::GeometrySignature(APRTProbeVolume* V){return PRTGI::GetVolumeGeometrySignature(V);}
bool UPRTGIAssetMigrationLibrary::ImportVerifiedRelocatedCache(APRTProbeVolume* V,const FString& P,const FString& S)
{return PRTGI::ImportRelocatedVolumeCache(V,P,S);}
