"""Stage, verify and publish the portable Windows x64 app and ZIP archives."""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import time
from contextlib import contextmanager

ROOT = Path(__file__).resolve().parents[1]
NAME = "gfxrecon-gui"
ARCHIVE = NAME + "-windows-x64.zip"
CLI = "gfxrecon-cli.exe"
# astcenc ships as two CPU variants; the core picks AVX2 when supported, else SSE2 (design D9).
ASTCENC = ("astcenc-avx2.exe", "astcenc-sse2.exe")
ASTCENC_LICENSE = "licenses/astc-encoder.txt"
# Tool keys that doctor must resolve to the bundled tools/ directory (same resolution as the GUI).
BUNDLED_TOOL_KEYS = dict(convert="gfxrecon-convert", info="gfxrecon-info", extract="gfxrecon-extract", optimize="gfxrecon-optimize",
                         replay="gfxrecon-replay", compiler="glslangValidator", disassembler="spirv-dis", assembler="spirv-as",
                         validator="spirv-val", adb="adb")


@contextmanager
def staging_directory(parent):
    temporary = Path(tempfile.mkdtemp(prefix=".imgui-template-package-", dir=parent))
    try:
        yield temporary
    except BaseException:
        print(f"Unpublished package retained for diagnosis: {temporary}", file=sys.stderr)
        raise
    else:
        if temporary.absolute().parent.resolve() != parent.resolve():
            raise ValueError("Package cleanup escaped its staging parent")
        # Windows may briefly retain image-file locks after compiler/test exit.
        for attempt in range(12):
            try:
                shutil.rmtree(temporary)
                break
            except PermissionError:
                if attempt == 11:
                    raise
                time.sleep(0.25)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_link(path):
    return path.is_symlink() or path.resolve() != path.absolute()


def require_x64(path):
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise ValueError(f"Not a Windows executable: {path}")
        stream.seek(0x3C)
        stream.seek(struct.unpack("<I", stream.read(4))[0])
        if stream.read(6) != b"PE\0\0\x64\x86":
            raise ValueError(f"Expected a Windows x64 executable: {path}")


def release_files(stage):
    required = {"gfxrecon-gui.exe", CLI, "README.md", "LICENSE", "build-info.json",
                "msvcp140.dll", "vcruntime140.dll", "licenses/MSVC-RUNTIME-NOTICE.txt", ASTCENC_LICENSE,
                *("tools/astcenc/" + name for name in ASTCENC)}
    files = []
    for path in sorted(stage.rglob("*")):
        relative = path.relative_to(stage)
        if is_link(path):
            raise ValueError(f"Links are not allowed in releases: {relative}")
        if not path.is_file():
            continue
        tool_file = relative.parts[0] == "tools" and path.suffix.lower() in (".exe", ".dll", ".json", ".apk")
        sample_file = relative.parts[0] == "samples" and path.suffix.lower() in (".gfxr", ".md", ".spv")
        if not (relative.as_posix() in required or relative.parts[0] == "licenses" or tool_file or sample_file or
                relative.as_posix() in ("dependencies.json", "tool-versions.json") or
                (len(relative.parts) == 1 and path.suffix.lower() == ".dll")):
            raise ValueError(f"Unexpected release file: {relative}")
        files.append(path)
    missing = required - {p.relative_to(stage).as_posix() for p in files}
    if missing:
        raise ValueError(f"Missing release files: {', '.join(sorted(missing))}")
    return files


