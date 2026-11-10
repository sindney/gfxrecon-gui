"""Stage and verify a local macOS app/CLI distribution and ZIP without device access."""
from __future__ import annotations

from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tempfile

from package_release import publish, sha256, staging_directory

ROOT = Path(__file__).resolve().parents[1]
NAME = "gfxrecon-gui"
APP = NAME + ".app"
TOOLS = {
    **{name: "gfxrecon-" + name for name in ("convert", "info", "extract", "optimize", "replay")},
    "compiler": "glslangValidator", "disassembler": "spirv-dis", "assembler": "spirv-as", "validator": "spirv-val",
}
ASTCENC = "astcenc/astcenc"


def run(command, **kwargs):
    result = subprocess.run([str(word) for word in command], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=120, **kwargs)
    if result.returncode:
        raise ValueError(f"Command failed ({result.returncode}): {command}\n{result.stdout}\n{result.stderr}")
    return result


def require_native(binary, architecture):
    architectures = run(["/usr/bin/lipo", "-archs", binary]).stdout.split()
    if architecture not in architectures:
        raise ValueError(f"{binary} does not support {architecture}: {architectures}")
    # A distribution must not silently depend on Homebrew or build-machine libraries.
    libraries = run(["/usr/bin/otool", "-L", binary]).stdout.splitlines()[1:]
    for line in libraries:
        if not line.startswith("\t"):
            continue  # universal binary architecture heading
        dependency = line.strip().split(" (", 1)[0]
        if not dependency.startswith(("/usr/lib/", "/System/Library/")):
            raise ValueError(f"Unbundled dynamic dependency in {binary}: {dependency}")


def inventory(directory):
    files = {}
    for file in sorted(directory.rglob("*")):
        if file.is_symlink():
            raise ValueError(f"Unexpected symlink in distribution: {file}")
        if file.is_file() and file != directory / "build-info.json":
            files[file.relative_to(directory).as_posix()] = sha256(file)
    return files


def verify_manifest(directory):
    manifest = json.loads((directory / "build-info.json").read_text())
    if manifest["files"] != inventory(directory):
        raise ValueError("macOS distribution inventory or hashes changed")
    required = {"dependencies.json", "docs/RELATIONS.md", f"{APP}/Contents/Resources/licenses/sqlite.txt",
                f"{APP}/Contents/Resources/tools/{ASTCENC}"}
    if manifest.get("adb_included"):
        required.add(f"{APP}/Contents/Resources/tools/adb")
    if manifest.get("artifact_transport_protocol"):
        helper=directory / APP / "Contents/Resources/tools/android/gfxrecon-artifact-transport"
        capability=json.loads(Path(str(helper)+'.capabilities.json').read_text())
        if capability.get('artifact_transport_protocol')!=1 or capability.get('binary_sha256')!=sha256(helper) or capability.get('abi')!='arm64-v8a':
            raise ValueError("Artifact transport helper capability/hash mismatch")
    missing = sorted(name for name in required if not (directory / name).is_file())
    if missing:
        raise ValueError(f"Missing release files: {', '.join(missing)}")
    app = directory / APP
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    icon = app / "Contents/Resources" / info["CFBundleIconFile"]
    if icon.read_bytes()[:4] != b"icns":
        raise ValueError("App bundle has no valid macOS icon")
    run(["/usr/bin/codesign", "--verify", "--deep", "--strict", app])
    for file in (directory / "gfxrecon-cli", app / "Contents/MacOS/gfxrecon-gui", app / "Contents/MacOS/gfxrecon-cli"):
        if not os.access(file, os.X_OK):
            raise ValueError(f"Executable permission missing: {file}")
    return manifest


