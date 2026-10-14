#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "PRTGISurfels.h"
struct FPRTGIGPUCoefficients;

namespace PRTGI
{
inline constexpr int32 MaxTransportBounces = 12;
// 64 bytes per sparse entry instead of 144. CPU and shader decode the same
// IEEE half weights; source identifiers and bounce counts remain exact integers.
struct FMultiBounceTerm
{
    uint32 Source=0, Bounce=0;
    uint32 Packed[14]={};
    FVector3f Weight(int32 Coefficient) const;
};
static_assert(sizeof(FMultiBounceTerm)==64);
uint32 MultiBounceBaseCRC(const FSurfelVolume& Base);
// Static diffuse path transport. No light parameters or evaluated radiance are persisted.
// Rows map directly-lit surface clusters to probe irradiance SH, separately for bounces 1..12.
struct FMultiBounceData
{
    uint64 Signature = 0;
    uint32 BaseCRC = 0;
    float CellSize = 10.f;
    int32 ProbeCount = 0;
    TArray<FVector4f> Surfaces; // position, normal, diffuse reflectance (3 float4 / source)
    // Geometric normal and numerical origin offset, separate from the shading normal.
    // Empty for v3 caches; zero normals retain the legacy offset for unbound samples.
    TArray<FVector4f> SourceGeometry;
    TArray<FVector4f> Triangles; // vertex, edge1, edge2; vertex.w = CastShadow
    TArray<FVector4f> Nodes; // preorder BVH: bounds min/max; leaf first/count, internal -1/escape
    TArray<FMultiBounceTerm> Terms;
    TArray<FUintVector2> Ranges;
    TArray<float> Validity;
    int32 Paths[MaxTransportBounces] = {};
    int32 UnmappedHits = 0;
    int32 SurfaceHits = 0;
    FString Error;
    double BuildSeconds = 0;
    double QuantizationRelativeError = 0;
    int32 CorrectedSamples = 0;
    float MaxPositionCorrectionCm = 0;
    int32 CacheVersion = 7;
    int32 BoundSamples = 0;
    int32 UnboundSamples = 0;
    int32 UnmappedByBounce[MaxTransportBounces-1] = {};
    int32 MaterialHits = 0;
    int32 MaxBounces = MaxTransportBounces;
    float MinThroughput = .01f;
    int32 TerminatedLowThroughput[MaxTransportBounces] = {};
    FString QualityReport() const;
    bool Save(const FString& Path) const;
    static TSharedPtr<FMultiBounceData, ESPMode::ThreadSafe> Load(const FString& Path, uint64 Signature, float CellSize, uint32 BaseCRC, float ExpectedMinThroughput=-1);
    void EvaluateCPU(const FVector3f& Sun, const FVector3f& Color, float Lux, bool Shadows, float Bias,
        int32 Bounces, TArray<FVector4f>& Output) const;
    void EvaluateUnclusteredCPU(const FSurfelVolume& Base,const FVector3f& Sun,const FVector3f& Color,
        float Lux,bool Shadows,float Bias,TArray<FVector4f>& Output,const FString& DebugPath=FString()) const;
};

struct FMultiBounceBuild
{
    float MinThroughput = .01f;
    TAtomic<bool> Cancelled{false};
    TAtomic<int32> CompletedProbes{0};
    TFuture<TSharedPtr<FMultiBounceData, ESPMode::ThreadSafe>> Future;
};
TSharedPtr<FMultiBounceBuild, ESPMode::ThreadSafe> BuildMultiBounce(UWorld* World,
    TSharedPtr<FSurfelVolume> Base, float CellSize, float MinThroughput=.01f);

class FMultiBounceGPU
{
public:
    FMultiBounceGPU();
    ~FMultiBounceGPU();
    bool IsPending() const;
    void Submit(TSharedPtr<FMultiBounceData, ESPMode::ThreadSafe> Data, FVector3f Sun, FVector3f Color,
        float Lux, bool Shadows, float Bias, int32 Bounces, bool Readback);
    bool Poll(TArray<FVector4f>& Output,TSharedPtr<const FPRTGIGPUCoefficients,ESPMode::ThreadSafe>& GPUOutput);
private:
    struct FState;
    TSharedPtr<FState, ESPMode::ThreadSafe> State;
};
}
