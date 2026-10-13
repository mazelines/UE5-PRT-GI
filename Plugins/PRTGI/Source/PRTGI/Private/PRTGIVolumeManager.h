#pragma once
#include "CoreMinimal.h"
#include "PRTGIVolumeData.h"

class APRTProbeVolume;
class ADirectionalLight;
class ULineBatchComponent;
namespace PRTGI { class FSurfelVolume; }

/** 场景 Actor 的独立构建队列和缓存。旧配置后端继续工作，不参与此队列。 */
class FPRTGIVolumeManager
{
public:
    using FSignature = TFunction<uint64(UWorld*, ADirectionalLight*, const FBox&, FIntVector, bool, bool, float, int32)>;
    using FReadMatrix = TFunction<TSharedPtr<const FPRTGITransportData, ESPMode::ThreadSafe>(const FString&, FIntVector)>;
    FPRTGIVolumeManager(FSignature InSignature, FReadMatrix InReadMatrix);
    ~FPRTGIVolumeManager();
    bool Tick(UWorld* World, ADirectionalLight* Light, int32 ShowProbes, bool bLegacyBaking, bool bPRTActive);
    void RequestBuild(APRTProbeVolume* Actor);
    void CancelBuild(APRTProbeVolume* Actor);
    void Reset();
    bool IsBaking() const;
    FString ValidateMultiBounce();
    const TArray<FPRTGIRenderVolume>& GetRenderVolumes() const { return RenderVolumes; }
    int32 GetReadyCount() const { return RenderVolumes.Num(); }

private:
    struct FState;
    TArray<TUniquePtr<FState>> States;
    TArray<TWeakObjectPtr<APRTProbeVolume>> Queue;
    TWeakObjectPtr<UWorld> ActiveWorld;
    TWeakObjectPtr<APRTProbeVolume> BakeActor;
    TSharedPtr<PRTGI::FSurfelVolume> Bake;
    TArray<FPRTGIRenderVolume> RenderVolumes;
    FSignature Signature;
    FReadMatrix ReadMatrix;
    ULineBatchComponent* Markers = nullptr;
    bool bDirty = true;
    double NextMarkers = 0;
};
