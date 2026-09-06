#pragma once
#include "CoreMinimal.h"

namespace PRTGI { class FSurfelVolume; struct FMultiBounceData; }

/** Static first-hit planes reconstructed from the existing geometry cubemaps.
 * Relative to each probe: dot(normal, hit - probe) = plane.w. No light values.
 */
struct FPRTGIProbeVisibility
{
    int32 Resolution = 0;
    TArray<FVector4f> Planes;
    static TSharedPtr<const FPRTGIProbeVisibility, ESPMode::ThreadSafe> Build(const PRTGI::FSurfelVolume& Base);
    // Capture near planes enclose a cube, not a sphere. Derive at load time;
    // never change persisted transport/validity or depend on the current light.
    static TArray<uint8> BuildNearClipMask(const PRTGI::FSurfelVolume& Base, const PRTGI::FMultiBounceData& Geometry);
};
