#include "PRTGISurfels.h"
#include "PRTProbeVolume.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogPRTCaptureAudit,Log,All);

namespace PRTGI
{
    namespace
    {
        constexpr uint32 Magic = 0x46535250; // PRSF, separate from directional transport caches.
        constexpr int32 Version = 2; // Single supported layout, including audit counters.
        constexpr int32 MaxSamples = 4000000;
        constexpr int32 RecordBytes = 76;

        float CaptureAuditRayTolerance(const FVector3f& Normal,const FVector3f& Direction)
        {
            // Match the bounded half-centimetre surface reprojection used by transport.
            return FMath::Min(20.f,.5f/FMath::Max(FMath::Abs(FVector3f::DotProduct(Normal,Direction)),.025f));
        }

        bool CaptureAuditMatches(const FSurfelSample& Sample,const FVector3f& Direction,const FVector3f& HitPosition,const FVector3f& HitNormal)
        {
            const FVector3f Delta=HitPosition-Sample.Position;
            return Delta.Size()<=CaptureAuditRayTolerance(Sample.Normal,Direction)+.01f
                && FMath::Abs(FVector3f::DotProduct(Delta,HitNormal))<=.5f
                && FVector3f::DotProduct(Sample.Normal,HitNormal)>.9f;
        }

        void IgnorePawns(UWorld* World, FCollisionQueryParams& Query)
        {
            for (TActorIterator<APawn> It(World); It; ++It) Query.AddIgnoredActor(*It);
        }

        FVector3f ReconstructCubePosition(const FVector3f& Origin, const FVector3f& Direction, float AxialDepth)
        {
            // SceneDepth is forward-axis depth, NOT distance along the pixel ray.
            const float ForwardCos = Direction.GetAbsMax();
            return Origin + Direction * (AxialDepth / ForwardCos);
        }

        void ArchiveSample(FArchive& Archive, FSurfelSample& Sample)
        {
            Archive << Sample.Position << Sample.Normal << Sample.Albedo << Sample.Probe;
            for (float& Weight : Sample.Weights) Archive << Weight;
        }
    }

    FSurfelVolume::~FSurfelVolume() { ReleaseCapture(); }

    void FSurfelVolume::ReleaseCapture()
    {
        if (Capture)
        {
            Capture->UnregisterComponent();
            Capture->TextureTarget = nullptr;
            Capture->RemoveFromRoot();
            Capture = nullptr;
        }
        if (Target)
        {
            Target->ReleaseResource();
            Target->RemoveFromRoot();
            Target = nullptr;
        }
        DiffuseCaptureMaterial=nullptr;
    }

    FVector FSurfelVolume::ProbePosition(int32 Probe) const
    {
        const FVector A(double(Probe % GridCounts.X) / (GridCounts.X - 1),
            double((Probe / GridCounts.X) % GridCounts.Y) / (GridCounts.Y - 1),
            double(Probe / (GridCounts.X * GridCounts.Y)) / (GridCounts.Z - 1));
        return FVector(GridMin) + A * FVector(GridMax - GridMin);
    }

