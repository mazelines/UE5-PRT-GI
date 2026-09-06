#include "PRTGIMath.h"

#include "Math/UnrealMathUtility.h"

namespace PRTGI::Math
{
    namespace
    {
        constexpr int32 AugmentedMatrixColumnCount = CoefficientCount * 2;

        bool InvertGramMatrix(
            const double Matrix[CoefficientCount][CoefficientCount],
            double OutInverse[CoefficientCount][CoefficientCount])
        {
            double Augmented[CoefficientCount][AugmentedMatrixColumnCount] = {};
            double MatrixScale = 0.0;
            for (int32 Row = 0; Row < CoefficientCount; ++Row)
            {
                for (int32 Column = 0; Column < CoefficientCount; ++Column)
                {
                    const double Value = Matrix[Row][Column];
                    if (!FMath::IsFinite(Value))
                    {
                        return false;
                    }
                    Augmented[Row][Column] = Value;
                    MatrixScale = FMath::Max(MatrixScale, FMath::Abs(Value));
                }
                Augmented[Row][CoefficientCount + Row] = 1.0;
            }

            if (MatrixScale <= UE_DOUBLE_SMALL_NUMBER)
            {
                return false;
            }

            const double PivotTolerance = MatrixScale * 1.0e-12;
            for (int32 PivotColumn = 0; PivotColumn < CoefficientCount; ++PivotColumn)
            {
                int32 PivotRow = PivotColumn;
                double PivotMagnitude = FMath::Abs(Augmented[PivotRow][PivotColumn]);
                for (int32 CandidateRow = PivotColumn + 1; CandidateRow < CoefficientCount; ++CandidateRow)
                {
                    const double CandidateMagnitude = FMath::Abs(Augmented[CandidateRow][PivotColumn]);
                    if (CandidateMagnitude > PivotMagnitude)
                    {
                        PivotRow = CandidateRow;
                        PivotMagnitude = CandidateMagnitude;
                    }
                }

                if (!FMath::IsFinite(PivotMagnitude) || PivotMagnitude <= PivotTolerance)
                {
                    return false;
                }

                if (PivotRow != PivotColumn)
                {
                    for (int32 Column = 0; Column < AugmentedMatrixColumnCount; ++Column)
                    {
                        Swap(Augmented[PivotColumn][Column], Augmented[PivotRow][Column]);
                    }
                }

                const double Pivot = Augmented[PivotColumn][PivotColumn];
                for (int32 Column = 0; Column < AugmentedMatrixColumnCount; ++Column)
                {
                    Augmented[PivotColumn][Column] /= Pivot;
                }

                for (int32 Row = 0; Row < CoefficientCount; ++Row)
                {
                    if (Row == PivotColumn)
                    {
                        continue;
                    }

                    const double Factor = Augmented[Row][PivotColumn];
                    if (Factor == 0.0)
                    {
                        continue;
                    }
                    for (int32 Column = 0; Column < AugmentedMatrixColumnCount; ++Column)
                    {
                        Augmented[Row][Column] -= Factor * Augmented[PivotColumn][Column];
                    }
                }
            }

            for (int32 Row = 0; Row < CoefficientCount; ++Row)
            {
                for (int32 Column = 0; Column < CoefficientCount; ++Column)
                {
                    const double Value = Augmented[Row][CoefficientCount + Column];
                    if (!FMath::IsFinite(Value))
                    {
                        return false;
                    }
                    OutInverse[Row][Column] = Value;
                }
            }
            return true;
        }
    }

    void EvaluateSH3(const FVector3f& Direction, float OutBasis[CoefficientCount])
    {
        const FVector3f D = Direction.GetSafeNormal();
        const float X = D.X;
        const float Y = D.Y;
        const float Z = D.Z;

        // 与 PBR_PRTdemo 的 real SH 顺序 l*(l+1)+m 完全一致。
        OutBasis[0] = 0.2820947918f;
        OutBasis[1] = 0.4886025119f * Y;
        OutBasis[2] = 0.4886025119f * Z;
        OutBasis[3] = 0.4886025119f * X;
        OutBasis[4] = 1.0925484306f * X * Y;
        OutBasis[5] = 1.0925484306f * Y * Z;
        OutBasis[6] = 0.3153915653f * (3.0f * Z * Z - 1.0f);
        OutBasis[7] = 1.0925484306f * X * Z;
        OutBasis[8] = 0.5462742153f * (X * X - Y * Y);
    }

