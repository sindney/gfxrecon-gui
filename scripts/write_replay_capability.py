"""Verify the generated backend marker and bind cache capability to exact bytes."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

MARKER = b'GfxReconGui indexed pipeline cache protocol 1'


def write_capability(binary):
    binary = Path(binary)
    if binary.suffix == '.apk':
        with zipfile.ZipFile(binary) as archive:
            supported = any(MARKER in archive.read(name) for name in archive.namelist()
                            if name.startswith('lib/arm64-v8a/') and 'replay' in name and name.endswith('.so'))
    else:
        supported = MARKER in binary.read_bytes()
    if not supported:
        raise ValueError(f'Backend lacks the validated indexed-cache/lifetime fix: {binary}')
    result = {'pipeline_cache_protocol':1,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()}
    output = Path(str(binary)+'.capabilities.json')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return output


if __name__=='__main__':
    print(write_capability(sys.argv[1]))