    bool FSurfelVolume::Begin(UWorld* World, const FBox& Bounds, FIntVector Counts, int32 Resolution, float NearClip, uint64 Signature)
    {
        const int64 Count = int64(Counts.X) * Counts.Y * Counts.Z;
        if (!World || !Bounds.IsValid || Counts.GetMin() < 2 || Count > 32768
            || Resolution < 4 || Resolution > 32 || Count * 6 * Resolution * Resolution > MaxSamples
            || !FMath::IsFinite(NearClip) || NearClip <= 0.0f || !Signature) return false;
        GridCounts = Counts;
        GridMin = FVector3f(Bounds.Min); GridMax = FVector3f(Bounds.Max);
        SceneSignature = Signature; CaptureResolution = Resolution; CaptureNearClipCm = NearClip;
        NextProbe = 0; Samples.Reset(); Validity.Init(1.0f, int32(Count));
        GeometryChecks = GeometryMismatches = LegacyGeometryMismatches = 0; MaxGeometryErrorCm = 0.0f; Error.Reset();
        AuditedProbes = RejectedCaptureProbes = 0;
        DiffuseCaptureMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/PRTGI/Materials/M_PRTDiffuseCapture.M_PRTDiffuseCapture"));
        if (!DiffuseCaptureMaterial) { Error=TEXT("Missing PRT diffuse G-buffer capture material"); return false; }
        Target = NewObject<UTextureRenderTargetCube>(GetTransientPackage(), NAME_None, RF_Transient);
        Target->AddToRoot();
        Target->bForceLinearGamma = true;
        Target->ClearColor = FLinearColor::Black;
        // 32-bit depth prevents half-float quantization moving a reconstructed surfel through a wall.
        Target->Init(Resolution, PF_A32B32G32R32F);
        Target->UpdateResourceImmediate(true);
        Capture = NewObject<USceneCaptureComponentCube>(GetTransientPackage(), NAME_None, RF_Transient);
        Capture->AddToRoot(); Capture->TextureTarget = Target;
        Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false;
        // UE skips post-process blendables when View.State is absent.
        Capture->bAlwaysPersistRenderingState = true; Capture->bCaptureRotation = false;
        Capture->PostProcessBlendWeight = 1.0f;
        Capture->PostProcessSettings.AddBlendable(DiffuseCaptureMaterial,1.0f);
        Capture->ShowFlags.SetTonemapper(false);
        Capture->ShowFlags.SetColorGrading(false);
        Capture->ShowFlags.SetAntiAliasing(false);
        Capture->ShowFlags.SetTemporalAA(false);
        Capture->ShowFlags.SetMotionBlur(false);
        Capture->ShowFlags.SetBloom(false);
        Capture->ShowFlags.SetLighting(false);
        Capture->ShowFlags.SetGlobalIllumination(false);
        Capture->ShowFlags.SetLumenGlobalIllumination(false);
        Capture->ShowFlags.SetSpecular(false);
        Capture->ShowFlags.SetSkyLighting(false);
        Capture->ShowFlags.SetReflectionEnvironment(false);
        Capture->ShowFlags.SetAtmosphere(false);
        Capture->ShowFlags.SetFog(false);
        Capture->ShowFlags.SetVolumetricFog(false);
        Capture->ShowFlags.SetPostProcessing(false);
        Capture->ShowFlags.SetEyeAdaptation(false);
        for (TActorIterator<APawn> It(World); It; ++It) Capture->HiddenActors.Add(*It);
        Capture->RegisterComponentWithWorld(World);
        return Capture->IsRegistered();
    }