    FVector3f CubemapTexelDirection(const int32 FaceIndex, const int32 X, const int32 Y, const int32 Resolution)
    {
        const float U = 2.0f * (static_cast<float>(X) + 0.5f) / static_cast<float>(Resolution) - 1.0f;
        const float V = 1.0f - 2.0f * (static_cast<float>(Y) + 0.5f) / static_cast<float>(Resolution);

        FVector3f Up(0.0f, 1.0f, 0.0f);
        FVector3f Forward;
        switch (FaceIndex)
        {
        case 0: Forward = FVector3f(1.0f, 0.0f, 0.0f); break;
        case 1: Forward = FVector3f(-1.0f, 0.0f, 0.0f); break;
        case 2: Up = FVector3f(0.0f, 0.0f, -1.0f); Forward = FVector3f(0.0f, 1.0f, 0.0f); break;
        case 3: Up = FVector3f(0.0f, 0.0f, 1.0f); Forward = FVector3f(0.0f, -1.0f, 0.0f); break;
        case 4: Forward = FVector3f(0.0f, 0.0f, 1.0f); break;
        default: Forward = FVector3f(0.0f, 0.0f, -1.0f); break;
        }

        const FVector3f Right = FVector3f::CrossProduct(Up, Forward);
        return (Forward + U * Right + V * Up).GetSafeNormal();
    }

    void GenerateFibonacciSphereDirections(const int32 SampleCount, TArray<FVector3f>& OutDirections)
    {
        OutDirections.Reset();
        if (SampleCount <= 0)
        {
            return;
        }

        OutDirections.SetNumUninitialized(SampleCount);
        const double GoldenAngle = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
        for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
        {
            const double Z = 1.0 - 2.0 * (static_cast<double>(SampleIndex) + 0.5) / static_cast<double>(SampleCount);
            const double Radius = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
            const double Azimuth = GoldenAngle * static_cast<double>(SampleIndex);
            OutDirections[SampleIndex] = FVector3f(
                static_cast<float>(Radius * FMath::Cos(Azimuth)),
                static_cast<float>(Radius * FMath::Sin(Azimuth)),
                static_cast<float>(Z));
        }
    }

    bool BuildSHProjectionWeights(
        const TConstArrayView<FVector3f> SampleDirections,
        TArray<float>& OutWeights,
        const double Regularization)
    {
        OutWeights.Reset();
        const int32 SampleCount = SampleDirections.Num();
        if (SampleCount < CoefficientCount ||
            SampleCount > MAX_int32 / CoefficientCount ||
            !FMath::IsFinite(Regularization) ||
            Regularization < 0.0)
        {
            return false;
        }

        TArray<float> BasisMatrix;
        BasisMatrix.SetNumUninitialized(SampleCount * CoefficientCount);
        double Gram[CoefficientCount][CoefficientCount] = {};
        for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
        {
            const FVector3f& Direction = SampleDirections[SampleIndex];
            if (!FMath::IsFinite(Direction.X) || !FMath::IsFinite(Direction.Y) ||
                !FMath::IsFinite(Direction.Z) || Direction.SizeSquared() <= UE_SMALL_NUMBER)
            {
                return false;
            }

            float* Basis = BasisMatrix.GetData() + SampleIndex * CoefficientCount;
            EvaluateSH3(Direction, Basis);
            for (int32 Row = 0; Row < CoefficientCount; ++Row)
            {
                for (int32 Column = 0; Column < CoefficientCount; ++Column)
                {
                    Gram[Row][Column] += static_cast<double>(Basis[Row]) * Basis[Column];
                }
            }
        }

        for (int32 Index = 0; Index < CoefficientCount; ++Index)
        {
            Gram[Index][Index] += Regularization;
        }

        double GramInverse[CoefficientCount][CoefficientCount];
        if (!InvertGramMatrix(Gram, GramInverse))
        {
            return false;
        }

        OutWeights.SetNumUninitialized(SampleCount * CoefficientCount);
        for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
        {
            const float* Basis = BasisMatrix.GetData() + SampleIndex * CoefficientCount;
            for (int32 CoefficientIndex = 0; CoefficientIndex < CoefficientCount; ++CoefficientIndex)
            {
                double Weight = 0.0;
                for (int32 BasisIndex = 0; BasisIndex < CoefficientCount; ++BasisIndex)
                {
                    Weight += GramInverse[CoefficientIndex][BasisIndex] * Basis[BasisIndex];
                }
                if (!FMath::IsFinite(Weight))
                {
                    OutWeights.Reset();
                    return false;
                }
                OutWeights[SampleIndex * CoefficientCount + CoefficientIndex] = static_cast<float>(Weight);
            }
        }
        return true;
    }

