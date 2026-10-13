#include "PRTGIMultiBounce.h"
#include "PRTGIMaterialAtlas.h"
#include "PRTProbeVolume.h"
#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Algo/Sort.h"
#include "Math/Float16.h"
#include "Misc/Crc.h"

DEFINE_LOG_CATEGORY_STATIC(LogPRTMulti, Log, All);
namespace PRTGI
{
FVector3f FMultiBounceTerm::Weight(int32 Coefficient) const
{
    FVector3f Result;
    for(int C=0;C<3;++C){const int I=Coefficient*3+C;FFloat16 H;H.Encoded=uint16(Packed[I/2]>>((I%2)*16));Result[C]=float(H);}
    return Result;
}
uint32 MultiBounceBaseCRC(const FSurfelVolume& Base)
{
    uint32 CRC=FCrc::MemCrc32(Base.Validity.GetData(),Base.Validity.Num()*sizeof(float));
    return FCrc::MemCrc32(Base.Samples.GetData(),Base.Samples.Num()*sizeof(FSurfelSample),CRC);
}
namespace
{
constexpr uint32 MultiMagic = 0x34424D50; // PMB4, companion file; never replaces legacy geometry.
constexpr int32 MultiVersion = 6;
constexpr int32 MaxTerms = 16000000; // 1024 MB packed transport budget, never silently coarsen.
constexpr int64 MaxCacheBytes = int64(MaxTerms)*sizeof(FMultiBounceTerm)+128*1024*1024;
struct FTriangle
{
    FVector3f A, B, C; bool Shadow = true; uint32 SurfaceGroup = 0;
    TSharedPtr<const FMaterialAtlas,ESPMode::ThreadSafe> Material;
    int32 MaterialTriangle=INDEX_NONE;
    FVector3f Normal[3]={},Tangent[3]={},Bitangent[3]={};
};

bool HitMaterial(const FTriangle& T,FVector3f P,FVector3f Facing,FVector3f& Rho,FVector3f& Normal)
{
    if(!T.Material)return false;
    const FVector3f E1=T.B-T.A,E2=T.C-T.A,D=P-T.A;
    // Cross products avoid cancellation in A*C-B*B on long thin triangles.
    const FVector3f Cross=FVector3f::CrossProduct(E1,E2);
    const float Det=Cross.SizeSquared();if(Det<=1.e-12f)return false;
    const float V=FVector3f::DotProduct(FVector3f::CrossProduct(D,E2),Cross)/Det;
    const float W=FVector3f::DotProduct(FVector3f::CrossProduct(E1,D),Cross)/Det;
    const FVector3f Bary(1-V-W,V,W);FVector3f Local;
    if(!T.Material->Sample(T.MaterialTriangle,Bary,Rho,Local))return false;
    FVector3f N(0),TX(0),TY(0);
    for(int32 I=0;I<3;++I){N+=T.Normal[I]*Bary[I];TX+=T.Tangent[I]*Bary[I];TY+=T.Bitangent[I]*Bary[I];}
    N.Normalize();TX=(TX-N*FVector3f::DotProduct(TX,N)).GetSafeNormal();
    TY=FVector3f::CrossProduct(N,TX)*(FVector3f::DotProduct(FVector3f::CrossProduct(N,TX),TY)<0?-1.f:1.f);
    Normal=(TX*Local.X+TY*Local.Y+N*Local.Z).GetSafeNormal();
    if(FVector3f::DotProduct(Normal,Facing)<0)Normal=-Normal;
    return Normal.SizeSquared()>.5f;
}
struct FNode { FVector3f Min, Max; int32 First=-1, Count=0, Escape=0; };

bool BoxHit(FVector3f O, FVector3f D, FVector3f Min, FVector3f Max, float Limit)
{
    float Lo=0, Hi=Limit;
    for(int Axis=0;Axis<3;++Axis)
    {
        if(FMath::Abs(D[Axis])<1.e-8f) { if(O[Axis]<Min[Axis] || O[Axis]>Max[Axis]) return false; }
        else { float A=(Min[Axis]-O[Axis])/D[Axis],B=(Max[Axis]-O[Axis])/D[Axis];
            Lo=FMath::Max(Lo,FMath::Min(A,B)); Hi=FMath::Min(Hi,FMath::Max(A,B)); }
    }
    return Hi>=Lo;
}


bool TriangleHit(FVector3f O,FVector3f D,FVector3f A,FVector3f E1,FVector3f E2,float& Distance)
{
    const FVector3f P=FVector3f::CrossProduct(D,E2); const float Det=FVector3f::DotProduct(E1,P);
    if(FMath::Abs(Det)<1.e-8f) return false;
    const float Inv=1.f/Det; const FVector3f T=O-A;
    const float U=FVector3f::DotProduct(T,P)*Inv; if(U<0 || U>1) return false;
    const FVector3f Q=FVector3f::CrossProduct(T,E1);
    const float V=FVector3f::DotProduct(D,Q)*Inv; if(V<0 || U+V>1) return false;
    const float Hit=FVector3f::DotProduct(E2,Q)*Inv;
    if(Hit<.001f || Hit>=Distance) return false;
    Distance=Hit; return true;
}
int32 Trace(const FMultiBounceData& Data,FVector3f O,FVector3f D,float& Limit,bool ShadowOnly)
{
    int32 Hit=-1;
    for(int32 N=0;N<Data.Nodes.Num()/2;)
    {
        const auto Min=Data.Nodes[N*2],Max=Data.Nodes[N*2+1];
        if(!BoxHit(O,D,FVector3f(Min),FVector3f(Max),Limit)) { N=Min.W<0?int32(Max.W):N+1; continue; }
        if(Min.W<0) { ++N; continue; }
        for(int32 T=int32(Min.W);T<int32(Min.W+Max.W);++T)
        {
            if(ShadowOnly && Data.Triangles[T*3].W==0) continue;
            if(TriangleHit(O,D,FVector3f(Data.Triangles[T*3]),FVector3f(Data.Triangles[T*3+1]),FVector3f(Data.Triangles[T*3+2]),Limit))
            { Hit=T; if(ShadowOnly) return Hit; }
        }
        ++N;
    }
    return Hit;
}
float SurfaceOffset(FVector3f Position, FVector3f E1, FVector3f E2)
{
    // Arithmetic error estimate for our float BVH and reconstructed world positions.
    // Keep this separate from capture displacement, which is corrected by projection.
    return FMath::Max(.002f, 4.f * FLT_EPSILON *
        (Position.GetAbsMax() + E1.GetAbsMax() + E2.GetAbsMax()));
}
FVector3f TriangleNormal(const FMultiBounceData& Data, int32 Triangle, FVector3f Facing)
{
    auto N=FVector3f::CrossProduct(FVector3f(Data.Triangles[Triangle*3+1]),FVector3f(Data.Triangles[Triangle*3+2])).GetSafeNormal();
    return FVector3f::DotProduct(N,Facing)<0 ? -N : N;
}
FVector3f SurfacePosition(const FMultiBounceData& Data,const FSurfelVolume& Base,const FSurfelSample& S,int32* OutTriangle=nullptr)
{
    if(OutTriangle)*OutTriangle=INDEX_NONE;
    if(Base.GridCounts.GetMin()<2)return S.Position; // analytic fixtures need no capture grid
    const FVector3f Origin(Base.ProbePosition(S.Probe)),Delta=S.Position-Origin;
    const float CapturedDistance=Delta.Size();if(CapturedDistance<.001f)return S.Position;
    const FVector3f Ray=Delta/CapturedDistance;
    // View-depth reconstruction error is magnified at grazing angles. Limit normal
    // displacement, then intersect the original probe ray so material/texel stays aligned.
    const float Tolerance=FMath::Min(20.f,.5f/FMath::Max(FMath::Abs(FVector3f::DotProduct(S.Normal,Ray)),.025f));
    float Distance=CapturedDistance+Tolerance;
    const int Hit=Trace(Data,Origin,Ray,Distance,false);if(Hit<0 || FMath::Abs(Distance-CapturedDistance)>Tolerance)return S.Position;
    const FVector3f N=FVector3f::CrossProduct(FVector3f(Data.Triangles[Hit*3+1]),FVector3f(Data.Triangles[Hit*3+2])).GetSafeNormal();
    // A shading normal can differ substantially from the geometric normal on a
    // smooth mesh. Depth/ray agreement binds the surface; shading is not topology.
    if(FMath::Abs(FVector3f::DotProduct(N,S.Normal))<(Data.CacheVersion>=4?.1f:.95f))return S.Position;
    if(OutTriangle)*OutTriangle=Hit;
    return Origin+Ray*Distance;
}
int32 BuildBVH(TArray<FTriangle>& Triangles,TArray<FNode>& Nodes,int32 First,int32 Count)
{
    const int32 Index=Nodes.AddDefaulted(); FNode Node;
    Node.Min=FVector3f(FLT_MAX); Node.Max=FVector3f(-FLT_MAX);
    for(int32 I=First;I<First+Count;++I) for(const auto V:{Triangles[I].A,Triangles[I].B,Triangles[I].C})
    { Node.Min=Node.Min.ComponentMin(V);Node.Max=Node.Max.ComponentMax(V); }
    Node.Min-=FVector3f(.001f);Node.Max+=FVector3f(.001f);
    if(Count<=8) { Node.First=First;Node.Count=Count; }
    else
    {
        const FVector3f E=Node.Max-Node.Min;
        const int Axis=E.X>E.Y?(E.X>E.Z?0:2):(E.Y>E.Z?1:2);
        Algo::Sort(MakeArrayView(Triangles.GetData()+First,Count),[Axis](const auto& A,const auto& B)
            {return A.A[Axis]+A.B[Axis]+A.C[Axis]<B.A[Axis]+B.B[Axis]+B.C[Axis];});
        BuildBVH(Triangles,Nodes,First,Count/2); BuildBVH(Triangles,Nodes,First+Count/2,Count-Count/2);
    }
    Node.Escape=Nodes.Num();Nodes[Index]=Node;return Index;
}
struct FSurfaceKey
{
    FIntVector Cell; uint32 Normal=0,Color=0; int32 Triangle=INDEX_NONE;
    bool operator==(const FSurfaceKey& B)const{return Cell==B.Cell && Normal==B.Normal && Color==B.Color && Triangle==B.Triangle;}
    friend uint32 GetTypeHash(const FSurfaceKey& K){return HashCombine(HashCombine(GetTypeHash(K.Cell),HashCombine(K.Normal,K.Color)),GetTypeHash(K.Triangle));}
};
FIntVector CellAt(FVector3f P,float Size){return FIntVector(FMath::FloorToInt(P.X/Size),FMath::FloorToInt(P.Y/Size),FMath::FloorToInt(P.Z/Size));}
uint32 Pack(FVector3f V,float Scale){return uint32(FMath::RoundToInt(V.X*Scale)) | (uint32(FMath::RoundToInt(V.Y*Scale))<<8) | (uint32(FMath::RoundToInt(V.Z*Scale))<<16);}
uint32 HashRandom(uint32 X){X^=X>>16;X*=0x7feb352d;X^=X>>15;X*=0x846ca68b;return X^(X>>16);}
FVector3f CosineDirection(FVector3f N,uint32 Seed)
{
    const float U=(float(HashRandom(Seed)&0xffffff)+.5f)/16777216.f;
    const float V=(float(HashRandom(Seed^0x9e3779b9)&0xffffff)+.5f)/16777216.f;
    const FVector3f T=FVector3f::CrossProduct(FMath::Abs(N.Z)<.99f?FVector3f(0,0,1):FVector3f(0,1,0),N).GetSafeNormal();
    const FVector3f B=FVector3f::CrossProduct(N,T);
    return T*(FMath::Sqrt(U)*FMath::Cos(2*UE_PI*V))+B*(FMath::Sqrt(U)*FMath::Sin(2*UE_PI*V))+N*FMath::Sqrt(1-U);
}
struct FWeights { FVector3f C[9]={}; };

TSharedPtr<FMultiBounceData,ESPMode::ThreadSafe> Build(TSharedPtr<FSurfelVolume,ESPMode::ThreadSafe> Base,TArray<FTriangle> Geometry,
    float CellSize,TSharedPtr<FMultiBounceBuild,ESPMode::ThreadSafe> Job)
{
    const double Start=FPlatformTime::Seconds();
    auto Data=MakeShared<FMultiBounceData,ESPMode::ThreadSafe>();
    Data->Signature=Base->SceneSignature;Data->BaseCRC=MultiBounceBaseCRC(*Base);Data->CellSize=CellSize;Data->ProbeCount=Base->Validity.Num();Data->Validity=Base->Validity;
    if(Geometry.IsEmpty()){Data->Error=TEXT("No supported opaque static geometry");return Data;}
    TArray<FNode> BVH;BuildBVH(Geometry,BVH,0,Geometry.Num());
    for(const auto& T:Geometry){Data->Triangles.Add(FVector4f(T.A,T.Shadow?1:0));Data->Triangles.Add(FVector4f(T.B-T.A,0));Data->Triangles.Add(FVector4f(T.C-T.A,0));}
    for(const auto& N:BVH){Data->Nodes.Add(FVector4f(N.Min,float(N.First)));Data->Nodes.Add(FVector4f(N.Max,float(N.First<0?N.Escape:N.Count)));}
    BVH.Reset();
    TMap<FSurfaceKey,int32> Clusters; TArray<int32> SourceForSample,Counts,SourceTriangles,SampleTriangles;
    TArray<FVector3d> PositionSums;
    TArray<FVector3f> Positions;Positions.Reserve(Base->Samples.Num());
    for(const auto& S:Base->Samples)
    {
        if(Job->Cancelled.Load()){Data->Error=TEXT("Cancelled");return Data;}
        int32 Triangle=INDEX_NONE;
        const FVector3f Position=SurfacePosition(*Data,*Base,S,&Triangle);Positions.Add(Position);SampleTriangles.Add(Triangle);
        if(Triangle>=0)++Data->BoundSamples;else ++Data->UnboundSamples;
        const float Correction=(Position-S.Position).Size();
        if(Correction>.001f)++Data->CorrectedSamples;
        Data->MaxPositionCorrectionCm=FMath::Max(Data->MaxPositionCorrectionCm,Correction);
        FSurfaceKey Key{CellAt(Position,CellSize),Pack(S.Normal+FVector3f(1),32),Pack(S.Albedo,63),Triangle};
        int32* Found=Clusters.Find(Key);int32 Source;
        if(!Found){Source=Counts.Add(0);Clusters.Add(Key,Source);Data->Surfaces.AddZeroed(3);SourceTriangles.Add(Triangle);PositionSums.Add(FVector3d(0));Data->SourceGeometry.Add(FVector4f(0));}else Source=*Found;
        SourceForSample.Add(Source);++Counts[Source];
        PositionSums[Source]+=FVector3d(Position);Data->Surfaces[Source*3+1]+=FVector4f(S.Normal,0);Data->Surfaces[Source*3+2]+=FVector4f(S.Albedo,0);
        if(Triangle>=0)
            Data->SourceGeometry[Source]=FVector4f(TriangleNormal(*Data,Triangle,S.Normal),
                FMath::Max(Data->SourceGeometry[Source].W,SurfaceOffset(Position,FVector3f(Data->Triangles[Triangle*3+1]),FVector3f(Data->Triangles[Triangle*3+2]))));
    }
    // Material lookup coverage must not shrink when direct-light shading cells get finer.
    const float LookupCell=FMath::Max(CellSize,10.f);
    TMap<FIntVector,TArray<int32>> Spatial;
    for(int32 S=0;S<Counts.Num();++S)
    {
        Data->Surfaces[S*3]=FVector4f(FVector3f(PositionSums[S]/double(Counts[S])),0);Data->Surfaces[S*3+1]=FVector4f(FVector3f(Data->Surfaces[S*3+1]).GetSafeNormal(),0);
        Data->Surfaces[S*3+2]/=float(Counts[S]);Spatial.FindOrAdd(CellAt(FVector3f(Data->Surfaces[S*3]),LookupCell)).Add(S);
    }
    auto FindSurface=[&](FVector3f P,FVector3f N,int32 HitTriangle)->int32
    {
        FVector3f Rho,ShadingNormal;
        if(HitMaterial(Geometry[HitTriangle],P,N,Rho,ShadingNormal))
        {
            ++Data->MaterialHits;
            FSurfaceKey Key{CellAt(P,CellSize),Pack(ShadingNormal+FVector3f(1),32),Pack(Rho,63),HitTriangle};
            if(const int32* Existing=Clusters.Find(Key))return *Existing;
            const int32 Source=Data->Surfaces.Num()/3;
            Clusters.Add(Key,Source);
            Data->Surfaces.Add(FVector4f(P,0));Data->Surfaces.Add(FVector4f(ShadingNormal,0));Data->Surfaces.Add(FVector4f(Rho,0));
            Data->SourceGeometry.Add(FVector4f(N,SurfaceOffset(P,Geometry[HitTriangle].B-Geometry[HitTriangle].A,Geometry[HitTriangle].C-Geometry[HitTriangle].A)));
            return Source;
        }
        const FIntVector Cell=CellAt(P,LookupCell);float Best=LookupCell*LookupCell*4.f;int32 Found=-1;
        for(int Z=-2;Z<=2;++Z)for(int Y=-2;Y<=2;++Y)for(int X=-2;X<=2;++X)
            if(const auto* List=Spatial.Find(Cell+FIntVector(X,Y,Z)))for(int S:*List)
            {
                const int32 SourceTriangle=SourceTriangles[S];
                if(SourceTriangle>=0 && Geometry[SourceTriangle].SurfaceGroup!=Geometry[HitTriangle].SurfaceGroup)continue;
                // Never match a production hit to an unbound capture. Analytic
                // fixtures without a capture grid retain the reference lookup.
                if(SourceTriangle<0 && Base->GridCounts.GetMin()>=2)continue;
                const FVector3f SN=SourceTriangle>=0?FVector3f(Data->SourceGeometry[S]):FVector3f(Data->Surfaces[S*3+1]);
                const FVector3f Delta=FVector3f(Data->Surfaces[S*3])-P;
                const float D=Delta.SizeSquared();
                // Do not borrow a material from the other side of a wall or another plane.
                if(D<Best && FVector3f::DotProduct(SN,N)>.97f && FMath::Abs(FVector3f::DotProduct(Delta,N))<.5f)
                {Best=D;Found=S;}
            }
        return Found;
    };
    int32 SampleIndex=0;double QuantizationDelta=0,QuantizationEnergy=0;
    for(int32 P=0;P<Data->ProbeCount;++P)
    {
        if(Job->Cancelled.Load()){Data->Error=TEXT("Cancelled");return Data;}
        TMap<uint64,FWeights> Weights;
        while(SampleIndex<Base->Samples.Num() && Base->Samples[SampleIndex].Probe==P)
        {
            const auto& Sample=Base->Samples[SampleIndex];int32 Source=SourceForSample[SampleIndex];
            FVector3f Position=Positions[SampleIndex],Normal=Sample.Normal,Throughput(1);
            int32 CurrentTriangle=SampleTriangles[SampleIndex];
            for(int32 Bounce=0;Bounce<4;++Bounce)
            {
                ++Data->Paths[Bounce];auto& W=Weights.FindOrAdd((uint64(Bounce)<<32)|uint32(Source));
                for(int C=0;C<9;++C)W.C[C]+=Throughput*Sample.Weights[C];
                if(Bounce==3)break;
                // Cosine importance sampling cancels cos(theta)/pi: only diffuse reflectance remains.
                Throughput*=FVector3f(Data->Surfaces[Source*3+2]);
                if(Throughput.GetMax()<1.e-7f)break;
                const FVector3f D=CosineDirection(Normal,HashRandom(uint32(SampleIndex))^uint32(0xa511e9b3*(Bounce+1)));
                const FVector3f GeometryNormal=CurrentTriangle>=0?TriangleNormal(*Data,CurrentTriangle,Normal):Normal;
                if(FVector3f::DotProduct(D,GeometryNormal)<=0)break;
                const float Offset=CurrentTriangle>=0?SurfaceOffset(Position,FVector3f(Data->Triangles[CurrentTriangle*3+1]),FVector3f(Data->Triangles[CurrentTriangle*3+2])):.05f;
                const FVector3f O=Position+GeometryNormal*Offset;float Distance=1.e7f;
                const int32 Hit=Trace(*Data,O,D,Distance,false);if(Hit<0)break;
                Position=O+D*Distance;
                Normal=FVector3f::CrossProduct(FVector3f(Data->Triangles[Hit*3+1]),FVector3f(Data->Triangles[Hit*3+2])).GetSafeNormal();
                // Render meshes can have reversed winding. Lookup uses the facing hemisphere,
                // and still requires a matching captured front-face material sample.
                if(FVector3f::DotProduct(Normal,-D)<0)Normal=-Normal;
                ++Data->SurfaceHits;Source=FindSurface(Position,Normal,Hit);
                if(Source<0)
                {
                    if(Data->UnmappedHits<12)UE_LOG(LogPRTMulti,Display,TEXT("Unmapped secondary surface P=%s N=%s"),*Position.ToString(),*Normal.ToString());
                    ++Data->UnmappedHits;++Data->UnmappedByBounce[Bounce];break;
                }
                CurrentTriangle=Hit;
                // Keep the captured material shading normal for lighting/scattering.
                Normal=FVector3f(Data->Surfaces[Source*3+1]);
            }
            ++SampleIndex;
        }
        const uint32 First=Data->Terms.Num();TArray<uint64> Keys;Weights.GetKeys(Keys);Keys.Sort();
        if(Data->Terms.Num()+int64(Keys.Num())>MaxTerms)
        {
            Data->Error=FString::Printf(TEXT("Transport budget exceeded at probe %d/%d: %lld terms > %d (%.1f MiB packed limit); Transport Cell Size %.2f cm. Increase cell size or split the volume."),
                P+1,Data->ProbeCount,int64(Data->Terms.Num())+Keys.Num(),MaxTerms,double(MaxTerms)*sizeof(FMultiBounceTerm)/1048576,CellSize);
            return Data;
        }
        for(uint64 Key:Keys)
        {
            const auto& W=Weights[Key];
            FMultiBounceTerm Term;Term.Source=uint32(Key);Term.Bounce=uint32((Key>>32)+1);
            for(int C=0;C<9;++C)for(int Channel=0;Channel<3;++Channel)
            {
                const int I=C*3+Channel;const float Value=W.C[C][Channel];const FFloat16 H(Value);
                Term.Packed[I/2]|=uint32(H.Encoded)<<((I%2)*16);
                QuantizationDelta+=FMath::Square(double(float(H))-Value);QuantizationEnergy+=double(Value)*Value;
            }
            Data->Terms.Add(Term);
        }
        Data->Ranges.Add(FUintVector2(First,Data->Terms.Num()));Job->CompletedProbes.Store(P+1);
    }
    Data->BuildSeconds=FPlatformTime::Seconds()-Start;
    Data->QuantizationRelativeError=FMath::Sqrt(QuantizationDelta/FMath::Max(QuantizationEnergy,1.e-30));
    if(Data->QuantizationRelativeError>.001f){Data->Error=TEXT("FP16 transport error exceeds 0.1%; cache rejected");return Data;}
    UE_LOG(LogPRTMulti,Display,TEXT("Precomputed 1..4 bounce paths: probes=%d sources=%d terms=%d paths=%d/%d/%d/%d missing=%d/%d seconds=%.3f"),
        Data->ProbeCount,Data->Surfaces.Num()/3,Data->Terms.Num(),Data->Paths[0],Data->Paths[1],Data->Paths[2],Data->Paths[3],Data->UnmappedHits,Data->SurfaceHits,Data->BuildSeconds);
    UE_LOG(LogPRTMulti,Display,TEXT("Reprojected %d captured samples onto static triangles; max ray displacement %.4f cm"),Data->CorrectedSamples,Data->MaxPositionCorrectionCm);
    return Data;
}
}

TSharedPtr<FMultiBounceBuild,ESPMode::ThreadSafe> BuildMultiBounce(UWorld* World,TSharedPtr<FSurfelVolume> Base,float CellSize)
{
    auto Job=MakeShared<FMultiBounceBuild,ESPMode::ThreadSafe>();TArray<FTriangle> Geometry;uint32 SurfaceGroup=0;
    for(TActorIterator<AActor> It(World);It;++It)
    {
        if(It->IsA<APRTProbeVolume>())continue;
        TInlineComponentArray<UStaticMeshComponent*> Components(*It);
        for(auto* C:Components)
        {
            if(!C->IsVisible() || C->Mobility!=EComponentMobility::Static || !C->GetStaticMesh())continue;
            const auto* Render=C->GetStaticMesh()->GetRenderData();if(!Render || Render->LODResources.IsEmpty())continue;
            const auto& LOD=Render->LODResources[0];const auto Indices=LOD.IndexBuffer.GetArrayView();
            TArray<FTransform> Transforms;
            if(auto* ISM=Cast<UInstancedStaticMeshComponent>(C))
            {for(int I=0;I<ISM->GetInstanceCount();++I){FTransform T;ISM->GetInstanceTransform(I,T,true);Transforms.Add(T);}}
            else Transforms.Add(C->GetComponentTransform());
            for(const auto& T:Transforms)for(const auto& Section:LOD.Sections)
            {
                ++SurfaceGroup; // distinct instance/material section, including shared materials
                auto* Material=C->GetMaterial(Section.MaterialIndex);
                if(Material && Material->GetBlendMode()!=BLEND_Opaque)
                {
                    UE_LOG(LogPRTMulti,Warning,TEXT("Multi-bounce static BVH excludes non-opaque material %s on %s; masked/translucent transport is not supported"),*Material->GetPathName(),*C->GetPathName());
                    continue;
                }
                FString MaterialError;
                const auto Atlas=BakeMaterialAtlas(C,LOD,Section,T,MaterialError);
                if(!Atlas)
                {
                    auto Failed=MakeShared<FMultiBounceData,ESPMode::ThreadSafe>();
                    Failed->Error=FString::Printf(TEXT("Material chart failed on %s: %s"),*C->GetPathName(),*MaterialError);
                    Job->Future=Async(EAsyncExecution::ThreadPool,[Failed]()->TSharedPtr<FMultiBounceData,ESPMode::ThreadSafe>{return Failed;});return Job;
                }
                const FMatrix NormalTransform=T.ToMatrixWithScale().Inverse().GetTransposed();
                for(uint32 I=Section.FirstIndex;I<Section.FirstIndex+Section.NumTriangles*3;I+=3)
                {
                    FTriangle Tri;Tri.A=FVector3f(T.TransformPosition(FVector(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I]))));
                    Tri.B=FVector3f(T.TransformPosition(FVector(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I+1]))));
                    Tri.C=FVector3f(T.TransformPosition(FVector(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I+2]))));
                    Tri.Shadow=C->CastShadow && Section.bCastShadow;
                    Tri.SurfaceGroup=SurfaceGroup;
                    Tri.Material=Atlas;Tri.MaterialTriangle=(I-Section.FirstIndex)/3;
                    for(int32 V=0;V<3;++V)
                    {
                        const int32 Index=Indices[I+V];
                        Tri.Normal[V]=FVector3f(FVector(NormalTransform.TransformVector(FVector(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(Index))))).GetSafeNormal();
                        Tri.Tangent[V]=FVector3f(T.TransformVector(FVector(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(Index)))).GetSafeNormal();
                        Tri.Bitangent[V]=FVector3f(T.TransformVector(FVector(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentY(Index)))).GetSafeNormal();
                    }
                    if(FVector3f::CrossProduct(Tri.B-Tri.A,Tri.C-Tri.A).SizeSquared()>1.e-8f)Geometry.Add(Tri);
                }
            }
        }
    }
    // The manager's legacy shared pointer is not thread-safe and may be released on
    // world change/cancel. The worker owns a separate immutable, UObject-free snapshot.
    auto Snapshot=MakeShared<FSurfelVolume,ESPMode::ThreadSafe>();
    Snapshot->SceneSignature=Base->SceneSignature;Snapshot->GridCounts=Base->GridCounts;
    Snapshot->GridMin=Base->GridMin;Snapshot->GridMax=Base->GridMax;
    Snapshot->Validity=Base->Validity;Snapshot->Samples=Base->Samples;
    Job->Future=Async(EAsyncExecution::ThreadPool,[Snapshot,Geometry=MoveTemp(Geometry),CellSize,Job]()mutable{return Build(Snapshot,MoveTemp(Geometry),CellSize,Job);});
    return Job;
}

