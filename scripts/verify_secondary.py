"""Verify secondary invocation selection against two visibly different GPU states."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

from test_workflow import registered_layer

ROOT = Path(__file__).resolve().parents[1]


def main():
    config = json.loads((ROOT / 'local-tools.json').read_text())
    out = ROOT / '.sessions/secondary-validation' / str(time.time_ns())
    out.mkdir(parents=True)
    env = dict(os.environ, VK_LAYER_PATH=config['layer'], VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',
               GFXRECON_CAPTURE_FILE=str(out / 'secondary.gfxr'), GFXRECON_CAPTURE_FRAMES='1-3',
               GFXRECON_CAPTURE_FILE_TIMESTAMP='false')
    with registered_layer(True, Path(config['layer']) / 'VkLayer_gfxreconstruct.json'):
        captured = subprocess.run([str(ROOT / 'build-tools/validation/Release/gfxrecon-gui-secondary.exe')],
                                  cwd=ROOT / 'thirdparty/gfxreconstruct/test/test_apps/res', env=env,
                                  capture_output=True, timeout=60)
        (out / 'capture.log').write_bytes(captured.stdout + captured.stderr)
        captured.check_returncode()
    source = next(out.glob('*.gfxr'))
    subprocess.run([config['convert'], '--format', 'jsonl', '--output', str(out / 'capture.jsonl'), str(source)], check=True)
    result = {'capture': str(source), 'capture_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'invocations': []}
    for occurrence in [0, 1]:
        destination = out / ('dump-' + str(occurrence))
        destination.mkdir()
        request = destination / 'request.json'
        subprocess.run([str(ROOT / 'build/Release/gfxrecon_core_tests.exe'), '--dump-request',
                        str(out / 'capture.jsonl'), str(occurrence), str(request)], check=True)
        document = json.loads(request.read_text())
        document['DumpResourcesOptions'] = {'OutputDir': str(destination), 'ImageFormat': 'bmp'}
        request.write_text(json.dumps(document, indent=2))
        replay = subprocess.run([config['replay'], '--dump-resources', str(request), '--quit-after-frame', '1', str(source)],
                                capture_output=True, timeout=60)
        (destination / 'replay.log').write_bytes(replay.stdout + replay.stderr)
        replay.check_returncode()
        image = next(destination.glob('*.bmp'))
        data = image.read_bytes()
        offset = struct.unpack_from('<I', data, 10)[0]
        width, height = struct.unpack_from('<ii', data, 18)
        bits = struct.unpack_from('<H', data, 28)[0]
        stride = (width * bits + 31) // 32 * 4
        center = offset + abs(height) // 2 * stride + width // 2 * (bits // 8)
        pixel = list(reversed(data[center:center + 3]))
        result['invocations'].append(dict(selector=document, center_rgb=pixel, image_sha256=hashlib.sha256(data).hexdigest()))
    first, second = result['invocations']
    assert all(abs(a - b) <= 2 for a, b in zip(first['center_rgb'], [188, 137, 137])), first
    assert all(abs(a - b) <= 2 for a, b in zip(second['center_rgb'], [255, 188, 188])), second
    assert first['image_sha256'] != second['image_sha256']
    (out / 'result.json').write_text(json.dumps(result, indent=2))
    print('Secondary invocation proof:', out)
    print('First/second center RGB:', first['center_rgb'], second['center_rgb'])


if __name__ == '__main__':
    main()