    bool BuildDirectionalProjection(
        const int32 SampleCount,
        TArray<FVector3f>& OutDirections,
        TArray<float>& OutProjectionWeights)
    {
        GenerateFibonacciSphereDirections(SampleCount, OutDirections);
        if (!BuildSHProjectionWeights(OutDirections, OutProjectionWeights))
        {
            OutDirections.Reset();
            OutProjectionWeights.Reset();
            return false;
        }
        return true;
    }

    bool EvaluateDirectionalTransport(
        const TConstArrayView<FVector4f> Transport,
        const FVector3f& IncidentLightDirection,
        const float LightScalar,
        const FVector3f& LightColor,
        TArray<FVector4f>& OutIrradianceCoefficients)
    {
        OutIrradianceCoefficients.Reset();
        if (Transport.Num() != CoefficientCount * CoefficientCount ||
            !FMath::IsFinite(IncidentLightDirection.X) || !FMath::IsFinite(IncidentLightDirection.Y) ||
            !FMath::IsFinite(IncidentLightDirection.Z) || IncidentLightDirection.SizeSquared() <= UE_SMALL_NUMBER ||
            !FMath::IsFinite(LightScalar) || !FMath::IsFinite(LightColor.X) ||
            !FMath::IsFinite(LightColor.Y) || !FMath::IsFinite(LightColor.Z))
        {
            return false;
        }

        float LightBasis[CoefficientCount];
        EvaluateSH3(IncidentLightDirection, LightBasis);
        OutIrradianceCoefficients.SetNumUninitialized(CoefficientCount);
        for (int32 OutputIndex = 0; OutputIndex < CoefficientCount; ++OutputIndex)
        {
            FVector3d Transported(0.0, 0.0, 0.0);
            for (int32 InputIndex = 0; InputIndex < CoefficientCount; ++InputIndex)
            {
                const FVector4f& Element = Transport[OutputIndex * CoefficientCount + InputIndex];
                if (!FMath::IsFinite(Element.X) || !FMath::IsFinite(Element.Y) || !FMath::IsFinite(Element.Z))
                {
                    OutIrradianceCoefficients.Reset();
                    return false;
                }
                Transported += FVector3d(Element) * static_cast<double>(LightBasis[InputIndex]);
            }

            const FVector3d Irradiance(
                Transported.X * static_cast<double>(LightScalar) * LightColor.X,
                Transported.Y * static_cast<double>(LightScalar) * LightColor.Y,
                Transported.Z * static_cast<double>(LightScalar) * LightColor.Z);
            OutIrradianceCoefficients[OutputIndex] = FVector4f(FVector3f(Irradiance), 0.0f);
        }
        return true;
    }

