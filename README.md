# UE5-PRT-GI

**English** | [简体中文](README.zh-CN.md)

A probe-based global illumination plugin for static scenes in UE 5.8. Includes a Cornell Box showcase, real-time directional light relighting, 1–4 indirect bounces, independent Probe Volumes, and visualization.

![PRT GI Cornell Box showcase](docs/images/prt-gi-showcase.png)

## Installation

**Git LFS** is required when fetching this plugin through Git: precomputed caches are stored in LFS. After cloning, run `git lfs install --local` and `git lfs pull` in the plugin repository directory to ensure that the `.surfels` / `.multi` files contain actual binary data rather than LFS pointer text. The repository root is the plugin directory; install it at the `Plugins/PRTGI` path shown below.

1. Place the plugin in `YourProject/Plugins/PRTGI`. This directory must directly contain `PRTGI.uplugin`, without an extra nested repository folder.
2. Build the **Editor target** using the UE version and C++ toolchain that match your host project. Blueprint projects also need an environment capable of compiling this C++ plugin; add a C++ class first if needed to generate a project target.
3. In **Edit → Plugins**, search for **UE5-PRT-GI**, enable it, and restart the Editor when prompted.

Currently validated on **Windows, UE 5.8, and DX12 / SM6**, using deferred rendering. The GI pass requires the GBuffer; Forward Shading is not supported by this path. The plugin references internal Renderer interfaces, so other UE versions and platforms require recompilation and validation. Precompiled DLLs are not guaranteed to be compatible across Engine builds. The current implementation does not require additional Engine source modifications to install the plugin.

## Quick Start

1. Enable **Show Plugin Content** in the Content Browser.
2. Open **PRTGI Content → Maps → PRTGI_Showcase**, asset path `/PRTGI/Maps/PRTGI_Showcase`.
3. Click the Editor viewport or PIE game view, then press **Ctrl+Alt+2** to enable PRT and **Ctrl+Alt+3** to compare it with Lumen.
4. Press **Ctrl+Alt+4** to inspect PRT indirect lighting only, and **Ctrl+Alt+P** to view probes.
5. Press **Ctrl+Alt+[ / ]** to rotate the sun and observe the relighting results.

This plugin depends on the current UE 5.8 Renderer interfaces. Comparing against hardware ray tracing Lumen requires DX12 / SM6, Ray Tracing, and the relevant support settings in the project. These are host project rendering settings; the plugin does not forcibly override the project's rendering configuration.

When comparing against hardware ray tracing Lumen, enable **Support Hardware Ray Tracing**, **Support Compute Skin Cache**, and **Use Hardware Ray Tracing when available**, and use a supported GPU. Restart the Editor after changing settings that require it. See Epic's [Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine).

The bundled showcase's default map profile temporarily sets `r.Lumen.HardwareRayTracing=1`, `r.Lumen.ScreenProbeGather.ScreenTraces=0`, and `r.EyeAdaptation.CachedLightingPreExposure=8`. Previous values are restored when leaving the map. Its Lumen comparison therefore has diffuse screen traces disabled; it is not the default Lumen configuration. These overrides do not enable missing project-level hardware ray tracing support.

See [showcase-renderer-settings.ini](docs/showcase-renderer-settings.ini) for the configuration used in standalone validation. Selectively merge the settings into your host project as needed; this file does not take effect automatically. Do not use it to overwrite the entire project configuration.

The showcase uses a single shared UE DirectionalLight. The original area-light regions of the boxes have been replaced with openings, and the front openings are retained for viewing.

| Volume | Bundled cache |
|---|---|
| PRT_Global, PRT_ClassicBox, PRT_ColorBox, PRT_InvertedBox | Built for 1–4 indirect bounces; defaults to 4 at runtime |
| PRT_ShinyBox | Built at 16×16 per face; 21 of 878 audited probes excluded with a capture warning |
| PRT_MetallicBox | Built at 16×16 per face, with 40 cm probe spacing and a 2 cm transport cell size; 77 of 2,212 audited probes excluded with a capture warning |

If your download does not include the large cache files, run **Build Precomputation** before comparing the lighting results.

## Adding a Probe Volume to Your Scene

