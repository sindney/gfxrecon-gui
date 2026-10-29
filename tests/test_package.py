import argparse
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release_package", ROOT / "scripts/package_release.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


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
        names = ["gfxrecon_gui.exe", "README.md", "LICENSE", "msvcp140.dll",
                 "vcruntime140.dll", "licenses/MSVC-RUNTIME-NOTICE.txt"]
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
            app = root / "gfxrecon_gui.exe"
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
