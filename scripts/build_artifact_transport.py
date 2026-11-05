"""Build the ARM64 Android LZ4 artifact helper using the NDK and pinned LZ4 source."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build_artifact_transport(ndk_root, output, lz4_source=None):
    ndk_root, output = Path(ndk_root).resolve(), Path(output).resolve()
    build = output.parent / 'artifact-transport-build'
    args = ['cmake', '-S', str(ROOT / 'src/platform/artifact_transport'), '-B', str(build),
            '-DCMAKE_TOOLCHAIN_FILE=' + str(ndk_root / 'build/cmake/android.toolchain.cmake'),
            '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-26', '-DANDROID_STL=c++_static',
            '-DCMAKE_BUILD_TYPE=Release']
    if sys.platform == 'win32':
        args.extend(['-G', 'Ninja'])
    if lz4_source:
        args.append('-DFETCHCONTENT_SOURCE_DIR_LZ4=' + str(Path(lz4_source).resolve()))
    subprocess.run(args, check=True)
    subprocess.run(['cmake', '--build', str(build), '--parallel', '8'], check=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(build / 'gfxrecon-artifact-transport', output)
    platform = 'windows-x86_64' if sys.platform == 'win32' else 'darwin-x86_64' if sys.platform == 'darwin' else 'linux-x86_64'
    strip = ndk_root / 'toolchains/llvm/prebuilt' / platform / 'bin' / ('llvm-strip.exe' if sys.platform == 'win32' else 'llvm-strip')
    subprocess.run([str(strip), '--strip-unneeded', str(output)], check=True)
    Path(str(output) + '.capabilities.json').write_text(json.dumps({
        'artifact_transport_protocol': 1, 'abi': 'arm64-v8a', 'codec': 'lz4',
        'content_hash': 'xxh64', 'binary_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
        'source': 'src/platform/android_artifact_transport.cpp'}, indent=2) + '\n')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lz4-source', type=Path)
    args = parser.parse_args()
    print(build_artifact_transport(args.ndk_root, args.output, args.lz4_source))


if __name__ == '__main__':
    main()
