"""Build pinned GFXReconstruct dependencies and write machine-local tool paths."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['host', 'android'])
    parser.add_argument('--config', default='Release', choices=['Release', 'Debug'])
    parser.add_argument('--sdk-root', type=Path)
    parser.add_argument('--java-home', type=Path)
    parser.add_argument('--shader-bin', type=Path)
    parser.add_argument('--parallel', type=int, default=8)
    args = parser.parse_args()
    source = ROOT / 'thirdparty/gfxreconstruct'
    run(['git', '-C', source, 'submodule', 'update', '--init', '--depth', '1',
         'external/Vulkan-Headers', 'external/SPIRV-Headers'])
    local_path = ROOT / 'local-tools.json'
    config = json.loads(local_path.read_text(encoding='utf-8')) if local_path.exists() else {}
    validation=ROOT/'thirdparty/validation-binaries/windows'
    if (validation/'VkLayer_khronos_validation.json').is_file():config['validation_layer']=str(validation)
    if args.shader_bin:
        for name, executable in [('compiler', 'glslangValidator'), ('disassembler', 'spirv-dis'),
                                 ('assembler', 'spirv-as'), ('validator', 'spirv-val')]:
            config[name] = str(args.shader_bin.resolve() / (executable + '.exe'))
    if args.target == 'host':
        build = ROOT / 'build-tools'
        have_sample = Path(config.get('compiler', '')).is_file()
        hook = 'validation_targets.cmake' if have_sample else 'backend_gui.cmake'
        local_path.write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
        run(['cmake', '-S', source, '-B', build, '-A', 'x64', '-DD3D12_SUPPORT=OFF',
             '-DGFXRECON_ENABLE_OPENXR=OFF', '-DGFXRECON_TOCPP_SUPPORT=OFF',
             '-DGFXRECON_INCLUDE_TEST_APPS=ON', '-DBUILD_WERROR=OFF', '-DCMAKE_CXX_FLAGS=/utf-8',
             '-DCMAKE_PROJECT_GFXReconstruct_INCLUDE='+str(ROOT/'cmake'/hook)])
        run(['cmake', '--build', build, '--config', args.config, '--parallel', args.parallel,
             '--target', 'gfxrecon-info', 'gfxrecon-convert', 'gfxrecon-extract', 'gfxrecon-replay', 'gfxrecon-optimize',
             'gfxrecon-test-launcher', 'VkLayer_gfxreconstruct_json'] + (['gfxrecon-gui-live'] if have_sample else []))
        for name in ['info', 'convert', 'extract', 'replay', 'optimize']:
            config[name] = str(build / 'tools' / name / args.config / ('gfxrecon-' + name + '.exe'))
        config['layer'] = str(build / 'layer' / args.config)
        from write_replay_capability import write_capability
        write_capability(config['replay'])
    else:
        if not args.sdk_root or not args.java_home:
            parser.error('android requires --sdk-root and --java-home (JDK 17 or compatible)')
        android = source / 'android'
        (android / 'local.properties').write_text('sdk.dir=' + args.sdk_root.resolve().as_posix() + '\n', encoding='utf-8')
        env = dict(os.environ, JAVA_HOME=str(args.java_home.resolve()))
        # Native app-private transfer uses run-as and requires debuggable APKs.
        variant = 'assembleDebug'
        run([android / 'gradlew.bat', ':test_launcher:' + variant, ':replay:' + variant,
             '-Parm64-v8a', '-PDisableOpenXR', '--init-script', ROOT / 'scripts/android_native_activity.init.gradle',
             '--console=plain'], cwd=android, env=env)
        config['adb'] = str(args.sdk_root.resolve() / 'platform-tools/adb.exe')
        config['replay_apk'] = str(android / 'tools/replay/build/outputs/apk/debug/replay-debug.apk')
        config['test_apk'] = str(android / 'test/test_apps/launcher/build/outputs/apk/debug/test_launcher-debug.apk')
        from write_replay_capability import write_capability
        write_capability(config['replay_apk'])
    local_path.write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
    print('Configured tool paths:', local_path)


if __name__ == '__main__':
    main()
