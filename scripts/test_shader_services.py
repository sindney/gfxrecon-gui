"""Opt-in shader compiler/interface/debug-metadata regressions; no GPU required."""
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    tools = json.loads((ROOT / 'local-tools.json').read_text())
    output = ROOT / '.sessions/shader-services' / str(time.time_ns())
    output.mkdir(parents=True)
    core = ROOT / 'build/Release/gfxrecon_core_tests.exe'
    results = []

    def run(args, success=True):
        result = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=30)
        if (result.returncode == 0) != success:
            raise AssertionError(f'{args}: unexpected result {result.returncode}\n{result.stdout}\n{result.stderr}')
        return result

    def compile(name, stage, text, debug=False, entry='main'):
        source = output / f'{name}.{stage}'
        binary = output / f'{name}.spv'
        source.write_text(text)
        args = [tools['compiler'], '-V', '--target-env', 'vulkan1.1', '-S', stage,
                '-e', entry, '-o', binary, source]
        if entry != 'main':
            args += ['--source-entrypoint', 'main']
        if debug:
            args += ['-g']
        run(args)
        run([tools['validator'], '--target-env', 'vulkan1.1', binary])
        return binary

    def compare(name, original, candidate, compatible):
        result = run([core, '--interface', original, candidate, 'main'], compatible)
        results.append(dict(name=name, compatible=compatible, diagnostics=result.stdout + result.stderr))

    fragment = '''#version 450
layout(location=0) in vec3 color;
layout(location=0) out vec4 outputColor;
layout(set=0,binding=1) uniform Params { vec4 tint; } params;
layout(push_constant) uniform Push { layout(offset=0) float gain; } push;
layout(constant_id=2) const float bias=0.0;
void main(){outputColor=vec4(color,1.0)*params.tint*push.gain+vec4(bias);}
'''
    original = compile('fragment', 'frag', fragment)
    compare('compatible fragment arithmetic', original, compile('fragment-edit', 'frag', fragment.replace('vec4(color,1.0)', 'vec4(color*0.5,1.0)')), True)
    for name, text in [
        ('descriptor binding', fragment.replace('binding=1', 'binding=2')),
        ('push offset', fragment.replace('offset=0', 'offset=16')),
        ('stage location', fragment.replace('location=0) in', 'location=1) in')),
        ('interpolation', fragment.replace('in vec3 color', 'flat in vec3 color')),
        ('specialization id', fragment.replace('constant_id=2', 'constant_id=3')),
    ]:
        compare(name, original, compile(name.replace(' ', '-'), 'frag', text), False)
    compare('entry identity', original, compile('entry', 'frag', fragment, entry='alternate'), False)
    compute = '''#version 450
layout(local_size_x=1) in;
layout(set=0,binding=0,std430) buffer Result { uint value[]; } result;
void main(){result.value[gl_GlobalInvocationID.x]=7u;}
'''
    comp = compile('compute', 'comp', compute)
    compare('compatible compute output', comp, compile('compute-edit', 'comp', compute.replace('=7u', '=19u')), True)
    compare('workgroup size', comp, compile('compute-size', 'comp', compute.replace('local_size_x=1', 'local_size_x=2')), False)
    vertex = '''#version 450
layout(location=0) in vec3 position;
void main(){gl_Position=vec4(position,1.0);}
'''
    vert = compile('vertex', 'vert', vertex)
    compare('compatible vertex arithmetic', vert, compile('vertex-edit', 'vert', vertex.replace('vec4(position,1.0)', 'vec4(position*0.5,1.0)')), True)
    compare('builtin output', vert, compile('vertex-builtin', 'vert', vertex.replace('gl_Position=vec4(position,1.0);', '')), False)
    debug = compile('debug', 'frag', fragment, debug=True)
    run([core, '--shader-metadata', debug, output / 'debug.json'])
    metadata = json.loads((output / 'debug.json').read_text())
    assert metadata['has_embedded_source'] and metadata['has_line_mappings']
    optimizer = Path(tools['validator']).with_name('spirv-opt.exe')
    run([optimizer, '--strip-debug', debug, '-o', output / 'stripped.spv'])
    run([core, '--shader-metadata', output / 'stripped.spv', output / 'stripped.json'])
    stripped = json.loads((output / 'stripped.json').read_text())
    assert not stripped['has_embedded_source'] and not stripped['has_line_mappings']
    report = dict(cases=results, debug_source=True, stripped_source=False,
                  binaries={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob('*.spv')})
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(f'{len(results)} interface cases and embedded/stripped metadata passed: {output}')


if __name__ == '__main__':
    main()
