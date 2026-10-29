"""Capture a debuggable Vulkan app with a journaled, serial-specific ADB session.

Requires the compatible GFXReconstruct replay/layer provider APK installed first.
Use `restore JOURNAL` after an interrupted host/device session. No rooting or APK
modification is performed. Capture data stays in the app's session-owned files.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time


class DeviceHelpRequired(RuntimeError):
    pass


class Adb:
    def __init__(self, executable, serial):
        self.executable = executable
        self.serial = serial

    def run(self, *args, binary=False):
        result = subprocess.run([self.executable, '-s', self.serial, *args],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if result.returncode:
            raise DeviceHelpRequired(result.stderr.decode('utf-8', 'replace').strip() or
                                     result.stdout.decode('utf-8', 'replace').strip())
        return result.stdout if binary else result.stdout.decode('utf-8', 'replace').strip()

    def shell(self, *args):
        return self.run('shell', ' '.join(shlex.quote(str(arg)) for arg in args))


def save(journal, path):
    partial = path.with_suffix('.tmp')
    partial.write_text(json.dumps(journal, indent=2), encoding='utf-8')
    partial.replace(path)


def restore(adb, path):
    journal = json.loads(path.read_text(encoding='utf-8'))
    if journal['serial'] != adb.serial:
        raise RuntimeError('Recovery journal belongs to a different device')
    conflicts = []
    for change in reversed(journal['changes']):
        if change.get('restored'):
            continue
        if change['kind'] == 'setting':
            current = adb.shell('settings', 'get', 'global', change['name'])
        else:
            current = adb.shell('getprop', change['name'])
        if current == change['previous']:
            change['restored'] = True
            save(journal, path)
            continue
        if current != change['value']:
            conflicts.append(change['name'])
            continue
        if change['kind'] == 'setting':
            if change['previous'] == 'null':
                adb.shell('settings', 'delete', 'global', change['name'])
            else:
                adb.shell('settings', 'put', 'global', change['name'], change['previous'])
        else:
            # Android has no unprivileged property deletion: an absent property's
            # effective empty value is restored using setprop with an empty string.
            adb.shell('setprop', change['name'], change['previous'])
        change['restored'] = True
        save(journal, path)
    journal['restored'] = not conflicts
    save(journal, path)
    if conflicts:
        raise DeviceHelpRequired('Recovery conflict; settings changed elsewhere: ' + ', '.join(conflicts))


def capture(adb, package, frames, output, wait_seconds, activity_args=''):
    if not re.fullmatch(r'[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+', package):
        raise ValueError('Invalid Android package name')
    if not re.fullmatch(r'\d+(?:-\d+)?(?:,\d+(?:-\d+)?)*', frames):
        raise ValueError('Invalid frame range')
    policy = adb.shell('dumpsys', 'window', 'policy')
    if re.search(r'\bshowing=true\b', policy):
        raise DeviceHelpRequired('Phone is locked. Unlock it before continuing.')
    adb.shell('run-as', package, 'id')
    if not adb.shell('pm', 'path', 'com.lunarg.gfxreconstruct.replay'):
        raise DeviceHelpRequired('Install the compatible replay/layer APK first.')
    output.mkdir(parents=True, exist_ok=False)
    journal_path = output / 'device-journal.json'
    # Android property values are limited to 91 bytes including this full path.
    session = 'g' + format(time.time_ns(), 'x')[-8:]
    relative = 'files/' + session
    remote = '/data/data/' + package + '/' + relative
    if len((remote + '/c.gfxr').encode('utf-8')) > 91:
        raise DeviceHelpRequired('The app-private capture path exceeds Android property limits; configure a shorter writable destination.')
    adb.shell('run-as', package, 'mkdir', '-p', relative)
    changes = [('setting', 'enable_gpu_debug_layers', '1'),
               ('setting', 'gpu_debug_app', package),
               ('setting', 'gpu_debug_layers', 'VK_LAYER_LUNARG_gfxreconstruct'),
               ('setting', 'gpu_debug_layer_app', 'com.lunarg.gfxreconstruct.replay'),
               ('property', 'debug.gfxrecon.capture_file', remote + '/c.gfxr'),
               ('property', 'debug.gfxrecon.capture_frames', frames),
               ('property', 'debug.gfxrecon.capture_file_timestamp', 'false')]
    journal = {'serial': adb.serial, 'package': package, 'remote': remote, 'restored': False, 'changes': []}
    for kind, name, value in changes:
        previous = adb.shell('settings', 'get', 'global', name) if kind == 'setting' else adb.shell('getprop', name)
        journal['changes'].append(dict(kind=kind, name=name, value=value, previous=previous, restored=False))
    save(journal, journal_path)
    try:
        for change in journal['changes']:
            if change['kind'] == 'setting':
                adb.shell('settings', 'put', 'global', change['name'], change['value'])
            else:
                adb.shell('setprop', change['name'], change['value'])
        adb.shell('am', 'force-stop', package)
        activity = adb.shell('cmd', 'package', 'resolve-activity', '--brief', package).splitlines()[-1]
        if '/' not in activity:
            raise DeviceHelpRequired('No launchable activity found for ' + package)
        launch_args = ['am', 'start', '-W', '-n', activity]
        if activity_args:
            launch_args += ['--es', 'args', activity_args]
        launched = adb.shell(*launch_args)
        (output / 'launch.log').write_text(launched, encoding='utf-8')
        if 'Error:' in launched or 'Exception' in launched:
            raise DeviceHelpRequired(launched)
        # Wait for the bounded range to finish; force-stop flushes owned app data
        # before transfer. A non-empty trace is verified by host info/conversion.
        time.sleep(wait_seconds)
        adb.shell('am', 'force-stop', package)
        files = adb.shell('run-as', package, 'find', relative, '-type', 'f', '-name', '*.gfxr').splitlines()
        (output / 'logcat.txt').write_text(adb.shell('logcat', '-d', '-s', 'gfxrecon:V', 'AndroidRuntime:E'), encoding='utf-8')
        if not files:
            raise DeviceHelpRequired('No capture was produced. Check that the selected app uses Vulkan and can start; see logcat.txt.')
        pulled = []
        for name in files:
            if not name.startswith(relative + '/') or '..' in name.split('/'):
                raise RuntimeError('Unexpected capture path')
            data = adb.run('exec-out', 'run-as', package, 'cat', name, binary=True)
            if not data:
                raise DeviceHelpRequired('Capture is empty: ' + name)
            local = output / Path(name).name
            local.write_bytes(data)
            pulled.append(str(local))
        (output / 'captures.json').write_text(json.dumps(pulled, indent=2), encoding='utf-8')
        return pulled
    finally:
        restore(adb, journal_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', required=True)
    commands = parser.add_subparsers(dest='command', required=True)
    cap = commands.add_parser('capture')
    cap.add_argument('--package', required=True)
    cap.add_argument('--frames', default='1-3')
    cap.add_argument('--output', type=Path, required=True)
    cap.add_argument('--wait', type=float, default=12)
    cap.add_argument('--args', default='', help='NativeActivity args extra, e.g. triangle for the upstream test launcher')
    recovery = commands.add_parser('restore')
    recovery.add_argument('journal', type=Path)
    args = parser.parse_args()
    adb = Adb(args.adb, args.serial)
    try:
        if args.command == 'capture':
            print(json.dumps(capture(adb, args.package, args.frames, args.output, args.wait, args.args)))
        else:
            restore(adb, args.journal)
    except DeviceHelpRequired as error:
        print('USER HELP REQUIRED: ' + str(error))
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
