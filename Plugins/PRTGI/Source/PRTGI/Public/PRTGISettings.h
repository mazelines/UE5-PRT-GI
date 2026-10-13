#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PRTGISettings.generated.h"

USTRUCT()
struct FPRTGILocalVolume
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Probe Volume")
    FString Name;
    UPROPERTY(EditAnywhere, Category="Probe Volume")
    FVector BoundsMin = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, Category="Probe Volume")
    FVector BoundsMax = FVector(100.0);
    UPROPERTY(EditAnywhere, Category="Probe Volume")
    FIntVector GridCounts = FIntVector(8);
    UPROPERTY(EditAnywhere, Category="Probe Volume")
    FString DataFile;
    /** Explicit receiver bounds, independent of inset probe centers. */
    UPROPERTY(EditAnywhere, Category="Probe Volume")
    FVector ReceiverMin = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, Category="Probe Volume")
    FVector ReceiverMax = FVector(100.0);
    /** Optional transition only at an opening: 0=none, 1/2=-/+X, 3/4=-/+Y, 5/6=-/+Z. */
    UPROPERTY(EditAnywhere, Category="Probe Volume", meta=(ClampMin="0", ClampMax="6"))
    int32 BlendFace = 0;
    UPROPERTY(EditAnywhere, Category="Probe Volume", meta=(ClampMin="0.0"))
    float BlendDistance = 0.0f;

    bool IsValid() const;
};

USTRUCT()
struct FPRTGIMapProfile
{
    GENERATED_BODY()

    /** Long package name, without object suffix or PIE prefix. */
    UPROPERTY(EditAnywhere, Category="Map")
    FString MapPackage;

    UPROPERTY(EditAnywhere, Category="Map")
    FVector BoundsMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="Map")
    FVector BoundsMax = FVector(1000.0);

    UPROPERTY(EditAnywhere, Category="Map")
    FIntVector GridCounts = FIntVector(7, 7, 4);

    /** Must be unique per map; relative to Content. */
    UPROPERTY(EditAnywhere, Category="Map")
    FString DataFile;

    /** Evaluation-only: enable hardware Lumen and disable diffuse screen traces.
     * Previous values are restored when leaving this profile's map. */
    UPROPERTY(EditAnywhere, Category="Map")
    bool bHardwareLumenComparison = false;

    /** Extend receiver coverage beyond the probe centers (cm), clamping sampling
     * to the edge probes. Keeps the outer probes out of solid Cornell walls. */
    UPROPERTY(EditAnywhere, Category="Map", meta=(ClampMin="0.0"))
    float ReceiverBoundsPadding = 0.0f;

    /** Independently baked refinements. Fine volume replaces, never adds to, the global estimate. */
    UPROPERTY(EditAnywhere, Category="Map")
    TArray<FPRTGILocalVolume> LocalVolumes;
};