def verify_extracted(directory, scratch, build):
    manifest = verify_manifest(directory)
    settings = scratch / "settings"
    settings.mkdir()
    # No device discovery, SDK/PATH tools, or development configuration may hide a missing bundle file.
    (settings / "gfxrecon_tools.json").write_text('{"adb":""}\n')
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("GFXRECON_", "DYLD_", "VK_")) and
                   key not in ("VULKAN_SDK", "ANDROID_HOME", "ANDROID_SDK_ROOT", "JAVA_HOME")}
    environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    cli = directory / "gfxrecon-cli"
    logs = []

    def invoke(words, command_environment=environment):
        result = run([cli, *words, "--settings-dir", settings, "--quiet"], cwd=scratch, env=command_environment)
        logs.append(result.stdout + result.stderr)
        document = json.loads(result.stdout)
        if document.get("ok") is not True:
            raise ValueError(f"Packaged CLI failed: {document}")
        return document["result"]

    help_result = run([cli, "--help"], cwd=scratch, env=environment)
    if "Usage:" not in help_result.stdout:
        raise ValueError("Packaged CLI help is missing")
    invoke(["describe"])
    tools = invoke(["doctor", "--no-versions"])["tools"]
    bundled = directory / APP / "Contents/Resources/tools"
    for key, executable in {**TOOLS, "astcenc": ASTCENC}.items():
        if not tools[key]["available"] or Path(tools[key]["path"]).resolve() != (bundled / executable).resolve():
            raise ValueError(f"Packaged tool is not available from the app bundle: {key}")
    run([bundled / ASTCENC, "-version"], cwd=scratch, env=environment)
    # Exercise the shipped codec under the same clean PATH, including PNG output.
    tga = bytearray(18)
    tga[2], tga[12], tga[14], tga[16], tga[17] = 2, 4, 4, 32, 0x28
    (scratch / "astc-source.tga").write_bytes(tga + bytes((30, 100, 220, 255)) * 16)
    run([bundled / ASTCENC, "-cl", scratch / "astc-source.tga", scratch / "texture.astc", "4x4", "-fast", "-silent"], cwd=scratch, env=environment)
    run([bundled / ASTCENC, "-dl", scratch / "texture.astc", scratch / "texture.png", "-silent"], cwd=scratch, env=environment)
    if (scratch / "texture.png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Packaged astcenc did not decode to PNG")
    if manifest.get("artifact_transport_protocol"):
        helper=bundled / "android/gfxrecon-artifact-transport"
        if not tools.get('artifact_transport',{}).get('available') or Path(tools['artifact_transport']['path']).resolve()!=helper.resolve():
            raise ValueError("Bundled artifact transport helper was not resolved")
    samples = directory / APP / "Contents/Resources/samples"
    expected_samples = {"windows-triangle.gfxr", "android-triangle.gfxr", "indexed-cube.gfxr", "indirect-triangles.gfxr",
                        "mip-depth-stencil.gfxr"}
    packaged_samples = sorted(samples.glob("*.gfxr"))
    if not expected_samples.issubset({file.name for file in packaged_samples}):
        raise ValueError("Packaged public captures are incomplete")
    for sample in packaged_samples:
        info = invoke(["info", "--capture", sample])
        if info["frame_count"] != 3 or info["execution_count"] < 1:
            raise ValueError(f"Packaged capture did not decode correctly: {sample}")
    invoke(["dump", "--capture", samples / "indexed-cube.gfxr", "--out", scratch / "cube-dump"])
    exported = scratch / "shader"
    invoke(["shader", "export", "21", "--capture", samples / "windows-triangle.gfxr", "--out", exported])
    invoke(["shader", "compile", exported / "module.glsl", "--stage", "frag", "--output", scratch / "shader.spv"])
    invoke(["shader", "check", "21", scratch / "shader.spv", "--capture", samples / "windows-triangle.gfxr"])
    replay_environment = dict(environment)
    runtime = os.environ.get("GFXRECON_VULKAN_LIBRARY_PATH")
    if runtime:
        replay_environment["GFXRECON_VULKAN_LIBRARY_PATH"] = runtime
        if os.environ.get("VK_DRIVER_FILES"):
            replay_environment["VK_DRIVER_FILES"] = os.environ["VK_DRIVER_FILES"]
        for name in ("windows-triangle", "indexed-cube", "indirect-triangles"):
            for mode in ("frame", "draw"):
                selector = ["--frame", "1"] if mode == "frame" else ["--execution", "0", "--full"]
                replay = invoke(["replay", mode, "--capture", samples / (name + ".gfxr"), *selector,
                                 "--target", "local", "--timeout", "30", "--out", scratch / (name + "-" + mode)], replay_environment)
                if mode == "frame" and not Path(replay["image_png"]).is_file():
                    raise ValueError("Packaged local replay produced no PNG")
    result = run([directory / APP / "Contents/MacOS/gfxrecon-gui", "--settings-dir", settings,
                  "--open", samples / "indexed-cube.gfxr", "--quit-when-ready", "--quit-after", "600"],
                 cwd=scratch, env=replay_environment)
    logs.append(result.stdout + result.stderr)
    if "160 recorded calls across 3 frames." not in result.stdout or "[shutdown] window done" not in result.stderr:
        raise ValueError("Extracted GUI did not load the bundled sample and shut down cleanly")
    # Check Launch Services/Finder-style launch as well as direct executable launch.
    run(["/usr/bin/open", "-n", "-W", directory / APP, "--args", "--settings-dir", settings,
         "--quit-after", "60"], cwd=scratch, env=environment)
    verify_manifest(directory)  # launching GUI/CLI must not modify the signed app or distribution
    (build / "packaged-macos-workflow.log").write_text("\n".join(logs))
    return {"architecture": manifest["platform"], "hashes_and_signature": "passed", "icon": "passed",
            "clean_path_tools": "passed", "astc_roundtrip": "passed", "samples_decoded": len(packaged_samples), "shader_roundtrip": "passed",
            "gui_sample_and_launch_services": "passed", "package_unchanged_after_launch": True,
            "local_frame_and_full_draw_replays": 6 if runtime else "skipped; no explicit Vulkan loader configured",
            "device_access": "disabled", "vulkan_runtime": "external; local replay requires SDK environment"}