    bool ProjectCubemapRadiance(
        const TArray<TArray<FLinearColor>>& Faces,
        const int32 Resolution,
        TArray<FVector4f>& OutIrradianceCoefficients)
    {
        if (Resolution <= 0 || Faces.Num() != CubeFaceCount)
        {
            return false;
        }

        const int32 TexelCount = Resolution * Resolution;
        for (const TArray<FLinearColor>& Face : Faces)
        {
            if (Face.Num() != TexelCount)
            {
                return false;
            }
        }

        FVector3d RadianceCoefficients[CoefficientCount] = {};
        double TotalWeight = 0.0;

        for (int32 FaceIndex = 0; FaceIndex < CubeFaceCount; ++FaceIndex)
        {
            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    const float U = 2.0f * (static_cast<float>(X) + 0.5f) / static_cast<float>(Resolution) - 1.0f;
                    const float V = 1.0f - 2.0f * (static_cast<float>(Y) + 0.5f) / static_cast<float>(Resolution);
                    const double Weight = 1.0 / FMath::Pow(1.0 + U * U + V * V, 1.5);
                    const FLinearColor Pixel = Faces[FaceIndex][Y * Resolution + X];
                    if (!FMath::IsFinite(Pixel.R) || !FMath::IsFinite(Pixel.G) || !FMath::IsFinite(Pixel.B))
                    {
                        continue;
                    }

                    const FVector3d Radiance(
                        FMath::Max(0.0f, Pixel.R),
                        FMath::Max(0.0f, Pixel.G),
                        FMath::Max(0.0f, Pixel.B));
                    float Basis[CoefficientCount];
                    EvaluateSH3(CubemapTexelDirection(FaceIndex, X, Y, Resolution), Basis);
                    for (int32 CoefficientIndex = 0; CoefficientIndex < CoefficientCount; ++CoefficientIndex)
                    {
                        RadianceCoefficients[CoefficientIndex] += Radiance * (Weight * Basis[CoefficientIndex]);
                    }
                    TotalWeight += Weight;
                }
            }
        }

        if (TotalWeight <= UE_DOUBLE_SMALL_NUMBER)
        {
            return false;
        }

        const double SolidAngleScale = 4.0 * UE_DOUBLE_PI / TotalWeight;
        const double CosineConvolution[3] = { UE_DOUBLE_PI, 2.0 * UE_DOUBLE_PI / 3.0, UE_DOUBLE_PI / 4.0 };
        OutIrradianceCoefficients.SetNumUninitialized(CoefficientCount);
        for (int32 CoefficientIndex = 0; CoefficientIndex < CoefficientCount; ++CoefficientIndex)
        {
            const int32 Band = CoefficientIndex == 0 ? 0 : (CoefficientIndex < 4 ? 1 : 2);
            const FVector3d IrradianceCoefficient =
                RadianceCoefficients[CoefficientIndex] * SolidAngleScale * CosineConvolution[Band];
            OutIrradianceCoefficients[CoefficientIndex] = FVector4f(FVector3f(IrradianceCoefficient), 0.0f);
        }
        return true;
    }

    FVector3f ReconstructIrradiance(
        const TConstArrayView<FVector4f> IrradianceCoefficients,
        const FVector3f& Normal)
    {
        if (IrradianceCoefficients.Num() < CoefficientCount)
        {
            return FVector3f::ZeroVector;
        }

        float Basis[CoefficientCount];
        EvaluateSH3(Normal, Basis);
        FVector3f Result = FVector3f::ZeroVector;
        for (int32 Index = 0; Index < CoefficientCount; ++Index)
        {
            Result += FVector3f(IrradianceCoefficients[Index]) * Basis[Index];
        }
        return FVector3f(
            FMath::Max(Result.X, 0.0f),
            FMath::Max(Result.Y, 0.0f),
            FMath::Max(Result.Z, 0.0f));
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPRTGIConstantCubemapTest,
    "PRTGI.Math.ConstantRadianceProducesPiIrradiance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPRTGIConstantCubemapTest::RunTest(const FString& Parameters)
{
    constexpr int32 Resolution = 16;
    const FLinearColor ConstantRadiance(0.25f, 0.5f, 1.0f, 1.0f);
    TArray<TArray<FLinearColor>> Faces;
    Faces.SetNum(PRTGI::Math::CubeFaceCount);
    for (TArray<FLinearColor>& Face : Faces)
    {
        Face.Init(ConstantRadiance, Resolution * Resolution);
    }

    TArray<FVector4f> Coefficients;
    TestTrue(TEXT("Cubemap projection succeeds"),
        PRTGI::Math::ProjectCubemapRadiance(Faces, Resolution, Coefficients));

    const FVector3f Expected(ConstantRadiance.R * UE_PI, ConstantRadiance.G * UE_PI, ConstantRadiance.B * UE_PI);
    const FVector3f Directions[] =
    {
        FVector3f::XAxisVector,
        FVector3f::YAxisVector,
        FVector3f::ZAxisVector,
        FVector3f(1.0f, 2.0f, 3.0f).GetSafeNormal()
    };
    for (const FVector3f& Direction : Directions)
    {
        const FVector3f Actual = PRTGI::Math::ReconstructIrradiance(Coefficients, Direction);
        TestTrue(TEXT("Constant field R"), FMath::IsNearlyEqual(Actual.X, Expected.X, 0.005f));
        TestTrue(TEXT("Constant field G"), FMath::IsNearlyEqual(Actual.Y, Expected.Y, 0.005f));
        TestTrue(TEXT("Constant field B"), FMath::IsNearlyEqual(Actual.Z, Expected.Z, 0.005f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPRTGIFibonacciProjectionTest,
    "PRTGI.Math.FibonacciProjectionRecoversSH3",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPRTGIFibonacciProjectionTest::RunTest(const FString& Parameters)
{
    constexpr int32 SampleCount = 64;
    constexpr float ReconstructionTolerance = 2.0e-4f;
    const float KnownCoefficients[PRTGI::Math::CoefficientCount] =
    {
        0.72f, -0.18f, 0.31f, 0.09f, -0.07f, 0.11f, 0.04f, -0.13f, 0.08f
    };

    TArray<FVector3f> Directions;
    TArray<FVector3f> RepeatedDirections;
    TArray<float> ProjectionWeights;
    if (!TestTrue(TEXT("Directional projection construction succeeds"),
        PRTGI::Math::BuildDirectionalProjection(SampleCount, Directions, ProjectionWeights)))
    {
        return false;
    }
    PRTGI::Math::GenerateFibonacciSphereDirections(SampleCount, RepeatedDirections);
    TestEqual(TEXT("Fibonacci sample count"), Directions.Num(), SampleCount);
    TestTrue(TEXT("Fibonacci generation is deterministic"), Directions == RepeatedDirections);

    TArray<float> KnownSamples;
    KnownSamples.SetNumUninitialized(SampleCount);
    constexpr float ConstantValue = 0.625f;
    TArray<float> ConstantSamples;
    ConstantSamples.Init(ConstantValue, SampleCount);
    for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
    {
        float Basis[PRTGI::Math::CoefficientCount];
        PRTGI::Math::EvaluateSH3(Directions[SampleIndex], Basis);
        KnownSamples[SampleIndex] = 0.0f;
        for (int32 CoefficientIndex = 0; CoefficientIndex < PRTGI::Math::CoefficientCount; ++CoefficientIndex)
        {
            KnownSamples[SampleIndex] += KnownCoefficients[CoefficientIndex] * Basis[CoefficientIndex];
        }
    }

    float FittedKnown[PRTGI::Math::CoefficientCount] = {};
    float FittedConstant[PRTGI::Math::CoefficientCount] = {};
    for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
    {
        for (int32 CoefficientIndex = 0; CoefficientIndex < PRTGI::Math::CoefficientCount; ++CoefficientIndex)
        {
            const float Weight = ProjectionWeights[SampleIndex * PRTGI::Math::CoefficientCount + CoefficientIndex];
            FittedKnown[CoefficientIndex] += Weight * KnownSamples[SampleIndex];
            FittedConstant[CoefficientIndex] += Weight * ConstantSamples[SampleIndex];
        }
    }

    for (int32 CoefficientIndex = 0; CoefficientIndex < PRTGI::Math::CoefficientCount; ++CoefficientIndex)
    {
        TestTrue(
            *FString::Printf(TEXT("Known SH coefficient %d"), CoefficientIndex),
            FMath::IsNearlyEqual(FittedKnown[CoefficientIndex], KnownCoefficients[CoefficientIndex], ReconstructionTolerance));
    }

    const FVector3f EvaluationDirections[] =
    {
        FVector3f::XAxisVector,
        FVector3f::YAxisVector,
        FVector3f::ZAxisVector,
        FVector3f(-0.3f, 0.7f, -0.2f).GetSafeNormal()
    };
    for (const FVector3f& Direction : EvaluationDirections)
    {
        float Basis[PRTGI::Math::CoefficientCount];
        PRTGI::Math::EvaluateSH3(Direction, Basis);
        float ReconstructedConstant = 0.0f;
        for (int32 CoefficientIndex = 0; CoefficientIndex < PRTGI::Math::CoefficientCount; ++CoefficientIndex)
        {
            ReconstructedConstant += FittedConstant[CoefficientIndex] * Basis[CoefficientIndex];
        }
        TestTrue(TEXT("Constant response reconstructs at an unseen direction"),
            FMath::IsNearlyEqual(ReconstructedConstant, ConstantValue, ReconstructionTolerance));
    }
    return true;
}
#endif
