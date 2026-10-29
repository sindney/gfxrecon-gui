import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('android_capture', Path(__file__).parents[1] / 'scripts/android_capture.py')
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


class FakeAdb:
    serial = 'test-device'

    def __init__(self):
        self.settings = {'gpu_debug_app': 'capture.target'}
        self.properties = {'debug.gfxrecon.capture_frames': '1-3'}
        self.writes = []

    def shell(self, *args):
        if args[:3] == ('settings', 'get', 'global'):
            return self.settings.get(args[3], 'null')
        if args[0] == 'getprop':
            return self.properties.get(args[1], '')
        self.writes.append(args)
        if args[:3] == ('settings', 'delete', 'global'):
            self.settings.pop(args[3], None)
        elif args[:3] == ('settings', 'put', 'global'):
            self.settings[args[3]] = args[4]
        elif args[0] == 'setprop':
            self.properties[args[1]] = args[2]
        else:
            raise AssertionError(args)
        return ''


class AndroidRecoveryTests(unittest.TestCase):
    def journal(self, path):
        value = {'serial': 'test-device', 'changes': [
            {'kind': 'setting', 'name': 'gpu_debug_app', 'previous': 'null', 'value': 'capture.target'},
            {'kind': 'property', 'name': 'debug.gfxrecon.capture_frames', 'previous': '', 'value': '1-3'}]}
        path.write_text(json.dumps(value), encoding='utf-8')

    def test_restoration_preserves_absence_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'journal.json'
            self.journal(path)
            adb = FakeAdb()
            CAPTURE.restore(adb, path)
            self.assertNotIn('gpu_debug_app', adb.settings)
            self.assertEqual(adb.properties['debug.gfxrecon.capture_frames'], '')
            self.assertTrue(json.loads(path.read_text())['restored'])
            writes = list(adb.writes)
            CAPTURE.restore(adb, path)
            self.assertEqual(adb.writes, writes)

    def test_intervening_change_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'journal.json'
            self.journal(path)
            adb = FakeAdb()
            adb.settings['gpu_debug_app'] = 'another.session'
            with self.assertRaises(CAPTURE.DeviceHelpRequired):
                CAPTURE.restore(adb, path)
            self.assertEqual(adb.settings['gpu_debug_app'], 'another.session')
            self.assertFalse(json.loads(path.read_text())['restored'])

    def test_wrong_device_is_rejected_without_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'journal.json'
            self.journal(path)
            adb = FakeAdb()
            adb.serial = 'another-device'
            with self.assertRaises(RuntimeError):
                CAPTURE.restore(adb, path)
            self.assertEqual(adb.writes, [])
