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

A draw click fetches shaders, geometry, images and diagnostic modes in the
background. Previous images stay visible under the refresh overlay. Select a
different draw to supersede a query, or **File > Cancel current operation**.
Event/frame dots show progress; **Console** contains diagnostics.

## Images

Click an **Inputs** or **Outputs** thumbnail to open its image tab. **Frame
presentation** shows the completed frame. Image identity, format, mip and slice
are in the footer; hover it for details.

| Control | Use |
| --- | --- |
| Fit / zoom | Fit the canvas or choose a scale; wheel zooms at the pointer |
| Middle drag | Pan |
| RGB / sRGB | Inspect channels or change display interpretation |
| Rotate / Flip | Change display orientation without changing source bytes |
| Range / Auto / Reset | Adjust black/white points or restore the display range |

Modes: **Original**, **Solid coverage**, **Wireframe**, **Depth pass & fail**,
**UV density**, **Orange outline**. A completed query caches these modes locally;
switching starts no replay. The initial query can take several replay passes.
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
dialog for theme and logging preferences.
