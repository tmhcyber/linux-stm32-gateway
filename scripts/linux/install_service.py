"""Install a verified immutable release. Run with guest sudo; never auto-start."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('/opt/stm32-gateway/releases')
UNIT=Path('/etc/systemd/system/stm32-gateway.service')

def run(*args): subprocess.run(args,check=True)
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['install','rollback','uninstall'])
    p.add_argument('--build',type=Path);p.add_argument('--release');a=p.parse_args()
    if os.geteuid()!=0: p.error('guest sudo required')
    if a.action=='uninstall':
        run('systemctl','disable','--now','stm32-gateway.service')
        UNIT.unlink(missing_ok=True);run('systemctl','daemon-reload')
        print('Removed service unit; retained config, releases and all evidence/state.');return
    if a.action=='rollback':
        if not a.release or Path(a.release).name!=a.release: p.error('exact installed release name required')
        release=BASE/a.release
        if not (release/'BUILD.json').is_file(): p.error('release does not exist')
        run('systemctl','stop','stm32-gateway.service')
        UNIT.write_bytes((release/'unit.service').read_bytes());run('systemctl','daemon-reload')
        print('Rollback selected; inspect config, then explicitly start service.');return
    if not a.build: p.error('--build required')
    source=json.loads((ROOT/'SOURCE.json').read_text());snapshot=source['snapshot']
    for name,digest in source['files'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest: raise ValueError('source mismatch '+name)
    if json.loads((a.build/'SOURCE.json').read_text())!=source: raise ValueError('build/source manifest mismatch')
    BASE.mkdir(parents=True,exist_ok=True);release=BASE/snapshot
    if release.exists(): raise FileExistsError(release)
    release.mkdir();(release/'source').mkdir()
    for name in [*source['files'],'SOURCE.json']:
        target=release/'source'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
    binaries={}
    for name in ('gateway','gatewayctl'):
        shutil.copyfile(a.build/name,release/name);(release/name).chmod(0o755)
        binaries[name]=hashlib.sha256((release/name).read_bytes()).hexdigest()
    build=dict(snapshot=snapshot,source_manifest_sha256=source['source_manifest_sha256'],git_commit=source['git_commit'],git_dirty=source['git_dirty'],
               build_directory=str(a.build.resolve()),compiler=subprocess.check_output(['gcc','--version'],text=True),binaries=binaries)
    (release/'BUILD.json').write_text(json.dumps(build,indent=2)+'\n')
    unit=(ROOT/'config/stm32-gateway.service').read_text().replace('@RELEASE@',str(release))
    (release/'unit.service').write_text(unit)
    run('systemd-analyze','verify',str(release/'unit.service'))
    try: pwd.getpwnam('stm32gw')
    except KeyError: run('useradd','--system','--user-group','--home-dir','/nonexistent','--shell','/usr/sbin/nologin','stm32gw')
    run('usermod','-a','-G','dialout','stm32gw')
    config=Path('/etc/stm32-gateway.toml')
    if not config.exists(): shutil.copyfile(ROOT/'config/stm32-gateway.toml',config);config.chmod(0o644)
    if UNIT.exists():
        shutil.copyfile(UNIT,release/'previous-unit.service')
        run('systemctl','stop','stm32-gateway.service')
    UNIT.write_text(unit);run('systemctl','daemon-reload')
    print(json.dumps(dict(release=str(release),unit=str(UNIT),config=str(config),started=False),indent=2))
if __name__=='__main__': main()
