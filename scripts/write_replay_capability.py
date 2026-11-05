"""Verify the generated backend marker and bind cache capability to exact bytes."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

MARKER = b'GfxReconGui indexed pipeline cache protocol 1'
GPU_MARKER = b'GfxReconGui frame GPU profiling protocol 1'
COUNTER_WINDOW_MARKER = b'GfxReconGui frame counter window protocol 1'


def write_capability(binary):
    binary = Path(binary)
    if binary.suffix == '.apk':
        with zipfile.ZipFile(binary) as archive:
            libraries = [archive.read(name) for name in archive.namelist()
                         if name.startswith('lib/arm64-v8a/') and 'replay' in name and name.endswith('.so')]
            supported = any(MARKER in data for data in libraries)
            gpu_supported = any(GPU_MARKER in data for data in libraries)
            counter_window_supported = any(COUNTER_WINDOW_MARKER in data for data in libraries)
    else:
        data = binary.read_bytes()
        supported = MARKER in data
        gpu_supported = GPU_MARKER in data
        counter_window_supported = COUNTER_WINDOW_MARKER in data
    if not supported:
        raise ValueError(f'Backend lacks the validated indexed-cache/lifetime fix: {binary}')
    result = {'pipeline_cache_protocol':1,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()}
    if gpu_supported:
        result['gpu_profiling_protocol'] = 1
    if counter_window_supported:
        result['frame_counter_window_protocol'] = 1
    output = Path(str(binary)+'.capabilities.json')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return output


if __name__=='__main__':
    print(write_capability(sys.argv[1]))