1. Save the level and ensure that the geometry and materials participating in precomputation remain static.
2. Add a UE **DirectionalLight** and set its Mobility to **Movable** for real-time rotation. If there are multiple directional lights, add the Actor Tag `PRTGI.PrimaryLight` to the intended light.
3. Search for **PRT Probe Volume** in Place Actors and drag it into the scene.
4. Configure the volume bounds, probe spacing, Near Clip, and Priority.
5. Enable PRT GI, then click **Build Precomputation** under the Actor's Details → **PRT → Build**.
6. Wait for completion, check **Cache Status**, **GI Active**, **Effective Bounces**, and **Build Warnings** under **PRT → Status**, then save the level.

Volumes contribute indirect lighting and display probes only in **PRT GI mode**. Switching to Lumen disables the PRT GI contribution.

### Parameters

| Parameter | Description |
|---|---|
| Enabled | Whether this volume is enabled |
| Extent | Half-size along each axis, in cm; Actor Scale also affects the bounds |
| Probe Spacing | Target maximum spacing along each axis, in cm; see Actual Spacing for the resulting uniform spacing |
| Probe Inset | Distance by which the probe grid is inset from the volume boundaries, in cm |
| Priority | Use a higher priority for locally dense regions |
| Near Clip Cm | Near clipping distance for probe captures, default 1 cm, range 0.01–10 cm; separate from the main camera's Near Clip |
| Capture Resolution | Resolution of each capture face, default 16, range 4–32; increasing it raises capture cost |
| Transport Cell Size | Static surface aggregation size, default 2 cm, range 1–100 cm; smaller values generally increase data size and computation |
| Blend Face / Distance | One transition face and its distance in cm: 0=none, 1/2=−/+X, 3/4=−/+Y, 5/6=−/+Z; configure only at an opening where a transition is needed |
| Display Mode | Hidden, Positions, or Lighting; Lighting shows each probe's lighting color |
| Probe Radius | Radius of the visualization spheres, in cm; does not change the actual sampling positions |

Volumes are currently **axis-aligned** and do not support rotation. Each axis supports up to 32 probes. Increase the spacing or split the volume if this limit is exceeded.

There is also a separate **4,000,000 capture sample budget** per volume: `ProbeCounts.X × ProbeCounts.Y × ProbeCounts.Z × 6 × CaptureResolution²` must not exceed this limit. At the default resolution of 16, this allows at most 2,604 probes in total, subject to the per-axis limit. Reduce capture resolution, increase probe spacing, or split the volume if the budget is exceeded.

A starting point is Priority=0 with roughly 200 cm spacing for a global volume, and Priority=10 with roughly 30–40 cm spacing for local volumes. Adjust these values to your scene's dimensions. Avoid placing probes inside walls or configuring blend transitions across solid walls. An excessively large Near Clip can clip geometry close to a probe, causing incorrect visibility or light leaks.

### Precomputation and Progress

- Select one or more volumes and click **Build Precomputation** to queue their builds. Use **Cancel Build** to cancel builds for the selected volumes.
- Under **PRT → Build**, **Build Progress** covers geometry capture, transport computation, and saving. **Build Stage** describes the current operation; both appear alongside the build buttons. An existing valid geometry cache is reused, so a build may run only the transport stage. Progress is weighted by stage and is not an estimate of remaining time.
- The current build combines UE scene capture with CPU transport precomputation. Runtime relighting uses the GPU by default; the entire build is not a GPU-only bake.
- A build saves the data needed to switch between 1–4 indirect bounces. Switching between cached bounce counts at runtime does not require recapturing the scene. The default is 4 bounces.
- Build time depends on probe count, capture resolution, geometry complexity, transport cell size, and hardware. Validate a small volume first, then expand the coverage.
- Rebuild after changing static geometry, materials, the probe grid layout, or capture parameters. Duplicating a volume generates a new GUID, so the duplicate needs its own cache.

Probes with geometry audit mismatches or empty captures are excluded from transport and interpolation. A build can finish with a warning if the excluded count is at most 5% of audited probes and the remaining data passes validation; larger failures stop the build. Inspect **Build Warnings**, **Invalid Probes**, and the affected area before using the result. The capture audit uses collision queries on the **Visibility** channel, so participating geometry needs collision that matches its rendered surfaces and blocks this channel.