    bool FSurfelVolume::Step(UWorld* World)
    {
        if (!World || !Capture || Capture->GetWorld()!=World || !Target || IsComplete()) return false;
        const FVector Origin = ProbePosition(NextProbe);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PRTGISurfelCapture), false);
        IgnorePawns(World, Query);
        if (World->OverlapBlockingTestByChannel(Origin, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(1.0f), Query))
        {
            Validity[NextProbe++] = -1.0f;
            return true;
        }
        Capture->SetWorldLocation(Origin);
        TArray<TArray<FLinearColor>> Channels[3];
        const ESceneCaptureSource Sources[] = { SCS_SceneDepth, SCS_Normal, SCS_FinalColorHDR };
        for (int32 Channel = 0; Channel < 3; ++Channel)
        {
            Capture->CaptureSource = Sources[Channel];
            Capture->ShowFlags.SetPostProcessing(Channel==2);
            // FinalColorHDR does not force a G-buffer pass the way SCS_Normal does.
            // Keep deferred lighting enabled so the material can read DiffuseColor;
            // the output material replaces all shaded radiance with raw attributes.
            Capture->ShowFlags.SetLighting(Channel==2);
            {
                TGuardValue<float> NearGuard(GNearClippingPlane, CaptureNearClipCm);
                IConsoleVariable* CubePass=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SceneCapture.CubeSinglePass"));
                TGuardConsoleVariable<int32> CubePassGuard(CubePass,0,CubePass
                    ? static_cast<EConsoleVariableFlags>(CubePass->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode);
                IConsoleVariable* Occlusion=IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowOcclusionQueries"));
                TGuardConsoleVariable<int32> OcclusionGuard(Occlusion,0,Occlusion
                    ? static_cast<EConsoleVariableFlags>(Occlusion->GetFlags() & ECVF_SetByMask) : ECVF_SetByCode);
                Capture->CaptureScene();
            }
            Channels[Channel].SetNum(6);
            auto* Resource = Target->GameThread_GetRenderTargetResource();
            for (int32 Face = 0; Face < 6; ++Face)
            {
                FReadSurfaceDataFlags Flags(RCM_MinMax, ECubeFace(Face)); Flags.SetLinearToGamma(false);
                if (!Resource || !Resource->ReadLinearColorPixels(Channels[Channel][Face], Flags)
                    || Channels[Channel][Face].Num() != CaptureResolution * CaptureResolution)
                { Error = TEXT("G-buffer cubemap readback failed"); return false; }
            }
        }
        const int32 Before = Samples.Num();
        const int32 MismatchesBefore = GeometryMismatches;
        double TotalWeight = 0.0;
        for (int32 Y=0; Y<CaptureResolution; ++Y) for (int32 X=0; X<CaptureResolution; ++X)
        {
            const double U=2.0*(X+0.5)/CaptureResolution-1.0, V=1.0-2.0*(Y+0.5)/CaptureResolution;
            TotalWeight += 6.0 / FMath::Pow(1.0+U*U+V*V, 1.5);
        }
        const double Scale = 4.0*UE_DOUBLE_PI/TotalWeight;
        const double Convolution[3] = { UE_DOUBLE_PI, 2.0*UE_DOUBLE_PI/3.0, UE_DOUBLE_PI/4.0 };
        Query.bTraceComplex = true;
        for (int32 Face=0; Face<6; ++Face) for (int32 Y=0; Y<CaptureResolution; ++Y) for (int32 X=0; X<CaptureResolution; ++X)
        {
            const int32 Pixel=Y*CaptureResolution+X;
            const float Depth=Channels[0][Face][Pixel].R;
            const auto N=Channels[1][Face][Pixel], A=Channels[2][Face][Pixel];
            FVector3f Normal(N.R,N.G,N.B), Albedo(A.R,A.G,A.B);
            if (!FMath::IsFinite(Depth) || Depth<CaptureNearClipCm || Depth>1.e7f
                || Normal.ContainsNaN() || Albedo.ContainsNaN() || Normal.SizeSquared()<0.5f) continue;
            Normal.Normalize();
            const FVector3f D = Math::CubemapTexelDirection(Face,X,Y,CaptureResolution);
            if (FVector3f::DotProduct(Normal,-D)<=0.0f) continue;
            FSurfelSample& Sample=Samples.AddDefaulted_GetRef();
            Sample.Position=ReconstructCubePosition(FVector3f(Origin),D,Depth);
            Sample.Normal=Normal;
            Sample.Albedo=Albedo.ComponentMax(FVector3f(0.0f)).ComponentMin(FVector3f(1.0f));
            Sample.Probe=NextProbe;
            float Basis[9]; Math::EvaluateSH3(D,Basis);
            const double U=2.0*(X+0.5)/CaptureResolution-1.0, V=1.0-2.0*(Y+0.5)/CaptureResolution;
            const double W=Scale/FMath::Pow(1.0+U*U+V*V,1.5);
            for (int32 C=0; C<9; ++C) Sample.Weights[C]=float(W*Basis[C]*Convolution[C==0?0:(C<4?1:2)]);
            // Deterministic independent collision check guards depth convention and cube orientation.
            if (X==CaptureResolution/2 && Y==CaptureResolution/2)
            {
                FHitResult Hit;
                const float RayTolerance=FMath::Max(2.f,CaptureAuditRayTolerance(Sample.Normal,D)+.01f);
                const bool HitAny=World->LineTraceSingleByChannel(Hit,Origin,FVector(Sample.Position)+FVector(D)*RayTolerance,ECC_Visibility,Query);
                ++GeometryChecks;
                const float Delta=HitAny ? float(FVector::Distance(Hit.ImpactPoint,FVector(Sample.Position))) : 1.e6f;
                MaxGeometryErrorCm=FMath::Max(MaxGeometryErrorCm,Delta);
                if(!HitAny || Hit.Distance>(FVector(Sample.Position)-Origin).Size()+2.0 || Delta>1.f)++LegacyGeometryMismatches;
                if (!HitAny || !CaptureAuditMatches(Sample,D,FVector3f(Hit.ImpactPoint),FVector3f(Hit.ImpactNormal)))
                {
                    ++GeometryMismatches;
                    if(GeometryMismatches<=12)UE_LOG(LogPRTCaptureAudit,Warning,TEXT("Probe=%d Face=%d Origin=%s Captured=%s Collision=%s Actor=%s Delta=%.3f cm"),
                        NextProbe,Face,*Origin.ToString(),*Sample.Position.ToString(),*Hit.ImpactPoint.ToString(),*GetNameSafe(Hit.GetActor()),Delta);
                }
            }
        }
        FinishProbeCapture(Before, GeometryMismatches != MismatchesBefore);
        ++NextProbe;
        return true;
    }

    void FSurfelVolume::FinishProbeCapture(int32 FirstSample, bool bAuditMismatch)
    {
        ++AuditedProbes;
        // A centre-ray mismatch does not identify every bad texel. Exclude the
        // whole suspect probe rather than mixing unchecked samples into transport.
        if (bAuditMismatch || Samples.Num()==FirstSample)
        {
            Samples.SetNum(FirstSample, EAllowShrinking::No);
            Validity[NextProbe]=-1.f;
            ++RejectedCaptureProbes;
        }
    }

    bool FSurfelVolume::PassesGeometryAudit() const
    {
        return GeometryChecks>0 && AuditedProbes>0 && !Samples.IsEmpty()
            && int64(RejectedCaptureProbes)*20<=AuditedProbes;
    }

    FString FSurfelVolume::AuditWarning() const
    {
        return RejectedCaptureProbes>0
            ? FString::Printf(TEXT("Excluded %d/%d audited probes (geometry mismatch or empty capture); their data is excluded from transport and interpolation"),RejectedCaptureProbes,AuditedProbes)
            : FString();
    }

    bool FSurfelVolume::Save(const FString& Path) const
    {
        if (!IsComplete() || Samples.IsEmpty() || Samples.Num()>MaxSamples) return false;
        TArray<uint8> Bytes; FMemoryWriter Ar(Bytes);
        uint32 M=Magic; int32 V=Version, Count=Samples.Num(), P=Validity.Num();
        Ar<<M<<V;
        auto Grid=GridCounts; auto Min=GridMin, Max=GridMax; auto Sig=SceneSignature;
        int32 Res=CaptureResolution; float Clip=CaptureNearClipCm;
        Ar<<Grid<<Min<<Max<<Sig<<Res<<Clip<<P<<Count;
        for (float Value:Validity) Ar<<Value;
        for (auto Sample:Samples) ArchiveSample(Ar,Sample);
        auto Checks=GeometryChecks, Mismatches=GeometryMismatches, Audited=AuditedProbes, Rejected=RejectedCaptureProbes;
        Ar<<Checks<<Mismatches<<Audited<<Rejected;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
        const FString Temp=Path+TEXT(".tmp");
        return !Ar.IsError() && FFileHelper::SaveArrayToFile(Bytes,*Temp)
            && IFileManager::Get().Move(*Path,*Temp,true,true);
    }

    TSharedPtr<FSurfelVolume> FSurfelVolume::Load(const FString& Path, uint64 ExpectedSignature)
    {
        const int64 Size=IFileManager::Get().FileSize(*Path);
        if (Size<84 || Size>int64(MaxSamples)*RecordBytes+200000) return nullptr;
        TArray<uint8> Bytes; if (!FFileHelper::LoadFileToArray(Bytes,*Path)) return nullptr;
        FMemoryReader Ar(Bytes); uint32 M=0; int32 V=0,P=0,Count=0;
        auto Data=MakeShared<FSurfelVolume>();
        Ar<<M<<V<<Data->GridCounts<<Data->GridMin<<Data->GridMax<<Data->SceneSignature
            <<Data->CaptureResolution<<Data->CaptureNearClipCm<<P<<Count;
        if (Ar.IsError() || M!=Magic || V!=Version || Data->SceneSignature!=ExpectedSignature
            || Data->GridCounts.GetMin()<2 || Data->GridCounts.GetMax()>32
            || P!=Data->GridCounts.X*Data->GridCounts.Y*Data->GridCounts.Z
            || Count<=0 || Count>MaxSamples || Data->GridMin.ContainsNaN() || Data->GridMax.ContainsNaN()
            || (Data->GridMax-Data->GridMin).GetMin()<=0.0f
            || Ar.Tell()+int64(P)*4+int64(Count)*RecordBytes+16!=Size) return nullptr;
        Data->Validity.SetNumUninitialized(P);
        for (float& Value:Data->Validity) { Ar<<Value; if (Value!=1.0f && Value!=-1.0f) return nullptr; }
        Data->Samples.SetNum(Count);
        for (auto& Sample:Data->Samples)
        {
            ArchiveSample(Ar,Sample);
            if (Sample.Probe<0 || Sample.Probe>=P || Data->Validity[Sample.Probe]<0.0f
                || Sample.Position.ContainsNaN() || Sample.Normal.ContainsNaN() || Sample.Albedo.ContainsNaN()
                || !FMath::IsNearlyEqual(Sample.Normal.SizeSquared(),1.0f,0.01f)) return nullptr;
            for (float W:Sample.Weights) if (!FMath::IsFinite(W)) return nullptr;
        }
        Ar<<Data->GeometryChecks<<Data->GeometryMismatches<<Data->AuditedProbes<<Data->RejectedCaptureProbes;
        if (Data->GeometryChecks<0 || Data->GeometryChecks>P*6 || Data->GeometryMismatches<0
            || Data->GeometryMismatches>Data->GeometryChecks || Data->AuditedProbes<0 || Data->AuditedProbes>P
            || Data->RejectedCaptureProbes<0 || Data->RejectedCaptureProbes>Data->AuditedProbes
            || Data->RejectedCaptureProbes>Data->Validity.FilterByPredicate([](float X){return X<0;}).Num()) return nullptr;
        if (Ar.IsError()) return nullptr;
        Data->NextProbe=P; return Data;
    }

    bool FSurfelVolume::Relight(UWorld* World, const FVector3f& TowardSun, const FVector3f& LightColor,
        float Lux, bool CastShadows, float BiasCm, TArray<FVector4f>& OutCoefficients, const FString& DebugPath)
    {
        if (!World || !IsComplete() || Samples.IsEmpty()) return false;
        const double Start=FPlatformTime::Seconds();
        OutCoefficients.Init(FVector4f(0,0,0,0),Validity.Num()*9);
        const FVector3f L=TowardSun.GetSafeNormal();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PRTGISurfelSunShadow),true);
        IgnorePawns(World,Query);
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
            for (auto* Mesh:Meshes) if (!Mesh->CastShadow || !Mesh->IsVisible()) Query.AddIgnoredComponent(Mesh);
        }
        LastShadowRays=0;
        FString Debug=TEXT("probe,px,py,pz,nx,ny,nz,ar,ag,ab,cpu_r,cpu_g,cpu_b,blocked,hit,distance\n");
        // Correctness backend: CPU scene queries, only on light/backend changes, no feedback bounce.
        for (const auto& Sample:Samples)
        {
            const float Cosine=FMath::Max(0.0f,FVector3f::DotProduct(Sample.Normal,L));
            const bool Record=!DebugPath.IsEmpty() && (Sample.Probe==335 || Sample.Probe==383);
            if ((Cosine<=0.0f || Lux<=0.0f) && !Record) continue;
            bool Blocked=false; FHitResult Hit;
            if (CastShadows && Cosine>0.0f && Lux>0.0f)
            {
                ++LastShadowRays;
                const FVector Origin=FVector(Sample.Position)+FVector(Sample.Normal)*BiasCm;
                Blocked=Record ? World->LineTraceSingleByChannel(Hit,Origin,Origin+FVector(L)*1.e7,ECC_Visibility,Query)
                    : World->LineTraceTestByChannel(Origin,Origin+FVector(L)*1.e7,ECC_Visibility,Query);
            }
            const FVector3f Radiance=Blocked ? FVector3f(0) : Sample.Albedo*LightColor*(Lux*Cosine/UE_PI);
            if (Record)
            {
                Debug+=FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9g,%.9g,%.9g,%d,%s,%.6f\n"),
                    Sample.Probe,Sample.Position.X,Sample.Position.Y,Sample.Position.Z,Sample.Normal.X,Sample.Normal.Y,Sample.Normal.Z,
                    Sample.Albedo.X,Sample.Albedo.Y,Sample.Albedo.Z,Radiance.X,Radiance.Y,Radiance.Z,Blocked?1:0,
                    Hit.GetActor()?*Hit.GetActor()->GetName():TEXT("none"),Hit.Distance);
            }
            for (int32 C=0; C<9; ++C)
                OutCoefficients[Sample.Probe*9+C]+=FVector4f(Radiance*Sample.Weights[C],0.0f);
        }
        for (int32 P=0; P<Validity.Num(); ++P) OutCoefficients[P*9].W=Validity[P];
        LastRelightMs=(FPlatformTime::Seconds()-Start)*1000.0;
        if (!DebugPath.IsEmpty()) FFileHelper::SaveStringToFile(Debug,*DebugPath);
        return true;
    }
}

