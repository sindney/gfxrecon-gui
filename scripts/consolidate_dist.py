"""Preserve runtime/user data, then retire known historical distribution folders.

Run --stage before packaging the canonical distribution; --finish only after it
has passed extracted-copy verification. Generated binaries are not user backups.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import time

ROOT=Path(__file__).resolve().parents[1]
DIST=ROOT/'dist'
CANONICAL=DIST/'GfxReconGui'
SETTINGS={'gfxrecon_gui.ini','gfxrecon_layout.ini','gfxrecon_tools.json','gfxrecon_preferences.json'}


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as source:
        for data in iter(lambda:source.read(1024*1024),b''):h.update(data)
    return h.hexdigest()


def inside(path,parent):
    if path.is_symlink() or not path.resolve().is_relative_to(parent.resolve()):
        raise ValueError(f'Path escaped its owner: {path}')


def stage():
    backup=ROOT/'.sessions/dist-migration'/str(time.time_ns())
    backup.mkdir(parents=True)
    roots=[DIST/'GfxreconGui']+[DIST/name/'GfxreconGui' for name in ['ui-review','status-review','mesh-review']]
    plan={'backup':str(backup),'canonical':str(CANONICAL),'roots':[],'files':[]}
    for index,release in enumerate(roots):
        if not release.exists():continue
        inside(release,DIST)
        manifest=json.loads((release/'build-info.json').read_text(encoding='utf-8'))
        generated=manifest['files']
        plan['roots'].append(str(release))
        for file in release.rglob('*'):
            if file.is_symlink():raise ValueError(f'Unexpected release link: {file}')
            if not file.is_file():continue
            relative=file.relative_to(release).as_posix()
            if relative=='build-info.json':continue
            sha=digest(file)
            if relative in generated and generated[relative]==sha:continue
            saved=backup/str(index)/relative
            saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,saved)
            if digest(saved)!=sha:raise ValueError('Runtime backup hash mismatch')
            plan['files'].append(dict(root=str(release),relative=relative,backup=str(saved),sha256=sha,
                                      modified=file.stat().st_mtime,generated=relative in generated))
        if release.parent!=DIST:
            for file in release.parent.rglob('*'):
                if not file.is_file() or file.is_relative_to(release):continue
                if file.name.casefold() in {'gfxrecongui.zip','gfxrecongui-windows-x64.zip','gfxrecongui-windows-x64.zip.sha256'}:continue
                inside(file,release.parent)
                relative=file.relative_to(release.parent).as_posix();saved=backup/(str(index)+'-outside')/relative
                saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,saved);sha=digest(file)
                if digest(saved)!=sha:raise ValueError('Wrapper backup mismatch')
                plan['files'].append(dict(root=str(release.parent),relative=relative,backup=str(saved),sha256=sha,modified=file.stat().st_mtime,generated=True))
    target=backup/'plan.json';target.write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    print(target)


def redirected(value,roots):
    if isinstance(value,str):
        for root in roots:
            for prefix,replacement in [(root,str(CANONICAL)),(Path(root).as_posix(),CANONICAL.as_posix())]:
                if value.casefold().startswith(prefix.casefold()):return replacement+value[len(prefix):]
    if isinstance(value,dict):return {key:redirected(item,roots) for key,item in value.items()}
    if isinstance(value,list):return [redirected(item,roots) for item in value]
    return value


def finish(path):
    plan=json.loads(path.read_text(encoding='utf-8'));backup=Path(plan['backup']);inside(backup,ROOT/'.sessions/dist-migration')
    manifest=json.loads((CANONICAL/'build-info.json').read_text(encoding='utf-8'))
    for name,sha in manifest['files'].items():
        if digest(CANONICAL/name)!=sha:raise ValueError(f'Canonical binary inventory changed: {name}')
    newest={}
    for entry in plan['files']:
        saved=Path(entry['backup']);inside(saved,backup)
        if digest(saved)!=entry['sha256']:raise ValueError('Preserved data changed')
        relative=entry['relative']
        if entry['generated']:continue # modified shipped files remain in the backup
        if relative in SETTINGS:
            if relative not in newest or entry['modified']>newest[relative]['modified']:newest[relative]=entry
            continue
        target=CANONICAL/relative;inside(target,CANONICAL)
        if target.exists() and digest(target)!=entry['sha256']:continue # preserve both; conflict stays in backup
        target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():shutil.copy2(saved,target)
    for relative,entry in newest.items():
        saved=Path(entry['backup']);target=CANONICAL/relative
        if target.suffix=='.json':target.write_text(json.dumps(redirected(json.loads(saved.read_text(encoding='utf-8')),plan['roots']),indent=2)+'\n',encoding='utf-8')
        else:shutil.copy2(saved,target)
    # Only these explicitly named, verified children are retired. Keep userdata
    # outside dist and a single runnable distribution plus its ZIP/checksum.
    for name in ['ui-review','status-review','mesh-review']:
        target=DIST/name
        if target.exists():
            inside(target,DIST)
            if target.resolve().parent!=DIST.resolve():raise ValueError('Unsafe legacy release target')
            shutil.rmtree(target)
    for name in ['GfxreconGui.zip','GfxreconGui-windows-x64.zip','GfxreconGui-windows-x64.zip.sha256']:
        target=DIST/name
        # The case-insensitive canonical archive names are retained on Windows.
        if name.casefold() in {'gfxrecongui-windows-x64.zip','gfxrecongui-windows-x64.zip.sha256'}:continue
        if target.exists():inside(target,DIST);target.unlink()
    plan['complete']=True
    (backup/'completed.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    print(f'Canonical distribution: {CANONICAL}\nPreserved runtime/user files: {len(plan["files"])}\nBackup: {backup}')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',action='store_true');parser.add_argument('--finish',type=Path)
    args=parser.parse_args()
    if args.finish:finish(args.finish)
    elif args.stage:stage()
    else:parser.error('Choose --stage or --finish PLAN')
