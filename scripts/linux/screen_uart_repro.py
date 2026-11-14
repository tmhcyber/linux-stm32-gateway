"""Bounded live-UART screening: five windows, six paired windows only if >=2 bad.

No firmware/driver/VM changes and no sanitizer load during capture. Run once into a new
directory. Signal handlers forward normal termination to our own receiver child only.
"""
import argparse
from contextlib import closing
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import signal
import sqlite3
import subprocess

from check_stage3_evidence import check

SOURCE=Path(__file__).resolve().parents[2]


def save(path,value):
    path.write_text(json.dumps(value,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--build',type=Path,required=True);parser.add_argument('--port',default='/dev/ttyS1')
    args=parser.parse_args();args.output.mkdir()
    built=json.loads((args.build/'SOURCE.json').read_text())
    evaluator=json.loads((SOURCE/'SOURCE.json').read_text())
    for name,digest in evaluator['files'].items():
        assert hashlib.sha256((SOURCE/name).read_bytes()).hexdigest()==digest,name
    for name,digest in built['files'].items():
        if name.startswith(('src/','include/','tests/')) or name=='Makefile':
            assert evaluator['files'][name]==digest,name
    (args.output/'SOURCE.json').write_bytes((args.build/'SOURCE.json').read_bytes())
    (args.output/'EVALUATOR_SOURCE.json').write_bytes((SOURCE/'SOURCE.json').read_bytes())
    binary=args.build/'gateway';binary_hash=hashlib.sha256(binary.read_bytes()).hexdigest()
    windows=[];replays=[];child=None;cancelled=0;metadata=[]

    def stop(sig,_frame):
        nonlocal cancelled
        cancelled=sig
        if child and child.poll() is None:
            child.send_signal(sig)
    for sig in (signal.SIGINT,signal.SIGTERM):
        signal.signal(sig,stop)

    def command(command,name):
        p=subprocess.run(list(map(str,command)),capture_output=True,text=True)
        (args.output/(name+'.stdout.txt')).write_text(p.stdout)
        (args.output/(name+'.stderr.txt')).write_text(p.stderr)
        metadata.append(dict(name=name,command=list(map(str,command)),exit_code=p.returncode))
        save(args.output/'metadata-commands.json',metadata)
        return p

    def free_port(label):
        p=command(['fuser',args.port],label+'-holders')
        assert p.returncode==1 and not p.stdout,(p.returncode,p.stdout,p.stderr)

    free_port('initial')
    configured=command(['bash',SOURCE/'scripts/linux/configure_serial.sh',args.port],'idle-baseline')
    assert configured.returncode==0,configured.stderr
    ready=command(['bash',SOURCE/'scripts/linux/check_environment.sh','serial',args.port],'environment')
    assert ready.returncode==0,ready.stdout+ready.stderr

    def snapshot(name):
        p=command(['stty','-F',args.port,'-a'],name+'-attributes')
        assert p.returncode==0,p.stderr
        command(['sudo','-n','cat','/proc/tty/driver/serial'],name+'-driver')

    def capture(name,with_database):
        nonlocal child
        if cancelled:
            raise InterruptedError(cancelled)
        free_port(name+'-before');snapshot(name+'-before')
        out=args.output/name
        cmd=[str(binary),'--port',args.port,'--duration','60','--output',str(out)]
        if with_database:
            cmd+=['--db',str(args.output/'history.sqlite')]
        started=datetime.now(timezone.utc).isoformat()
        with (args.output/(name+'-runner.txt')).open('x') as log:
            child=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
            code=child.wait()
        child=None
        if cancelled:
            raise InterruptedError(cancelled)
        assert (out/'summary.json').exists(),(name,code)
        summary=json.loads((out/'summary.json').read_text())
        (out/'SOURCE.json').write_bytes((args.build/'SOURCE.json').read_bytes())
        save(out/'BUILD.json',dict(command=cmd,binary_sha256=binary_hash,started_utc=started,
                                 source=built,exit_code=code))
        snapshot(name+'-after');free_port(name+'-after')
        actual_bad=sum(summary['parse_errors'].values())+summary['overlong']
        environment_ok=bool(summary['complete'] and not summary['signal'] and
             not summary['open_errors'] and not summary['link_errors'] and not summary['io_error'] and summary['valid_records'])
        result=dict(name=name,database=with_database,started_utc=summary['started_utc'],
          elapsed_s=summary['elapsed_s'],exit_code=code,records=summary['valid_records'],
          parse_errors=summary['parse_errors'],overlong=summary['overlong'],bad=actual_bad>0,
          actual_bad_frames=actual_bad,environment_ok=environment_ok,sequence=summary['sequence'],
          sync_discards=summary['sync_discards'],discarded_bytes=summary['discarded_bytes'],pending_bytes=summary['pending_bytes'],
          storage=summary.get('storage'))
        windows.append(result);save(args.output/'progress.json',dict(windows=windows,replays=replays))
        if not environment_ok:
            # Zero bytes/failed links are not evidence of a low bad-frame frequency.
            save(args.output/'result.json',dict(classification='not_assessed_environment_unavailable',
              screening_windows_valid=sum(w['environment_ok'] for w in windows if w['name'].startswith('screen-')),
              capture_windows=len(windows),requested_capture_seconds=60*len(windows),
              windows=windows,replays=replays,modified_uart_business_logic=False,
              reason='No valid telemetry or capture environment error; five-window frequency rule cannot be applied'))
        assert environment_ok,('capture cannot be used for bad-frame frequency assessment',result)
        return result

    for index in range(5):
        capture(f'screen-{index+1:02}',True)
    bad=[w for w in windows if w['bad']]
    classification='not_reproduced_current_baseline' if not bad else 'occasional_not_modified'
    if len(bad)>=2:
        classification='repeated_not_located'
        for window in bad:
            original=args.output/window['name']
            expected_rows=[json.loads(line) for line in (original/'records.jsonl').read_text().splitlines()]
            summary=json.loads((original/'summary.json').read_text())
            for repeat in (1,2):
                out=args.output/(window['name']+f'-replay-{repeat}')
                cmd=[str(binary),'--replay',str(original/'raw.bin'),'--boundary','stream','--output',str(out)]
                p=command(cmd,window['name']+f'-replay-{repeat}')
                replay=json.loads((out/'summary.json').read_text())
                rows=[json.loads(line) for line in (out/'records.jsonl').read_text().splitlines()]
                fields=('bytes','valid_records','overlong','parse_errors','sync_discards','discarded_bytes','pending_bytes','dropping')
                consistent=all(replay[key]==summary[key] for key in fields)
                normalize=lambda records:[{key:value for key,value in row.items() if key not in ('utc','elapsed_s')} for row in records]
                consistent=consistent and normalize(rows)==normalize(expected_rows) and p.returncode==summary['exit_code']
                replays.append(dict(window=window['name'],repeat=repeat,consistent=consistent,command=cmd,exit_code=p.returncode))
                save(args.output/'progress.json',dict(windows=windows,replays=replays))
        for pair,order in enumerate(((False,True),(True,False),(False,True)),1):
            for with_database in order:
                capture(f'ab-{pair}-'+('db' if with_database else 'no-db'),with_database)
        if any(not r['consistent'] for r in replays):
            classification='offline_inconsistency_repository_investigation'
        else:
            ab=windows[5:]
            db_bad=sum(w['bad'] for w in ab if w['database'])
            no_db_bad=sum(w['bad'] for w in ab if not w['database'])
            if db_bad and no_db_bad:
                classification='repeated_shared_receive_path_not_located'
            elif db_bad>=2 and not no_db_bad:
                classification='repeated_associated_with_storage_not_proven_causal'
            else:
                classification='repeated_not_located'
    checks=check(args.output);save(args.output/'database-checks.json',checks)
    result=dict(classification=classification,screening_bad_windows=len(bad),screening_windows=5,
        capture_windows=len(windows),requested_capture_seconds=60*len(windows),windows=windows,replays=replays,
        storage_checks_passed=checks['passed'],modified_uart_business_logic=False,
        scope='stop at 0-1 bad/5; at >=2 bad/5 add exactly three alternating pairs, no further live windows')
    save(args.output/'result.json',result);free_port('final')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
