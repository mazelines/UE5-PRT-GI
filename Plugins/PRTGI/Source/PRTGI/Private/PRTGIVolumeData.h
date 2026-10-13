#pragma once
#include "CoreMinimal.h"
struct FPRTGIProbeVisibility;

struct FPRTGITransportData
{
    FIntVector GridCounts = FIntVector::ZeroValue;
    FVector3f GridMin = FVector3f::ZeroVector;
    FVector3f GridMax = FVector3f::ZeroVector;
    uint64 SceneSignature = 0;
    TArray<FVector4f> TransportCoefficients;
};

struct FPRTGIRenderVolume
{
    FIntVector GridCounts = FIntVector::ZeroValue;
    FVector3f GridMin = FVector3f::ZeroVector;
    FVector3f GridMax = FVector3f::ZeroVector;
    float ReceiverBoundsPadding = 0.0f;
    FVector3f ReceiverMin = FVector3f::ZeroVector;
    FVector3f ReceiverMax = FVector3f::ZeroVector;
    int32 BlendFace = 0;
    float BlendDistance = 0.0f;
    int32 Priority = 0;
    bool bTrilinear = false;
    TSharedPtr<const FPRTGIProbeVisibility, ESPMode::ThreadSafe> Visibility;
    TArray<FVector4f> Coefficients;
};
