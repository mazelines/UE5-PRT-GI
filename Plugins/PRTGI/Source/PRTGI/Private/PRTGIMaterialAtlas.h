#pragma once
#include "CoreMinimal.h"

class UStaticMeshComponent;
struct FStaticMeshLODResources;
struct FStaticMeshSection;

namespace PRTGI
{
// Per-triangle charts preserve the mesh's original UV channels, vertex colors
// and primitive transform during UE material evaluation. No UObject reaches a worker.
struct FMaterialAtlas
{
    static constexpr int32 TileSize=16;
    int32 Size=0, TilesX=0, TriangleCount=0;
    bool bMirrored=false;
    TArray<FVector3f> Reflectance, TangentNormals;
    TArray<uint8> Coverage;
    bool Sample(int32 Triangle,FVector3f Barycentric,FVector3f& Rho,FVector3f& Normal) const;
};
TSharedPtr<const FMaterialAtlas,ESPMode::ThreadSafe> BakeMaterialAtlas(
    UStaticMeshComponent* Component,const FStaticMeshLODResources& LOD,
    const FStaticMeshSection& Section,const FTransform& Transform,FString& Error);
}