#if WITH_EDITOR
// Diagnostic only: samples a volume without saving or replacing any cache.
static FAutoConsoleCommandWithWorldAndArgs AuditVolumeCaptureCommand(
    TEXT("PRTGI.AuditVolumeCapture"),TEXT("PRTGI.AuditVolumeCapture <ActorLabel> [probe stride=11]; diagnostic capture, no cache writes."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if(!World || Args.IsEmpty())return;
        for(TActorIterator<APRTProbeVolume> It(World);It;++It)
        {
            if(It->GetActorLabel()!=Args[0])continue;
            FBox Grid,Receiver;FIntVector Counts;FString Error;
            if(!It->GetLayout(Grid,Receiver,Counts,Error))return;
            if(Counts==It->LegacyCounts && Grid.Min.Equals(It->LegacyGridMin,.001) && Grid.Max.Equals(It->LegacyGridMax,.001))Grid=FBox(It->LegacyGridMin,It->LegacyGridMax);
            const int32 Stride=Args.Num()>1?FMath::Max(1,FCString::Atoi(*Args[1])):11;
            PRTGI::FSurfelVolume Capture;
            if(!Capture.Begin(World,Grid,Counts,It->CaptureResolution,It->NearClipCm,1))return;
            const int32 Total=Counts.X*Counts.Y*Counts.Z;
            for(int32 P=0;P<Total;P+=Stride)
            {
                Capture.NextProbe=P;
                if(!Capture.Step(World))break;
            }
            Capture.ReleaseCapture();
            const FString Report=FString::Printf(TEXT("{\"checks\":%d,\"mismatches\":%d,\"legacy_mismatches\":%d,\"maximum_ray_cm\":%.5f,\"samples\":%d,\"stride\":%d,\"step_error\":%s}"),
                Capture.GeometryChecks,Capture.GeometryMismatches,Capture.LegacyGeometryMismatches,Capture.MaxGeometryErrorCm,Capture.Samples.Num(),Stride,Capture.Error.IsEmpty()?TEXT("false"):TEXT("true"));
            const FString Directory=FPaths::ProjectSavedDir()/TEXT("PRTBuildFailure");
            IFileManager::Get().MakeDirectory(*Directory,true);
            FFileHelper::SaveStringToFile(Report,*(Directory/TEXT("capture-audit.json")));
            UE_LOG(LogPRTCaptureAudit,Display,TEXT("%s %s Error=%s"),*Args[0],*Report,*Capture.Error);
            return;
        }
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGISurfelPartialAuditTest,"PRTGI.Surfel.PartialCaptureAudit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPRTGISurfelPartialAuditTest::RunTest(const FString&)
{
    PRTGI::FSurfelVolume Data;
    Data.Validity.Init(1.f,40);Data.NextProbe=1;
    Data.Samples.AddDefaulted_GetRef().Probe=0;
    Data.Samples.AddDefaulted_GetRef().Probe=1;
    Data.FinishProbeCapture(1,true);
    TestEqual(TEXT("Suspect probe marked invalid for interpolation"),Data.Validity[1],-1.f);
    TestEqual(TEXT("All suspect samples removed before transport"),Data.Samples.Num(),1);
    TestEqual(TEXT("Unaffected sample retained"),Data.Samples[0].Probe,0);
    Data.AuditedProbes=40;Data.GeometryChecks=240;Data.GeometryMismatches=6;
    Data.RejectedCaptureProbes=2;
    TestTrue(TEXT("Five percent rejected probes may finish"),Data.PassesGeometryAudit());
    TestFalse(TEXT("Partial completion has a visible warning"),Data.AuditWarning().IsEmpty());
    Data.RejectedCaptureProbes=3;
    TestFalse(TEXT("Widespread mismatches still fail"),Data.PassesGeometryAudit());
    Data.RejectedCaptureProbes=0;Data.GeometryChecks=0;
    TestFalse(TEXT("No independent checks is not a successful audit"),Data.PassesGeometryAudit());
    Data.GeometryChecks=240;Data.Samples.Reset();
    TestFalse(TEXT("Empty transport cannot be published"),Data.PassesGeometryAudit());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGISurfelGrazingAuditTest,"PRTGI.Surfel.CaptureAuditGrazing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPRTGISurfelGrazingAuditTest::RunTest(const FString&)
{
    // Replay ShinyBox probe 154 against its independently known z=190 floor.
    const FVector3f Origin(-852.684f,755.519f,242.396f);
    PRTGI::FSurfelSample Sample;Sample.Position=FVector3f(-16.271f,703.243f,190.120f);Sample.Normal=FVector3f(0,0,1);
    const FVector3f Ray=(Sample.Position-Origin).GetSafeNormal();
    const FVector3f Hit=Origin+Ray*((190.f-Origin.Z)/Ray.Z);
    TestTrue(TEXT("Recorded floor reproduces former false rejection"),(Hit-Sample.Position).Size()>1.f);
    TestTrue(TEXT("Same floor within 0.12cm passes surface-space audit"),PRTGI::CaptureAuditMatches(Sample,Ray,Hit,FVector3f(0,0,1)));
    TestTrue(TEXT("Trace reaches the floor when the old 2cm extension is insufficient"),PRTGI::CaptureAuditRayTolerance(Sample.Normal,Ray)>2.f);
    TestFalse(TEXT("A different parallel surface stays rejected"),PRTGI::CaptureAuditMatches(Sample,Ray,Sample.Position+FVector3f(0,0,2),FVector3f(0,0,1)));
    TestFalse(TEXT("Wrong cube face / surface normal stays rejected"),PRTGI::CaptureAuditMatches(Sample,Ray,Hit,FVector3f(1,0,0)));
    TestFalse(TEXT("Unbounded grazing displacement stays rejected"),PRTGI::CaptureAuditMatches(Sample,Ray,Sample.Position+Ray*25.f,FVector3f(0,0,1)));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGISurfelDepthTest,"PRTGI.Surfel.CubeAxialDepth",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPRTGISurfelDepthTest::RunTest(const FString& Parameters)
{
    // A six-sided box 100cm from the probe: every reconstructed point must lie on its face.
    for(int32 F=0;F<6;++F) for(int32 Y=0;Y<8;++Y) for(int32 X=0;X<8;++X)
    {
        const FVector3f D=PRTGI::Math::CubemapTexelDirection(F,X,Y,8);
        const FVector3f P=PRTGI::ReconstructCubePosition(FVector3f(0),D,100.0f);
        if (!FMath::IsNearlyEqual(P.GetAbsMax(),100.0f,0.001f)) { AddError(TEXT("Cube depth reconstruction misses plane")); return false; }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGISurfelTransportTest,"PRTGI.Surfel.RelightAndPersistence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPRTGISurfelTransportTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (const FWorldContext& Context:GEngine->GetWorldContexts())
        if (Context.WorldType==EWorldType::Editor) World=Context.World();
    if (!World) { AddError(TEXT("Requires editor world")); return false; }
    PRTGI::FSurfelVolume Data;
    Data.GridCounts=FIntVector(2); Data.GridMin=FVector3f(0); Data.GridMax=FVector3f(100);
    Data.SceneSignature=12345; Data.CaptureResolution=16; Data.Validity.Init(1.0f,8);
    Data.Validity[7]=-1.0f; Data.NextProbe=8;
    Data.AuditedProbes=8;Data.RejectedCaptureProbes=1;Data.GeometryChecks=48;Data.GeometryMismatches=1;
    auto& Sample=Data.Samples.AddDefaulted_GetRef();
    Sample.Normal=FVector3f(0,0,1); Sample.Albedo=FVector3f(0.5f,0.25f,1.0f); Sample.Weights[0]=1.0f;
    TArray<FVector4f> A,B,Dark;
    TestTrue(TEXT("Relight succeeds"),Data.Relight(World,FVector3f(0,0,1),FVector3f(1),UE_PI,false,0.1f,A));
    Data.Relight(World,FVector3f(0,0,1),FVector3f(1),2.0f*UE_PI,false,0.1f,B);
    Data.Relight(World,FVector3f(0,0,-1),FVector3f(1),UE_PI,false,0.1f,Dark);
    TestTrue(TEXT("Lambertian radiance preserves albedo and 1/pi"),FVector3f(A[0]).Equals(Sample.Albedo,1.e-5f));
    TestTrue(TEXT("Doubling source energy doubles transport result"),FVector3f(B[0]).Equals(FVector3f(A[0])*2.0f,1.e-5f));
    TestTrue(TEXT("Back-facing illumination contributes zero"),FVector3f(Dark[0]).IsNearlyZero());
    TestEqual(TEXT("Invalid probe marker survives reconstruction"),A[7*9].W,-1.0f);
    const FString Path=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/PRTGI/surfel-roundtrip.bin"));
    TestTrue(TEXT("Save independent geometry cache"),Data.Save(Path));
    const auto Loaded=PRTGI::FSurfelVolume::Load(Path,Data.SceneSignature);
    TestTrue(TEXT("Load checked geometry cache"),Loaded.IsValid());
    if (Loaded)
    {
        TestEqual(TEXT("Audit exclusions survive cache reload"),Loaded->RejectedCaptureProbes,1);
        TestEqual(TEXT("Audit warning survives cache reload"),Loaded->AuditWarning(),Data.AuditWarning());
        TArray<FVector4f> Restored;
        Loaded->Relight(World,FVector3f(0,0,1),FVector3f(1),UE_PI,false,0.1f,Restored);
        TestTrue(TEXT("Roundtrip preserves evaluated transport"),Restored[0].Equals(A[0],1.e-6f));
    }
    TestFalse(TEXT("Wrong geometry signature is rejected"),PRTGI::FSurfelVolume::Load(Path,67890).IsValid());
    // Unsupported layouts must be rejected, never interpreted as current data.
    TArray<uint8> LegacyBytes;FFileHelper::LoadFileToArray(LegacyBytes,*Path);
    LegacyBytes.SetNum(LegacyBytes.Num()-16);int32 LegacyVersion=1;
    FMemory::Memcpy(LegacyBytes.GetData()+4,&LegacyVersion,4);
    FFileHelper::SaveArrayToFile(LegacyBytes,*Path);
    TestFalse(TEXT("Old geometry layout is rejected"),PRTGI::FSurfelVolume::Load(Path,Data.SceneSignature).IsValid());
    int32 CurrentVersion=2;FMemory::Memcpy(LegacyBytes.GetData()+4,&CurrentVersion,4);
    FFileHelper::SaveArrayToFile(LegacyBytes,*Path);
    TestFalse(TEXT("Current layout requires audit trailer"),PRTGI::FSurfelVolume::Load(Path,Data.SceneSignature).IsValid());
    IFileManager::Get().Delete(*Path);
    return !HasAnyErrors();
}
#endif
