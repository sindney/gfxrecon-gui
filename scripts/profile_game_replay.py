"""Opt-in same-device cold/warm replay profile against an already uploaded trace."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'com.lunarg.gfxreconstruct.replay'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--remote-capture', required=True)
    parser.add_argument('--remote-cache', help='Existing cache from an earlier cold profile')
    parser.add_argument('--modes', default='cold,warm')
    args = parser.parse_args()
    adb = json.loads((ROOT/'local-tools.json').read_text(encoding='utf-8'))['adb']
    output = ROOT/'.sessions/game-replay-perf'/str(time.time_ns())
    output.mkdir(parents=True)
    def run(*words, binary=False):
        value = subprocess.check_output([adb,'-s',args.serial,*words],stderr=subprocess.STDOUT,timeout=30)
        return value if binary else value.decode('utf-8',errors='replace').strip()
    def shell(*words): return run('shell',shlex.join(words))
    if 'showing=true' in shell('dumpsys','window','policy'):
        raise RuntimeError('Phone is locked. Unlock it before profiling.')
    shell('run-as',PACKAGE,'test','-s',args.remote_capture)
    relative='files/gui_perf_'+str(time.time_ns())
    remote='/data/data/'+PACKAGE+'/'+relative
    shell('run-as',PACKAGE,'mkdir','-p',relative)
    report={'serial':args.serial,'remote_capture':args.remote_capture,'remote':remote,'runs':[]}
    cache=(args.remote_cache or remote+'/pipelines.bin')
    for mode in args.modes.split(','):
        options=['--screenshots','1','--screenshot-format','bmp','--screenshot-dir',remote,
                 '--screenshot-prefix',mode,'--quit-after-frame','1',
                 '--add-new-pipeline-caches','--log-file',remote+'/'+mode+'.log']
        if mode=='cold': options+=['--omit-pipeline-cache-data']
        options += ['--save-pipeline-cache' if mode=='cold' else '--load-pipeline-cache',cache,args.remote_capture]
        shell('am','force-stop',PACKAGE)
        started=time.monotonic()
        shell('am','start','-W','-n',PACKAGE+'/android.app.NativeActivity','--es','args',' '.join(options))
        try:
            while time.monotonic()-started<240:
                time.sleep(.5)
                try: log=shell('run-as',PACKAGE,'cat',remote+'/'+mode+'.log')
                except subprocess.CalledProcessError: continue
                (output/(mode+'.log')).write_text(log,encoding='utf-8')
                if '[gfxrecon] FATAL' in log: raise RuntimeError(log)
                if 'Exiting android_main' in log: break
            else: raise TimeoutError('Replay exceeded 240 seconds')
            elapsed=time.monotonic()-started
            data=run('exec-out',shlex.join(['run-as',PACKAGE,'cat',remote+'/'+mode+'_frame_1.bmp']),binary=True)
            (output/(mode+'.bmp')).write_bytes(data)
            load=re.search(r'Load time:\s*([\d.]+)',log)
            report['runs'].append({'mode':mode,'seconds':elapsed,'load_seconds':float(load[1]) if load else None,
                                   'image_sha256':hashlib.sha256(data).hexdigest()})
            (output/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
            print(json.dumps(report['runs'][-1]),flush=True)
        finally: shell('am','force-stop',PACKAGE)
    if len(report['runs'])==2:
        assert report['runs'][0]['image_sha256']==report['runs'][1]['image_sha256'],'Cache changed rendered output'
    print(output,flush=True)


if __name__=='__main__': main()
