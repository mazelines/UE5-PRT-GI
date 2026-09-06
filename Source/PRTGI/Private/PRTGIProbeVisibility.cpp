#include "PRTGIProbeVisibility.h"
#include "PRTGISurfels.h"
#include "PRTGIMultiBounce.h"

namespace
{
    bool TriangleIntersectsCube(const FVector3f& A,const FVector3f& B,const FVector3f& C,float H)
    {
        const FVector3f Axes[3]={FVector3f(1,0,0),FVector3f(0,1,0),FVector3f(0,0,1)};
        const FVector3f Edges[3]={B-A,C-B,A-C};
        auto Separated=[&](const FVector3f& Axis)
        {
            const float P=FVector3f::DotProduct(A,Axis),Q=FVector3f::DotProduct(B,Axis),R=FVector3f::DotProduct(C,Axis);
            const auto Abs=Axis.GetAbs();const float Radius=H*(Abs.X+Abs.Y+Abs.Z);
            return FMath::Min3(P,Q,R)>Radius || FMath::Max3(P,Q,R)<-Radius;
        };
        for(const auto& Axis:Axes)if(Separated(Axis))return false;
        if(Separated(FVector3f::CrossProduct(Edges[0],Edges[1])))return false;
        for(const auto& Edge:Edges)for(const auto& Axis:Axes)
            if(Separated(FVector3f::CrossProduct(Edge,Axis)))return false;
        return true;
    }

    int32 CubeTexel(const FVector3f& D, int32 R)
    {
        const FVector3f A=D.GetAbs();
        int32 Face; float U,V;
        if(A.X>=A.Y && A.X>=A.Z)
        { Face=D.X>=0?0:1; U=(D.X>=0?-D.Z:D.Z)/A.X; V=D.Y/A.X; }
        else if(A.Y>=A.Z)
        { Face=D.Y>=0?2:3; U=D.X/A.Y; V=(D.Y>=0?-D.Z:D.Z)/A.Y; }
        else
        { Face=D.Z>=0?4:5; U=(D.Z>=0?D.X:-D.X)/A.Z; V=D.Y/A.Z; }
        const int32 X=FMath::Clamp(FMath::FloorToInt((U+1)*.5f*R),0,R-1);
        const int32 Y=FMath::Clamp(FMath::FloorToInt((1-V)*.5f*R),0,R-1);
        return (Face*R+Y)*R+X;
    }
}

TArray<uint8> FPRTGIProbeVisibility::BuildNearClipMask(const PRTGI::FSurfelVolume& Base,const PRTGI::FMultiBounceData& G)
{
    TArray<uint8> Mask;Mask.Init(0,Base.Validity.Num());
    const float H=FMath::Max(.001f,Base.CaptureNearClipCm)+.01f;
    for(int32 P=0;P<Mask.Num();++P)
    {
        if(Base.Validity[P]<0)continue;
        const FVector3f Origin(Base.ProbePosition(P)),Lo=Origin-FVector3f(H),Hi=Origin+FVector3f(H);
        for(int32 N=0;N<G.Nodes.Num()/2;)
        {
            const auto Min=G.Nodes[N*2],Max=G.Nodes[N*2+1];
            if(Min.X>Hi.X || Min.Y>Hi.Y || Min.Z>Hi.Z || Max.X<Lo.X || Max.Y<Lo.Y || Max.Z<Lo.Z)
            {N=Min.W<0?int32(Max.W):N+1;continue;}
            if(Min.W<0){++N;continue;}
            for(int32 T=int32(Min.W);T<int32(Min.W+Max.W);++T)
            {
                const FVector3f A=FVector3f(G.Triangles[T*3])-Origin;
                if(TriangleIntersectsCube(A,A+FVector3f(G.Triangles[T*3+1]),A+FVector3f(G.Triangles[T*3+2]),H))
                {Mask[P]=1;break;}
            }
            if(Mask[P])break;
            ++N;
        }
    }
    return Mask;
}