UCLASS(Config=Engine, DefaultConfig, meta=(DisplayName="PRT GI"))
class PRTGI_API UPRTGISettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPRTGISettings();

    /** Optional isolated probe bounds and caches for additional evaluation maps. */
    UPROPERTY(Config, EditAnywhere, Category="Map Profiles")
    TArray<FPRTGIMapProfile> MapProfiles;

    const FPRTGIMapProfile* FindMapProfile(const UWorld* World) const;
    FIntVector GetGridCounts(const UWorld* World) const;

    /** 默认对 Sponza render mesh 自动计算边界。 */
    UPROPERTY(Config, EditAnywhere, Category="Probe Grid", meta=(ToolTip="Automatically detect bounds from Sponza render meshes by default."))
    bool bAutoDetectBounds = true;

    /** 自动边界检测只接收此前缀下的 StaticMesh asset；留空表示所有 StaticMesh。 */
    UPROPERTY(Config, EditAnywhere, Category="Probe Grid", meta=(ToolTip="Restrict automatic bounds detection to StaticMesh assets under this path prefix. Leave empty to include all StaticMesh assets."))
    FString GeometryAssetPathPrefix = TEXT("/Game/ClassicSponza/Art/Sponza/Meshes");

    /** bAutoDetectBounds=false 时使用的世界坐标边界，单位 cm。 */
    UPROPERTY(Config, EditAnywhere, Category="Probe Grid", meta=(ToolTip="World-space bounds in cm, used when bAutoDetectBounds is false."))
    FVector ManualBoundsMin = FVector(-1200.0, -1800.0, -200.0);

    UPROPERTY(Config, EditAnywhere, Category="Probe Grid")
    FVector ManualBoundsMax = FVector(1200.0, 1800.0, 1400.0);

    /** X/Y/Z 规则网格分辨率。系数以 X fastest 顺序保存。 */
    UPROPERTY(Config, EditAnywhere, Category="Probe Grid", meta=(ToolTip="Regular grid resolution along X/Y/Z. Coefficients are stored with X varying fastest.", ClampMin="2", ClampMax="32"))
    // demo 使用 Y-up 的 7x4x7；映射到 UE Z-up 后为 7x7x4。
    FIntVector GridCounts = FIntVector(7, 7, 4);

    /** 防止最外层 Probe 正好落在墙面上的边界内缩比例。 */
    UPROPERTY(Config, EditAnywhere, Category="Probe Grid", meta=(ToolTip="Fraction by which bounds are inset to keep the outer probes off wall surfaces.", ClampMin="0.0", ClampMax="0.2"))
    float BoundsInsetFraction = 0.02f;

    /** 每个 cubemap face 的分辨率；小开口的采集精度需独立验证。 */
    UPROPERTY(Config, EditAnywhere, Category="Bake", meta=(ToolTip="Resolution of each cubemap face. Validate capture accuracy separately for small openings.", ClampMin="4", ClampMax="128"))
    int32 CaptureResolution = 16;

    /** PRT 专用近裁剪距离（cm），仅在采集调用内使用，不修改玩家相机。 */
    UPROPERTY(Config, EditAnywhere, Category="Bake", meta=(ToolTip="PRT capture near clipping distance in cm. Applies only during capture and does not change the player camera.", ClampMin="0.01", ClampMax="10.0"))
    float CaptureNearClipCm = 1.0f;

    /** 缺失或失效缓存不自动启动昂贵烘焙；由显式 Bake 命令重建。 */
    UPROPERTY(Config, EditAnywhere, Category="Bake", meta=(ToolTip="Whether missing or stale caches trigger an automatic bake. Disabled by default; use an explicit Bake command to rebuild."))
    bool bAutoBakeMissingCache = false;

    /** 独立几何采集分辨率，不影响方向矩阵缓存。 */
    UPROPERTY(Config, EditAnywhere, Category="Surfel", meta=(ToolTip="Geometry capture resolution, independent of the directional matrix cache.", ClampMin="4", ClampMax="32"))
    int32 SurfelCaptureResolution = 16;

    /** CPU 验证后端的表面法线偏移（cm），用于避免 shadow ray 自相交。 */
    UPROPERTY(Config, EditAnywhere, Category="Surfel", meta=(ToolTip="Surface normal offset in cm for the CPU validation backend, used to avoid shadow ray self-intersections.", ClampMin="0.001", ClampMax="1.0"))
    float SurfelShadowBiasCm = 0.1f;

    float GetCaptureNearClipCm() const
    {
        return FMath::IsFinite(CaptureNearClipCm) ? FMath::Clamp(CaptureNearClipCm, 0.01f, 10.0f) : 1.0f;
    }

    /** 用于拟合 3-band directional transport 的 Fibonacci sphere 光照方向数。 */
    UPROPERTY(Config, EditAnywhere, Category="Bake", meta=(ToolTip="Number of Fibonacci sphere light directions used to fit three-band directional transport.", ClampMin="9", ClampMax="128"))
    int32 DirectionalSampleCount = 32;

    /** Bake 每个方向使用的 DirectionalLight 参考强度，单位 lux；缓存会按此值归一化。 */
    UPROPERTY(Config, EditAnywhere, Category="Bake", meta=(ToolTip="Reference DirectionalLight intensity in lux for each bake direction. The cache is normalized by this value.", ClampMin="1.0", ClampMax="1000000.0"))
    float TransportReferenceIntensity = 100000.0f;

    /** 每个 Editor tick 执行的 cubemap capture 数；同步 GPU readback，默认保持 1。 */
    UPROPERTY(Config, EditAnywhere, Category="Bake", meta=(ToolTip="Cubemap captures per Editor tick. Uses synchronous GPU readback; defaults to 1.", ClampMin="1", ClampMax="16"))
    int32 ProbesPerTick = 1;

    /** 保存位置，相对于 Project Content directory。 */
    UPROPERTY(Config, EditAnywhere, Category="Persistence", meta=(ToolTip="Save path relative to the project Content directory."))
    FString DataFile = TEXT("PRTGI/ClassicSponza.prtgi");

    /** Editor、PIE 和 desktop game 快捷键每次旋转的角度。 */
    UPROPERTY(Config, EditAnywhere, Category="Interaction", meta=(ToolTip="Rotation step in degrees for Editor, PIE, and desktop game shortcuts.", ClampMin="0.1", ClampMax="45.0"))
    float RotationStepDegrees = 5.0f;
};
