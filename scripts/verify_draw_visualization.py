"""Capture three indirect triangles and verify native diagnostic replay pixels."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import argparse

from PIL import Image
from test_workflow import registered_layer

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--existing',type=Path,help='Reuse an existing fixture directory for further replay checks')
    args=parser.parse_args()
    tools = json.loads((ROOT / 'local-tools.json').read_text())
    out = args.existing.resolve() if args.existing else ROOT / '.sessions/draw-visualization' / str(time.time_ns())
    out.mkdir(parents=True,exist_ok=True)
    env = dict(os.environ, VK_LAYER_PATH=tools['layer'], VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',
               GFXRECON_CAPTURE_FILE=str(out / 'indirect-triangles.gfxr'), GFXRECON_CAPTURE_FRAMES='1-3',
               GFXRECON_CAPTURE_FILE_TIMESTAMP='false')
    with registered_layer(not args.existing, Path(tools['layer']) / 'VkLayer_gfxreconstruct.json'):
      if not args.existing:
        captured = subprocess.run([str(ROOT / 'build-tools/validation/Release/gfxrecon-gui-indirect.exe')],
                                  cwd=ROOT / 'build-tools/gui-mesh/res', env=env, capture_output=True, timeout=40)
        (out / 'capture.log').write_bytes(captured.stdout + captured.stderr)
        captured.check_returncode()
    source = next(out.glob('*.gfxr'))
    original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    subprocess.run([tools['convert'], '--format', 'jsonl', '--output', str(out / 'capture.jsonl'), str(source)], check=True)
    shaders = out / 'shaders'
    subprocess.run([tools['extract'], '--dir', str(shaders), str(source)], cwd=out, check=True, capture_output=True)
    calls = [json.loads(line) for line in (out / 'capture.jsonl').read_text().splitlines()]
    block = next(call['index'] for call in calls if call.get('function', {}).get('name') == 'vkCmdDrawIndexedIndirect')
    report = dict(capture=str(source), sha256=original_hash, block=block, results={})
    run_root=out/('verification-'+str(time.time_ns())) if args.existing else out
    run_root.mkdir(exist_ok=True)
    for variant in ['outputs', 'solid', 'wireframe', 'depth', 'silhouette', 'subdraw0:solid', 'subdraw1:solid', 'subdraw2:solid', 'validate:solid', 'validate:wireframe', 'validate:depth', 'validate:silhouette']:
        destination = run_root / variant.replace(':', '-')
        with registered_layer(variant.startswith('validate:'), Path(tools['validation_layer'])/'VkLayer_khronos_validation.json'):
          run = subprocess.run([str(ROOT / 'build/Release/gfxrecon_core_tests.exe'), '--inspect-draw',
                              str(source), str(out / 'capture.jsonl'), 'local', str(destination),
                              str(ROOT / 'local-tools.json'), str(block), variant], capture_output=True, timeout=50)
        (run_root / (variant.replace(':', '-') + '.log')).write_bytes(run.stdout + run.stderr)
        run.check_returncode()
        if variant.startswith('validate:'):
            validation=json.loads((destination/'validation.json').read_text())
            assert validation['enabled'] and validation['errors']==0, validation
        pixels = list(Image.open(next(destination.glob('image_*.bmp'))).convert('RGB').getdata())
        orange = sum(r > 250 and 120 < g < 240 and b < 5 for r, g, b in pixels)
        green = sum(r < 5 and g > 250 and b < 180 for r, g, b in pixels)
        report['results'][variant] = dict(orange=orange, green=green)
    results = report['results']
    assert results['solid']['orange'] > 10000, results
    assert 100 < results['wireframe']['orange'] < results['solid']['orange'] / 10, results
    assert results['depth']['green'] == results['solid']['orange'], results
    assert results['outputs']['orange'] == 0, results
    assert 0 < results['silhouette']['orange'] < results['solid']['orange'] / 5, results
    individual = [results[f'subdraw{i}:solid']['orange'] for i in range(3)]
    assert all(count > 10000 for count in individual), results
    assert sum(individual) == results['solid']['orange'], results
    assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
    (run_root / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