def package_runtime(args):
    if args.verify_android:
        raise ValueError("macOS packaging is host-only; device verification is a separate opt-in step")
    architecture = platform.machine()
    if architecture not in ("arm64", "x86_64"):
        raise ValueError(f"Unsupported macOS architecture: {architecture}")
    build = args.build_dir.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    if "CMAKE_BUILD_TYPE:STRING=Release\n" not in cache:
        raise ValueError("Select a single-configuration Release build for macOS packaging")
    if f"IMGUI_TEMPLATE_OS_LAYER:STRING={args.os_layer}\n" not in cache:
        raise ValueError("Build OS layer does not match --os-layer")
    target = args.output_dir.absolute()
    if target.name != NAME or target.is_symlink():
        raise ValueError(f"Use a distribution directory named {NAME}, e.g. dist/{NAME}")
    out = target.parent.resolve()
    out.mkdir(parents=True, exist_ok=True)
    archive_name = f"{NAME}-macos-{architecture}.zip"
    config = json.loads(args.tools_config.read_text())
    with staging_directory(out) as temporary:
        stage = temporary / NAME
        app = stage / APP
        macos = app / "Contents/MacOS"
        resources = app / "Contents/Resources"
        macos.mkdir(parents=True)
        resources.mkdir()
        source_app = build / APP
        shutil.copy2(source_app / "Contents/Info.plist", app / "Contents/Info.plist")
        shutil.copy2(source_app / "Contents/Resources/app.icns", resources / "app.icns")
        shutil.copy2(source_app / "Contents/MacOS/gfxrecon-gui", macos / "gfxrecon-gui")
        shutil.copy2(build / "gfxrecon-cli", macos / "gfxrecon-cli")
        shutil.copytree(ROOT / "samples", resources / "samples")
        shutil.copytree(ROOT / "licenses", resources / "licenses")
        shutil.copy2(ROOT / "LICENSE", resources / "LICENSE")
        shutil.copytree(ROOT / "docs", stage / "docs")
        shutil.copy2(ROOT / "README.md", stage / "README.md")
        shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
        shutil.copy2(ROOT / "dependencies.json", stage / "dependencies.json")
        tools = resources / "tools"
        tools.mkdir()
        require_native(macos / "gfxrecon-gui", architecture)
        binaries = [macos / "gfxrecon-cli"]
        for key, name in TOOLS.items():
            source = Path(config.get(key, ""))
            if not source.is_file():
                raise ValueError(f"Configure an existing {key} executable in --tools-config")
            shutil.copy2(source, tools / name)
            binaries.append(tools / name)
        astcenc = Path(config.get("astcenc") or build / "tools" / ASTCENC)
        if not astcenc.is_file():
            raise ValueError("Build gfxrecon_astcenc or configure an existing native astcenc executable")
        (tools / "astcenc").mkdir()
        shutil.copy2(astcenc, tools / ASTCENC)
        binaries.append(tools / ASTCENC)
        # Device tools/APKs are included only when explicitly configured. Never run ADB here.
        if config.get("adb"):
            shutil.copy2(config["adb"], tools / "adb")
            binaries.append(tools / "adb")
        for key, name in (("replay_apk", "replay-debug.apk"), ("test_apk", "test_launcher-debug.apk"), ("gpu_counter_producer", "gpu-counter-producer"), ("artifact_transport", "gfxrecon-artifact-transport")):
            if config.get(key):
                (tools / "android").mkdir(exist_ok=True)
                shutil.copy2(config[key], tools / "android" / name)
                if key in ("gpu_counter_producer", "artifact_transport"):
                    shutil.copy2(str(config[key]) + '.capabilities.json', tools / 'android' / (name + '.capabilities.json'))
                if key == "replay_apk":
                    from write_replay_capability import write_capability
                    write_capability(tools / "android" / name)
        wrapper = stage / "gfxrecon-cli"
        wrapper.write_text('#!/bin/sh\nexec "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gfxrecon-gui.app/Contents/MacOS/gfxrecon-cli" "$@"\n')
        wrapper.chmod(0o755)
        for binary in binaries:
            require_native(binary, architecture)
            run(["/usr/bin/codesign", "--force", "--sign", "-", binary])
        from write_replay_capability import write_capability
        write_capability(tools / "gfxrecon-replay")  # bind capability to the final signed bytes
        versions = {key: run([tools / name, "--version"]).stdout.strip() for key, name in TOOLS.items()}
        versions["astcenc"] = run([tools / ASTCENC, "-version"]).stdout.strip()
        (stage / "tool-versions.json").write_text(json.dumps(versions, indent=2) + "\n")
        run(["/usr/bin/codesign", "--force", "--sign", "-", app])
        commit = run(["git", "rev-parse", "HEAD"], cwd=ROOT).stdout.strip()
        manifest = {"schema_version": 1, "version": (build / "distribution-version.txt").read_text().strip(),
                    "platform": "macos-" + architecture, "configuration": "Release", "os_layer": args.os_layer,
                    "source_commit": commit, "source_dirty": bool(run(["git", "status", "--porcelain"], cwd=ROOT).stdout),
                    "built_at_utc": datetime.now(timezone.utc).isoformat(), "signing": "ad-hoc; not notarized",
                    "icon_source_sha256": sha256(ROOT / "assets/app-icon.png"),
                    "external_vulkan_runtime": True, "android_apks_included": bool(config.get("replay_apk")),
                    "adb_included": bool(config.get("adb")), "artifact_transport_protocol": 1 if config.get("artifact_transport") else 0, "files": inventory(stage)}
        (stage / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
        verify_manifest(stage)
        archive = temporary / archive_name
        run(["/usr/bin/ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent", stage, archive])
        # Extract outside the repository so parent local-tools.json cannot hide packaging errors.
        with tempfile.TemporaryDirectory(prefix="gfxrecon-macos-package-") as scratch:
            scratch = Path(scratch)
            extracted = scratch / "extracted"
            run(["/usr/bin/ditto", "-x", "-k", archive, extracted])
            verified = verify_extracted(extracted / NAME, scratch, build)
        (build / "packaged-macos-verification.json").write_text(json.dumps(verified, indent=2) + "\n")
        checksum = temporary / (archive_name + ".sha256")
        checksum.write_text(sha256(archive) + "  " + archive_name + "\n")
        publish([(stage, out / NAME), (archive, out / archive_name), (checksum, out / checksum.name)], temporary, out)
    print(f"macOS app/CLI: {out / NAME}\nZIP: {out / archive_name}\nSHA-256: {out / checksum.name}")
