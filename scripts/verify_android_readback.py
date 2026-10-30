"""Verify that CLI full Android inspection retrieves every referenced buffer file."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, default=ROOT / 'build/Release/gfxrecon-cli.exe')
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--execution', type=int, default=0)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--settings-dir', type=Path, default=ROOT / 'dist/gfxrecon-gui')
    args = parser.parse_args()
    command = [str(args.cli), 'replay', 'draw', '--capture', str(args.capture),
               '--execution', str(args.execution), '--full', '--fresh', '--target', 'android',
               '--serial', args.serial, '--out', str(args.out), '--settings-dir', str(args.settings_dir),
               '--wait-device', '180']
    completed = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=300)
    document = json.loads(completed.stdout)
    if completed.returncode or not document.get('ok'):
        raise RuntimeError(completed.stdout + '\n' + completed.stderr)
    checked = set()
    for run in document['result']['runs'].values():
        directory = Path(run['output_dir'])

        def visit(value):
            if isinstance(value, dict):
                name = Path(value.get('file', '')).name
                if name.startswith('buffer_') and name.endswith('.bin'):
                    path = directory / name
                    if not path.is_file() or not path.stat().st_size:
                        raise AssertionError('Missing full-inspection buffer: ' + str(path))
                    checked.add(str(path))
                for child in value.values():
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)

        for metadata in directory.glob('*_dr.json'):
            visit(json.loads(metadata.read_text(encoding='utf-8')))
    if not checked:
        raise AssertionError('Choose a buffer-backed draw; this selection exposes no buffer readbacks.')
    print(json.dumps({'ok': True, 'verified_buffers': len(checked), 'output': str(args.out)}, indent=2))


if __name__ == '__main__':
    main()
