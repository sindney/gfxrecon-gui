# Build and test

## Windows

Use Visual Studio 2022 with C++/Windows SDK support, CMake 3.24+, Git and Python 3.10+.

```powershell
git submodule update --init --recursive
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release --output-on-failure
```

Executables are in `build/Release`. SDL3 is the default backend; configure
`-DIMGUI_TEMPLATE_OS_LAYER=SFML` for the alternative. Native macOS builds and
app/CLI ZIP packaging are also supported below.

## macOS

Use Xcode Command Line Tools, CMake 3.24+, Ninja, Git and Python 3.10+.
The default SDL3 backend builds native app bundles and a console CLI:

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
ctest --test-dir build -C Release --output-on-failure
```

The executables are `build/gfxrecon-cli` and
`build/gfxrecon-gui.app/Contents/MacOS/gfxrecon-gui`. The GUI bundles include
the captures used by **File > Samples** and an `.icns` generated from
`assets/app-icon.png`. App resources and bundled tools live in `Contents/Resources`.
GUI and CLI settings, sessions and caches share
`~/Library/Application Support/GFXReconstruct Inspector`; `--settings-dir DIR`
overrides that location. They do not modify the signed app bundle.

For sample decoding, shader compilation and local GPU replay, install the
[LunarG Vulkan SDK for macOS](https://vulkan.lunarg.com/sdk/home#mac)
with MoltenVK and build the pinned host tools. Local replay requires the Vulkan
loader and driver; this build does not bundle them. The full SDK is the
supported way to obtain them for this development setup. The GUI and offline
inspection do not require a local Vulkan runtime, and replaying on an Android
device does not require Vulkan on the host Mac. Set `VULKAN_SDK` to its
`macOS` directory, then run:

```sh
export VULKAN_SDK="$HOME/VulkanSDK/1.4.363.0/macOS"
export GFXRECON_VULKAN_LIBRARY_PATH="$VULKAN_SDK/lib/libvulkan.dylib"
export VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
python3 scripts/setup_backend.py host --shader-bin "$VULKAN_SDK/bin"
```

The setup script records host tool paths in ignored `local-tools.json`.
The explicit loader path also survives launches through Apple's system Python,
which strips `DYLD_LIBRARY_PATH` from its environment.
For host-only testing, set its `adb` entry to an empty string to disable device
discovery. macOS replay rebinds captured allocations to the local GPU's memory
requirements; captures can still require unavailable GPU features or extensions.
Live application capture remains Windows/Android only.

Run the real GUI sample checks without selecting a device or capturing a game:

```sh
python3 scripts/test_workflow.py --capture samples/windows-triangle.gfxr \
  --mesh-capture samples/indexed-cube.gfxr \
  --indirect-capture samples/indirect-triangles.gfxr \
  --screenshot .sessions/macos/inspector.png
