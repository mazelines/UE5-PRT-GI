#include "PRTGIMultiBounce.h"
#include "PRTGIVolumeData.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"
#include "RHIGPUReadback.h"

FPRTGIGPUCoefficients::~FPRTGIGPUCoefficients()
{
    if(IsInRenderingThread())Buffer.SafeRelease();
    else
    {
        auto Release=MoveTemp(Buffer);
        ENQUEUE_RENDER_COMMAND(PRTReleasePublishedSH)([Release=MoveTemp(Release)](FRHICommandListImmediate&)mutable{Release.SafeRelease();});
    }
}

namespace
{
class FPRTShadeSourcesCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FPRTShadeSourcesCS);
    SHADER_USE_PARAMETER_STRUCT(FPRTShadeSourcesCS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Surfaces)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,SourceGeometry)
        SHADER_PARAMETER(uint32,HasSourceGeometry)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Triangles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Nodes)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,OutRadiance)
        SHADER_PARAMETER(uint32,SourceCount)
        SHADER_PARAMETER(uint32,NodeCount)
        SHADER_PARAMETER(uint32,CastShadows)
        SHADER_PARAMETER(FVector3f,SunDirection)
        SHADER_PARAMETER(FVector3f,SunColor)
        SHADER_PARAMETER(float,SunLux)
        SHADER_PARAMETER(float,ShadowBias)
    END_SHADER_PARAMETER_STRUCT()
};
class FPRTApplyTransportCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FPRTApplyTransportCS);
    SHADER_USE_PARAMETER_STRUCT(FPRTApplyTransportCS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTransportTerm>,Terms)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>,Ranges)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,SourceRadiance)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,OutSH)
        SHADER_PARAMETER(uint32,BounceCount)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPRTShadeSourcesCS,"/Plugin/PRTGI/Private/PRTGIMultiBounce.usf","ShadeSourcesCS",SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPRTApplyTransportCS,"/Plugin/PRTGI/Private/PRTGIMultiBounce.usf","ApplyTransportCS",SF_Compute);
}
namespace PRTGI
{
struct FMultiBounceGPU::FState
{
    TAtomic<bool> Pending{false},Ready{false};
    TAtomic<bool> PollQueued{false};
    TAtomic<uint32> Generation{0};
    TArray<FVector4f> Result;
    TSharedPtr<const FPRTGIGPUCoefficients,ESPMode::ThreadSafe> Published;
    TSharedPtr<FMultiBounceData,ESPMode::ThreadSafe> Data;
    TRefCountPtr<FRDGPooledBuffer> Surfaces,SourceGeometry,Triangles,Nodes,Terms,Ranges;
    TUniquePtr<FRHIGPUBufferReadback> Readback;
};
FMultiBounceGPU::FMultiBounceGPU():State(MakeShared<FState,ESPMode::ThreadSafe>()){}
FMultiBounceGPU::~FMultiBounceGPU()
{
    auto S=MoveTemp(State);
    ENQUEUE_RENDER_COMMAND(PRTReleaseTransport)([S](FRHICommandListImmediate&){S->Readback.Reset();S->Surfaces.SafeRelease();S->SourceGeometry.SafeRelease();S->Triangles.SafeRelease();S->Nodes.SafeRelease();S->Terms.SafeRelease();S->Ranges.SafeRelease();});
}
bool FMultiBounceGPU::IsPending()const{return State->Pending.Load();}
void FMultiBounceGPU::Submit(TSharedPtr<FMultiBounceData,ESPMode::ThreadSafe> Data,FVector3f Sun,FVector3f Color,float Lux,bool Shadows,float Bias,int32 Bounces,bool Readback)
{
    check(!IsPending());State->Generation.Store(State->Generation.Load()+1);State->Pending.Store(true);State->Ready.Store(false);auto S=State;
    ENQUEUE_RENDER_COMMAND(PRTEvaluateTransport)([S,Data,Sun,Color,Lux,Shadows,Bias,Bounces,Readback](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder Graph(RHICmdList);
        if(S->Data!=Data)
        {
            S->Data=Data;
            auto Upload=[&](const TCHAR* Name,const auto& Array,TRefCountPtr<FRDGPooledBuffer>& Buffer)
            {auto B=CreateStructuredBuffer(Graph,Name,Array);Buffer=Graph.ConvertToExternalBuffer(B);};
            Upload(TEXT("PRT.Multi.Surfaces"),Data->Surfaces,S->Surfaces);Upload(TEXT("PRT.Multi.Triangles"),Data->Triangles,S->Triangles);
            if(!Data->SourceGeometry.IsEmpty())Upload(TEXT("PRT.Multi.SourceGeometry"),Data->SourceGeometry,S->SourceGeometry);
            else {TArray<FVector4f> Empty{FVector4f(0)};Upload(TEXT("PRT.Multi.SourceGeometry"),Empty,S->SourceGeometry);}
            Upload(TEXT("PRT.Multi.Nodes"),Data->Nodes,S->Nodes);Upload(TEXT("PRT.Multi.Terms"),Data->Terms,S->Terms);
            S->Ranges=Graph.ConvertToExternalBuffer(CreateStructuredBuffer(Graph,TEXT("PRT.Multi.Ranges"),Data->Ranges));
        }
        auto Sources=Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(16,Data->Surfaces.Num()/3),TEXT("PRT.Multi.Radiance"));
        auto Output=Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(16,Data->ProbeCount*9),TEXT("PRT.Multi.SH"));
        auto* Shade=Graph.AllocParameters<FPRTShadeSourcesCS::FParameters>();
        Shade->Surfaces=Graph.CreateSRV(Graph.RegisterExternalBuffer(S->Surfaces));Shade->Triangles=Graph.CreateSRV(Graph.RegisterExternalBuffer(S->Triangles));
        Shade->Nodes=Graph.CreateSRV(Graph.RegisterExternalBuffer(S->Nodes));Shade->OutRadiance=Graph.CreateUAV(Sources);
        Shade->SourceCount=Data->Surfaces.Num()/3;Shade->NodeCount=Data->Nodes.Num()/2;Shade->CastShadows=Shadows;
        Shade->SourceGeometry=Graph.CreateSRV(Graph.RegisterExternalBuffer(S->SourceGeometry));Shade->HasSourceGeometry=Data->SourceGeometry.Num()==int32(Shade->SourceCount)?1u:0u;
        Shade->SunDirection=Sun;Shade->SunColor=Color;Shade->SunLux=Lux;Shade->ShadowBias=Bias;
        TShaderMapRef<FPRTShadeSourcesCS> ShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(Graph,RDG_EVENT_NAME("PRT Multi Direct Lighting"),ShadeShader,Shade,FIntVector(FMath::DivideAndRoundUp(Shade->SourceCount,64u),1,1));
        auto* Apply=Graph.AllocParameters<FPRTApplyTransportCS::FParameters>();
        Apply->Terms=Graph.CreateSRV(Graph.RegisterExternalBuffer(S->Terms));Apply->Ranges=Graph.CreateSRV(Graph.RegisterExternalBuffer(S->Ranges));
        Apply->SourceRadiance=Graph.CreateSRV(Sources);Apply->OutSH=Graph.CreateUAV(Output);Apply->BounceCount=FMath::Clamp(Bounces,1,Data->MaxBounces);
        TShaderMapRef<FPRTApplyTransportCS> ApplyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(Graph,RDG_EVENT_NAME("PRT Multi Precomputed Transport"),ApplyShader,Apply,FIntVector(Data->ProbeCount,1,1));
        auto Publication=MakeShared<FPRTGIGPUCoefficients,ESPMode::ThreadSafe>();
        Publication->Buffer=Graph.ConvertToExternalBuffer(Output);
        S->Result.Reset();S->Readback.Reset();
        if(Readback)
        {
            S->Readback=MakeUnique<FRHIGPUBufferReadback>(TEXT("PRT.Multi.SHReadback"));
            AddEnqueueCopyPass(Graph,S->Readback.Get(),Output,Data->ProbeCount*9*16);
        }
        Graph.Execute();S->Published=Publication;
        if(!Readback)S->Ready.Store(true);
    });
}
bool FMultiBounceGPU::Poll(TArray<FVector4f>& Output,TSharedPtr<const FPRTGIGPUCoefficients,ESPMode::ThreadSafe>& GPUOutput)
{
    if(!State->Pending.Load())return false;
    if(State->Ready.Load())
    {
        Output=MoveTemp(State->Result);GPUOutput=State->Published;State->Ready.Store(false);State->Pending.Store(false);
        if(!Output.IsEmpty())for(int P=0;P<State->Data->ProbeCount;++P)Output[P*9].W=State->Data->Validity[P];
        return true;
    }
    // A slow render thread must not accumulate polling commands for an old readback.
    // Otherwise an old result can be tagged as a newly submitted light generation.
    if(State->PollQueued.Exchange(true))return false;
    auto S=State;const uint32 Generation=S->Generation.Load();
    ENQUEUE_RENDER_COMMAND(PRTPollTransport)([S,Generation](FRHICommandListImmediate&)
    {
        if(S->Generation.Load()!=Generation || S->Ready.Load() || !S->Pending.Load() || !S->Readback || !S->Readback->IsReady())
        {S->PollQueued.Store(false);return;}
        const int32 Count=S->Data->ProbeCount*9;S->Result.SetNumUninitialized(Count);
        if(const void* Ptr=S->Readback->Lock(Count*16)){FMemory::Memcpy(S->Result.GetData(),Ptr,Count*16);S->Readback->Unlock();}
        else S->Result.Init(FVector4f(0),Count);
        S->Ready.Store(true);
        S->PollQueued.Store(false);
    });
    return false;
}
}
