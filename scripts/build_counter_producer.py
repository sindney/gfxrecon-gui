"""Build the unprivileged ARM64 Adreno/Mali OEM counter-producer launcher."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def build_counter_producer(ndk_root, output):
    ndk_root, output = Path(ndk_root), Path(output)
    platform = 'windows-x86_64' if sys.platform == 'win32' else 'darwin-x86_64' if sys.platform == 'darwin' else 'linux-x86_64'
    compiler = ndk_root / 'toolchains/llvm/prebuilt' / platform / 'bin' / ('clang++.exe' if sys.platform == 'win32' else 'clang++')
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(compiler), '--target=aarch64-linux-android26', '-std=c++17', '-O2', '-fPIE', '-pie',
               '-fno-exceptions', '-fno-rtti', '-nostdlib++',
               str(ROOT / 'src/platform/android_counter_producer.cpp'), '-ldl', '-o', str(output)]
    subprocess.run(command, check=True)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    sidecar = Path(str(output) + '.capabilities.json')
    sidecar.write_text(json.dumps({'gpu_counter_producer_protocol': 1, 'binary_sha256': digest,
                                  'abi': 'arm64-v8a', 'source': 'src/platform/android_counter_producer.cpp'}, indent=2) + '\n')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', '--ndk', dest='ndk_root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(build_counter_producer(args.ndk_root, args.output))


if __name__ == '__main__':
    main()
