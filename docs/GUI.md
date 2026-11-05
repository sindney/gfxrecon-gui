# GUI guide

## Prerequisites

For local replay on macOS, install the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#mac) and
[configure the Vulkan loader, MoltenVK and host tools](BUILD.md#macos).
The GUI and offline inspection do not need a local Vulkan runtime; opening
`.gfxr` files still needs `gfxrecon-convert` and shader extraction uses
`gfxrecon-extract`. Replaying on an Android phone does not require Vulkan on
the host Mac.

Android capture/replay needs SDK Platform-Tools (`adb`), USB or wireless debugging
with this host authorized, and a compatible debug replay/layer APK installed on
an unlocked Vulkan device. Capture targets must be debuggable Vulkan apps.
NDK, JDK and the remaining SDK build tools are only needed when building the APKs; the test sample APK and
validation layers are optional. See [Android setup](BUILD.md#android-device-capture-and-replay).

## Open and navigate

Use **File > Open** for a `.gfxr` capture, or **File > Samples** for the bundled
triangle, indexed cube and indirect triangles. Click a frame tile to render it;
click a submitted draw in **Events** to inspect that execution.

Search Events by API name, label or object. Pipeline/resource links open the
corresponding inspector. **View > Reset Layout** restores the default docks.

A draw click fetches shaders, geometry, images and the selected view in the
background. Previous images stay visible under the refresh overlay. Select a
different draw to supersede a query, or **File > Cancel current operation**.
Event/frame dots show progress; **Console** contains diagnostics.

## GPU performance

Click the Events clock/**Perf** button or **View > Perf** to open Perf alongside
the Pipeline/Image inspector tabs. It remains dockable; existing docked layouts
are retained, and older floating Perf windows join the inspector on reopening.
**Settings** contains **Frame**, **Counters**, **Runs**, and **Swapchain**.
Leave **Counters** unchecked for GPU duration only, or enable it to collect all
advertised provider counters supported together. There is no precision/bandwidth
filter. Provider co-collection limits leave explicit unavailable rows rather
than hiding metrics. **Counter us** appears while counters are enabled.

**Probe** optionally checks the GPU backend and provider. **Start** validates the
current settings and launches fresh
replay processes and becomes **Cancel** while running. Each process loads the
capture and renders the selected frame once. **Runs** controls repetitions;
with applied shader overrides, baseline and modified processes alternate.
Changing frame, counter capture, swapchain, or counter period does not require
another probe before starting. Invalid inputs disable Start. **Shader Override**
on the first settings row compares applied overrides against original shaders.
Unsupported counters
remain unavailable while supported GPU timing can still run.

The table shows each metric's unit, mean, minimum and maximum. With shader
overrides, paired values are original / modified. Hover a metric for sample
count, P95, standard deviation, coverage and measurement limitations. Select rows
to display their graphs; a single click toggles one row without changing others,
and Shift-click adds a range. Right-click the table for **Select all** / **Deselect all**
(matching rows when filtered). Any number of metrics can be selected, including GPU timing. The fixed-width
**Filter** input matches metric names/units in both the table and graphs. Filtering
preserves the selection, so clearing it restores previously selected graphs.
Bandwidth and other rates use readable SI units (KB/s, MB/s, GB/s, etc.) shared
by each metric's baseline/modified statistics, graph and tooltip. Saved reports
and exports retain the original provider units and raw values; 1 MB is 1000000 B.
Plots use milliseconds from each frame window's start, and each outer run is a
separate overlay. Points and lines preserve real zeros and missing-data gaps;
short windows may contain few counter samples. Right-click a graph for **Fit**.
The GPU timing graph's horizontal-axis tooltip explains milliseconds from frame
start. Wheel zoom changes only the shared time axis; vertical ranges stay fitted
to each metric's full data.

Hover the phone label for device/GPU/driver information and measurement scope. Progress,
errors, and artifact paths go to **Console** and the panel status dot.
**Open...** reopens a saved `result.json`; new results appear automatically.
Saved older reports remain inspectable. See [CLI measurement usage](CLI.md#gpu-frame-measurements).

## Replay memory compatibility

**Settings > Replay > Memory translation** selects the backend allocation policy.
For a capture made on another GPU/driver, try **Remap** to choose compatible memory
types. **Realign** and **Rebind** adapt allocations further but have different
limitations. The selected policy is explicit, saved, applied to frame/draw/shader
and Perf replay, and recorded in provenance with separate caches. Changing it
clears replay-derived views; render again to apply it.

**Default** preserves existing platform behavior: local macOS uses Rebind,
Windows/Android use None. Explicit None is honored everywhere. Cross-device replay
remains dependent on supported features/resources; successful replay is not a
promise of pixel-identical output or comparable GPU performance on different GPUs.

## Images

Click an **Inputs** or **Outputs** thumbnail to open its image tab. **Frame
presentation** shows the completed frame. Image identity, format, mip and slice
are in the footer; hover it for details. Image previews have two stable rows:

- Draw visualization and indirect-subdraw selection, with equal fixed widths,
  then **| Rotate / Flip H/V | Fit / scale**.
- RGB mask, sRGB and display range, including Auto/Reset.

During previews, unavailable controls stay visible and gray. Empty views show
only their placeholder, without a toolbar. Narrow panels scroll horizontally;
selecting a normal or indirect draw does not change the toolbar layout.

| Control | Use |
| --- | --- |
| Fit / zoom | Fit the canvas or choose a scale; wheel zooms at the pointer |
| Middle drag | Pan |
| RGB / sRGB | Inspect channels or encode the displayed linear RGB values for sRGB |
| Rotate / Flip | Change display orientation without changing source bytes |
| Range / Auto / Reset | Adjust black/white points or restore the display range |

**sRGB** is enabled by default. The viewer first interprets source pixels using
the captured resource format, applies the display range in linear RGB, then
encodes the result for an sRGB display when checked. For example, a linear value
of 0.5 appears near byte 188 when checked and byte 128 when unchecked. Existing
`_SRGB` resources are decoded before this transform, so their encoded values do
not receive gamma twice. Presentation images without resource-format metadata
keep the existing sRGB interpretation. Known UNORM and floating-point color
resources are treated as linear; an application that manually gamma-encoded
colors into UNORM may need the checkbox disabled to inspect its stored encoding.

This control changes the display transfer function; it does not apply exposure
or HDR tone mapping. Current backend color previews are clipped 8-bit images, so
the viewer cannot recover HDR values above that preview range. **Auto** fits the
range in linear space regardless of the checkbox. Alpha and raw pixel readouts
remain unchanged, and depth/stencil views use their scalar range without an sRGB
transfer. Thumbnails use the same source-format interpretation as the full view.

For HDR inspection, range mapping can be sufficient; a filmic tone mapper is
optional. [RenderDoc's texture viewer](https://github.com/baldurk/renderdoc/blob/v1.x/docs/window/texture_viewer.rst)
offers adjustable black/white points, Auto-fit and gamma display, as well as
custom visualization shaders. Complete HDR inspection here first requires a
float-preserving readback path, so values above 1 reach the viewer before any
display mapping. Adding exposure or a tone mapper to the current RGB8 preview
cannot recover highlights or precision already lost during export. Inspect
**Frame presentation** to see the application's final rendering; an intermediate
HDR attachment need not include its final tone mapping or color grading.

Modes: **Original**, **Solid coverage**, **Wireframe**, **Depth pass & fail**,
**UV density**, **Orange outline**. **Settings > Replay > Draw replay** defaults to
**Light (on demand)**: fetch the selected view and its required passes only.
**Full (all views)** preloads all six views. Both keep fetched views cached for
instant switching; selecting an unfetched view in Light starts a new query.
Indirect calls also offer All or one argument record.

Depth uses green for passing coverage and red for failing coverage. UVs are
selected automatically. Outline preserves original pixels inside the boundary.
These modes show geometry coverage: fragment discard, stencil and shader-written
depth are not reproduced.

## Shader editing

1. Select a pipeline stage and **View shader**.
2. Choose reconstructed GLSL or SPIR-V assembly; enable **Edit**.
3. Change the text and **Compile**. Read diagnostics in Console.
4. **Apply** replays and compares matching original/modified frames.

**Undo/Redo** change text only. Tooltips show Ctrl/Cmd+Z/Y; unavailable actions
are grey. **Reset shader** restores original text and the exact captured binary,
clears this module's candidate/override and replays automatically. Other module
overrides remain applied. A failed reset retains edits and the last working result.

**More**, beside Edit, contains copy/export and text-only revert. Export authored
text before switching documents when you need to retain it. A shared module
replacement affects every pipeline that uses that module.

## Meshes

Choose **Local (input)** or **Screen (post-VS)**, then solid, wireframe or points.
Left drag orbits local geometry; middle drag pans; wheel zooms; **Fit** restores
the view. **Position** chooses an attribute; **Instance** chooses a relative instance.
Indirect batches default to **All drawable meshes**, with local record selection.
The Vertices table shows original IDs and coordinates.

Post-VS positions are evaluated locally in OpenGL using replay-readback inputs,
not instrumented on the replay device. Unsupported formats, shaders and missing
resources are explained in the panel.

## Capture and settings

Open **File > Capture application** and follow the target/setup/recording steps.
On-demand capture arms the process first; trigger after it reaches the desired
scene. Completed captures offer **Save** and **Save As**.

Windows sample: `tools/gfxrecon-gui-live.exe`, working directory `samples/test-res`.
For Android, connect an authorized device and install the bundled sample/replay
APKs from the setup controls. The target must support capture; restart it when
enabling its capture layer.

Right-click a frame to choose replay target, validation, optimization or screen-off
replay. Select a serial in **Tools and devices** when several phones are connected.
Unlock secure locks on the phone, then choose **Retry replay**. **Settings** is a
dialog for theme, draw replay and logging preferences.