Each volume is limited to **16 million transport entries**, at 64 bytes per entry, for up to approximately **976.6 MiB** of transport data. The file loading limit allows an additional 128 MiB for geometry indices and other data. This is not a total memory budget: temporary build copies, CPU caches, and GPU buffers require additional memory. Capture resolution is not automatically reduced when approaching the limit.

## Content Layout

- `Content/Maps`: Showcase level.
- `Content/Showcase/Materials`, `Meshes`, etc.: Copies of project assets required by the showcase, with references redirected to assets inside the plugin.
- `Content/Materials`: Probe visualization and geometry capture materials.
- `Content/Precomputed/Volumes`: Raw `.surfels` / `.multi` caches, separated by level package name and volume GUID; they do not contain light parameters.

The `.surfels` file stores captured geometry data, and its paired `.multi` file stores multi-bounce transport data. Keep both files together. These are raw binary files and do not appear as `.uasset` files in the Content Browser. The caches are not final images for a fixed sun angle: at runtime, the plugin still computes the current light input and occlusion before applying the precomputed transport.

For plugin levels under `/PRTGI/`, caches are saved inside the plugin at `Content/Precomputed/Volumes/<level-package>/<GUID>.prtgi.surfels`, with a companion `<GUID>.prtgi.surfels.multi`. Other levels, including your own `/Game/` levels, save to `YourProject/Content/PRTGI/Volumes/<level-package>/` instead. Slashes in the level package name are replaced with underscores. Moving or renaming a level changes its cache path, so check **Cache File** and rebuild if needed.

The plugin's build rules stage its own `Content/Precomputed` directory as loose **NonUFS** files. For packaged projects using `/Game/` levels, also stage `Content/PRTGI/Volumes` via **Additional Non-Asset Directories To Copy** (path relative to project Content: `PRTGI/Volumes`). Verify cache loading in the packaged build; the included validation report covers Editor usage.

## Current Limitations

- PRT computes indirect diffuse lighting driven by one selected DirectionalLight. Point, spot, and rect lights, SkyLight, and emissive materials do not drive this PRT transport; specular GI and reflections are not supplied by the plugin.
- Geometry and materials used by the transport must remain static. Runtime light changes reuse cached geometry for GPU occlusion; moving occluders are not incorporated into that cached transport automatically.
- Curved surfaces can show probe-related rings, and brightness can differ from Lumen. Check the results in your scene before relying on the lighting comparison.

## Keyboard Shortcuts

Click the Editor / PIE viewport first to give it focus.

| Shortcut | Action |
|---|---|
| Ctrl+Alt+1 | Disable dynamic GI; this showcase has no offline baked lighting |
| Ctrl+Alt+2 | PRT GI |
| Ctrl+Alt+3 | Lumen GI |
| Ctrl+Alt+4 | Toggle PRT indirect diffuse only / full view |
| Ctrl+Alt+P | Show / hide probes; the volume's Display Mode determines whether they appear as position markers or lighting spheres |
| Ctrl+Alt+G | Cycle through 1–4 indirect bounces; requires the corresponding cache |
| Ctrl+Alt+[ / ] | Decrease / increase DirectionalLight Yaw by 5° (default step) |
| Ctrl+Alt+Shift+[ / ] | Decrease / increase DirectionalLight Pitch by 5° (default step) |
| Ctrl+Alt+B | Legacy global bake command; does not build Actor Volumes |

For local precomputation, select the corresponding `PRTProbeVolume` and click **Build Precomputation**. The default is 4 indirect bounces.

If focus or keyboard layout interferes with the shortcuts, run these commands individually in the UE Console:

```text
PRTGI.SetMode PRT
PRTGI.SetMode Lumen
PRTGI.ToggleIndirectOnly
r.PRTGI.ShowProbes 1
r.PRTGI.Bounces 4
PRTGI.RotateLight 5 0
PRTGI.Status
```

`PRTGI.SetMode Offline` is a legacy command name for exiting dynamic GI. It does not automatically generate offline lighting or a Path Tracing reference image.