build/gfxrecon-cli dump --capture samples/indexed-cube.gfxr --out .sessions/macos/cube-dump
build/gfxrecon-gui.app/Contents/MacOS/gfxrecon-gui --open samples/indexed-cube.gfxr
```

GUI tests require an interactive macOS desktop. The sample workflow covers
frame/draw replay, shader compile/apply/reset, indexed input and post-VS mesh
preview, and indirect draw visualization. Android captures can be decoded
offline; Android replay tests remain opt-in.

## Replay/capture tools

Offline inspection can use converted data without a replay target. Capture,
shader compilation and replay need configured tools. `gfxrecon-cli doctor`
reports availability. Machine-local paths belong in ignored `local-tools.json`.

Build the pinned host backend, specifying the directory with `glslangValidator`,
`spirv-as`, `spirv-dis` and `spirv-val`:

```powershell
python scripts/setup_backend.py host --shader-bin "PATH_TO_SHADER_TOOLS"
python scripts/setup_validation.py
```

## Android device capture and replay

Using compatible prebuilt APKs requires:

- [Android SDK Platform-Tools](https://developer.android.com/tools/releases/platform-tools)
  for the host OS (`adb`); Android Studio and the NDK are not needed to use them.
- A Vulkan-capable Android device with GPU debug-layer loading support. Enable
  USB or wireless debugging, authorize this host, and unlock the phone.
- Our compatible **debug replay APK**, matching the device ABI. It includes the
  capture layer and must remain debuggable for app-private file transfer.
- For capture, a **debuggable Vulkan target app**; this workflow uses `run-as`
  and cannot capture arbitrary non-debuggable production APKs.
- Host `gfxrecon-convert`/`gfxrecon-extract` for inspection, plus shader tools when
  editing shaders. A local macOS Vulkan runtime is unnecessary for device replay.

Configure `adb` and `replay_apk` in **Tools and devices** or `local-tools.json`.
Use **File > Capture application > Android** to select a device, check setup
and install the replay APK. The sample APK (`test_apk`) is only needed for the
bundled Android test workload. Validation layers are only needed when enabling
validation. Replay still depends on GPU/extension compatibility; use the original
capture device when possible.

Building the Android APKs from source additionally requires:

- JDK 17 and Android SDK command-line tools, with accepted SDK licenses.
- SDK Platform **34** and NDK **26.3.11579264**, pinned in
  `thirdparty/gfxreconstruct/android/gradle.properties`.
- SDK Build-Tools ([AGP 9.2 uses **36.0.0**](https://developer.android.com/build/releases/agp-9-2-0-release-notes)) and CMake/Ninja for native builds.
  Gradle is supplied by the checked-in wrapper; Android Studio is optional.

```sh
python3 scripts/setup_backend.py android --sdk-root "PATH_TO_ANDROID_SDK" --java-home "PATH_TO_JDK_17"
```

The script uses `gradlew` and `adb` on macOS/Linux, and their Windows equivalents
on Windows. It builds arm64 debug replay/sample APKs and records their paths;
installing them and running device checks are separate steps. Device checks
can also use compatible prebuilt debug APKs. APK compilation on
this Mac has not been validated; the real-device workflow was tested using APKs
already installed on the connected phone, with their hashes recorded locally.

To exercise the real GUI capture/replay workflow on an authorized, unlocked
device with `adb`, `replay_apk` and `test_apk` explicitly configured:

```sh
python3 scripts/test_workflow.py --android SERIAL --screenshot .sessions/android/inspector.png
```

This installs the configured replay/sample APKs, checks cancelled capture
restoration, records the sample and exercises inspection and shader replay.
Validation is a separate opt-in replay check; review its reported errors even
when the capture/replay workflow completes successfully.

## Public regression samples

The checked-in samples are triangle, indexed cube and indirect triangles:

```powershell
python scripts/test_workflow.py --capture samples/indexed-cube.gfxr
```

`verify_mesh.py`, `verify_draw_visualization.py` and `verify_secondary.py` generate
or reuse public Vulkan fixtures for focused checks. Tests run the real GUI through
ImGui Test Engine; ImGui errors and ID conflicts fail acceptance. Generated
captures/evidence stay under ignored `.sessions/` and build directories.

`verify_android_readback.py --help` describes an opt-in check that full Android
draw inspection retrieves all referenced vertex/index/uniform buffer files.

## Package

On macOS, after configuring the host tools above:

```sh
python3 scripts/build.py release
```

This builds and tests the Release targets, then creates
`dist/gfxrecon-gui/gfxrecon-gui.app`, the `dist/gfxrecon-gui/gfxrecon-cli` launcher,
`dist/gfxrecon-gui-macos-arm64.zip` (or `x86_64` for an Intel build), and its
`.sha256` checksum. `python3 scripts/build.py package` reuses an existing build.
The app contains the logo, native host/shader tools, samples and licenses. ADB
and Android APKs are included only when explicitly configured; no device is
queried during packaging.

Verification extracts the ZIP outside the repository, checks file hashes,
executable permissions and signatures, and runs CLI decode/shader checks and
GUI/Launch Services startup with a clean PATH. When the explicit
`GFXRECON_VULKAN_LIBRARY_PATH` environment variable is supplied, it also verifies
frame and full-draw replay for all three desktop samples. Verification results
are in `build/packaged-macos-verification.json` and
`build/packaged-macos-workflow.log`.

macOS builds are ad-hoc signed for local testing and are not notarized. The ZIP
preserves app structure and executable permissions; its companion `.sha256`
file and the package's `build-info.json` record archive and file hashes.

From the extracted macOS distribution folder, open `gfxrecon-gui.app` or run
`./gfxrecon-cli --help`. **File > Samples** opens the four bundled captures.
For CLI inspection:

```sh
./gfxrecon-cli info --capture gfxrecon-gui.app/Contents/Resources/samples/indexed-cube.gfxr
```

The Vulkan runtime remains external. For local GPU replay, configure the SDK
environment described in [macOS setup](#macos), then launch:

```sh
./gfxrecon-gui.app/Contents/MacOS/gfxrecon-gui
```

Offline inspection and shader tools work without the Vulkan runtime. Host tools
resolve from the bundle; **Tools and devices** and CLI `--tools FILE` allow manual
overrides. GUI and CLI share settings, sessions and caches in
`~/Library/Application Support/GFXReconstruct Inspector`; `--settings-dir DIR`
selects another location. Keep the signed app bundle intact.

For device use, see [Android prerequisites](#android-device-capture-and-replay).
Check `build-info.json` to see whether ADB and the Android APKs are included.

On Windows:

```powershell
python scripts/build.py release
```

This builds, tests and verifies an extracted ZIP with a clean PATH. Results are
`dist/gfxrecon-gui`, the ZIP and its checksum. When preserving an installation's
sessions/settings, package to a separate staging destination first:

```powershell
python scripts/build.py package --output-dir .sessions/release-stage/gfxrecon-gui
```

## Source map

| Directory | Responsibility |
| --- | --- |
| `src/analysis` | Indexes, state, dump schema, resource decoding |
| `src/replay`, `src/capture` | Shared services and provenance |
| `src/gui`, `src/cli` | Workspace panels; headless commands/help/JSON |
| `src/tests`, `tests` | Native, GUI and packaging tests |
| `cmake` | Build-copy patches for the pinned replay backend |

Keep submodules pinned. Backend changes are applied to generated copies through
CMake; do not edit vendored replay sources in place.
