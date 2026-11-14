"""Normal/ASan/UBSan/TSan regression with source and per-child evidence. No board access."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',required=True,type=Path);args=parser.parse_args()
    source=Path(__file__).resolve().parents[2];manifest=json.loads((source/'SOURCE.json').read_text())
    for name,digest in manifest['files'].items():
        assert hashlib.sha256((source/name).read_bytes()).hexdigest()==digest,name
    args.output.mkdir();(args.output/'SOURCE.json').write_bytes((source/'SOURCE.json').read_bytes())
    build=Path.home()/'project/build'/manifest['snapshot'];commands=[]
    for name,target in [('normal','test'),('asan','sanitize')]:
        environment=dict(os.environ,STAGE3_EVIDENCE=str(args.output/(name+'-storage')),
                         MQTT_EVIDENCE=str(args.output/(name+'-mqtt')),TEST_PROCESS_AUDIT=str(args.output/(name+'-processes.jsonl')),
                         TEST_SUITE_REPORT=str(args.output/(name+'-suite.json')))
        command=['make',f'BUILD={build}',target]
        with (args.output/(name+'.txt')).open('w') as stream:
            result=subprocess.run(command,cwd=source,env=environment,stdout=stream,stderr=subprocess.STDOUT)
        commands.append(dict(command=command,exit_code=result.returncode))
        (args.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode:
            raise RuntimeError(name+' failed, evidence retained: '+str(args.output))
    command=[sys.executable,str(source/'scripts/linux/validate_tsan.py'),'--build',str(build),'--output',str(args.output/'tsan')]
    with (args.output/'tsan.txt').open('w') as stream:
        result=subprocess.run(command,cwd=source,stdout=stream,stderr=subprocess.STDOUT)
    commands.append(dict(command=command,exit_code=result.returncode))
    (args.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    suites={name:json.loads((args.output/(name+'-suite.json')).read_text()) for name in ('normal','asan')}
    tsan=json.loads((args.output/'tsan/result.json').read_text()) if (args.output/'tsan/result.json').exists() else {}
    report=dict(passed=all(c['exit_code']==0 for c in commands) and all(s['passed'] for s in suites.values()) and tsan.get('passed',False),
                suites=suites,tsan=tsan,build_dir=str(build),completed_utc=datetime.now(timezone.utc).isoformat(),
                binaries={str(p.relative_to(build)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
                          [build/'gateway',build/'sanitize/gateway',build/'tsan/gateway']})
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=report['passed'],suites={n:s['tests_run'] for n,s in suites.items()},tsan_passed=tsan.get('passed',False))))
    return 0 if report['passed'] else 1


if __name__=='__main__':
    sys.exit(main())
