"""Build opt-in FP32/FP16 fragment controls; no device access unless --run is supplied.

The optional host driver is a separately built test executable linked with the
shared performance core, never a new gfxrecon-cli startup path or command mode.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def locate_tool(tools, key, tools_file):
    value = str(tools.get(key, ''))
    if not value:
        raise ValueError(f'Missing {key} in {tools_file}')
    path = Path(value)
    if path.is_absolute():
        return str(path)
    local = tools_file.parent / path
    if local.is_file():
        return str(local.resolve())
    found = shutil.which(value)
    if not found:
        raise ValueError(f'Cannot find {key}: {value}')
    return found


def ndk_path(explicit, sdk):
    if explicit:
        return explicit.resolve()
    if not sdk:
        raise ValueError('Specify --ndk-root or --sdk-root')
    versions = [path for path in (sdk / 'ndk').glob('*') if path.is_dir()]
    if not versions:
        raise ValueError(f'No installed NDK under {sdk / "ndk"}')
    return max(versions, key=lambda p: tuple(int(x) for x in re.findall(r'\d+', p.name))).resolve()


def build_controls(ndk, tools_file, output):
    tools = json.loads(tools_file.read_text(encoding='utf-8'))
    output.mkdir(parents=True, exist_ok=True)
    sources = ['precision.vert', 'precision_full.frag', 'precision_half.frag']
    for name in sources:
        shader = ROOT / 'tests/shaders' / name
        target = output / (name + '.spv')
        subprocess.run([locate_tool(tools, 'compiler', tools_file), '-V', '--target-env', 'vulkan1.2',
                        str(shader), '-o', str(target)], check=True)
        subprocess.run([locate_tool(tools, 'validator', tools_file), '--target-env', 'vulkan1.2', str(target)], check=True)
        assembly = subprocess.check_output([locate_tool(tools, 'disassembler', tools_file), str(target)], text=True)
        (output / (name + '.spvasm')).write_text(assembly, encoding='utf-8')
        if name == 'precision_full.frag':
            if not re.search(r'OpTypeFloat\s+32', assembly) or re.search(r'OpTypeFloat\s+16', assembly) or 'RelaxedPrecision' in assembly:
                raise ValueError('FP32 control must have Float32 arithmetic without Float16 or RelaxedPrecision')
        if name == 'precision_half.frag':
            if not re.search(r'OpTypeFloat\s+16', assembly) or 'OpCapability Float16' not in assembly:
                raise ValueError('FP16 control must declare explicit Float16 arithmetic/capability')

    platform = 'windows-x86_64' if sys.platform == 'win32' else 'darwin-x86_64' if sys.platform == 'darwin' else 'linux-x86_64'
    compiler = ndk / 'toolchains/llvm/prebuilt' / platform / 'bin' / ('clang++.exe' if sys.platform == 'win32' else 'clang++')
    if not compiler.is_file():
        raise ValueError(f'NDK compiler unavailable: {compiler}')
    binary = output / 'precision-workload'
    subprocess.run([str(compiler), '--target=aarch64-linux-android26', '-std=c++17', '-O2', '-fPIE', '-pie',
                    '-static-libstdc++', '-I' + str(ROOT / 'thirdparty/json/include'), '-I' + str(ROOT),
                    str(ROOT / 'src/tests/precision_calibration.cpp'), '-lvulkan', '-ldl', '-o', str(binary)], check=True)
    files = [binary, *[output / (name + '.spv') for name in sources]]
    provenance = {
        'source': 'src/tests/precision_calibration.cpp', 'abi': 'arm64-v8a', 'minimum_android_api': 26,
        'ndk_root': str(ndk), 'tools_file': str(tools_file),
        'hashes': {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in files},
        'source_hashes': {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in [ROOT / 'src/tests/precision_calibration.cpp', *[ROOT / 'tests/shaders' / n for n in sources]]},
        'full_control': 'Float32 without Float16 types or RelaxedPrecision decorations',
        'half_control': 'explicit Float16 types/capability; runtime shaderFloat16 support required',
        'scope': 'Controlled offscreen fragment arithmetic; device-wide sampled counters, no shader-specific causality claim',
        'readback': 'outside comparison window; FP16 rounding may intentionally change pixels',
    }
    (output / 'build-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--sdk-root', type=Path)
    parser.add_argument('--tools', type=Path, default=ROOT / 'local-tools.json')
    parser.add_argument('--output', type=Path, required=True, help='Directory for generated workload/SPIR-V/provenance')
    parser.add_argument('--run', type=Path, metavar='HOST_DRIVER', help='Explicitly run the separately built opt-in host diagnostic')
    parser.add_argument('--serial', help='Required with --run')
    parser.add_argument('--run-output', type=Path, help='New result directory, required with --run')
    parser.add_argument('--frames', type=int, default=30)
    parser.add_argument('--iterations', type=int, default=256)
    parser.add_argument('--fps', type=float, default=30)
    args = parser.parse_args()
    tools_file, output = args.tools.resolve(), args.output.resolve()
    binary = build_controls(ndk_path(args.ndk_root, args.sdk_root), tools_file, output)
    print(f'Built {binary}; no device calls performed during build.')
    if args.run:
        if not args.serial or not args.run_output:
            parser.error('--run requires --serial and --run-output')
        subprocess.run([str(args.run.resolve()), '--serial', args.serial, '--tools', str(tools_file),
                        '--workload-dir', str(output), '--output', str(args.run_output.resolve()),
                        '--frames', str(args.frames), '--iterations', str(args.iterations), '--fps', str(args.fps)], check=True)


if __name__ == '__main__':
    main()
