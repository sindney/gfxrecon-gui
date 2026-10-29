"""Semantic shader entry/export and descriptor-compatibility regression."""
import json,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
TOOLS=json.loads((ROOT/'local-tools.json').read_text(encoding='utf-8'))
OUT=ROOT/'.sessions/nonmain-entry-review/fixtures';OUT.mkdir(parents=True,exist_ok=True)
CORE=ROOT/'build/Release/gfxrecon_core_tests.exe'
def run(args,valid=True):
 r=subprocess.run([str(v) for v in args],capture_output=True,text=True,encoding='utf-8',errors='replace')
 assert (r.returncode==0)==valid,r.stdout+r.stderr
 return r.stdout

def compile(stage,name,text,suffix):
 source=OUT/(suffix+'.glsl');source.write_text(text,encoding='utf-8');binary=OUT/(suffix+'.spv')
 run([TOOLS['compiler'],'-V','--target-env','vulkan1.1','-S',stage,'--source-entrypoint','main','-e',name,source,'-o',binary])
 run([TOOLS['validator'],'--target-env','vulkan1.1',binary])
 assert ('"'+name+'"') in run([TOOLS['disassembler'],binary])
 return binary
cases={
 'vert':'#version 450\nlayout(location=0) in vec3 pos;void main(){gl_Position=vec4(pos*FACTOR,1.0);}',
 'frag':'#version 450\nlayout(location=0) in vec3 color;layout(location=0) out vec4 result;void main(){result=vec4(color*FACTOR,1.0);}',
 'comp':'#version 450\nlayout(local_size_x=4,local_size_y=2) in;layout(set=0,binding=0,std430) buffer Data{uint values[];} data;void main(){data.values[gl_GlobalInvocationID.x]=uint(FACTOR);}'
}
for stage,source in cases.items():
 name='captured_'+stage
 before=compile(stage,name,source.replace('FACTOR','1'),stage+'-before')
 after=compile(stage,name,source.replace('FACTOR','2'),stage+'-after')
 run([CORE,'--interface',before,after,name])
 wrong=compile(stage,'wrong_entry',source.replace('FACTOR','2'),stage+'-wrong-entry')
 run([CORE,'--interface',before,wrong,name],False)
 if stage=='comp':
  incompatible=compile(stage,name,source.replace('FACTOR','2').replace('binding=0','binding=1'),stage+'-wrong-binding')
  run([CORE,'--interface',before,incompatible,name],False)
shadow='#version 450\nlayout(set=0,binding=0) uniform sampler2DShadow tex;layout(location=0) out vec4 result;void main(){result=vec4(texture(tex,vec3(.5)));}'
known=compile('frag','depth_entry',shadow,'depth-known')
words=list(struct.unpack('<%dI'%(known.stat().st_size//4),known.read_bytes()));offset=5
while offset<len(words):
 count=words[offset]>>16
 if words[offset]&65535==25:words[offset+4]=2
 offset+=count
unknown=OUT/'depth-unknown.spv';unknown.write_bytes(struct.pack('<%dI'%len(words),*words));run([TOOLS['validator'],'--target-env','vulkan1.1',unknown])
run([CORE,'--interface',unknown,known,'depth_entry'])
regular=compile('frag','depth_entry',shadow.replace('sampler2DShadow','sampler2D').replace('vec3(.5)','vec2(.5)'),'depth-color')
run([CORE,'--interface',regular,known,'depth_entry'],False)
print('Named vertex/fragment/compute exports, wrong entry/binding rejection and unknown-depth refinement passed.')
