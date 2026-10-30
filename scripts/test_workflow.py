"""Opt-in real GUI tests with local samples, Windows capture or an authorized Android phone.

The Windows Vulkan loader ignores environment-only layer discovery in elevated
processes. Only for this development test, temporarily register the pinned layer
as an explicit layer and restore the exact previous registry value in finally.
The application itself never changes layer registration.
"""
import argparse
from contextlib import contextmanager
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def registered_layer(enabled, manifest):
    if not enabled or os.name != 'nt' or not ctypes.windll.shell32.IsUserAnAdmin():
        yield
        return
    import winreg
    key = winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Khronos\Vulkan\ExplicitLayers',
                            0, winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY)
    name = str(manifest.resolve())
    try:
        try:
            previous = winreg.QueryValueEx(key, name)
        except FileNotFoundError:
            previous = None
        journal = ROOT / '.sessions/workflow-layer-journal.json'
        journal.parent.mkdir(exist_ok=True)
        state = dict(name=name, previous=previous, restored=False)
        journal.write_text(json.dumps(state, indent=2))
        try:
            winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, 0)
            yield
        finally:
            if previous is None:
                winreg.DeleteValue(key, name)
            else:
                winreg.SetValueEx(key, name, 0, previous[1], previous[0])
            state['restored'] = True
            journal.write_text(json.dumps(state, indent=2))
    finally:
        key.Close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--android', metavar='SERIAL')
    parser.add_argument('--windows', action='store_true')
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--screenshot', type=Path)
    parser.add_argument('--stress', action='store_true')
    parser.add_argument('--navigation-jsonl', type=Path, help='Profile CPU navigation on an existing large converted capture, without device replay')
    parser.add_argument('--capture', type=Path, help='Replay/inspect an existing Windows trace without capturing or using a phone')
    parser.add_argument('--mesh-capture', type=Path, help='Indexed cube fixture for geometry and post-VS preview checks')
    parser.add_argument('--indirect-capture', type=Path, help='Indirect triangle fixture for draw visualization checks')
    args = parser.parse_args()
    env = dict(os.environ)
    tools = ROOT / 'local-tools.json'
    binaries = args.build / 'Release' if os.name == 'nt' else args.build
    test_binaries = args.build / 'gfxrecon_gui_tests.app/Contents/MacOS' if sys.platform == 'darwin' else binaries
    if tools.exists():
        state = test_binaries / 'test-state'
        state.mkdir(parents=True, exist_ok=True)
        (state / 'gfxrecon_tools.json').write_bytes(tools.read_bytes())
    for name in ['GFXRECON_GUI_TEST_CAPTURE', 'GFXRECON_GUI_ANDROID_CAPTURE', 'GFXRECON_GUI_CAPTURE_DEVICE',
                 'GFXRECON_GUI_CAPTURE_WINDOWS', 'GFXRECON_GUI_CAPTURE_WORKDIR',
                 'GFXRECON_GUI_LIVE_GAME', 'GFXRECON_GUI_LIVE_WORKDIR', 'GFXRECON_GUI_NAVIGATION_JSONL',
                 'GFXRECON_GUI_MESH_CAPTURE', 'GFXRECON_GUI_INDIRECT_CAPTURE']:
        env.pop(name, None)
    if args.android:
        config = json.loads(tools.read_text(encoding='utf-8')) if tools.exists() else {}
        policy = subprocess.run([config.get('adb', 'adb'), '-s', args.android, 'shell', 'dumpsys', 'window', 'policy'],
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=15, check=True)
        if 'showing=true' in policy.stdout:
            raise RuntimeError(f'Phone {args.android} is locked. Unlock it before starting device tests.')
        env['GFXRECON_GUI_CAPTURE_DEVICE'] = args.android
    if args.capture:
        env['GFXRECON_GUI_TEST_CAPTURE'] = str(args.capture.resolve())
    if args.mesh_capture:
        env['GFXRECON_GUI_MESH_CAPTURE'] = str(args.mesh_capture.resolve())
    if args.indirect_capture:
        env['GFXRECON_GUI_INDIRECT_CAPTURE'] = str(args.indirect_capture.resolve())
    if args.navigation_jsonl:
        env['GFXRECON_GUI_NAVIGATION_JSONL'] = str(args.navigation_jsonl.resolve())
    if args.stress:
        benchmark = ROOT / '.sessions/index-benchmark'
        subprocess.run([str(binaries / ('gfxrecon_core_tests.exe' if os.name == 'nt' else 'gfxrecon_core_tests')), '--benchmark-index', str(benchmark)], check=True)
        env['GFXRECON_GUI_STRESS_CAPTURE'] = str(benchmark / '100k.jsonl')
    if args.windows:
        env['GFXRECON_GUI_CAPTURE_WINDOWS'] = str(ROOT / 'build-tools/test/test_apps/launcher/Release/gfxrecon-test-launcher.exe')
        env['GFXRECON_GUI_CAPTURE_WORKDIR'] = str(ROOT / 'thirdparty/gfxreconstruct/test/test_apps/res')
    if args.screenshot:
        args.screenshot.parent.mkdir(parents=True, exist_ok=True)
        env['GFXRECON_GUI_SCREENSHOT'] = str(args.screenshot.resolve())
    with registered_layer(args.windows, ROOT / 'build-tools/layer/Release/VkLayer_gfxreconstruct.json'):
        return subprocess.run(['ctest', '--test-dir', str(args.build), '-C', 'Release', '--output-on-failure'], env=env).returncode


if __name__ == '__main__':
    raise SystemExit(main())
