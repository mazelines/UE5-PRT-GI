#pragma once

#include "CoreMinimal.h"

namespace PRTGI::Math
{
    constexpr int32 CoefficientCount = 9;
    constexpr int32 CubeFaceCount = 6;

    void EvaluateSH3(const FVector3f& Direction, float OutBasis[CoefficientCount]);
    FVector3f CubemapTexelDirection(int32 FaceIndex, int32 X, int32 Y, int32 Resolution);

    /** 生成覆盖单位球面的确定性 Fibonacci directions。 */
    void GenerateFibonacciSphereDirections(int32 SampleCount, TArray<FVector3f>& OutDirections);

    /**
     * 构建 regularized least-squares SH projection matrix：
     * Coefficient[k] = Sum_i(OutWeights[i * CoefficientCount + k] * SampleValue[i])。
     * OutWeights 使用 sample-major 布局。Regularization 加到 9x9 Gram matrix 的对角线。
     */
    bool BuildSHProjectionWeights(
        TConstArrayView<FVector3f> SampleDirections,
        TArray<float>& OutWeights,
        double Regularization = 1.0e-8);

    /** 生成 Fibonacci samples，并一次性构建其 sample-major SH projection weights。 */
    bool BuildDirectionalProjection(
        int32 SampleCount,
        TArray<FVector3f>& OutDirections,
        TArray<float>& OutProjectionWeights);

    /**
     * 用单个 directional light 评估一个 probe 的 RGB SH transport。
     * Transport 使用 output-coefficient-major 布局：Transport[Output * 9 + Input]。
     * IncidentLightDirection 是从 shading point 指向光源的单位方向。
     */
    bool EvaluateDirectionalTransport(
        TConstArrayView<FVector4f> Transport,
        const FVector3f& IncidentLightDirection,
        float LightScalar,
        const FVector3f& LightColor,
        TArray<FVector4f>& OutIrradianceCoefficients);

    /** 将 scene-linear cubemap radiance 投影并解析卷积为 irradiance SH。 */
    bool ProjectCubemapRadiance(
        const TArray<TArray<FLinearColor>>& Faces,
        int32 Resolution,
        TArray<FVector4f>& OutIrradianceCoefficients);

    FVector3f ReconstructIrradiance(
        TConstArrayView<FVector4f> IrradianceCoefficients,
        const FVector3f& Normal);
}
