import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release_package", ROOT / "scripts/package_release.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


def fake_x64_executable(path):
    """Smallest file that passes require_x64: MZ header, PE offset 0x40, PE signature and AMD64 machine."""
    path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray(0x80)
    header[0:2] = b"MZ"
    header[0x3C:0x40] = (0x40).to_bytes(4, "little")
    header[0x40:0x46] = b"PE\0\0\x64\x86"
    path.write_bytes(bytes(header))


def find_cli():
    """Locate a built gfxrecon-cli.exe: GFXRECON_CLI_EXE, then any ROOT/build*/Release build."""
    if os.environ.get("GFXRECON_CLI_EXE"):
        return Path(os.environ["GFXRECON_CLI_EXE"])
    candidates = [path for path in ROOT.glob("build*/Release/gfxrecon-cli.exe") if path.is_file()]
    return max(candidates, key=lambda path: path.stat().st_mtime) if candidates else None


class ReleasePackageTests(unittest.TestCase):
    def test_publication_restores_folder_and_archives_on_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            temporary = root / "temporary"
            temporary.mkdir()
            target = root / "GfxreconGui"
            target.mkdir()
            (target / "app").write_bytes(b"old")
            source = temporary / "application"
            source.mkdir()
            (source / "app").write_bytes(b"new")
            archive = root / "GfxreconGui.zip"
            archive.write_bytes(b"old zip")
            new_archive = temporary / "release.zip"
            new_archive.write_bytes(b"new zip")
            replace = os.replace

            def fail(source_path, target_path):
                if Path(source_path) == new_archive:
                    raise OSError("injected archive replacement failure")
                return replace(source_path, target_path)

            with patch.object(package.os, "replace", side_effect=fail):
                with self.assertRaisesRegex(OSError, "replacement failure"):
                    package.publish([(source, target), (new_archive, archive)], temporary, root)
            self.assertEqual((target / "app").read_bytes(), b"old")
            self.assertEqual(archive.read_bytes(), b"old zip")
            self.assertEqual((source / "app").read_bytes(), b"new")

    def test_publication_rejects_source_and_destination_escapes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            temporary, out = root / "temporary", root / "dist"
            temporary.mkdir()
            out.mkdir()
            source = temporary / "app"
            source.write_bytes(b"new")
            with self.assertRaisesRegex(ValueError, "Unsafe release destination"):
                package.publish([(source, root / "elsewhere")], temporary, out)
            outside = root / "outside"
            outside.write_bytes(b"external")
            with self.assertRaisesRegex(ValueError, "escaped temporary staging"):
                package.publish([(outside, out / "app")], temporary, out)
            self.assertTrue(source.exists())
            self.assertTrue(outside.exists())

    def make_release(self, root):
        names = ["gfxrecon-gui.exe", "gfxrecon-cli.exe", "README.md", "LICENSE", "msvcp140.dll",
                 "docs/GUI.md", "docs/CLI.md", "docs/BUILD.md",
                 "vcruntime140.dll", "licenses/MSVC-RUNTIME-NOTICE.txt", "licenses/astc-encoder.txt",
                 "tools/astcenc/astcenc-avx2.exe", "tools/astcenc/astcenc-sse2.exe"]
        for name in names:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        files = {name: package.sha256(root / name) for name in names}
        (root / "build-info.json").write_text(json.dumps(dict(files=files)), encoding="utf-8")

    def test_manifest_detects_missing_modified_and_unexpected_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_release(root)
            package.verify_manifest(root)
            app = root / "gfxrecon-gui.exe"
            original = app.read_bytes()
            app.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "hashes"):
                package.verify_manifest(root)
            app.write_bytes(original)
            for name in ("gfxrecon_gui_tests.exe", "imgui.ini", "scratch.pdb"):
                path = root / name
                path.write_bytes(b"scratch")
                with self.assertRaisesRegex(ValueError, "Unexpected release file"):
                    package.verify_manifest(root)
                path.unlink()
            app.unlink()
            with self.assertRaisesRegex(ValueError, "Missing release files"):
                package.verify_manifest(root)

    def test_release_requires_cli_and_astcenc_in_inventory(self):
        for name in ("gfxrecon-cli.exe", "tools/astcenc/astcenc-avx2.exe", "tools/astcenc/astcenc-sse2.exe",
                     "licenses/astc-encoder.txt"):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                self.make_release(root)
                manifest = json.loads((root / "build-info.json").read_text(encoding="utf-8"))
                self.assertIn(name, manifest["files"])
                package.verify_manifest(root)
                (root / name).unlink()
                with self.assertRaisesRegex(ValueError, "Missing release files.*" + name.replace(".", r"\.")):
                    package.verify_manifest(root)

    def test_bundle_cli_stages_binaries_and_fails_clearly_when_missing(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build, stage = root / "build", root / "stage"
            stage.mkdir()
            with self.assertRaisesRegex(ValueError, "Missing .*gfxrecon-cli.exe.*gfxrecon_cli target"):
                package.bundle_cli(stage, build)
            fake_x64_executable(build / "Release/gfxrecon-cli.exe")
            with self.assertRaisesRegex(ValueError, "Missing astcenc binaries.*astcenc-avx2.exe, astcenc-sse2.exe"):
                package.bundle_cli(stage, build)
            fake_x64_executable(build / "Release/tools/astcenc/astcenc-avx2.exe")
            with self.assertRaisesRegex(ValueError, "Missing astcenc binaries.*astcenc-sse2.exe"):
                package.bundle_cli(stage, build)
            fake_x64_executable(build / "Release/tools/astcenc/astcenc-sse2.exe")
            package.bundle_cli(stage, build)
            for name in ("gfxrecon-cli.exe", "tools/astcenc/astcenc-avx2.exe", "tools/astcenc/astcenc-sse2.exe"):
                self.assertTrue((stage / name).is_file(), name)
            (build / "Release/gfxrecon-cli.exe").write_bytes(b"not a pe")
            with self.assertRaisesRegex(ValueError, "Not a Windows executable"):
                package.bundle_cli(stage, build)

    def test_consolidation_treats_cli_and_astcenc_as_distribution_binaries(self):
        spec = importlib.util.spec_from_file_location("consolidate_dist", ROOT / "scripts/consolidate_dist.py")
        consolidate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(consolidate)
        for name in ("gfxrecon-cli.exe", "tools/astcenc/astcenc-avx2.exe", "tools/astcenc/astcenc-sse2.exe"):
            self.assertTrue(consolidate.distribution_owned(name), name)
        for name in ("gfxrecon-gui.exe", "gfxrecon_tools.json", "sessions/a/b.bin"):
            self.assertFalse(consolidate.distribution_owned(name), name)

    @unittest.skipUnless(os.name == "nt", "gfxrecon-cli.exe is a Windows executable")
    def test_packaged_cli_smoke(self):
        cli = find_cli()
        if cli is None or not cli.is_file():
            self.skipTest("gfxrecon-cli.exe is not built (set GFXRECON_CLI_EXE or build target gfxrecon_cli)")
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            stage, settings = root / "gfxrecon-gui", root / "settings"
            settings.mkdir()
            stage.mkdir()
            shutil.copy2(cli, stage / "gfxrecon-cli.exe")
            # Placeholder tools are enough for --no-versions: the CLI resolves them by location, not by running them.
            for name in [*("tools/" + n + ".exe" for n in package.BUNDLED_TOOL_KEYS.values()),
                         *("tools/astcenc/" + n for n in package.ASTCENC)]:
                path = stage / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"placeholder")
            names = ["gfxrecon-cli.exe", *("tools/astcenc/" + n for n in package.ASTCENC)]
            (stage / "build-info.json").write_text(json.dumps(dict(files={n: package.sha256(stage / n) for n in names if (stage / n).is_file()})))
            package.verify_cli(stage, settings)
            # A missing bundled tool must fail the smoke test rather than silently pass.
            (stage / "tools/gfxrecon-convert.exe").unlink()
            with self.assertRaises(ValueError):
                package.verify_cli(stage, settings)

    def test_existing_visual_studio_build_preserves_generator_platform(self):
        spec = importlib.util.spec_from_file_location("release_build", ROOT / "scripts/build.py")
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            args = argparse.Namespace(build_dir=root, mode="release", os_layer="SFML", parallel=2)
            self.assertIn("x64", build.commands(args)[0])
            (root / "CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n")
            self.assertNotIn("-A", build.commands(args)[0])
            self.assertIn("-DIMGUI_TEMPLATE_OS_LAYER=SFML", build.commands(args)[0])


if __name__ == "__main__":
    unittest.main()
