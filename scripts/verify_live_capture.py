"""Opt-in native on-demand capture acceptance against the long-running Vulkan cube."""
from pathlib import Path
import json
import subprocess
import time
import os
from test_workflow import registered_layer

ROOT=Path(__file__).resolve().parents[1]

def main():
    tools=json.loads((ROOT/'local-tools.json').read_text(encoding='utf-8'))
    output=ROOT/'.sessions/live-capture'/str(time.time_ns())
    with registered_layer(True,Path(tools['layer'])/'VkLayer_gfxreconstruct.json'):
        subprocess.run([str(ROOT/'build/Release/gfxrecon_core_tests.exe'),'--live-capture',
                        str(ROOT/'build-tools/validation/Release/gfxrecon-gui-live.exe'),tools['layer'],
                        str(ROOT/'build-tools/gui-mesh/res'),tools['convert'],str(output)],check=True,timeout=150)
        state=ROOT/'build/Release/test-state';state.mkdir(exist_ok=True)
        (state/'gfxrecon_tools.json').write_text(json.dumps(tools,indent=2),encoding='utf-8')
        env=dict(os.environ,GFXRECON_GUI_LIVE_GAME=str(ROOT/'build-tools/validation/Release/gfxrecon-gui-live.exe'),
                 GFXRECON_GUI_LIVE_WORKDIR=str(ROOT/'build-tools/gui-mesh/res'))
        for name in ['GFXRECON_GUI_TEST_CAPTURE','GFXRECON_GUI_ANDROID_CAPTURE','GFXRECON_GUI_CAPTURE_DEVICE','GFXRECON_GUI_CAPTURE_WINDOWS','GFXRECON_GUI_MESH_CAPTURE','GFXRECON_GUI_STRESS_CAPTURE']:
            env.pop(name,None)
        subprocess.run(['ctest','--test-dir',str(ROOT/'build'),'-C','Release','--output-on-failure'],env=env,check=True,timeout=150)

if __name__=='__main__':main()