def bundle_tools(stage, config, runtimes):
    directory = stage / "tools"
    directory.mkdir()
    names = dict(convert="gfxrecon-convert", info="gfxrecon-info", extract="gfxrecon-extract", optimize="gfxrecon-optimize", replay="gfxrecon-replay",
                 compiler="glslangValidator", disassembler="spirv-dis", assembler="spirv-as", validator="spirv-val", adb="adb")
    versions = {}
    for key, name in names.items():
        source = Path(config.get(key, ""))
        if not source.is_file():
            raise ValueError(f"Configure an existing {key} executable in --tools-config")
        shutil.copy2(source, directory / (name + ".exe"))
        if key == "replay":
            from write_replay_capability import write_capability
            shutil.copy2(write_capability(source), directory / (name + ".exe.capabilities.json"))
        if key == "adb":
            versions[key] = "Bundled ADB; not invoked during offline package validation. SHA-256: " + sha256(source)
            continue
        result = subprocess.run([str(source), "version" if key == "adb" else "--version"], capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=30)
        if result.returncode:
            raise ValueError(f"Tool version check failed: {key}: {result.stderr}")
        versions[key] = "\n".join(line for line in result.stdout.splitlines() if not line.startswith("Installed as"))
    for source in runtimes:
        shutil.copy2(source, directory / source.name)
    adb_directory = Path(config["adb"]).parent
    for name in ["AdbWinApi.dll", "AdbWinUsbApi.dll", "libwinpthread-1.dll"]:
        if (adb_directory / name).is_file(): shutil.copy2(adb_directory / name, directory / name)
    layer = directory / "layer"
    layer.mkdir()
    for name in ["VkLayer_gfxreconstruct.dll", "VkLayer_gfxreconstruct.json", "capture-control.json"]:
        source = Path(config["layer"]) / name
        if not source.is_file(): raise ValueError(f"Missing capture layer file: {source}")
        shutil.copy2(source, layer / name)
    for source in runtimes: shutil.copy2(source, layer / source.name)
    validation=directory/"validation";validation.mkdir()
    validation_source=Path(config.get("validation_layer",str(ROOT/'thirdparty/validation-binaries/windows')))
    for name in ['VkLayer_khronos_validation.dll','VkLayer_khronos_validation.json']:
        shutil.copy2(validation_source/name,validation/name)
    for source in runtimes:shutil.copy2(source,validation/source.name)
    android = directory / "android"
    android.mkdir()
    for key, name in [("replay_apk", "replay-debug.apk"), ("test_apk", "test_launcher-debug.apk")]:
        source = Path(config.get(key, ""))
        if not source.is_file(): raise ValueError(f"Configure the source-built debuggable {key}")
        shutil.copy2(source, android / name)
        if key == "replay_apk":
            with zipfile.ZipFile(source) as archive:
                if not any(name.endswith('arm64-v8a/libVkLayer_khronos_validation.so') for name in archive.namelist()):
                    raise ValueError('Build the replay APK after scripts/setup_validation.py to include Khronos validation')
            from write_replay_capability import write_capability
            shutil.copy2(write_capability(source), android / (name + ".capabilities.json"))
    launcher = ROOT / "build-tools/test/test_apps/launcher/Release/gfxrecon-test-launcher.exe"
    if not launcher.is_file(): raise ValueError("Build the upstream Windows test launcher before packaging")
    shutil.copy2(launcher, directory / launcher.name)
    shutil.copytree(ROOT / "thirdparty/gfxreconstruct/test/test_apps/res/triangle", stage / "samples/test-res/triangle")
    live_game = ROOT / "build-tools/validation/Release/gfxrecon-gui-live.exe"
    if not live_game.is_file(): raise ValueError("Build the long-running on-demand capture sample before packaging")
    shutil.copy2(live_game, directory / live_game.name)
    shutil.copytree(ROOT / "build-tools/gui-mesh/res/mesh-preview", stage / "samples/test-res/mesh-preview")
    (stage / "tool-versions.json").write_text(json.dumps(versions, indent=2) + "\n", encoding="utf-8")


def bundle_cli(stage, build):
    """Ship the agent CLI beside the GUI and astcenc under tools/astcenc. Both are mandatory: fail clearly if absent."""
    cli = build / "Release" / CLI
    if not cli.is_file():
        raise ValueError(f"Missing {cli}; build the gfxrecon_cli target (cmake --build <build> --config Release --target gfxrecon_cli)")
    require_x64(cli)
    shutil.copy2(cli, stage / CLI)
    source_directory = build / "Release/tools/astcenc"
    missing = [name for name in ASTCENC if not (source_directory / name).is_file()]
    if missing:
        raise ValueError(f"Missing astcenc binaries in {source_directory}: {', '.join(missing)}; "
                         "configure/build with cmake/astcenc.cmake (set GFXRECON_ASTCENC_ZIP for offline builds)")
    target = stage / "tools/astcenc"
    target.mkdir(parents=True, exist_ok=True)
    for name in ASTCENC:
        require_x64(source_directory / name)
        shutil.copy2(source_directory / name, target / name)


