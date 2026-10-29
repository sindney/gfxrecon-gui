"""Fetch hash-pinned official Khronos validation binaries for the replay bundle."""
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
ASSETS=[
 ('android','vulkan-sdk-1.4.363.0','android-binaries-1.4.363.0.zip','9a3b8e32cb3330406dd4f3651594201635baee4528da8e2a5300e4f55e1a9cc7'),
 ('windows','nightly','Validation_Layers_Windows_64.zip','e6d5aebde6915386e0215824235db9b918f89e26824f8b083e037841f64258ba'),
]

def main():
    root=ROOT/'thirdparty/validation-binaries';root.mkdir(exist_ok=True)
    records=[]
    for platform,tag,name,expected in ASSETS:
        archive=root/name;url=f'https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/{tag}/{name}'
        if not archive.exists():urllib.request.urlretrieve(url,archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:
            raise RuntimeError(f'Validation archive changed: {archive}. Review/update the pinned dependency before using newer bytes.')
        output=root/platform;output.mkdir(exist_ok=True)
        with zipfile.ZipFile(archive) as z:
            for entry in z.namelist():
                if platform=='android' and 'arm64-v8a/' in entry and entry.endswith('libVkLayer_khronos_validation.so'):
                    p=output/'arm64-v8a/libVkLayer_khronos_validation.so';p.parent.mkdir(exist_ok=True);p.write_bytes(z.read(entry))
                if platform=='windows' and Path(entry).name in ['VkLayer_khronos_validation.dll','VkLayer_khronos_validation.json']:
                    (output/Path(entry).name).write_bytes(z.read(entry))
        records.append(dict(platform=platform,tag=tag,url=url,sha256=expected))
    (root/'manifest.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
    license=ROOT/'licenses/Vulkan-ValidationLayers.txt'
    if not license.exists():urllib.request.urlretrieve('https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/vulkan-sdk-1.4.363.0/LICENSE.txt',license)
    print(root)

if __name__=='__main__':main()
