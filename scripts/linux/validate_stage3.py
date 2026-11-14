"""Reproducible offline/PTY stage 3 acceptance; never opens the board serial port."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from check_stage3_evidence import check


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--raw',type=Path,required=True)
    parser.add_argument('--resume',action='store_true',help='reuse recorded commands after verifying identical C/header/test/build inputs')
    args=parser.parse_args()
    source=Path(__file__).resolve().parents[2]
    evaluator=json.loads((source/'SOURCE.json').read_text())
    for name,digest in evaluator['files'].items():
        if hashlib.sha256((source/name).read_bytes()).hexdigest()!=digest:
            raise ValueError('source snapshot modified: '+name)
    if args.resume:
        manifest=json.loads((args.output/'SOURCE.json').read_text())
        for name,digest in manifest['files'].items():
            if name.startswith(('src/','include/','tests/')) or name=='Makefile':
                if evaluator['files'].get(name)!=digest:
                    raise ValueError('resume would change tested code: '+name)
        (args.output/'EVALUATOR_SOURCE.json').write_bytes((source/'SOURCE.json').read_bytes())
    else:
        args.output.mkdir()  # Refuse old evidence; runtime DBs stay in the new evidence directory.
        (args.output/'SOURCE.json').write_bytes((source/'SOURCE.json').read_bytes())
        manifest=evaluator
    build=Path.home()/'project/build'/manifest['snapshot']
    commands=json.loads((args.output/'commands.json').read_text()) if args.resume else []

    def run(command,log,env=None,allow_failure=False):
        if args.resume:
            prior=next((entry for entry in commands if entry['log']==log),None)
            if prior and prior.get('exit_code') is not None:
                if prior['command']!=list(map(str,command)) or not (args.output/log).is_file():
                    raise ValueError('resume command/log mismatch: '+log)
                if prior['exit_code'] and not allow_failure:
                    raise RuntimeError('prior required command failed: '+log)
                return prior['exit_code']
        commands.append(dict(command=list(map(str,command)),log=log))
        (args.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        with (args.output/log).open('x') as stream:
            result=subprocess.run(command,cwd=source,env=env,stdout=stream,stderr=subprocess.STDOUT)
        commands[-1]['exit_code']=result.returncode
        (args.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode and not allow_failure:
            raise RuntimeError(f'{command}: failed; see {args.output/log}')
        return result.returncode

    run(['make',f'BUILD={build}','test'],'tests.txt',
        dict(os.environ,STAGE3_EVIDENCE=str(args.output/'faults'),TEST_PROCESS_AUDIT=str(args.output/'normal-processes.jsonl'),TEST_SUITE_REPORT=str(args.output/'normal-suite.json')))
    run(['make',f'BUILD={build}','sanitize'],'sanitize.txt',
        dict(os.environ,STAGE3_EVIDENCE=str(args.output/'sanitize-faults'),TEST_PROCESS_AUDIT=str(args.output/'asan-processes.jsonl'),TEST_SUITE_REPORT=str(args.output/'asan-suite.json')))
    tsan_exit=run([sys.executable,str(source/'scripts/linux/validate_tsan.py'),
                   '--build',str(build),'--output',str(args.output/'tsan')],
                  'tsan-validation.txt',allow_failure=True)
    tsan=json.loads((args.output/'tsan/result.json').read_text()) if (args.output/'tsan/result.json').exists() else dict(passed=False,error='TSan validation did not produce a result')
    tsan['validation_exit_code']=tsan_exit
    for name,binary in [('normal-replay',build/'gateway'),('asan-replay',build/'sanitize/gateway')]:
        run([sys.executable,str(source/'scripts/linux/validate_stage3_checkpoint.py'),
            '--raw',str(args.raw),'--binary',str(binary),'--output',str(args.output/name)],name+'.txt')
    checks=check(args.output)
    (args.output/'database-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
    binaries={str(p.relative_to(build)):hashlib.sha256(p.read_bytes()).hexdigest()
              for p in (build/'gateway',build/'gateway-fault',build/'sanitize/gateway',
                        build/'sanitize/gateway-fault',build/'tsan/gateway',build/'tsan/test_queue_threads')}
    report=dict(passed=tsan_exit==0 and tsan.get('passed',False),source=manifest,evaluator_source=evaluator,build_dir=str(build),binaries=binaries,
                gcc=subprocess.check_output(['gcc','--version'],text=True),uname=list(os.uname()),
                tsan=tsan,checked_database_runs=checks['checked_runs'],
                completed_utc=datetime.now(timezone.utc).isoformat(),
                scope='offline/PTY; SQLITE_FULL via real max_page_count, IOERR via test-only VFS; no hardware disk exhaustion')
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=report['passed'],build_dir=str(build),tsan=dict(passed=tsan.get('passed',False),
                         tests=(tsan.get('suite') or {}).get('tests_run',0),startup_checks=len(tsan.get('startup_checks',[]))),
                         checked_database_runs=checks['checked_runs']),indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':
    sys.exit(main())