bool FMultiBounceData::Save(const FString& Path)const
{
    if(!Error.IsEmpty() || Ranges.Num()!=ProbeCount || Terms.IsEmpty() || Terms.Num()>MaxTerms)return false;
    if(!SourceGeometry.IsEmpty() && SourceGeometry.Num()!=Surfaces.Num()/3)return false;
    if(CacheVersion<3 || CacheVersion>MultiVersion)return false;
    // A loaded v3 cache has no per-bounce counters or bound geometry. Preserve
    // its version on resave instead of inventing incomplete v4 metadata.
    TArray<uint8> Bytes;FMemoryWriter Ar(Bytes);uint32 M=MultiMagic;int32 V=CacheVersion;Ar<<M<<V;
    auto Sig=Signature;auto Cell=CellSize;auto P=ProbeCount;auto CRC=BaseCRC;Ar<<Sig<<Cell<<P<<CRC;
    for(const auto* Array:{&Surfaces,&Triangles,&Nodes}){int32 N=Array->Num();Ar<<N;Ar.Serialize(const_cast<FVector4f*>(Array->GetData()),int64(N)*sizeof(FVector4f));}
    int32 TermCount=Terms.Num();Ar<<TermCount;Ar.Serialize(const_cast<FMultiBounceTerm*>(Terms.GetData()),int64(TermCount)*sizeof(FMultiBounceTerm));
    Ar.Serialize(const_cast<FUintVector2*>(Ranges.GetData()),int64(ProbeCount)*sizeof(FUintVector2));
    Ar.Serialize(const_cast<float*>(Validity.GetData()),int64(ProbeCount)*sizeof(float));
    Ar.Serialize(const_cast<int32*>(Paths),sizeof(Paths));auto Missing=UnmappedHits,Hits=SurfaceHits;Ar<<Missing<<Hits;
    auto Seconds=BuildSeconds,Quantization=QuantizationRelativeError;Ar<<Seconds<<Quantization;
    auto Corrected=CorrectedSamples;auto MaxCorrection=MaxPositionCorrectionCm;Ar<<Corrected<<MaxCorrection;
    if(V>=4)
    {
        for(int32 S=0;S<Surfaces.Num()/3;++S){FVector4f G=SourceGeometry.IsValidIndex(S)?SourceGeometry[S]:FVector4f(0);Ar<<G;}
        auto Bound=BoundSamples,Unbound=UnboundSamples;Ar<<Bound<<Unbound;
        Ar.Serialize(const_cast<int32*>(UnmappedByBounce),sizeof(UnmappedByBounce));
    }
    if(V>=5){auto Count=MaterialHits;Ar<<Count;}
    uint32 PayloadCRC=FCrc::MemCrc32(Bytes.GetData(),Bytes.Num());Ar<<PayloadCRC;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    return !Ar.IsError() && Bytes.Num()<=MaxCacheBytes && FFileHelper::SaveArrayToFile(Bytes,*(Path+TEXT(".tmp"))) && IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true,true);
}
TSharedPtr<FMultiBounceData,ESPMode::ThreadSafe> FMultiBounceData::Load(const FString& Path,uint64 Expected,float Cell,uint32 ExpectedBaseCRC)
{
    const int64 Size=IFileManager::Get().FileSize(*Path);if(Size<64 || Size>MaxCacheBytes)return nullptr;
    TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return nullptr;
    uint32 PayloadCRC=0;FMemory::Memcpy(&PayloadCRC,Bytes.GetData()+Bytes.Num()-4,4);
    if(PayloadCRC!=FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()-4))return nullptr;
    FMemoryReader Ar(Bytes);uint32 M;int32 V;Ar<<M<<V;auto Data=MakeShared<FMultiBounceData,ESPMode::ThreadSafe>();
    Ar<<Data->Signature<<Data->CellSize<<Data->ProbeCount<<Data->BaseCRC;
    if(M!=MultiMagic || V<3 || V>MultiVersion || Expected!=Data->Signature || Cell!=Data->CellSize || ExpectedBaseCRC!=Data->BaseCRC || Data->ProbeCount<1 || Data->ProbeCount>32768)return nullptr;
    Data->CacheVersion=V;
    for(auto* Array:{&Data->Surfaces,&Data->Triangles,&Data->Nodes})
    {
        int32 N=0;Ar<<N;if(Ar.IsError() || N<1 || N>MaxTerms || int64(N)*16>Ar.TotalSize()-Ar.Tell())return nullptr;
        Array->SetNumUninitialized(N);Ar.Serialize(Array->GetData(),int64(N)*16);
        for(const auto& F:*Array)if(!FMath::IsFinite(F.X)||!FMath::IsFinite(F.Y)||!FMath::IsFinite(F.Z)||!FMath::IsFinite(F.W))return nullptr;
    }
    int32 TermCount=0;Ar<<TermCount;
    if(Ar.IsError() || TermCount<1 || TermCount>MaxTerms || int64(TermCount)*64>Ar.TotalSize()-Ar.Tell())return nullptr;
    Data->Terms.SetNumUninitialized(TermCount);Ar.Serialize(Data->Terms.GetData(),int64(TermCount)*64);
    if(Data->Surfaces.Num()%3 || Data->Triangles.Num()%3 || Data->Nodes.Num()%2)return nullptr;
    const int64 Tail=int64(Data->ProbeCount)*12+sizeof(Data->Paths)+8+8+8+8+4
        +(V>=4?int64(Data->Surfaces.Num()/3)*16+20:0)+(V>=5?4:0);if(Ar.TotalSize()-Ar.Tell()!=Tail)return nullptr;
    Data->Ranges.SetNum(Data->ProbeCount);Ar.Serialize(Data->Ranges.GetData(),int64(Data->ProbeCount)*8);
    Data->Validity.SetNum(Data->ProbeCount);Ar.Serialize(Data->Validity.GetData(),int64(Data->ProbeCount)*4);
    Ar.Serialize(Data->Paths,sizeof(Data->Paths));Ar<<Data->UnmappedHits<<Data->SurfaceHits<<Data->BuildSeconds<<Data->QuantizationRelativeError;
    Ar<<Data->CorrectedSamples<<Data->MaxPositionCorrectionCm;
    if(V>=4)
    {
        Data->SourceGeometry.SetNum(Data->Surfaces.Num()/3);
        for(auto& G:Data->SourceGeometry)
        {
            Ar<<G;
            const float Length=FVector3f(G).SizeSquared();
            if(G.ContainsNaN() || G.W<0 || (Length>0 && (!FMath::IsNearlyEqual(Length,1.f,.01f) || G.W<=0)))return nullptr;
        }
        Ar<<Data->BoundSamples<<Data->UnboundSamples;
        Ar.Serialize(Data->UnmappedByBounce,sizeof(Data->UnmappedByBounce));
        if(Data->BoundSamples<0 || Data->UnboundSamples<0)return nullptr;
        int64 Missing=0;for(int32 Count:Data->UnmappedByBounce){if(Count<0)return nullptr;Missing+=Count;}
        if(Missing!=Data->UnmappedHits)return nullptr;
    }
    if(V>=5){Ar<<Data->MaterialHits;if(Data->MaterialHits<0 || Data->MaterialHits>Data->SurfaceHits)return nullptr;}
    uint32 End=0;for(const auto R:Data->Ranges){if(R.X!=End||R.Y<R.X||R.Y>uint32(Data->Terms.Num()))return nullptr;End=R.Y;}
    if(End!=uint32(Data->Terms.Num()))return nullptr;
    for(const auto& T:Data->Terms)
    {
        if(T.Source>=uint32(Data->Surfaces.Num()/3)||T.Bounce<1||T.Bounce>4)return nullptr;
        for(int C=0;C<9;++C)if(T.Weight(C).ContainsNaN())return nullptr;
    }
    for(int I=0;I<Data->Nodes.Num();I+=2){const float A=Data->Nodes[I].W,B=Data->Nodes[I+1].W;
        if(A!=FMath::FloorToFloat(A)||B!=FMath::FloorToFloat(B))return nullptr;
        if(A<0?(A!=-1||B<=I/2||B>Data->Nodes.Num()/2):(A+B>Data->Triangles.Num()/3||B<1))return nullptr;}
    for(float Vld:Data->Validity)if(Vld!=1 && Vld!=-1)return nullptr;
    if(!FMath::IsFinite(Data->BuildSeconds)||!FMath::IsFinite(Data->QuantizationRelativeError)||Data->QuantizationRelativeError>.001f)return nullptr;
    if(Data->CorrectedSamples<0||!FMath::IsFinite(Data->MaxPositionCorrectionCm)||Data->MaxPositionCorrectionCm<0)return nullptr;
    if(Ar.IsError())return nullptr; return Data;
}
void FMultiBounceData::EvaluateCPU(const FVector3f& Sun,const FVector3f& Color,float Lux,bool Shadows,float Bias,int32 Bounces,TArray<FVector4f>& Output)const
{
    TArray<FVector3f> Radiance;Radiance.SetNum(Surfaces.Num()/3);
    for(int S=0;S<Radiance.Num();++S)
    {
        const FVector3f P(Surfaces[S*3]),N(Surfaces[S*3+1]),Rho(Surfaces[S*3+2]);float Dist=1.e7f;
        const FVector4f G=SourceGeometry.IsValidIndex(S)?SourceGeometry[S]:FVector4f(0);
        const bool Bound=FVector3f(G).SizeSquared()>.5f;
        const FVector3f Ng=Bound?FVector3f(G):N;
        const float Offset=Bound?G.W:Bias;
        const float Cos=FVector3f::DotProduct(Ng,Sun)>0?FMath::Max(0.f,FVector3f::DotProduct(N,Sun)):0;
        Radiance[S]=(Shadows && Cos>0 && Trace(*this,P+Ng*Offset,Sun,Dist,true)>=0)?FVector3f(0):Rho*Color*(Lux*Cos/UE_PI);
    }
    Output.Init(FVector4f(0),ProbeCount*9);
    for(int P=0;P<ProbeCount;++P){for(uint32 T=Ranges[P].X;T<Ranges[P].Y;++T)
        if(Terms[T].Bounce<=uint32(Bounces))for(int C=0;C<9;++C)Output[P*9+C]+=FVector4f(Terms[T].Weight(C)*Radiance[Terms[T].Source],0);
        Output[P*9].W=Validity[P];}
}
void FMultiBounceData::EvaluateUnclusteredCPU(const FSurfelVolume& Base,const FVector3f& Sun,const FVector3f& Color,
    float Lux,bool Shadows,float Bias,TArray<FVector4f>& Output,const FString& DebugPath)const
{
    Output.Init(FVector4f(0),ProbeCount*9);
    FString Debug=TEXT("probe,px,py,pz,blocked,distance,hx,hy,hz,hnx,hny,hnz\n");
    for(const auto& S:Base.Samples)
    {
        int32 Triangle=INDEX_NONE;
        const FVector3f Position=SurfacePosition(*this,Base,S,&Triangle);
        const bool Bound=CacheVersion>=4 && Triangle>=0;
        const FVector3f Ng=Bound?TriangleNormal(*this,Triangle,S.Normal):S.Normal;
        const float Offset=Bound?SurfaceOffset(Position,FVector3f(Triangles[Triangle*3+1]),FVector3f(Triangles[Triangle*3+2])):Bias;
        const float Cos=FVector3f::DotProduct(Ng,Sun)>0?FMath::Max(0.f,FVector3f::DotProduct(S.Normal,Sun)):0;
        float Distance=1.e7f;
        const int32 Hit=Shadows && Cos>0 && Lux>0 ? Trace(*this,Position+Ng*Offset,Sun,Distance,true):-1;
        if(!DebugPath.IsEmpty() && (S.Probe==335 || S.Probe==383))
        {
            const FVector3f HP=Hit>=0?Position+Ng*Offset+Sun*Distance:FVector3f(0);
            const FVector3f HN=Hit>=0?FVector3f::CrossProduct(FVector3f(Triangles[Hit*3+1]),FVector3f(Triangles[Hit*3+2])).GetSafeNormal():FVector3f(0);
            Debug+=FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),S.Probe,S.Position.X,S.Position.Y,S.Position.Z,int(Hit>=0),Distance,HP.X,HP.Y,HP.Z,HN.X,HN.Y,HN.Z);
        }
        if(Cos<=0 || Hit>=0)continue;
        const FVector3f L=S.Albedo*Color*(Lux*Cos/UE_PI);
        for(int C=0;C<9;++C)Output[S.Probe*9+C]+=FVector4f(L*S.Weights[C],0);
    }
    for(int P=0;P<ProbeCount;++P)Output[P*9].W=Validity[P];
    if(!DebugPath.IsEmpty())FFileHelper::SaveStringToFile(Debug,*DebugPath);
}
FString FMultiBounceData::QualityReport()const
{
    const FString Bound=CacheVersion>=4?FString::FromInt(BoundSamples):TEXT("null");
    const FString Unbound=CacheVersion>=4?FString::FromInt(UnboundSamples):TEXT("null");
    const FString MissingByBounce=CacheVersion>=4?FString::Printf(TEXT("[%d,%d,%d]"),UnmappedByBounce[0],UnmappedByBounce[1],UnmappedByBounce[2]):TEXT("null");
    const FString Material=CacheVersion>=5?FString::FromInt(MaterialHits):TEXT("null");
    return FString::Printf(TEXT("{\"cache_version\":%d,\"probes\":%d,\"sources\":%d,\"terms\":%d,\"bound_samples\":%s,\"unbound_samples\":%s,\"surface_hits\":%d,\"unmapped_hits\":%d,\"unmapped_hit_fraction\":%.9g,\"unmapped_by_bounce\":%s,\"paths\":[%d,%d,%d,%d],\"max_capture_correction_cm\":%.9g,\"fp16_relative_error\":%.9g,\"build_seconds\":%.6f,\"material_hits\":%s}"),
        CacheVersion,ProbeCount,Surfaces.Num()/3,Terms.Num(),*Bound,*Unbound,SurfaceHits,UnmappedHits,
        double(UnmappedHits)/FMath::Max(1,SurfaceHits),*MissingByBounce,
        Paths[0],Paths[1],Paths[2],Paths[3],MaxPositionCorrectionCm,QuantizationRelativeError,BuildSeconds,*Material);
}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTMultiBouncePhysics,"PRTGI.MultiBounce.AnalyticTwoPlanes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPRTMultiBouncePhysics::RunTest(const FString&)
{
    using namespace PRTGI;
    auto Base=MakeShared<FSurfelVolume,ESPMode::ThreadSafe>();Base->SceneSignature=123456;Base->Validity={1};
    constexpr int N=4096;
    for(int I=0;I<N;++I){FSurfelSample S;S.Position=FVector3f(0);S.Normal=FVector3f(0,0,1);S.Albedo=FVector3f(.5f);S.Weights[0]=1.f/N;Base->Samples.Add(S);}
    FSurfelSample Ceiling;Ceiling.Position=FVector3f(0,0,1);Ceiling.Normal=FVector3f(0,0,-1);Ceiling.Albedo=FVector3f(.8f,.2f,.2f);Base->Samples.Add(Ceiling);
    TArray<FTriangle> Geometry;
    for(float Z:{0.f,1.f})
    {Geometry.Add({FVector3f(-10000,-10000,Z),FVector3f(10000,-10000,Z),FVector3f(10000,10000,Z)});
     Geometry.Add({FVector3f(-10000,-10000,Z),FVector3f(10000,10000,Z),FVector3f(-10000,10000,Z)});}
    auto Job=MakeShared<FMultiBounceBuild,ESPMode::ThreadSafe>();auto Data=Build(Base,MoveTemp(Geometry),100.f,Job);
    TestTrue(TEXT("Transport built"),Data->Error.IsEmpty());
    TArray<FVector4f> SH[4];for(int B=1;B<=4;++B)Data->EvaluateCPU(FVector3f(0,0,-1),FVector3f(1),UE_PI,false,.05f,B,SH[B-1]);
    // Only ceiling receives the injected light. Floor direct=0; second=.5f*rhoCeiling;
    // third adds zero, fourth adds (.5f*rhoCeiling)^2. Visibility is tested separately.
    TestTrue(TEXT("One indirect bounce cannot reach the unlit floor via ceiling"),FVector3f(SH[0][0]).Size()<1.e-5f);
    TestTrue(TEXT("Second bounce equals analytic diffuse transport"),FVector3f(SH[1][0]).Equals(FVector3f(.4f,.1f,.1f),.003f));
    TestTrue(TEXT("Third order has zero source injection in this setup"),FVector3f(SH[2][0]).Equals(FVector3f(SH[1][0]),1.e-5f));
    TestTrue(TEXT("Fourth order uses products of reflectance, not repeated total lighting"),FVector3f(SH[3][0]).Equals(FVector3f(.56f,.11f,.11f),.005f));
    float Limit=100;TestTrue(TEXT("Opaque ceiling blocks upward shadow ray"),Trace(*Data,FVector3f(0,0,.05f),FVector3f(0,0,1),Limit,true)>=0);
    Limit=100;TestTrue(TEXT("Ray parallel to both planes escapes"),Trace(*Data,FVector3f(0,0,.5f),FVector3f(1,0,0),Limit,true)<0);
    FSurfelVolume ProjectionBase;ProjectionBase.GridCounts=FIntVector(2);ProjectionBase.GridMin=FVector3f(0,0,.5f);ProjectionBase.GridMax=FVector3f(1,1,2);
    FSurfelSample Submerged;Submerged.Position=FVector3f(0,0,-.15f);Submerged.Normal=FVector3f(0,0,1);
    TestTrue(TEXT("Depth error is projected onto the actual surface"),SurfacePosition(*Data,ProjectionBase,Submerged).Equals(FVector3f(0),.001f));
    const FString Path=FPaths::ProjectSavedDir()/TEXT("PRTMultiBounceAnalytic.multi");
    TestTrue(TEXT("Save light-independent path transport"),Data->Save(Path));
    auto Reload=FMultiBounceData::Load(Path,123456,100,Data->BaseCRC);TestTrue(TEXT("Reload"),Reload.IsValid());
    if(Reload){TestEqual(TEXT("v4 metadata survives reload"),Reload->UnboundSamples,Data->UnboundSamples);TestEqual(TEXT("Source geometry count"),Reload->SourceGeometry.Num(),Data->Surfaces.Num()/3);}
    TestFalse(TEXT("Changed geometry rejects cache"),FMultiBounceData::Load(Path,123457,100,Data->BaseCRC).IsValid());
    TestFalse(TEXT("Changed cluster precision rejects cache"),FMultiBounceData::Load(Path,123456,50,Data->BaseCRC).IsValid());
    TestFalse(TEXT("Recaptured base samples reject old companion"),FMultiBounceData::Load(Path,123456,100,Data->BaseCRC+1).IsValid());
    TArray<uint8> Corrupted;FFileHelper::LoadFileToArray(Corrupted,*Path);Corrupted[Corrupted.Num()/2]^=1;FFileHelper::SaveArrayToFile(Corrupted,*Path);
    TestFalse(TEXT("Corrupted transport rejects cache"),FMultiBounceData::Load(Path,123456,100,Data->BaseCRC).IsValid());
    Data->CacheVersion=4;
    TestTrue(TEXT("Save v4 cache"),Data->Save(Path));
    auto Version4=FMultiBounceData::Load(Path,123456,100,Data->BaseCRC);
    TestTrue(TEXT("v4 remains readable"),Version4.IsValid() && Version4->CacheVersion==4);
    Data->CacheVersion=3;
    Data->UnmappedHits=7; // Legacy caches do not contain per-bounce counters.
    TestTrue(TEXT("Save legacy transport without v4 metadata"),Data->Save(Path));
    auto Legacy=FMultiBounceData::Load(Path,123456,100,Data->BaseCRC);
    TestTrue(TEXT("Load legacy transport"),Legacy.IsValid());
    if(Legacy)
    {
        TestEqual(TEXT("Preserve legacy version"),Legacy->CacheVersion,3);
        TestTrue(TEXT("Legacy source geometry is absent"),Legacy->SourceGeometry.IsEmpty());
        TestTrue(TEXT("Resave legacy cache"),Legacy->Save(Path));
        auto Resaved=FMultiBounceData::Load(Path,123456,100,Data->BaseCRC);
        TestTrue(TEXT("Resaved legacy cache remains readable"),Resaved.IsValid());
        if(Resaved)TestEqual(TEXT("Preserve legacy missing-hit count"),Resaved->UnmappedHits,7);
    }
    IFileManager::Get().Delete(*Path);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTMultiSurfaceBinding,"PRTGI.MultiBounce.SurfaceBindingAndThinOccluder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPRTMultiSurfaceBinding::RunTest(const FString&)
{
    using namespace PRTGI;
    FMultiBounceData Data;
    Data.Triangles={FVector4f(-10,-10,0,1),FVector4f(20,0,0,0),FVector4f(10,20,0,0)};
    Data.Nodes={FVector4f(-10,-10,-.001f,0),FVector4f(10,10,.001f,1)};
    FSurfelVolume Base;Base.GridCounts=FIntVector(2);Base.GridMin=FVector3f(0,0,1);Base.GridMax=FVector3f(2,2,3);
    FSurfelSample Sample;Sample.Position=FVector3f(0,0,-.1f);Sample.Normal=FVector3f(.6f,0,.8f);
    int32 Triangle=INDEX_NONE;
    TestTrue(TEXT("Smooth shading normal does not prevent geometric binding"),SurfacePosition(Data,Base,Sample,&Triangle).Equals(FVector3f(0),.001f));
    TestEqual(TEXT("Actual hit triangle retained"),Triangle,0);
    TestTrue(TEXT("Offset uses geometric normal, not shading normal"),TriangleNormal(Data,Triangle,Sample.Normal).Equals(FVector3f(0,0,1)));
    Sample.Position=FVector3f(0,0,-5);
    SurfacePosition(Data,Base,Sample,&Triangle);
    TestEqual(TEXT("Depth mismatch must not bind another surface"),Triangle,INDEX_NONE);
    FSurfaceKey A{FIntVector(0),0,0,0},B{FIntVector(0),0,0,1};
    TestFalse(TEXT("Distinct triangles cannot merge despite same cell/material samples"),A==B);

    Data.Triangles[0].Z=.03f;Data.Nodes[0].Z=.029f;Data.Nodes[1].Z=.031f;
    const float Offset=SurfaceOffset(FVector3f(0),FVector3f(20,0,0),FVector3f(10,20,0));
    float Distance=1;
    TestTrue(TEXT("Error-based offset preserves nearby 0.3mm occluder"),Trace(Data,FVector3f(0,0,Offset),FVector3f(0,0,1),Distance,true)>=0);
    Distance=1;
    TestTrue(TEXT("Former fixed 0.5mm offset skips the same occluder"),Trace(Data,FVector3f(0,0,.05f),FVector3f(0,0,1),Distance,true)<0);
    auto Atlas=MakeShared<FMaterialAtlas,ESPMode::ThreadSafe>();Atlas->Size=16;Atlas->TilesX=1;Atlas->TriangleCount=1;
    Atlas->Reflectance.Init(FVector3f(.2f,.4f,.8f),256);Atlas->TangentNormals.Init(FVector3f(0,0,1),256);
    FTriangle Thin;Thin.A=FVector3f(0);Thin.B=FVector3f(10000,0,0);Thin.C=FVector3f(10000,.01f,0);
    Thin.Material=Atlas;Thin.MaterialTriangle=0;
    for(int32 I=0;I<3;++I){Thin.Normal[I]=FVector3f(0,0,1);Thin.Tangent[I]=FVector3f(1,0,0);Thin.Bitangent[I]=FVector3f(0,1,0);}
    FVector3f Rho,N;
    TestTrue(TEXT("Long thin triangle retains material hit"),HitMaterial(Thin,(Thin.A+Thin.B+Thin.C)/3,FVector3f(0,0,1),Rho,N));
    TestTrue(TEXT("Hit material reflectance"),Rho.Equals(FVector3f(.2f,.4f,.8f),1.e-5f));
    return true;
}
#endif
