#pragma once

#include "SceneViewExtension.h"

/** Runs the indirect-only replacement after scene lighting/fog and before post processing. */
class FPRTGIViewExtension final : public FSceneViewExtensionBase
{
public:
    using FDraw = TFunction<void(FRDGBuilder&, const FSceneView&, const FPostProcessingInputs&)>;

    FPRTGIViewExtension(const FAutoRegister& AutoRegister, FDraw InDraw)
        : FSceneViewExtensionBase(AutoRegister), Draw(MoveTemp(InDraw)) {}

    virtual void SetupViewFamily(FSceneViewFamily&) override {}
    virtual void SetupView(FSceneViewFamily&, FSceneView&) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}
    virtual void PrePostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override
    {
        Draw(GraphBuilder, View, Inputs);
    }

private:
    FDraw Draw;
};
