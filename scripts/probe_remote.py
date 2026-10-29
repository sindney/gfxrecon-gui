"""Bounded loopback-only compatibility probe; the GUI uses CLI/ADB file transport."""
import hashlib
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def exact(channel, count):
    data = bytearray()
    while len(data) < count:
        chunk = channel.recv(min(65536, count - len(data)))
        if not chunk:
            raise EOFError('Remote peer closed before a complete frame')
        data.extend(chunk)
    return bytes(data)


def frame(channel):
    length, = struct.unpack('<I', exact(channel, 4))
    if length > 32 * 1024 * 1024:
        raise ValueError('Remote frame exceeds 32 MiB probe limit')
    return exact(channel, length)


def main():
    tools = json.loads((ROOT / 'local-tools.json').read_text())
    destination = ROOT / '.sessions/remote-probe' / str(time.time_ns())
    destination.mkdir(parents=True)
    report = dict(transport='loopback TCP', backend=tools['replay'], frames=[], files=[],
                  gui_transport='CLI / ADB files', remote_enabled_in_gui=False)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(10)
        process = subprocess.Popen([tools['replay'], '--remote-connect',
                                    f'tcp:127.0.0.1:{listener.getsockname()[1]}'],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                hello = json.loads(frame(peer))
                report['frames'].append(hello)
                if hello != dict(type='hello', version='1'):
                    raise ValueError(f'Unsupported handshake: {hello}')
                settings = dict(type='settings', options=dict(capture_file=str(ROOT / 'samples/windows-triangle.gfxr'),
                                screenshots='1', screenshot_format='bmp', screenshot_prefix='probe',
                                quit_after_frame='1', omit_pipeline_cache_data='true'))
                data = json.dumps(settings).encode()
                peer.sendall(struct.pack('<I', len(data)) + data)
                total = 0
                for _ in range(64):
                    message = json.loads(frame(peer))
                    report['frames'].append(message)
                    if message.get('type') == 'file':
                        data = frame(peer)
                        total += len(data)
                        if len(data) != message['size'] or total > 128 * 1024 * 1024:
                            raise ValueError('Invalid/oversized artifact payload')
                        name = str(message['name']).replace('\\', '/')
                        if '..' in name.split('/') or name.startswith('/') or ':' in name:
                            raise ValueError('Unconfined remote artifact path')
                        # Never trust remote names as local paths.
                        owned = destination / f'artifact-{len(report["files"])}.bin'
                        owned.write_bytes(data)
                        report['files'].append(dict(name=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
                    elif message.get('type') == 'done':
                        if message.get('success') is not True:
                            raise RuntimeError('Remote replay reported failure')
                        break
                else:
                    raise RuntimeError('Remote replay exceeded message limit')
            output = process.communicate(timeout=10)[0]
            (destination / 'replay.log').write_bytes(output)
            if process.returncode or not report['files']:
                raise RuntimeError('Remote replay did not produce its requested screenshot')
            report['compatible'] = True
        except Exception as error:
            report['compatible'] = False
            report['error'] = str(error)
            process.kill()
            (destination / 'replay.log').write_bytes(process.communicate(timeout=5)[0])
        finally:
            (destination / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0 if report['compatible'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
