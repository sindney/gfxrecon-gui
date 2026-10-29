"""Capture the indexed-cube fixture and run the real inspector against its readbacks."""
import json
import os
from pathlib import Path
import subprocess
import shutil
import time
from test_workflow import registered_layer

ROOT = Path(__file__).resolve().parents[1]


def main():
    tools = json.loads((ROOT / 'local-tools.json').read_text())
    output = ROOT / '.sessions/mesh-validation' / str(time.time_ns())
    output.mkdir(parents=True)
    env = dict(os.environ, VK_LAYER_PATH=tools['layer'], VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',
               GFXRECON_CAPTURE_FILE=str(output / 'indexed-cube.gfxr'), GFXRECON_CAPTURE_FRAMES='1-3',
               GFXRECON_CAPTURE_FILE_TIMESTAMP='false')
    with registered_layer(True, Path(tools['layer']) / 'VkLayer_gfxreconstruct.json'):
        captured = subprocess.run([str(ROOT / 'build-tools/validation/Release/gfxrecon-gui-mesh.exe')],
                                  cwd=ROOT / 'build-tools/gui-mesh/res', env=env,
                                  capture_output=True, timeout=30)
        (output / 'capture.log').write_bytes(captured.stdout + captured.stderr)
        captured.check_returncode()
    source = next(output.glob('*.gfxr'))
    test_env = dict(os.environ, GFXRECON_GUI_MESH_CAPTURE=str(source),
                    GFXRECON_GUI_MESH_SCREENSHOT=str(output / 'mesh-ui.png'))
    for key in ['GFXRECON_GUI_TEST_CAPTURE', 'GFXRECON_GUI_ANDROID_CAPTURE', 'GFXRECON_GUI_CAPTURE_DEVICE',
                'GFXRECON_GUI_CAPTURE_WINDOWS', 'GFXRECON_GUI_STRESS_CAPTURE']:
        test_env.pop(key, None)
    tested = subprocess.run(['ctest', '--test-dir', str(ROOT / 'build'), '-C', 'Release', '--output-on-failure'],
                            env=test_env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    (output / 'tests.log').write_text(tested.stdout + tested.stderr, encoding='utf-8')
    print(tested.stdout + tested.stderr)
    tested.check_returncode()
    shutil.copy2(source, ROOT / 'samples/indexed-cube.gfxr')
    shutil.copy2(source, ROOT / 'build/Release/samples/indexed-cube.gfxr')
    print(source)


if __name__ == '__main__':
    main()
