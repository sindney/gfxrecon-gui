"""Build, test and package a portable Windows x64 distribution (Python 3.10+)."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def positive(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("Expected a positive integer")
    return number


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("mode", nargs="?", choices=("release", "package"), default="release")
    result.add_argument("--build-dir", type=Path, default=ROOT / "build")
    result.add_argument("--output-dir", type=Path, default=ROOT / "dist/GfxReconGui")
    result.add_argument("--os-layer", choices=("SDL3", "SFML"), default="SDL3")
    result.add_argument("--parallel", type=positive, default=min(os.cpu_count() or 1, 8))
    result.add_argument("--noninteractive", action="store_true", help="Accepted for scripted builds; no prompts are used")
    result.add_argument("--dry-run", action="store_true")
    result.add_argument("--tools-config", type=Path, default=ROOT / "local-tools.json")
    result.add_argument("--verify-android", metavar="SERIAL", help="Run packaged GUI capture/replay tests on this authorized phone")
    return result


def commands(args):
    if args.mode == "package":
        return []
    configure = ["cmake", "-S", str(ROOT), "-B", str(args.build_dir)]
    cache_path = args.build_dir / "CMakeCache.txt"
    if cache_path.exists():
        cache = cache_path.read_text(encoding="utf-8")
        if "CMAKE_GENERATOR:INTERNAL=Visual Studio " not in cache:
            raise ValueError("Select a Visual Studio build directory for Windows release packaging")
    else:
        configure.extend(["-A", "x64"])
    configure.append("-DIMGUI_TEMPLATE_OS_LAYER=" + args.os_layer)
    return [
        configure,
        ["cmake", "--build", str(args.build_dir), "--config", "Release", "--parallel", str(args.parallel),
         "--target", "gfxrecon_gui", "gfxrecon_gui_tests", "gfxrecon_core_tests"],
        ["ctest", "--test-dir", str(args.build_dir), "-C", "Release", "--output-on-failure"],
    ]


def main(argv=None):
    args = parser().parse_args(argv)
    args.build_dir = args.build_dir.resolve()
    args.output_dir = args.output_dir.absolute()
    try:
        if sys.platform != "win32":
            raise ValueError("Portable packaging currently supports Windows x64 only")
        for command in commands(args):
            print(subprocess.list2cmdline(command), flush=True)
            if not args.dry_run:
                subprocess.run(command, cwd=ROOT, check=True)
        if args.dry_run:
            print(f"Bundle Release app, licenses and MSVC runtime; verify extracted ZIP; publish {args.output_dir}")
            return 0
        from package_release import package_runtime
        package_runtime(args)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"build: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("build: cancelled", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
