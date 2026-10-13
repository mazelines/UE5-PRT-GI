#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PRTGIAssetMigrationLibrary.generated.h"
class APRTProbeVolume;
UCLASS()
class UPRTGIAssetMigrationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="PRT|Migration")
    static bool CopyPackages(const TMap<FString,FString>& Packages);
    UFUNCTION(BlueprintCallable,Category="PRT|Migration")
    static FString GeometrySignature(APRTProbeVolume* Volume);
    UFUNCTION(BlueprintCallable,Category="PRT|Migration")
    static bool ImportVerifiedRelocatedCache(APRTProbeVolume* Volume,const FString& SourcePath,const FString& SourceSignature);
};
