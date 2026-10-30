# GFXReconstruct Inspector

A Vulkan capture inspector with a desktop GUI and a JSON CLI. Inspect draws,
images, buffers and meshes; edit shaders and verify changes by replay.

![Wireframe draw inspection in GFXReconstruct Inspector](docs/images/inspector-wireframe.png)

## Start

Extract the Windows x64 release and run `gfxrecon-gui.exe`. Keep the folder
writable: settings, sessions and caches live beside the executable.

1. Open **File > Samples > Indexed cube (mesh)**.
2. Select a submitted draw in **Events**.
3. Use **Image** for attachments, **Mesh** for geometry, and **Pipeline** for bindings.
4. Open a shader, enable **Edit**, then **Compile** and **Apply**.
5. **Reset shader** restores the captured shader and replays automatically.

The bundled triangle, cube and indirect-triangle captures work without a phone.
Android replay needs a compatible, authorized, unlocked device and the replay APK.

## CLI

Run from the extracted release folder:

```powershell
.\gfxrecon-cli.exe --help
.\gfxrecon-cli.exe dump --capture samples/indexed-cube.gfxr --out decoded
.\gfxrecon-cli.exe replay draw --capture samples/indexed-cube.gfxr --execution 0 --full
```

Commands return JSON; help is plain text. Use `describe` for the machine-readable
command schema, or `<command> --help` for command options.

## Guides

- [GUI: capture, inspect and edit](docs/GUI.md)
- [CLI: decode, search, replay and probe](docs/CLI.md)
- [Build, test and package](docs/BUILD.md)

The GUI and CLI share capture/replay services and caches. Original captures are
preserved; shader replacements and replay outputs are separate artifacts.

## Build

```powershell
git submodule update --init --recursive
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release --output-on-failure
```

See the [build guide](docs/BUILD.md) for replay tools and portable packaging.
Licensed under [MIT](LICENSE). Third-party components retain their own licenses;
see [licenses](licenses).