def run_cli(package, arguments, cwd, environment):
    """Run the packaged CLI and return (exit code, the single JSON document on stdout)."""
    result = subprocess.run([str(package / CLI), *arguments], cwd=cwd, env=environment, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=120)
    try:
        document = json.loads(result.stdout)
    except ValueError as error:
        raise ValueError(f"{CLI} {' '.join(arguments)} did not print exactly one JSON document ({error}):\n"
                         f"{result.stdout}\n{result.stderr}")
    if not isinstance(document, dict):
        raise ValueError(f"{CLI} {' '.join(arguments)} printed a JSON value that is not an object")
    return result.returncode, document


def verify_cli(package, settings_dir):
    """Smoke-test the CLI inside an extracted package with a clean PATH; raises ValueError on any violation."""
    package, settings_dir = Path(package).resolve(), Path(settings_dir).resolve()
    environment = dict(os.environ, PATH=str(Path(os.environ["SystemRoot"]) / "System32"))
    cwd = settings_dir  # not the package directory: nothing may depend on the working directory
    code, document = run_cli(package, ["describe"], cwd, environment)
    if code or document.get("ok") is not True:
        raise ValueError(f"{CLI} describe failed ({code}): {document}")
    names = {item.get("name") for item in document["result"].get("commands", [])}
    if "doctor" not in names:
        raise ValueError(f"{CLI} describe does not list doctor: {sorted(names)}")
    code, document = run_cli(package, ["doctor", "--no-versions", "--settings-dir", str(settings_dir)], cwd, environment)
    if code or document.get("ok") is not True:
        raise ValueError(f"{CLI} doctor failed ({code}): {document}")
    tools = document["result"].get("tools", {})
    expected = {key: package / "tools" / (name + ".exe") for key, name in BUNDLED_TOOL_KEYS.items()}
    expected["astcenc"] = package / "tools/astcenc" / ASTCENC[0]
    for key, path in expected.items():
        entry = tools.get(key)
        if entry is None:
            raise ValueError(f"{CLI} doctor does not report tool '{key}'")
        reported = Path(entry.get("path", ""))
        # astcenc selects AVX2 or SSE2 by CPU; any bundled variant is correct.
        accepted = [package / "tools/astcenc" / name for name in ASTCENC] if key == "astcenc" else [path]
        if not any(str(reported).casefold() == str(item).casefold() for item in accepted):
            raise ValueError(f"{CLI} doctor resolved '{key}' to {reported}, expected the bundled {path}")
        if entry.get("available") is not True:
            raise ValueError(f"{CLI} doctor reports bundled tool '{key}' unavailable: {entry}")
    code, document = run_cli(package, ["bogus"], cwd, environment)
    if code != 2 or document.get("ok") is not False or document.get("error", {}).get("code") != "usage":
        raise ValueError(f"{CLI} bogus must exit 2 with a usage error, got {code}: {document}")
    manifest_path = package / "build-info.json"
    if manifest_path.is_file():
        files = json.loads(manifest_path.read_text(encoding="utf-8")).get("files", {})
        for name in [CLI, *("tools/astcenc/" + item for item in ASTCENC)]:
            if name not in files:
                raise ValueError(f"{name} is missing from the release inventory in build-info.json")


def verify_manifest(stage):
    files = release_files(stage)
    actual = {p.relative_to(stage).as_posix(): sha256(p) for p in files if p.name != "build-info.json"}
    manifest = json.loads((stage / "build-info.json").read_text(encoding="utf-8"))
    if manifest.get("files") != actual:
        raise ValueError("Release file inventory or SHA-256 hashes do not match build-info.json")
    return files


