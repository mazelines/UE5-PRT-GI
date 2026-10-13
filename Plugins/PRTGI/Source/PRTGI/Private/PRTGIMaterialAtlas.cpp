#include "PRTGIMaterialAtlas.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "StaticMeshComponentLODInfo.h"
#if WITH_EDITOR
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#include "MaterialUtilities.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Modules/ModuleManager.h"
#endif

namespace PRTGI
{
bool FMaterialAtlas::Sample(int32 Triangle,FVector3f Bary,FVector3f& Rho,FVector3f& Normal) const
{
    if(Triangle<0 || Triangle>=TriangleCount || Size<=0 || TilesX<=0
        || Reflectance.Num()!=Size*Size || TangentNormals.Num()!=Size*Size)return false;
    // Keep the four taps in this chart. A small inset avoids rasterization's
    // top-left edge ambiguity; no neighbor's material is ever borrowed.
    if(bMirrored)Swap(Bary.X,Bary.Z);
    Bary=Bary.ComponentMax(FVector3f(1.f/24.f));Bary/=Bary.X+Bary.Y+Bary.Z;
    const FIntPoint Tile((Triangle%TilesX)*TileSize,(Triangle/TilesX)*TileSize);
    const FVector2f P=FVector2f(Tile)+FVector2f(2.f+12.f*Bary.Y,2.f+12.f*Bary.Z)-FVector2f(.5f);
    const FIntPoint Base(FMath::FloorToInt(P.X),FMath::FloorToInt(P.Y));
    const FVector2f T(P.X-Base.X,P.Y-Base.Y);
    Rho=Normal=FVector3f(0);float Sum=0;
    for(int32 Y=0;Y<2;++Y)for(int32 X=0;X<2;++X)
    {
        const int32 PX=Base.X+X,PY=Base.Y+Y;
        const float LX=PX-Tile.X+.5f,LY=PY-Tile.Y+.5f;
        if(LX<2 || LY<2 || LX+LY>=16 || PX<0 || PY<0 || PX>=Size || PY>=Size)continue;
        if(!Coverage.IsEmpty() && !Coverage[PY*Size+PX])continue;
        const float W=(X?T.X:1-T.X)*(Y?T.Y:1-T.Y);
        Rho+=Reflectance[PY*Size+PX]*W;Normal+=TangentNormals[PY*Size+PX]*W;Sum+=W;
    }
    if(Sum<=1.e-6f)return false;
    Rho/=Sum;Normal=Normal.GetSafeNormal();return !Rho.ContainsNaN() && Normal.SizeSquared()>.5f;
}

TSharedPtr<const FMaterialAtlas,ESPMode::ThreadSafe> BakeMaterialAtlas(
    UStaticMeshComponent* Component,const FStaticMeshLODResources& LOD,
    const FStaticMeshSection& Section,const FTransform& Transform,FString& Error)
{
#if WITH_EDITOR
    check(IsInGameThread());
    auto* Material=Component->GetMaterial(Section.MaterialIndex);
    if(!Material || Section.NumTriangles==0){Error=TEXT("Missing material or empty section");return nullptr;}
    auto Atlas=MakeShared<FMaterialAtlas,ESPMode::ThreadSafe>();
    Atlas->TriangleCount=Section.NumTriangles;
    Atlas->TilesX=FMath::CeilToInt(FMath::Sqrt(float(Section.NumTriangles)));
    Atlas->Size=Atlas->TilesX*FMaterialAtlas::TileSize;
    if(Atlas->Size>4096){Error=TEXT("Material chart exceeds 4096 texels; split mesh section");return nullptr;}
    FMeshDescription Mesh;FStaticMeshAttributes Attr(Mesh);Attr.Register();
    const auto Group=Mesh.CreatePolygonGroup();
    auto Positions=Attr.GetVertexPositions();auto Normals=Attr.GetVertexInstanceNormals();
    auto Tangents=Attr.GetVertexInstanceTangents();auto Signs=Attr.GetVertexInstanceBinormalSigns();
    auto Colors=Attr.GetVertexInstanceColors();auto UVs=Attr.GetVertexInstanceUVs();
    const int32 UVCount=FMath::Max(1,FMath::Min(6,int32(LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords())));
    UVs.SetNumChannels(UVCount);
    FMeshData MeshData;MeshData.MeshDescription=&Mesh;MeshData.MaterialIndices.Add(Group.GetValue());
    MeshData.PrimitiveData=FPrimitiveData(Component);MeshData.PrimitiveData->LocalToWorld=Transform.ToMatrixWithScale();
    Atlas->bMirrored=MeshData.bMirrored=Transform.ToMatrixWithScale().Determinant()<0;
    const auto Indices=LOD.IndexBuffer.GetArrayView();
    const FMatrix NormalTransform=Transform.ToMatrixWithScale().Inverse().GetTransposed();
    for(uint32 Triangle=0;Triangle<Section.NumTriangles;++Triangle)
    {
        FVertexInstanceID Corners[3];
        for(int32 C=0;C<3;++C)
        {
            const uint32 I=Indices[Section.FirstIndex+Triangle*3+C];
            auto V=Mesh.CreateVertex();Positions[V]=LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(I);
            auto VI=Mesh.CreateVertexInstance(V);Corners[C]=VI;
            Normals[VI]=FVector3f(FVector(NormalTransform.TransformVector(FVector(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I))))).GetSafeNormal();
            Tangents[VI]=FVector3f(Transform.TransformVector(FVector(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(I)))).GetSafeNormal();
            const FVector3f B=FVector3f(Transform.TransformVector(FVector(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentY(I)))).GetSafeNormal();
            Signs[VI]=FVector3f::DotProduct(FVector3f::CrossProduct(Normals[VI],Tangents[VI]),B)<0?-1.f:1.f;
            FColor Color=LOD.VertexBuffers.ColorVertexBuffer.GetNumVertices()>I?LOD.VertexBuffers.ColorVertexBuffer.VertexColor(I):FColor::White;
            if(Component->LODData.IsValidIndex(0) && Component->LODData[0].OverrideVertexColors
                && Component->LODData[0].OverrideVertexColors->GetNumVertices()>I)Color=Component->LODData[0].OverrideVertexColors->VertexColor(I);
            Colors[VI]=FVector4f(FLinearColor(Color));
            for(int32 U=0;U<UVCount;++U)UVs.Set(VI,U,LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()>0
                ?LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,U):FVector2f(0));
            const FVector2D Origin((Triangle%Atlas->TilesX)*FMaterialAtlas::TileSize,(Triangle/Atlas->TilesX)*FMaterialAtlas::TileSize);
            MeshData.CustomTextureCoordinates.Add((Origin+FVector2D(C==1?14:2,C==2?14:2))/Atlas->Size);
        }
        Mesh.CreateTriangle(Group,MakeArrayView(Corners));
    }
    FMaterialData Settings;Settings.Material=Material;Settings.bTangentSpaceNormal=true;
    Settings.bPerformBorderSmear=false;Settings.bPerformShrinking=false;
    for(auto Property:{MP_BaseColor,MP_Metallic,MP_Normal})Settings.PropertySizes.Add(Property,FIntPoint(Atlas->Size));
    TArray<FMaterialData*> Materials{&Settings};TArray<FMeshData*> Meshes{&MeshData};TArray<FBakeOutput> Outputs;
    auto& Baker=FModuleManager::LoadModuleChecked<IMaterialBakingModule>(TEXT("MaterialBaking"));
    // UE's baker rasterizes at z=0 after a float world/local round trip.
    // A primitive transform can move the chart just outside the clip plane.
    // A UV-only material can safely use identity primitive data; the engine's
    // material analyzer must prove that it does not require vertex/world data.
    bool Covered=false;
    for(int32 Attempt=0;Attempt<2 && !Covered;++Attempt)
    {
        if(Attempt>0)
        {
            int32 NumUVs=0;bool RequiresVertexData=true;
            FMaterialUtilities::AnalyzeMaterial(Material,TArray<EMaterialProperty>{MP_BaseColor,MP_Metallic,MP_Normal},NumUVs,RequiresVertexData);
            if(RequiresVertexData)break;
            MeshData.PrimitiveData.Reset();MeshData.bMirrored=false;Atlas->bMirrored=false;
        }
        Outputs.Reset();Baker.BakeMaterials(Materials,Meshes,Outputs);
        if(Outputs.Num()!=1 || !Outputs[0].PropertyData.Contains(MP_Metallic)
            || Outputs[0].PropertyData[MP_Metallic].Num()!=Atlas->Size*Atlas->Size)continue;
        const auto& Pixels=Outputs[0].PropertyData[MP_Metallic];Covered=true;
        for(int32 Tri=0;Tri<Atlas->TriangleCount;++Tri)
        {
            const int32 TX=(Tri%Atlas->TilesX)*16,TY=(Tri/Atlas->TilesX)*16;
            // Audit every interior texel, not just the center: rotated
            // primitives may clip only part of the chart against z=0.
            for(int32 Y=2;Y<14 && Covered;++Y)for(int32 X=2;X+Y<15;++X)
            {
                const FColor P=Pixels[(TY+Y)*Atlas->Size+TX+X];
                if(P.R!=P.G || P.G!=P.B){Covered=false;break;}
            }
            if(!Covered)break;
        }
    }
    if(!Covered){Error=TEXT("Material rasterization left uncovered charts and cannot safely ignore vertex/world dependencies; cache unchanged");return nullptr;}
    if(Outputs.Num()!=1){Error=TEXT("Material baker returned no output");return nullptr;}
    const auto& Out=Outputs[0];
    for(auto Property:{MP_BaseColor,MP_Metallic,MP_Normal})
        if(!Out.PropertyData.Contains(Property) || Out.PropertyData[Property].Num()!=Atlas->Size*Atlas->Size)
        {Error=TEXT("Material baker returned an incomplete chart");return nullptr;}
    Atlas->Reflectance.SetNumUninitialized(Atlas->Size*Atlas->Size);Atlas->TangentNormals.SetNumUninitialized(Atlas->Size*Atlas->Size);
    Atlas->Coverage.SetNumUninitialized(Atlas->Size*Atlas->Size);
    auto Linear=[&](EMaterialProperty Property,int32 I)
    {const FColor C=Out.PropertyData[Property][I];return Out.PropertyIsLinearColor.FindRef(Property)?C.ReinterpretAsLinear():FLinearColor::FromSRGBColor(C);};
    for(int32 I=0;I<Atlas->Reflectance.Num();++I)
    {
        const auto Pixel=Out.PropertyData[MP_Metallic][I];
        Atlas->Coverage[I]=(Pixel.R==Pixel.G && Pixel.G==Pixel.B)?1:0;
        const auto Base=Linear(MP_BaseColor,I);const float Metal=FMath::Clamp(Linear(MP_Metallic,I).R,0.f,1.f);
        Atlas->Reflectance[I]=FVector3f(Base.R,Base.G,Base.B)*(1-Metal);
        const auto N=Linear(MP_Normal,I);Atlas->TangentNormals[I]=FVector3f(N.R*2-1,N.G*2-1,N.B*2-1);
    }
    return Atlas;
