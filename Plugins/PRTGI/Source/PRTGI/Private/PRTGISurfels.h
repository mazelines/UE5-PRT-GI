#pragma once

#include "CoreMinimal.h"
#include "PRTGIMath.h"

class UWorld;
class USceneCaptureComponentCube;
class UTextureRenderTargetCube;
class UMaterialInterface;

namespace PRTGI
{
    // First correctness implementation: retain independent samples, without lossy clustering.
    // Light parameters and evaluated radiance are deliberately absent from the persisted data.
    struct FSurfelSample
    {
        FVector3f Position = FVector3f::ZeroVector;
        FVector3f Normal = FVector3f::ZeroVector;
        FVector3f Albedo = FVector3f::ZeroVector;
        int32 Probe = 0;
        float Weights[Math::CoefficientCount] = {};
    };

    class FSurfelVolume
    {
    public:
        ~FSurfelVolume();
        bool Begin(UWorld* World, const FBox& Bounds, FIntVector Counts, int32 Resolution, float NearClip, uint64 Signature);
        bool Step(UWorld* World); // At most one probe, three G-buffer captures; no lighting evaluation.
        void FinishProbeCapture(int32 FirstSample, bool bAuditMismatch);
        bool PassesGeometryAudit() const;
        FString AuditWarning() const;
        bool IsComplete() const { return NextProbe == Validity.Num(); }
        void ReleaseCapture();
        bool Save(const FString& Path) const;
        static TSharedPtr<FSurfelVolume> Load(const FString& Path, uint64 ExpectedSignature);
        bool Relight(UWorld* World, const FVector3f& TowardSun, const FVector3f& LightColor,
            float Lux, bool CastShadows, float BiasCm, TArray<FVector4f>& OutCoefficients,
            const FString& DebugPath = FString());
        FVector ProbePosition(int32 Probe) const;

        FIntVector GridCounts = FIntVector::ZeroValue;
        FVector3f GridMin = FVector3f::ZeroVector;
        FVector3f GridMax = FVector3f::ZeroVector;
        uint64 SceneSignature = 0;
        int32 CaptureResolution = 0;
        float CaptureNearClipCm = 1.0f;
        TArray<float> Validity;
        TArray<FSurfelSample> Samples;
        int32 NextProbe = 0;
        int32 GeometryChecks = 0;
        int32 GeometryMismatches = 0;
        int32 AuditedProbes = 0;
        int32 RejectedCaptureProbes = 0;
        int32 LegacyGeometryMismatches = 0; // Diagnostic comparison with the former 1cm ray-distance check.
        float MaxGeometryErrorCm = 0.0f;
        double LastRelightMs = 0.0;
        int32 LastShadowRays = 0;
        FString Error;

    private:
        USceneCaptureComponentCube* Capture = nullptr;
        UTextureRenderTargetCube* Target = nullptr;
        UMaterialInterface* DiffuseCaptureMaterial = nullptr;
    };
}
