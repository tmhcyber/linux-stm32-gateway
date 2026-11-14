"""Verify a fixed release, create unique evidence parent, then exec as service user."""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import sys
import tomllib
import uuid

def main():
    p=argparse.ArgumentParser();p.add_argument('--release',type=Path,required=True);p.add_argument('--config',type=Path,required=True)
    a=p.parse_args();release=a.release.resolve(strict=True)
    build=json.loads((release/'BUILD.json').read_text());source=json.loads((release/'source/SOURCE.json').read_text())
    for name,digest in build['binaries'].items():
        if hashlib.sha256((release/name).read_bytes()).hexdigest()!=digest: raise ValueError('release binary changed: '+name)
    for name,digest in source['files'].items():
        if hashlib.sha256((release/'source'/name).read_bytes()).hexdigest()!=digest: raise ValueError('release source changed: '+name)
    config=tomllib.loads(a.config.read_text())
    if set(config)!={'port','state','socket','mqtt','mqtt_port','gateway_id','device_id'}: raise ValueError('config keys')
    if not isinstance(config['mqtt'],bool) or type(config['mqtt_port']) is not int: raise ValueError('config types')
    state=Path(config['state']);state.mkdir(exist_ok=True)
    lock=os.open(state/'history.lock',os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW,0o600)
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);os.set_inheritable(lock,True)
    evidence=state/'evidence';evidence.mkdir(exist_ok=True)
    invocation=os.environ.get('INVOCATION_ID','manual')
    parent=evidence/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex)
    parent.mkdir(mode=0o700)
    for name,contents in {'SOURCE.json':(release/'source/SOURCE.json').read_bytes(),
                          'BUILD.json':(release/'BUILD.json').read_bytes(),
                          'config.toml':a.config.read_bytes(),
                          'unit.service':(release/'unit.service').read_bytes()}.items(): (parent/name).write_bytes(contents)
    command=[str(release/'gateway'),'--port',config['port'],'--continuous','--journal','--output',str(parent/'run'),
             '--db',str(state/'history.sqlite'),'--socket',config['socket']]
    if config['mqtt']: command+=['--mqtt','--mqtt-port',str(config['mqtt_port']),'--gateway-id',config['gateway_id'],'--device-id',config['device_id']]
    launch=dict(utc=datetime.now(timezone.utc).isoformat(),monotonic_s=__import__('time').monotonic(),
                invocation_id=invocation,pid=os.getpid(),uid=os.getuid(),gid=os.getgid(),release=str(release),
                config_sha256=hashlib.sha256(a.config.read_bytes()).hexdigest(),command=command)
    (parent/'launch.json').write_text(json.dumps(launch,indent=2)+'\n')
    runtime=Path(config['socket']).parent
    temporary=runtime/'latest-run.tmp';temporary.write_text(str(parent)+'\n');temporary.replace(runtime/'latest-run')
    print(f'level=info event=SERVICE_EXEC pid={os.getpid()} uid={os.getuid()} invocation={invocation} release={release} evidence={parent}',flush=True)
    os.execv(command[0],command)

if __name__=='__main__': main()