#else
    Error=TEXT("Material baking requires the Editor; load precomputed transport in packaged builds");return nullptr;
#endif
}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPRTMaterialChartTest,"PRTGI.MultiBounce.MaterialChartIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPRTMaterialChartTest::RunTest(const FString&)
{
    PRTGI::FMaterialAtlas Atlas;Atlas.Size=32;Atlas.TilesX=2;Atlas.TriangleCount=2;
    Atlas.Reflectance.Init(FVector3f(0,0,1),1024);Atlas.TangentNormals.Init(FVector3f(0,0,1),1024);
    for(int32 Y=0;Y<16;++Y)for(int32 X=0;X<16;++X)Atlas.Reflectance[Y*32+X]=FVector3f(1,0,0);
    for(auto B:{FVector3f(1,0,0),FVector3f(0,1,0),FVector3f(0,0,1),FVector3f(1.f/3)})
    {
        FVector3f R,N;TestTrue(TEXT("Triangle corner is covered"),Atlas.Sample(0,B,R,N));
        TestTrue(TEXT("Chart cannot borrow adjacent blue material"),R.Equals(FVector3f(1,0,0),1.e-5f));
        TestTrue(TEXT("Neighbor chart remains blue"),Atlas.Sample(1,B,R,N) && R.Equals(FVector3f(0,0,1),1.e-5f));
    }
    Atlas.Coverage.Init(0,1024);FVector3f R,N;
    TestFalse(TEXT("Unrendered chart is rejected, not treated as black"),Atlas.Sample(0,FVector3f(1.f/3),R,N));
    return true;
}
#endif
