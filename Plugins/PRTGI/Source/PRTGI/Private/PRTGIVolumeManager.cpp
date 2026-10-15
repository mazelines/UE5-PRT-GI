#include "PRTGIVolumeManager.h"
#include "PRTProbeVolume.h"
#include "PRTGIProbeVisibility.h"
#include "PRTGISurfels.h"
#include "PRTGIMultiBounce.h"
#include "PRTGISettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LineBatchComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogPRTVolume, Log, All);
static TAutoConsoleVariable<int32> CVarPRTBounces(TEXT("r.PRTGI.Bounces"),6,TEXT("Indirect bounces 1..12 (default 6); compare 4, 8, 12. Legacy caches cap at four until rebuilt."),ECVF_Default);
static TAutoConsoleVariable<int32> CVarPRTMultiGPU(TEXT("r.PRTGI.MultiBounceGPU"),1,TEXT("Use precomputed path transport and GPU lighting. 0: legacy CPU one-bounce reference."),ECVF_Default);
static TAutoConsoleVariable<int32> CVarPRTNearClipSafety(TEXT("r.PRTGI.ProbeNearClipSafety"),1,TEXT("Reject probes whose capture near-clip cube intersects cached geometry. 0: diagnostic comparison."),ECVF_Default);
static TAutoConsoleVariable<int32> CVarPRTGPUReadback(TEXT("r.PRTGI.GPUReadback"),0,TEXT("Read GPU coefficients back for numeric diagnostics. Normal rendering keeps coefficients on GPU; showing probes also enables readback."),ECVF_Default);
static TAutoConsoleVariable<int32> CVarPRTGPUResident(TEXT("r.PRTGI.GPUResident"),1,TEXT("Keep relit coefficients on GPU. 0 restores the readback/upload path for A/B validation."),ECVF_Default);

struct FPRTGIVolumeManager::FState
{
    TWeakObjectPtr<APRTProbeVolume> Actor;
    TSharedPtr<PRTGI::FSurfelVolume> Surfels;
    TSharedPtr<const FPRTGITransportData, ESPMode::ThreadSafe> Matrix;
    FPRTGIRenderVolume Render;
    TArray<float> Validity;
    TArray<uint8> NearClipMask;
    FBox Grid;
    uint64 GeometrySignature = 0;
    uint64 MatrixSignature = 0;
    uint32 ConfigHash = 0;
    double NextValidation = 0;
    FString LastLight;
    bool bVisualDirty = true;
    int32 LastDisplay = -1;
    float LastRadius = 0;
    TSharedPtr<PRTGI::FMultiBounceData,ESPMode::ThreadSafe> Multi;
    TSharedPtr<PRTGI::FMultiBounceBuild,ESPMode::ThreadSafe> MultiJob;
    TUniquePtr<PRTGI::FMultiBounceGPU> GPU;
    FString PendingLight;
    int32 PendingBounces=0;
    int32 PublishedBounces=0;
    float MultiCell=0;
    float MultiMinThroughput=-1;
    int32 MultiPaths=-1;
    bool MultiAdaptive=false;
    bool bBuildIncludesCapture=false;
};

FPRTGIVolumeManager::FPRTGIVolumeManager(FSignature InSignature, FReadMatrix InReadMatrix)
    : Signature(MoveTemp(InSignature)), ReadMatrix(MoveTemp(InReadMatrix)) {}
FPRTGIVolumeManager::~FPRTGIVolumeManager() { Reset(); }

FString FPRTGIVolumeManager::DumpQualityReport() const
{
    FString Report=TEXT("{\"volumes\":[");bool First=true;
    for(const auto& S:States)if(S->Actor.IsValid() && S->Multi)
    {
        if(!First)Report+=TEXT(",");First=false;
        Report+=FString::Printf(TEXT("{\"volume_id\":\"%s\",\"effective_bounces\":%d,\"active\":%s,\"gpu_resident\":%s,\"cpu_reference_ready\":%s,\"transport\":%s}"),
            *S->Actor->VolumeId.ToString(),S->PublishedBounces,S->Actor->bGIActive?TEXT("true"):TEXT("false"),
            S->Render.GPUCoefficients?TEXT("true"):TEXT("false"),S->Render.bCPUReferenceReady?TEXT("true"):TEXT("false"),*S->Multi->QualityReport());
    }
    Report+=TEXT("]}");
    const FString Path=FPaths::ProjectSavedDir()/TEXT("PRTGIQuality.json");
    if(!FFileHelper::SaveStringToFile(Report,*Path))return TEXT("Could not write Saved/PRTGIQuality.json");
    return Report;
}