TSharedPtr<const FPRTGIProbeVisibility, ESPMode::ThreadSafe> FPRTGIProbeVisibility::Build(const PRTGI::FSurfelVolume& Base)
{
    if(Base.CaptureResolution<4 || Base.CaptureResolution>32 || Base.Validity.IsEmpty()
        || int64(Base.Validity.Num())*6*Base.CaptureResolution*Base.CaptureResolution>4000000)return nullptr;
    auto Result=MakeShared<FPRTGIProbeVisibility,ESPMode::ThreadSafe>();
    Result->Resolution=Base.CaptureResolution;
    const int32 Stride=6*Result->Resolution*Result->Resolution;
    // Empty texels are sky/missing front-face samples. They must not manufacture blockers.
    Result->Planes.Init(FVector4f(0,0,0,0),Base.Validity.Num()*Stride);
    for(const auto& S:Base.Samples)
    {
        const FVector3f D=S.Position-FVector3f(Base.ProbePosition(S.Probe));
        if(D.SizeSquared()<1.e-8f)continue;
        Result->Planes[S.Probe*Stride+CubeTexel(D,Result->Resolution)]=FVector4f(S.Normal,FVector3f::DotProduct(S.Normal,D));
    }
    return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGINearClipTest,"PRTGI.Visibility.NearClipSafety",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPRTGINearClipTest::RunTest(const FString&)
{
    // Reproduces probe 98: a yaw-50 wall 1.115cm away misses the old 1cm
    // overlap sphere but cuts through the six-face capture's near-clip cube.
    const FVector3f N(-.76604444f,.64278761f,0),T(.64278761f,.76604444f,0),Z(0,0,1);
    auto Hit=[&](float D){const auto C=N*D;return TriangleIntersectsCube(C-T*10-Z*10,C+T*10-Z*10,C+Z*10,1);};
    TestTrue(TEXT("Rotated wall outside sphere still crosses capture near planes"),Hit(1.115f));
    TestFalse(TEXT("Rotated wall outside near cube stays usable"),Hit(2));
    TestFalse(TEXT("Axis aligned wall at 1.2cm stays usable"),TriangleIntersectsCube(FVector3f(1.2,-10,-10),FVector3f(1.2,10,-10),FVector3f(1.2,0,10),1));
    PRTGI::FSurfelVolume Base;Base.GridCounts=FIntVector(2);Base.GridMin=FVector3f(0);Base.GridMax=FVector3f(100);Base.Validity.Init(1,8);Base.CaptureNearClipCm=1;
    PRTGI::FMultiBounceData G;const auto C=N*1.115f,A=C-T*10-Z*10,B=C+T*10-Z*10,V=C+Z*10;
    G.Triangles={FVector4f(A,1),FVector4f(B-A,0),FVector4f(V-A,0)};
    G.Nodes={FVector4f(-20,-20,-20,0),FVector4f(20,20,20,1)};
    const auto Mask=FPRTGIProbeVisibility::BuildNearClipMask(Base,G);
    TestEqual(TEXT("Cached valid probe rejected by static triangle BVH"),int32(Mask[0]),1);
    TestEqual(TEXT("Distant probe remains valid"),int32(Mask[1]),0);
    TestEqual(TEXT("Persisted validity is never modified"),Base.Validity[0],1.f);
    Base.CaptureNearClipCm=.1f;
    TestEqual(TEXT("Small near plane does not reject safe capture"),int32(FPRTGIProbeVisibility::BuildNearClipMask(Base,G)[0]),0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTGIVisibilityCubeTest,"PRTGI.Visibility.CapturedPlaneMapping",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPRTGIVisibilityCubeTest::RunTest(const FString&)
{
    PRTGI::FSurfelVolume Base; Base.GridCounts=FIntVector(2); Base.GridMin=FVector3f(0);Base.GridMax=FVector3f(100);
    Base.Validity.Init(1,8);Base.CaptureResolution=16;
    for(int F=0;F<6;++F)for(int Y=0;Y<16;++Y)for(int X=0;X<16;++X)
    {
        const auto D=PRTGI::Math::CubemapTexelDirection(F,X,Y,16);
        auto& S=Base.Samples.AddDefaulted_GetRef();S.Position=D*(100/D.GetAbsMax());S.Probe=0;
        S.Normal=FVector3f(0);S.Normal[F/2]=F%2?1:-1;
        TestEqual(TEXT("Captured texel maps back to its original face/pixel"),CubeTexel(D,16),(F*16+Y)*16+X);
    }
    const auto Visibility=FPRTGIProbeVisibility::Build(Base);
    for(int I=0;I<1536;++I)
    {
        const auto P=Visibility->Planes[I];const auto N=FVector3f(P);
        TestTrue(TEXT("First-hit plane is 100cm from the probe"),FMath::IsNearlyEqual(P.W,-100.f,.001f));
        // Receiver before the wall is visible; on-wall must not self-shadow; beyond is occluded.
        TestTrue(TEXT("Receiver before wall"),P.W-FVector3f::DotProduct(N,-N*50)<0);
        TestTrue(TEXT("Receiver on wall"),FMath::IsNearlyZero(P.W-FVector3f::DotProduct(N,-N*100),.001f));
        TestTrue(TEXT("Receiver through wall"),P.W-FVector3f::DotProduct(N,-N*150)>2);
    }
    TestTrue(TEXT("Missing capture never becomes an invented wall"),Visibility->Planes[1536].IsNearlyZero());
    return true;
}
#endif
