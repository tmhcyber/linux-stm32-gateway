"""Full stage5 regression, bounded child waits, explicit per-suite provenance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    a.output.mkdir();source=json.loads((ROOT/'SOURCE.json').read_text())
    for name,digest in source['files'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest: raise ValueError('source mismatch '+name)
    (a.output/'SOURCE.json').write_bytes((ROOT/'SOURCE.json').read_bytes())
    build=Path.home()/'project/build'/source['snapshot'];commands=[]
    for name,target in [('normal','test'),('asan','sanitize')]:
        env=dict(os.environ,TEST_SUITE_REPORT=str(a.output/(name+'-suite.json')),
                 TEST_PROCESS_AUDIT=str(a.output/(name+'-processes.jsonl')),
                 STAGE3_EVIDENCE=str(a.output/(name+'-storage')),MQTT_EVIDENCE=str(a.output/(name+'-mqtt')))
        command=['make',f'BUILD={build}',target]
        with (a.output/(name+'.txt')).open('w') as log:
            result=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900)
        commands.append(dict(command=command,exit_code=result.returncode))
        (a.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode: return 1
    command=[sys.executable,str(ROOT/'scripts/linux/validate_tsan.py'),'--build',str(build),'--output',str(a.output/'tsan')]
    with (a.output/'tsan.txt').open('w') as log: result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=900)
    commands.append(dict(command=command,exit_code=result.returncode))
    suites={n:json.loads((a.output/(n+'-suite.json')).read_text()) for n in ('normal','asan')}
    passed=not result.returncode and all(s['passed'] for s in suites.values())
    report=dict(passed=passed,ended_utc=datetime.now(timezone.utc).isoformat(),commands=commands,suites=suites,
                binaries={n:hashlib.sha256((build/n).read_bytes()).hexdigest() for n in ('gateway','gatewayctl')})
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=passed,build=str(build))))
    return 0 if passed else 1
if __name__=='__main__': sys.exit(main())