FString FPRTGIVolumeManager::ValidateMultiBounce()
{
    for(const auto& S:States)if(S->Multi && S->Actor.IsValid() && S->Actor->bEnabled && !S->Render.bCPUReferenceReady)
        return TEXT("Set r.PRTGI.GPUReadback 1, wait for relighting, then validate. Current coefficients reside on GPU.");
    FString Report=TEXT("{\"volumes\":[");bool First=true;
    auto* World=ActiveWorld.Get();ADirectionalLight* Light=nullptr;
    if(World)for(TActorIterator<ADirectionalLight> It(World);It;++It){Light=*It;break;}
    auto* LC=Light?Cast<UDirectionalLightComponent>(Light->GetLightComponent()):nullptr;
    if(LC)for(const auto& S:States)if(S->Multi && S->Actor.IsValid() && S->Actor->bEnabled)
    {
        FLinearColor C=LC->GetLightColor();if(LC->bUseTemperature)C*=LC->GetColorTemperature();
        const FVector3f Sun(-LC->GetDirection()),Color(C.R,C.G,C.B);
        const float Lux=LC->IsVisible()&&LC->bAffectsWorld?LC->Intensity*LC->DiffuseScale*LC->IndirectLightingIntensity:0;
        const float Bias=FMath::Clamp(GetDefault<UPRTGISettings>()->SurfelShadowBiasCm,.001f,1.f);
        const int32 Orders[4]={1,4,FMath::Min(8,S->Multi->MaxBounces),FMath::Min(12,S->Multi->MaxBounces)};
        TArray<FVector4f> CPU[4],Legacy,Unclustered;
        for(int I=0;I<4;++I)S->Multi->EvaluateCPU(Sun,Color,Lux,LC->CastShadows,Bias,Orders[I],CPU[I]);
        S->Multi->EvaluateUnclusteredCPU(*S->Surfels,Sun,Color,Lux,LC->CastShadows,Bias,Unclustered,FPaths::ProjectSavedDir()/TEXT("PRTMultiBounce/bvh_visibility.csv"));
        auto Error=[](const auto& A,const auto& B)->double
        {if(A.Num()!=B.Num())return -1;double Delta=0,Energy=0;for(int I=0;I<A.Num();++I){Delta+=FVector3f(A[I]-B[I]).SizeSquared();Energy+=FVector3f(B[I]).SizeSquared();}return FMath::Sqrt(Delta/FMath::Max(Energy,1.e-20));};
        S->Surfels->Relight(World,Sun,Color,Lux,LC->CastShadows,Bias,Legacy,FPaths::ProjectSavedDir()/TEXT("PRTMultiBounce/collision_visibility.csv"));
        bool Monotonic=true;double Energy[4]={};for(int B=0;B<4;++B)for(int P=0;P<S->Multi->ProbeCount;++P)
        {
            const FVector3f E(CPU[B][P*9]);Energy[B]+=E.X+E.Y+E.Z;
            if(B>0 && (E-FVector3f(CPU[B-1][P*9])).GetMin()<-1.e-3f)Monotonic=false;
        }
        int Count=INDEX_NONE;for(int I=0;I<4;++I)if(Orders[I]==S->PublishedBounces)Count=I;
        TArray<FVector4f> Other;if(Count==INDEX_NONE)S->Multi->EvaluateCPU(Sun,Color,Lux,LC->CastShadows,Bias,S->PublishedBounces,Other);
        if(!First)Report+=TEXT(",");First=false;
        Report+=FString::Printf(TEXT("{\"actor\":\"%s\",\"effective_bounces\":%d,\"gpu_cpu_relative_error\":%.9g,\"cluster_one_bounce_relative_error\":%.9g,\"monotonic_l0\":%s,\"bounce_counts\":[1,4,%d,%d],\"l0_energy\":[%.9g,%.9g,%.9g,%.9g],\"sources\":%d,\"terms\":%d,\"unmapped_hits\":%d,\"surface_hits\":%d,\"build_seconds\":%.6f,\"clustering_only_relative_error\":%.9g,\"bvh_vs_collision_relative_error\":%.9g,\"fp16_weight_relative_error\":%.9g,\"transport_mb\":%.3f}"),
            *S->Actor->GetName(),S->PublishedBounces,Error(S->Render.Coefficients,Count==INDEX_NONE?Other:CPU[Count]),Error(CPU[0],Legacy),Monotonic?TEXT("true"):TEXT("false"),
            Orders[2],Orders[3],Energy[0],Energy[1],Energy[2],Energy[3],S->Multi->Surfaces.Num()/3,S->Multi->Terms.Num(),S->Multi->UnmappedHits,S->Multi->SurfaceHits,S->Multi->BuildSeconds,
            Error(CPU[0],Unclustered),Error(Unclustered,Legacy),S->Multi->QuantizationRelativeError,double(S->Multi->Terms.Num())*64/1048576);
    }
    Report+=TEXT("]}");FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("PRTMultiBounceValidation.json")));return Report;
}

void FPRTGIVolumeManager::Reset()
{
    for (const auto& State : States)
    {
        if (State->Actor.IsValid()) State->Actor->ClearProbeSpheres();
        if (State->MultiJob) State->MultiJob->Cancelled.Store(true);
    }
    if (Markers)
    {
        Markers->RemoveFromRoot(); Markers->DestroyComponent(); Markers = nullptr;
    }
    Bake.Reset(); BakeActor.Reset(); Queue.Reset(); States.Reset(); RenderVolumes.Reset(); ActiveWorld.Reset();
    bDirty = true;
}

bool FPRTGIVolumeManager::IsBaking() const { return Bake.IsValid() || !Queue.IsEmpty() || States.ContainsByPredicate([](const auto& S){return S->MultiJob.IsValid();}); }

