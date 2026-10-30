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
`-DIMGUI_TEMPLATE_OS_LAYER=SFML` for the alternative. CMake supports native macOS
builds; portable release packaging is Windows x64 only.

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

Android requires the Android SDK/NDK and JDK:

```powershell
python scripts/setup_backend.py android --sdk-root "PATH_TO_ANDROID_SDK" --java-home "PATH_TO_JDK"
```

Versions are pinned in the backend's Android build files. Install the generated
debug replay/sample APKs on an authorized device for device checks.

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
