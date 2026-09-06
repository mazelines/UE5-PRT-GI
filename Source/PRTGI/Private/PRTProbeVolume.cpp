#include "PRTProbeVolume.h"
#include "PRTGI.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

APRTProbeVolume::APRTProbeVolume()
{
    PrimaryActorTick.bCanEverTick = false;
    VolumeBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("VolumeBounds"));
    SetRootComponent(VolumeBounds);
    VolumeBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VolumeBounds->SetGenerateOverlapEvents(false);
    VolumeBounds->SetBoxExtent(Extent);
    VolumeBounds->SetHiddenInGame(true);
    VolumeBounds->ShapeColor = FColor::Cyan;
    VolumeBounds->SetHiddenInSceneCapture(true);
}

void APRTProbeVolume::PostActorCreated()
{
    Super::PostActorCreated();
    if (!VolumeId.IsValid()) VolumeId = FGuid::NewGuid();
}

void APRTProbeVolume::PostLoad()
{
    Super::PostLoad();
    if (!VolumeId.IsValid()) VolumeId = FGuid::NewGuid();
}

void APRTProbeVolume::PostDuplicate(EDuplicateMode::Type Mode)
{
    Super::PostDuplicate(Mode);
    if (Mode != EDuplicateMode::PIE)
    {
        VolumeId = FGuid::NewGuid();
        LegacyCacheBase.Reset();
    }
}

void APRTProbeVolume::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    VolumeBounds->SetBoxExtent(Extent.GetAbs());
    FBox Grid, Receiver;
    FString Error;
    if (GetLayout(Grid, Receiver, ProbeCounts, Error))
        ActualSpacing = Grid.GetSize() / FVector(ProbeCounts - FIntVector(1));
    else
    {
        ProbeCounts = FIntVector::ZeroValue;
        CacheStatus = Error;
    }
}

bool APRTProbeVolume::GetLayout(FBox& Grid, FBox& Receiver, FIntVector& Counts, FString& Error) const
{
    const FVector Scale = GetActorScale3D();
    if (!GetActorQuat().Equals(FQuat::Identity, 0.0001f))
    { Error = TEXT("Axis-aligned volumes only: reset Rotation to zero"); return false; }
    if (Extent.ContainsNaN() || Scale.ContainsNaN() || ProbeSpacing.ContainsNaN() || ProbeInset.ContainsNaN()
        || GetActorLocation().ContainsNaN() || Scale.GetMin() <= 0 || Extent.GetMin() <= 0
        || ProbeSpacing.GetMin() < 1 || ProbeInset.GetMin() < 0
        || !FMath::IsFinite(NearClipCm) || NearClipCm < .01f || NearClipCm > 10
        || CaptureResolution < 4 || CaptureResolution > 32)
    { Error = TEXT("Invalid extent, spacing, scale, inset, resolution or Near Clip"); return false; }
    Receiver = FBox(GetActorLocation() - Extent * Scale, GetActorLocation() + Extent * Scale);
    Grid = FBox(Receiver.Min + ProbeInset, Receiver.Max - ProbeInset);
    if (Grid.GetSize().GetMin() <= 0)
    { Error = TEXT("Probe Inset consumes the volume"); return false; }
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const double Cells = Grid.GetSize()[Axis] / ProbeSpacing[Axis];
        if (Cells > 31.00001)
        { Error = TEXT("More than 32 probes on an axis: increase spacing or split the volume"); return false; }
        Counts[Axis] = FMath::Max(2, FMath::CeilToInt(Cells - 1.e-5) + 1);
    }
    if (int64(Counts.X) * Counts.Y * Counts.Z * 6 * CaptureResolution * CaptureResolution > 4000000)
    { Error = TEXT("Capture exceeds 4M sample budget: reduce resolution, increase spacing or split volume"); return false; }
    return true;
}

FString APRTProbeVolume::GetCachePath() const
{
    const FString Map = GetWorld() ? UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) : TEXT("Unknown");
    if(Map.StartsWith(TEXT("/PRTGI/")))
    {
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("PRTGI"));
        if(Plugin)return FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetContentDir(),TEXT("Precomputed/Volumes"),
            Map.Replace(TEXT("/"),TEXT("_")),VolumeId.ToString(EGuidFormats::Digits)+TEXT(".prtgi.surfels")));
    }
    // Full package path prevents maps with the same short name sharing writable caches.
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("PRTGI/Volumes"),
        Map.Replace(TEXT("/"), TEXT("_")), VolumeId.ToString(EGuidFormats::Digits) + TEXT(".prtgi.surfels")));
}