void FPRTGIVolumeManager::RequestBuild(APRTProbeVolume* Actor)
{
    if (!IsValid(Actor) || !Actor->bEnabled) return;
    if (Actor->GetWorld() != ActiveWorld.Get())
    { Actor->CacheStatus = TEXT("Wait for this world to become active, then Build again"); return; }
    if (BakeActor.Get() == Actor || Queue.Contains(Actor)) return;
    if(States.ContainsByPredicate([&](const auto& S){return S->Actor.Get()==Actor && S->MultiJob.IsValid();}))return;
    Queue.Add(Actor);
    Actor->CacheStatus = TEXT("Queued"); Actor->BuildProgress = 0;
    Actor->BuildStage = TEXT("Queued for build");
}

void FPRTGIVolumeManager::CancelBuild(APRTProbeVolume* Actor)
{
    bool bWasBuilding=Queue.Remove(Actor)>0;
    if (BakeActor.Get() == Actor && Bake) { Bake.Reset(); BakeActor.Reset(); bWasBuilding=true; }
    for(auto& S:States)if(S->Actor.Get()==Actor && S->MultiJob){S->MultiJob->Cancelled.Store(true);S->MultiJob.Reset();bWasBuilding=true;}
    if (bWasBuilding && IsValid(Actor))
    { Actor->CacheStatus = TEXT("Cancelled; cache file unchanged"); Actor->BuildStage=TEXT("Cancelled: progress retained; transport cache not replaced"); }
}