def publish(items, temporary, out_dir):
    """Replace the distribution and archives together, restoring earlier outputs on failure."""
    out_dir, temporary = out_dir.resolve(), temporary.resolve()
    for source, target in items:
        if is_link(target) or target.resolve().parent != out_dir:
            raise ValueError(f"Unsafe release destination: {target}")
        if not source.resolve().is_relative_to(temporary):
            raise ValueError("Release source escaped temporary staging")
    moved, backups = [], []
    try:
        for index, (source, target) in enumerate(items):
            if target.exists():
                backup = temporary / f"previous-{index}"
                os.replace(target, backup)
                backups.append((backup, target))
            os.replace(source, target)
            moved.append((target, source))
    except BaseException:
        for target, source in reversed(moved):
            os.replace(target, source)
        for backup, target in reversed(backups):
            os.replace(backup, target)
        raise


def package_runtime(args):
    build = args.build_dir.resolve()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if f"IMGUI_TEMPLATE_OS_LAYER:STRING={args.os_layer}\n" not in cache:
        raise ValueError("Build OS layer does not match --os-layer; configure or select the matching build")
    executable = build / "Release/gfxrecon-gui.exe"
    require_x64(executable)
    sources = [Path(line) for line in (build / "runtime-redistributables.txt").read_text(encoding="utf-8").splitlines() if line.strip()]
    if not {"msvcp140.dll", "vcruntime140.dll"}.issubset({p.name.lower() for p in sources}):
        raise ValueError("MSVC x64 runtime inventory is incomplete; configure with the current CMakeLists.txt")
    target = args.output_dir.absolute()
    if target.name != NAME or is_link(target):
        raise ValueError(f"Use an output directory named {NAME} without links, e.g. dist/{NAME}")
    out_dir = target.parent.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / NAME
    with staging_directory(out_dir) as folder:
        temporary = Path(folder)
        stage = temporary / NAME
        stage.mkdir()
        shutil.copy2(executable, stage / executable.name)
        shutil.copytree(ROOT / "licenses", stage / "licenses")
        shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
        shutil.copy2(ROOT / "dependencies.json", stage / "dependencies.json")
        shutil.copytree(ROOT / "samples", stage / "samples")
        for source in sources:
            if source.suffix.lower() != ".dll":
                raise ValueError(f"Invalid compiler redistributable: {source}")
            require_x64(source)
            shutil.copy2(source, stage / source.name)
        tool_config = json.loads(args.tools_config.read_text(encoding="utf-8"))
        bundle_tools(stage, tool_config, sources)
        bundle_cli(stage, build)
        (stage / "licenses/MSVC-RUNTIME-NOTICE.txt").write_text(
            "Microsoft Visual C++ runtime redistributables from the selected Visual Studio toolchain.\n"
            "Copyright Microsoft Corporation. Redistribution is subject to the Visual Studio license terms.\n"
            "Windows system DLLs and the Universal CRT are supplied by supported Windows 10/11 installations.\n",
            encoding="utf-8")
        (stage / "README.md").write_text(
            "# GFXReconstruct Inspector for Windows x64\n\n"
            "Extract the complete folder to a writable location and run `gfxrecon-gui.exe`.\n"
            "Windows 10/11 x64 and an OpenGL-capable graphics driver are required.\n"
            "The MSVC runtime is bundled. No Python, Visual Studio or FFmpeg installation is needed.\n\n"
            "Change themes with File > Settings > Theme. Open the command palette with Ctrl+P.\n"
            "Programmer is the default theme; settings and docking layout are saved next to the executable.\n"
            "File > Samples opens bundled three-frame triangle captures.\n"
            "File > Samples > Indexed cube (mesh) is the mesh-preview sample: select a draw, then Mesh.\n"
            "Mesh supports local input and post-VS screen space, solid/wireframe/points and vertex tables.\n"
            "Orbit with left drag, pan with middle drag, zoom with the wheel, or Fit.\n"
            "Post-VS is evaluated locally from supported captured shaders/readbacks; unsupported state is explained.\n"
            "File > Capture application opens the procedural Windows/Android capture dialog.\n"
            "Choose On demand: N frames or On demand: start-stop, then Launch armed and play.\n"
            "Trigger repeated captures from the popup. Stop capture and End session keep the game running.\n"
            "Completed captures: Save keeps the default location; Save As chooses a folder and filename, retaining the session copy.\n"
            "Existing traces also support File > Save capture / Save capture as. Shader edits are exported separately.\n"
            "Close launched game is separate. Automatic replay is deferred during live sessions.\n"
            "For a long-running Windows sample, launch tools/gfxrecon-gui-live.exe with working directory samples/test-res.\n"
            "For Android, select your authorized, unlocked phone, install the bundled replay APK and test sample,\n"
            "then choose a debuggable Vulkan app and capture timing. Settings are journaled and restored.\n"
            "Only the initially selected frame auto-renders. Other tiles remain idle until clicked.\n"
            "Draw clicks request color outputs; newer selections supersede old automatic work.\n"
            "Shader/resource inspection can interrupt previews. Verified outputs are cached.\n"
            "gfxrecon-optimize prepares a cached replay copy by default; original inspection/capture bytes are preserved.\n"
            "Right-click Frames and disable Optimize replay copy to replay the original.\n"
            "Frames: select a preview to render it; right-click for render/dump/revert.\n"
            "Automatic uses a phone for Android captures and local replay for Windows captures.\n"
            "Select a pipeline stage, then View shader. Edit, Compile, Apply, compare, and revert; captured entry names are preserved.\n"
            "Image supports wheel zoom, inline Fit/scale, RGB channels and sRGB/linearized display.\n"
            "Image has open-image tabs and a right-side Inputs/Outputs thumbnail list; new draws default to color output.\n"
             "Image visualization offers Original, Solid coverage, Wireframe and geometry Depth pass/fail.\n"
             "Indirect triangles sample: choose All or an individual GPU argument record; earlier draws remain context.\n"
             "Wireframe requires fillModeNonSolid. Depth colors are green pass/red fail against pre-draw/batch depth; discard/stencil are not emulated.\n"
            "Draw clicks fetch shaders, mesh, textures and render targets together; new clicks replace old queries.\n"
            "The selected event's right-hand dot shows fetch status. Pipeline links open image/buffer inspectors and sampler properties.\n"
            "Image left/right rotation and flips affect display only. Range has draggable black/white points, Auto and Reset.\n"
            "Depth readback preserves full-precision D16/D24/D32 values; range adjustments retain original pixel values.\n"
            "Frame context > Vulkan validation runs original calls fresh; Resources > Validation shows enabled state, VUIDs and messages.\n"
             "Frames is the active footer tab at startup and layout reset; manual Console selection is respected.\n"
             "Frames shares the Console dock. Right-click for Replay target; there is no target/count toolbar.\n"
             "After unlocking Android, Retry replay follows the replay completion log; resource-transfer phases appear in Console.\n"
            "Dots appear only on Frames, frame tiles and draw events: yellow running, green success, red failed, grey inactive/cancelled.\n"
            "Refreshing inspectors have dimmed blocking overlays; Events and Console remain usable. Cancel from File, palette or frame menu.\n"
            "Locked Android replay shows an unlock/retry notice; only confirmed unsecured locks get automatic wake/dismissal.\n"
            "Frame context > Phone screen off while replaying uses supported physical display controls and restores owned state.\n"
            "Tools and devices > Restore phone display recovers an interrupted display journal after reconnection.\n"
            "Source-level shader stepping is not available in this version.\n\n"
            "See LICENSE and licenses/ for component license texts. build-info.json records the build and file hashes.\n",
            encoding="utf-8")
        commit = subprocess.run(["git", "rev-parse", "--verify", "HEAD"], cwd=ROOT, capture_output=True, text=True)
        manifest = dict(schema_version=1, version=(build / "distribution-version.txt").read_text().strip(),
                        source_commit=commit.stdout.strip() if commit.returncode == 0 else "uncommitted",
                        source_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True)),
                        built_at_utc=datetime.now(timezone.utc).isoformat(), configuration="Release",
                        platform="windows-x64", os_layer=args.os_layer,
                        files={p.relative_to(stage).as_posix(): sha256(p) for p in sorted(stage.rglob("*")) if p.is_file()})
        (stage / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        files = verify_manifest(stage)
        archive_path = temporary / ARCHIVE
        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in files:
                archive.write(path, NAME + "/" + path.relative_to(stage).as_posix())
        with zipfile.ZipFile(archive_path) as archive:
            if archive.testzip():
                raise ValueError("Release ZIP integrity verification failed")
            extracted = temporary / "extracted"
            archive.extractall(extracted)
        unpacked = extracted / NAME
        verify_manifest(unpacked)
        environment = dict(os.environ, PATH=str(Path(os.environ["SystemRoot"]) / "System32"))
        result = subprocess.run([str(unpacked / "gfxrecon-gui.exe"), "--open", str(unpacked / "samples/windows-triangle.gfxr"), "--quit-when-ready"],
                                cwd=unpacked, env=environment, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=30)
        if result.returncode:
            raise ValueError(f"Extracted app smoke test failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")
        if "Indexed " not in result.stdout:
            raise ValueError("Extracted app could not open its bundled capture with a clean PATH")
        with tempfile.TemporaryDirectory(prefix="cli-smoke-", dir=temporary) as settings:
            verify_cli(unpacked, settings)
        print("Extracted ZIP: gfxrecon-cli describe/doctor/usage smoke test passed", flush=True)
        # Tests are staged only in the extracted verification copy, never shipped.
        shutil.copy2(build / "Release/gfxrecon_gui_tests.exe", unpacked / "gfxrecon_gui_tests.exe")
        shutil.copy2(build / "Release/gfxrecon_core_tests.exe", unpacked / "gfxrecon_core_tests.exe")
        for key in list(environment):
            if key.startswith("GFXRECON_GUI_"): environment.pop(key)
        environment["GFXRECON_GUI_TEST_CAPTURE"] = str(unpacked / "samples/windows-triangle.gfxr")
        environment['GFXRECON_GUI_VALIDATE_FIXTURE']='1'
        if (unpacked / 'samples/indirect-triangles.gfxr').is_file():
            environment['GFXRECON_GUI_INDIRECT_CAPTURE']=str(unpacked / 'samples/indirect-triangles.gfxr')
        environment["GFXRECON_GUI_LIVE_GAME"] = str(unpacked / "tools/gfxrecon-gui-live.exe")
        environment["GFXRECON_GUI_LIVE_WORKDIR"] = str(unpacked / "samples/test-res")
        if (unpacked / "samples/indexed-cube.gfxr").is_file():
            environment["GFXRECON_GUI_MESH_CAPTURE"] = str(unpacked / "samples/indexed-cube.gfxr")
        if args.verify_android: environment["GFXRECON_GUI_CAPTURE_DEVICE"] = args.verify_android
        from test_workflow import registered_layer
        with registered_layer(True, unpacked / "tools/layer/VkLayer_gfxreconstruct.json"), registered_layer(True,unpacked/'tools/validation/VkLayer_khronos_validation.json'):
            tested = subprocess.run([str(unpacked / "gfxrecon_gui_tests.exe"), "--test"], cwd=unpacked,
                                    env=environment, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
        (build / "packaged-workflow.log").write_text(tested.stdout + tested.stderr, encoding="utf-8")
        if tested.returncode:
            raise ValueError(f"Packaged GUI capture/replay workflow failed:\n{tested.stdout}\n{tested.stderr}")
        print("Extracted ZIP: file hashes and app startup/shutdown passed", flush=True)
        checksum = temporary / (ARCHIVE + ".sha256")
        checksum.write_text(sha256(archive_path) + "  " + ARCHIVE + "\n", encoding="ascii")
        publish([(stage, target), (archive_path, out_dir / ARCHIVE),
                 (checksum, out_dir / checksum.name)], temporary, out_dir)
    print(f"Portable distribution: {target}\nRelease ZIP: {out_dir / ARCHIVE}\nSHA-256: {out_dir / checksum.name}", flush=True)


if __name__ == "__main__":
    from build import main
    raise SystemExit(main(["package", *sys.argv[1:]]))