void APRTProbeVolume::BuildPrecomputation() { PRTGI::BuildVolume(this); }
void APRTProbeVolume::CancelBuild() { PRTGI::CancelVolumeBuild(this); }

void APRTProbeVolume::ClearProbeSpheres()
{
    if (ProbeSpheres) ProbeSpheres->DestroyComponent();
    ProbeSpheres = nullptr;
    ProbeMaterial = nullptr;
    ProbeSHTexture = nullptr;
}

void APRTProbeVolume::UpdateProbeSpheres(const FBox& Grid, FIntVector Counts,
    const TArray<FVector4f>& SH, const TArray<float>& Validity, bool bShow)
{
    if (!bShow || DisplayMode != EPRTProbeDisplay::Lighting) { ClearProbeSpheres(); return; }
    const int32 Count = Counts.X * Counts.Y * Counts.Z;
    if (Count <= 0 || Count > 32768) return;
    if (!ProbeSpheres)
    {
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/PRTGI/Materials/M_PRTProbeSH.M_PRTProbeSH"));
        if (!Mesh || !Material) return;
        ProbeMaterial = UMaterialInstanceDynamic::Create(Material, this);
        ProbeSpheres = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
        ProbeSpheres->SetStaticMesh(Mesh);
        ProbeSpheres->SetMaterial(0, ProbeMaterial);
        ProbeSpheres->SetMobility(EComponentMobility::Movable);
        ProbeSpheres->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        ProbeSpheres->SetCastShadow(false);
        ProbeSpheres->bAffectDistanceFieldLighting = false;
        ProbeSpheres->bAffectDynamicIndirectLighting = false;
        ProbeSpheres->SetHiddenInSceneCapture(true);
        ProbeSpheres->NumCustomDataFloats = 1;
        ProbeSpheres->RegisterComponentWithWorld(GetWorld());
    }
    // Coefficients already contain the cosine convolution. Material only evaluates
    // SH at the sphere normal, divides by PI, then uses UE's normal exposure path.
    const int32 Height = FMath::DivideAndRoundUp(Count, 256);
    ProbeSHTexture = UTexture2D::CreateTransient(9 * 256, Height, PF_A32B32G32R32F);
    ProbeSHTexture->SRGB = false;
    ProbeSHTexture->Filter = TF_Nearest;
    auto& Mip = ProbeSHTexture->GetPlatformData()->Mips[0];
    auto* Pixels = static_cast<FVector4f*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memzero(Pixels, 9 * 256 * Height * sizeof(FVector4f));
    if (SH.Num() == Count * 9) FMemory::Memcpy(Pixels, SH.GetData(), SH.Num() * sizeof(FVector4f));
    Mip.BulkData.Unlock();
    ProbeSHTexture->UpdateResource();
    ProbeMaterial->SetTextureParameterValue(TEXT("ProbeSH"), ProbeSHTexture);
    ProbeMaterial->SetScalarParameterValue(TEXT("TextureHeight"), float(Height));
    ProbeSpheres->ClearInstances();
    TArray<FTransform> Transforms;
    Transforms.Reserve(Count);
    const float Radius = FMath::IsFinite(ProbeRadius) ? FMath::Clamp(ProbeRadius, .5f, 50.f) : 3.f;
    for (int32 P = 0; P < Count; ++P)
    {
        const FVector Alpha(double(P % Counts.X) / (Counts.X - 1),
            double((P / Counts.X) % Counts.Y) / (Counts.Y - 1), double(P / (Counts.X * Counts.Y)) / (Counts.Z - 1));
        Transforms.Emplace(FQuat::Identity, Grid.Min + Alpha * Grid.GetSize(), FVector(Radius / 50.0));
    }
    ProbeSpheres->AddInstances(Transforms, false, true, false);
    for (int32 P = 0; P < Count; ++P)
    {
        const float Index = Validity.IsValidIndex(P) && Validity[P] < 0 ? -2.f : SH.Num() == Count * 9 ? float(P) : -1.f;
        ProbeSpheres->SetCustomDataValue(P, 0, Index, false);
    }
    ProbeSpheres->MarkRenderStateDirty();
}