bool FPRTGIVolumeManager::Tick(UWorld* World, ADirectionalLight* Light, int32 ShowProbes, bool bLegacyBaking, bool bPRTActive)
{
    if (World != ActiveWorld.Get()) { Reset(); ActiveWorld = World; }
    if (!World) return true;
    const double Now = FPlatformTime::Seconds();
    TArray<APRTProbeVolume*> Actors;
    for (TActorIterator<APRTProbeVolume> It(World); It; ++It)
        if (IsValid(*It) && !It->IsActorBeingDestroyed()) Actors.Add(*It);
    // GUID order also makes equal-priority/equal-density selection deterministic.
    Actors.Sort([](const auto& A, const auto& B) { return A.VolumeId.ToString() < B.VolumeId.ToString(); });
    for (int32 Index = States.Num() - 1; Index >= 0; --Index)
    {
        if (!Actors.Contains(States[Index]->Actor.Get()))
        { if (BakeActor == States[Index]->Actor) { Bake.Reset(); BakeActor.Reset(); }
          if(States[Index]->MultiJob)States[Index]->MultiJob->Cancelled.Store(true);
          States.RemoveAt(Index); bDirty = true; }
    }
    auto* LC = Light ? Cast<UDirectionalLightComponent>(Light->GetLightComponent()) : nullptr;
    const FVector3f TowardSun = LC ? FVector3f(-LC->GetDirection()) : FVector3f(0,0,1);
    FLinearColor LightColor = LC ? LC->GetLightColor() : FLinearColor::Black;
    if (LC && LC->bUseTemperature) LightColor *= LC->GetColorTemperature();
    const FVector3f Color(LightColor.R,LightColor.G,LightColor.B);
    const float Lux = LC && LC->IsVisible() && LC->bAffectsWorld
        ? FMath::Max(0.f,LC->Intensity * LC->DiffuseScale * LC->IndirectLightingIntensity) : 0;
    const float Bias = FMath::Clamp(GetDefault<UPRTGISettings>()->SurfelShadowBiasCm, .001f, 1.f);
    const int32 RequestedBounces=FMath::Clamp(CVarPRTBounces.GetValueOnGameThread(),1,PRTGI::MaxTransportBounces);
    const bool UseGPU=CVarPRTMultiGPU.GetValueOnGameThread()!=0;
    const bool Resident=CVarPRTGPUResident.GetValueOnGameThread()!=0;
    const bool Readback=!Resident || CVarPRTGPUReadback.GetValueOnGameThread()!=0 || ShowProbes!=0;
    const FString LightKey = FString::Printf(TEXT("%.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %.9g %d %d %d %d"),
        TowardSun.X,TowardSun.Y,TowardSun.Z,Color.X,Color.Y,Color.Z,Lux,LC?int32(LC->CastShadows):0,Bias,RequestedBounces,int(UseGPU),int(Readback),int(Resident));

    for (APRTProbeVolume* Actor : Actors)
    {
        Actor->bGIActive=false;
        Actor->EffectiveBounces=0;
        auto* Found = States.FindByPredicate([&](const auto& S) { return S->Actor.Get() == Actor; });
        if (!Found)
        {
            auto State = MakeUnique<FState>(); State->Actor = Actor;
            States.Add(MoveTemp(State)); Found = &States.Last(); bDirty = true;
        }
        FState& S = **Found;
        if(S.MultiCell!=Actor->TransportCellSize || S.MultiMinThroughput!=Actor->TransportMinThroughput || S.MultiPaths!=Actor->TransportPathsPerSample || S.MultiAdaptive!=Actor->bAdaptiveTransportSampling)
        {
            // A request made immediately after editing settings targets the new settings.
            const bool bQueuedForNewSettings=Queue.Contains(Actor);
            CancelBuild(Actor);
            if(bQueuedForNewSettings)Queue.AddUnique(Actor);
            S.Multi.Reset();S.GPU.Reset();S.LastLight.Reset();S.MultiCell=Actor->TransportCellSize;S.MultiMinThroughput=Actor->TransportMinThroughput;S.MultiPaths=Actor->TransportPathsPerSample;S.MultiAdaptive=Actor->bAdaptiveTransportSampling;
            // Validate before consuming a queued rebuild, even inside the normal
            // two-second validation interval; otherwise a zero signature drops it.
            S.GeometrySignature=0;S.NextValidation=0;
        }
        FBox Grid, Receiver; FIntVector Counts; FString Error;
        const bool DuplicateId = Actors.ContainsByPredicate([&](const auto* Other) { return Other != Actor && Other->VolumeId == Actor->VolumeId; });
        if (!Actor->bEnabled || DuplicateId || !Actor->GetLayout(Grid, Receiver, Counts, Error)
            || !FMath::IsFinite(Actor->TransportCellSize) || Actor->TransportCellSize<1 || Actor->TransportCellSize>100
            || !FMath::IsFinite(Actor->TransportMinThroughput) || Actor->TransportMinThroughput<0 || Actor->TransportMinThroughput>.25f
            || Actor->TransportPathsPerSample<1 || Actor->TransportPathsPerSample>16
            || (Actor->bAdaptiveTransportSampling && Actor->TransportPathsPerSample<2))
        {
            CancelBuild(Actor);
            Actor->CacheStatus = DuplicateId ? TEXT("Duplicate VolumeId; duplicate using the editor to assign a new ID")
                : !Actor->bEnabled ? TEXT("Disabled") : !Error.IsEmpty() ? Error
                : Actor->bAdaptiveTransportSampling && Actor->TransportPathsPerSample<2
                    ? TEXT("Adaptive transport requires at least 2 production paths per sample")
                    : TEXT("Transport parameters are outside their supported ranges");
            Actor->BuildStage=Actor->bEnabled?TEXT("Invalid configuration; cannot build"):TEXT("Disabled");
            Actor->BuildProgress=0;
            Actor->ClearProbeSpheres(); Actor->ValidProbes = Actor->InvalidProbes = 0;
            if (!S.Render.Coefficients.IsEmpty()) { S.Render.Coefficients.Reset(); bDirty = true; }
            S.GeometrySignature = 0; S.ConfigHash = 0; S.Surfels.Reset(); S.Matrix.Reset();S.Render.Visibility.Reset();
            S.Multi.Reset();S.GPU.Reset();
            Actor->TransportSources=0;
            continue;
        }
        if (Counts == Actor->LegacyCounts
            && Grid.Min.Equals(Actor->LegacyGridMin,.001) && Grid.Max.Equals(Actor->LegacyGridMax,.001))
            Grid = FBox(Actor->LegacyGridMin, Actor->LegacyGridMax);
        Actor->ProbeCounts = Counts; Actor->ActualSpacing = Grid.GetSize() / FVector(Counts - FIntVector(1));
        Actor->CacheFile = Actor->GetCachePath();
        uint32 Config = GetTypeHash(Grid.Min);
        for (const uint32 Value : {GetTypeHash(Grid.Max),GetTypeHash(Receiver.Min),GetTypeHash(Receiver.Max),GetTypeHash(Counts),
            GetTypeHash(Actor->NearClipCm),GetTypeHash(Actor->CaptureResolution),GetTypeHash(Actor->Priority),
            GetTypeHash(Actor->BlendFace),GetTypeHash(Actor->BlendDistance),GetTypeHash(Actor->LegacyCacheBase)})
            Config = HashCombine(Config, Value);
        if (S.ConfigHash != Config || Now >= S.NextValidation)
        {
            const uint64 GeometrySig = Signature(World, Light, Grid, Counts, true, true, Actor->NearClipCm, Actor->CaptureResolution);
            const uint64 MatrixSig = !Actor->LegacyCacheBase.IsEmpty()
                ? Signature(World, Light, Grid, Counts, !Actor->bLegacyGlobal, false, Actor->NearClipCm, -1) : 0;
            if (GeometrySig != S.GeometrySignature || MatrixSig != S.MatrixSignature)
            {
                if(S.MultiJob)CancelBuild(Actor);
                S.Multi.Reset();S.GPU.Reset();
                S.Surfels.Reset(); S.Matrix.Reset(); S.Render.Coefficients.Reset(); S.Validity.Reset(); S.NearClipMask.Reset(); S.LastLight.Reset();
                S.Surfels = PRTGI::FSurfelVolume::Load(Actor->CacheFile, GeometrySig);
                const bool LegacyFits = Counts == Actor->LegacyCounts && Grid.Min.Equals(Actor->LegacyGridMin,.001)
                    && Grid.Max.Equals(Actor->LegacyGridMax,.001) && FPaths::IsRelative(Actor->LegacyCacheBase)
                    && !Actor->LegacyCacheBase.Contains(TEXT(".."));
                if (!S.Surfels && LegacyFits && !Actor->LegacyCacheBase.IsEmpty())
                {
                    const FString Base = FPaths::Combine(FPaths::ProjectContentDir(), Actor->LegacyCacheBase);
                    S.Surfels = PRTGI::FSurfelVolume::Load(Base + TEXT(".surfels"), GeometrySig);
                    if (!S.Surfels && LC)
                    {
                        S.Matrix = ReadMatrix(Base, Counts);
                        if (S.Matrix && S.Matrix->SceneSignature != MatrixSig) S.Matrix.Reset();
                    }
                }
                if (BakeActor.Get() == Actor && Bake && Bake->SceneSignature != GeometrySig) CancelBuild(Actor);
                S.GeometrySignature = GeometrySig; S.MatrixSignature = MatrixSig; bDirty = true; S.bVisualDirty = true;
                if(S.Surfels)S.Multi=PRTGI::FMultiBounceData::Load(Actor->CacheFile+TEXT(".multi"),GeometrySig,Actor->TransportCellSize,PRTGI::MultiBounceBaseCRC(*S.Surfels),Actor->TransportMinThroughput,Actor->TransportPathsPerSample,Actor->bAdaptiveTransportSampling);
                S.Render.Visibility=S.Surfels?FPRTGIProbeVisibility::Build(*S.Surfels):nullptr;
                if(S.Surfels && S.Multi)
                {
                    S.NearClipMask=FPRTGIProbeVisibility::BuildNearClipMask(*S.Surfels,*S.Multi);
                    UE_LOG(LogPRTVolume,Display,TEXT("Near clip safety: %s rejected=%d"),*Actor->GetName(),S.NearClipMask.FilterByPredicate([](uint8 V){return V!=0;}).Num());
                }
                if(!Queue.Contains(Actor) && BakeActor.Get()!=Actor)
                {
                    Actor->BuildProgress=S.Multi?1.f:0.f;
                    Actor->BuildStage=S.Multi?FString::Printf(TEXT("Complete: 1-%d bounce cache ready"),S.Multi->MaxBounces):
                        S.Surfels?TEXT("Geometry cache reusable; awaiting multi-bounce build"):TEXT("No valid multi-bounce cache; build required");
                }
                Actor->CacheStatus = S.Surfels ? TEXT("Ready: one-bounce Surfel cache") : S.Matrix ? TEXT("Ready: imported directional matrix")
                    : TEXT("Missing or stale cache: Build Precomputation required");
            }
            if (S.ConfigHash != Config) { bDirty = true; S.bVisualDirty = true; }
            S.ConfigHash = Config; S.NextValidation = Now + 2;
        }
        S.Grid = Grid;
        S.Render.GridCounts = Counts; S.Render.GridMin = FVector3f(Grid.Min); S.Render.GridMax = FVector3f(Grid.Max);
        S.Render.ReceiverMin = FVector3f(Receiver.Min); S.Render.ReceiverMax = FVector3f(Receiver.Max);
        S.Render.Priority = FMath::Clamp(Actor->Priority,-100000,100000);
        S.Render.BlendFace = FMath::Clamp(Actor->BlendFace,0,6); S.Render.BlendDistance = FMath::Max(0.f,Actor->BlendDistance);
        S.Render.bTrilinear = S.Matrix.IsValid() && Actor->bLegacyGlobal;
        if(S.MultiJob)
        {
            const int32 Completed=S.MultiJob->CompletedProbes.Load(),Total=Counts.X*Counts.Y*Counts.Z;
            const float Fraction=FMath::Clamp(float(Completed)/Total,0.f,1.f);
            Actor->BuildProgress=S.bBuildIncludesCapture?.5f+.48f*Fraction:.98f*Fraction;
            Actor->BuildStage=FString::Printf(TEXT("Stage %s: precomputing 1-12 bounces - %d%% (%d/%d probes)%s"),
                S.bBuildIncludesCapture?TEXT("2/2"):TEXT("1/1"),FMath::RoundToInt(Fraction*100),Completed,Total,
                Completed==0?TEXT("; preparing surface data"):S.bBuildIncludesCapture?TEXT(""):TEXT("; geometry reused"));
            Actor->CacheStatus=TEXT("Building static 1..12 bounce paths (CPU worker)");
            if(S.MultiJob->Future.IsReady())
            {
                auto Result=S.MultiJob->Future.Get();S.MultiJob.Reset();
                const uint64 Current=Signature(World,Light,Grid,Counts,true,true,Actor->NearClipCm,Actor->CaptureResolution);
                if(Result && Result->Error.IsEmpty() && Result->Signature==Current)
                {Actor->BuildProgress=.99f;Actor->BuildStage=TEXT("Validating and saving transport cache");}
                if(Result && Result->Error.IsEmpty() && Result->Signature==Current && Result->Save(Actor->CacheFile+TEXT(".multi")))
                {S.Multi=Result;S.NearClipMask=FPRTGIProbeVisibility::BuildNearClipMask(*S.Surfels,*S.Multi);S.GPU.Reset();S.LastLight.Reset();Actor->BuildProgress=1;Actor->BuildStage=TEXT("Complete: 1-12 bounce cache saved");Actor->CacheStatus=TEXT("Ready: precomputed 1..12 bounce paths");}
                else
                {
                    const FString Reason=!Result?TEXT("No worker result"):!Result->Error.IsEmpty()?Result->Error:
                        Result->Signature!=Current?TEXT("Scene changed during transport build"):
                        FString::Printf(TEXT("Could not save transport cache: %s"),*(Actor->CacheFile+TEXT(".multi")));
                    Actor->CacheStatus=TEXT("Multi-bounce build failed; old cache retained: ")+Reason;
                    Actor->BuildStage=TEXT("Transport build failed: ")+Reason;
                    UE_LOG(LogPRTVolume,Error,TEXT("%s: %s"),*Actor->GetActorNameOrLabel(),*Actor->CacheStatus);
                }
            }
        }
        Actor->TransportSources=S.Multi?S.Multi->Surfaces.Num()/3:0;
        S.Render.Geometry=S.Multi;
        if(S.GPU && S.GPU->IsPending())
        {
            TArray<FVector4f> Completed;
            // Publish the completed generation even while the sun keeps moving. Requiring
            // equality with this frame's light would starve every continuously animated light.
            TSharedPtr<const FPRTGIGPUCoefficients,ESPMode::ThreadSafe> GPUOutput;
            if(S.GPU->Poll(Completed,GPUOutput) && bPRTActive && UseGPU && S.Multi)
            {
                S.Render.bCPUReferenceReady=!Completed.IsEmpty();
                if(Completed.IsEmpty())Completed.SetNumZeroed(S.Multi->ProbeCount*9);
                S.Render.GPUCoefficients=Resident?GPUOutput:nullptr;
                S.Render.Coefficients=MoveTemp(Completed);S.Validity=S.Multi->Validity;S.LastLight=S.PendingLight;
                S.PublishedBounces=S.PendingBounces;bDirty=true;S.bVisualDirty=true;
                if(!S.MultiJob)Actor->CacheStatus=FString::Printf(TEXT("Ready: GPU %d indirect bounces"),S.PublishedBounces);
            }
        }
        if (S.Surfels && S.Validity.IsEmpty()) S.Validity=S.Surfels->Validity;
        if (bPRTActive && LC && !bLegacyBaking && (S.Surfels || S.Matrix) && (S.LastLight != LightKey || S.Render.Coefficients.IsEmpty()))
        {
            const int32 Count = Counts.X * Counts.Y * Counts.Z;
            bool Success = true;
            S.Validity.Init(1.f, Count);
            if (S.Surfels)
            {
                if(S.Multi && UseGPU)
                {
                    if(!S.GPU)S.GPU=MakeUnique<PRTGI::FMultiBounceGPU>();
                    if(!S.GPU->IsPending()){S.PendingLight=LightKey;S.PendingBounces=FMath::Min(RequestedBounces,S.Multi->MaxBounces);S.GPU->Submit(S.Multi,TowardSun,Color,Lux,LC->CastShadows,Bias,S.PendingBounces,Readback);}
                }
                else {S.Render.GPUCoefficients.Reset();S.Render.bCPUReferenceReady=true;Success = S.Surfels->Relight(World,TowardSun,Color,Lux,LC->CastShadows,Bias,S.Render.Coefficients);
                    S.PublishedBounces=1;
                    if(!S.MultiJob)Actor->CacheStatus=RequestedBounces>1?TEXT("One bounce fallback: Build Precomputation for up to 12 bounces"):TEXT("Ready: CPU one bounce");}
                S.Validity = S.Surfels->Validity;
            }
            else
            {
                S.Render.Coefficients.SetNum(Count * 9);
                TArray<FVector4f> SH;
                for (int32 P=0; P<Count; ++P)
                {
                    if (!PRTGI::Math::EvaluateDirectionalTransport(MakeArrayView(S.Matrix->TransportCoefficients.GetData()+P*81,81),
                        TowardSun,Lux,Color,SH)) { Success=false; break; }
                    for(int C=0;C<9;++C) S.Render.Coefficients[P*9+C]=SH[C];
                    if (!Actor->bLegacyGlobal) S.Validity[P]=S.Matrix->TransportCoefficients[P*81].W;
                    S.Render.Coefficients[P*9].W=S.Validity[P];
                }
            }
            if (!Success) { S.Render.Coefficients.Reset(); Actor->CacheStatus=TEXT("Relight failed"); }
            else if(!(S.Multi && UseGPU)) {S.LastLight = LightKey;if(S.Matrix)S.PublishedBounces=1;}
            if(!(S.Multi && UseGPU)){bDirty = true; S.bVisualDirty = true;}
        }
        if (!LC && !S.Render.Coefficients.IsEmpty()) { S.Render.Coefficients.Reset(); S.LastLight.Reset(); bDirty=true; S.bVisualDirty=true; }
        // Apply after every GPU/CPU publication and restore from immutable base
        // validity when toggled off. Relighting and bounce switches cannot revive
        // clipped probes; no lighting work or cache rewrite is needed for this mask.
        if(S.Surfels && S.NearClipMask.Num()==S.Surfels->Validity.Num())
        {
            const bool Enabled=CVarPRTNearClipSafety.GetValueOnGameThread()!=0;
            const auto& BaseValidity=S.Multi?S.Multi->Validity:S.Surfels->Validity;
            S.Validity.SetNum(BaseValidity.Num());
            for(int32 P=0;P<BaseValidity.Num();++P)
            {
                const float V=Enabled && S.NearClipMask[P]?-1.f:BaseValidity[P];
                if(S.Validity[P]!=V){S.Validity[P]=V;S.bVisualDirty=true;}
                if(S.Render.Coefficients.IsValidIndex(P*9) && S.Render.Coefficients[P*9].W!=V)
                {S.Render.Coefficients[P*9].W=V;bDirty=true;S.bVisualDirty=true;}
            }
        }
        Actor->InvalidProbes = S.Validity.FilterByPredicate([](float V){return V<0;}).Num();
        Actor->BuildWarnings=S.Surfels?S.Surfels->AuditWarning():FString();
        if (!Actor->BuildWarnings.IsEmpty())
        {
            if (Actor->CacheStatus.StartsWith(TEXT("Ready:")) && !Actor->CacheStatus.Contains(TEXT("[capture warning]")))
                Actor->CacheStatus+=TEXT(" [capture warning]");
            if (Actor->BuildStage.StartsWith(TEXT("Complete:")) && !Actor->BuildStage.Contains(TEXT("warnings present")))
                Actor->BuildStage+=TEXT("; warnings present; see Build Warnings");
        }
        Actor->ValidProbes = S.Validity.Num() - Actor->InvalidProbes;
        Actor->bGIActive=bPRTActive && !S.Render.Coefficients.IsEmpty();
        Actor->EffectiveBounces=Actor->bGIActive?S.PublishedBounces:0;
        if(S.Multi && RequestedBounces>S.Multi->MaxBounces && !S.MultiJob && Actor->CacheStatus.StartsWith(TEXT("Ready:")) && !Actor->CacheStatus.Contains(TEXT("cache capped")))
            Actor->CacheStatus+=FString::Printf(TEXT("; cache capped at %d, rebuild for %d"),S.Multi->MaxBounces,RequestedBounces);
        const bool Show = bPRTActive && ShowProbes != 0 && (ShowProbes != 2 || Actor->Priority > 0) && (ShowProbes != 3 || Actor->Priority <= 0);
        const int32 DisplayKey = Show ? int32(Actor->DisplayMode) : -1;
        if (S.bVisualDirty || DisplayKey != S.LastDisplay || S.LastRadius != Actor->ProbeRadius)
        {
            Actor->UpdateProbeSpheres(Grid,Counts,S.Render.Coefficients,S.Validity,Show);
            S.LastDisplay=DisplayKey; S.LastRadius=Actor->ProbeRadius; S.bVisualDirty=false;
        }
    }

    if (!bLegacyBaking && !Bake && !Queue.IsEmpty() && !States.ContainsByPredicate([](const auto& S){return S->MultiJob.IsValid();}))
    {
        APRTProbeVolume* Actor = Queue[0].Get(); Queue.RemoveAt(0);
        auto* S = States.FindByPredicate([&](const auto& Item){return Item->Actor.Get()==Actor;});
        if (Actor && S && Actor->bEnabled && (*S)->GeometrySignature)
        {
            if((*S)->Surfels)
            {
                (*S)->bBuildIncludesCapture=false;
                Actor->BuildProgress=0;Actor->BuildStage=TEXT("Stage 1/1: preparing 1-12 bounce precomputation (geometry cache reused)");
                (*S)->MultiJob=PRTGI::BuildMultiBounce(World,(*S)->Surfels,Actor->TransportCellSize,Actor->TransportMinThroughput,Actor->TransportPathsPerSample,Actor->bAdaptiveTransportSampling);
                Actor->CacheStatus=TEXT("Building static 1..12 bounce paths; reusing geometry");
            }
            else
            {
            (*S)->bBuildIncludesCapture=true;
            Actor->BuildProgress=0;Actor->BuildStage=TEXT("Stage 1/2: preparing geometry capture");
            Bake = MakeShared<PRTGI::FSurfelVolume>();
            if (Bake->Begin(World,(*S)->Grid,Actor->ProbeCounts,Actor->CaptureResolution,Actor->NearClipCm,(*S)->GeometrySignature))
            { BakeActor=Actor; Actor->CacheStatus=TEXT("Building static transport"); UE_LOG(LogPRTVolume,Display,TEXT("Build start: %s"),*Actor->GetName()); }
            else { Actor->CacheStatus=TEXT("Build failed: ")+Bake->Error; Actor->BuildStage=TEXT("Could not start geometry capture; see Cache Status"); Bake.Reset(); }
            }
        }
    }
    if (Bake && !bLegacyBaking)
    {
        APRTProbeVolume* Actor = BakeActor.Get();
        if (!Actor || !Bake->Step(World))
        { if (Actor) {Actor->CacheStatus=TEXT("Capture failed: ")+Bake->Error;Actor->BuildStage=TEXT("Geometry capture failed; see Cache Status");} Bake.Reset(); BakeActor.Reset(); }
        else
        {
            const float Fraction=float(Bake->NextProbe)/Bake->Validity.Num();
            Actor->BuildProgress=.5f*Fraction;
            Actor->BuildStage=FString::Printf(TEXT("Stage 1/2: capturing geometry - %d%% (%d/%d probes)"),FMath::RoundToInt(Fraction*100),Bake->NextProbe,Bake->Validity.Num());
            if (Bake->IsComplete())
            {
                Bake->ReleaseCapture();
                auto* State=States.FindByPredicate([&](const auto& Item){return Item->Actor.Get()==Actor;});
                const bool Audit = Bake->PassesGeometryAudit();
                if (Bake->RejectedCaptureProbes>0)
                    UE_LOG(LogPRTVolume,Warning,TEXT("Capture exclusion %s: %s"),*Actor->GetActorNameOrLabel(),*Bake->AuditWarning());
                UE_LOG(LogPRTVolume,Display,TEXT("Capture audit %s: surface mismatches=%d/%d, former ray-distance mismatches=%d, max ray error=%.3f cm"),
                    *Actor->GetActorNameOrLabel(),Bake->GeometryMismatches,Bake->GeometryChecks,Bake->LegacyGeometryMismatches,Bake->MaxGeometryErrorCm);
                const uint64 CurrentSig = State ? Signature(World,Light,(*State)->Grid,Actor->ProbeCounts,true,true,Actor->NearClipCm,Actor->CaptureResolution) : 0;
                if (Audit && CurrentSig==Bake->SceneSignature && Bake->Save(Actor->GetCachePath()))
                {
                    (*State)->Surfels=Bake; (*State)->Matrix.Reset(); (*State)->LastLight.Reset();
                    (*State)->Render.Visibility=FPRTGIProbeVisibility::Build(*Bake);
                    (*State)->bVisualDirty=true; Actor->CacheStatus=TEXT("Ready: one-bounce Surfel cache");
                    (*State)->Multi.Reset();(*State)->GPU.Reset();(*State)->NearClipMask.Reset();
                    (*State)->bBuildIncludesCapture=true;
                    Actor->BuildProgress=.5f;Actor->BuildStage=TEXT("Stage 2/2: preparing 1-12 bounce precomputation (geometry capture complete)");
                    (*State)->MultiJob=PRTGI::BuildMultiBounce(World,Bake,Actor->TransportCellSize,Actor->TransportMinThroughput,Actor->TransportPathsPerSample,Actor->bAdaptiveTransportSampling);
                    UE_LOG(LogPRTVolume,Display,TEXT("Build complete: %s probes=%d samples=%d file=%s"),*Actor->GetName(),Bake->Validity.Num(),Bake->Samples.Num(),*Actor->GetCachePath());
                    bDirty=true;
                }
                else
                {
                    const FString Reason=!Audit?FString::Printf(TEXT("Geometry audit: %d/%d capture probes rejected; allowed 5%%, requires nonempty samples and audit checks"),Bake->RejectedCaptureProbes,Bake->AuditedProbes):
                        CurrentSig!=Bake->SceneSignature?TEXT("Scene changed during geometry capture"):
                        FString::Printf(TEXT("Could not save geometry cache: %s"),*Actor->GetCachePath());
                    Actor->CacheStatus=TEXT("Build rejected; old file retained: ")+Reason;
                    Actor->BuildStage=TEXT("Geometry build failed: ")+Reason;
                    UE_LOG(LogPRTVolume,Error,TEXT("%s: %s"),*Actor->GetActorNameOrLabel(),*Actor->CacheStatus);
                }
                Bake.Reset(); BakeActor.Reset();
            }
        }
    }
    // Position-only preview is separate from light spheres and never enters captures.
    if (Now>=NextMarkers)
    {
        NextMarkers=Now+.5;
        if (Markers) Markers->Flush();
        if (bPRTActive && ShowProbes)
        {
            if (!Markers)
            {
                Markers=NewObject<ULineBatchComponent>(World,NAME_None,RF_Transient|RF_DuplicateTransient);
                Markers->AddToRoot(); Markers->SetHiddenInSceneCapture(true); Markers->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Markers->RegisterComponentWithWorld(World);
            }
            for(const auto& S:States)
            {
                auto* A=S->Actor.Get();
                if(!A || !A->bEnabled || A->DisplayMode!=EPRTProbeDisplay::Positions || !S->GeometrySignature
                    || (ShowProbes==2 && A->Priority<=0) || (ShowProbes==3 && A->Priority>0)) continue;
                const auto N=A->ProbeCounts;
                for(int P=0;P<N.X*N.Y*N.Z;++P)
                {
                    FVector Alpha(double(P%N.X)/(N.X-1),double((P/N.X)%N.Y)/(N.Y-1),double(P/(N.X*N.Y))/(N.Z-1));
                    FLinearColor C=S->Validity.IsValidIndex(P) ? S->Validity[P]<0 ? FLinearColor::Red : A->Priority>0 ? FLinearColor(1,.35f,0) : FLinearColor(0,.8f,1) : FLinearColor(.4f,.4f,.4f);
                    Markers->DrawSphere(S->Grid.Min+Alpha*S->Grid.GetSize(),FMath::Clamp(A->ProbeRadius,.5f,50.f),6,C,-1.f,SDPG_World,.5f);
                }
            }
        }
    }
    if (!bDirty) return false;
    RenderVolumes.Reset();
    for (auto* Actor:Actors)
        for (const auto& S:States)
            if (S->Actor.Get()==Actor && Actor->bEnabled && !S->Render.Coefficients.IsEmpty()) RenderVolumes.Add(S->Render);
    bDirty=false;
    return true;
}
