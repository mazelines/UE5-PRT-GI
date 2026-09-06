#pragma once

#include "CoreMinimal.h"

class FAutomationTestBase;
class APRTProbeVolume;

namespace PRTGI
{
#if WITH_DEV_AUTOMATION_TESTS
    PRTGI_API bool RunCacheIdentityChecks(FAutomationTestBase& Test);
#endif
    enum class EMode : uint8
    {
        Offline = 0,
        PRT = 1,
        Lumen = 2
    };

    /** 切换评测 GI。Offline 使用 UE baked lighting，PRT 使用本插件，Lumen 使用 UE Lumen。 */
    PRTGI_API void SetMode(EMode Mode);

    /** 从当前 UE 场景与主 DirectionalLight 重建整代 Probe SH 数据。 */
    PRTGI_API void RequestBake();

    /** Toggle the PRT indirect diffuse contribution view (Ctrl+Alt+4). */
    PRTGI_API void ToggleIndirectOnly();

    /** Toggle probe position markers (Ctrl+Alt+P), without changing GI or bake data. */
    PRTGI_API void ToggleProbes();
    PRTGI_API void CycleBounces();
    PRTGI_API void BuildVolume(APRTProbeVolume* Volume);
    PRTGI_API void CancelVolumeBuild(APRTProbeVolume* Volume);
#if WITH_EDITOR
    PRTGI_API FString GetVolumeGeometrySignature(APRTProbeVolume* Volume);
    // Editor migration only: caller must verify copied geometry/material equivalence.
    PRTGI_API bool ImportRelocatedVolumeCache(APRTProbeVolume* Volume,const FString& SourcePath,const FString& SourceSignature);
#endif

    /** 旋转唯一的 UE DirectionalLight；PRT 数据随后自动重建。 */
    PRTGI_API void RotatePrimaryDirectionalLight(float YawDeltaDegrees, float PitchDeltaDegrees);

    /** 返回当前模式、数据与重建进度，供 Console/MCP 验证。 */
    PRTGI_API FString GetStatus();
}
