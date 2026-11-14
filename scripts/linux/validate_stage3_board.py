"""Post-exit checks and sanitizer replay of an existing short board window. No UART opens."""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess

from analyze_window import analyze
from check_stage3_evidence import check


def save(path, value):
    with path.open('x') as stream:
        stream.write(json.dumps(value,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('directory',type=Path)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--acceptance-name',default='acceptance-60s')
    args=parser.parse_args();source=Path(__file__).resolve().parents[2]
    manifest=json.loads((source/'SOURCE.json').read_text())
    built=json.loads((args.build/'SOURCE.json').read_text())
    for name,digest in manifest['files'].items():
        assert hashlib.sha256((source/name).read_bytes()).hexdigest()==digest,name
    for name,digest in built['files'].items():
        if name.startswith(('src/','include/','tests/')) or name=='Makefile':
            assert manifest['files'][name]==digest,name
    summaries={}
    for window in sorted(args.directory.iterdir()):
        if not window.is_dir() or not (window/'summary.json').exists():
            continue
        name=window.name
        result=analyze(args.directory/name);save(args.directory/name/'analysis.json',result)
        summaries[name]=result
    accepted=summaries[args.acceptance_name]
    clean=accepted['clean_complete']
    assert accepted['records']>=50
    assert accepted['summary']['elapsed_s']>=60
    steady_rate=accepted['max_records_in_one_second']<=2
    clean=bool(clean and not accepted['summary']['sequence']['backward'] and not accepted['summary']['sequence']['jump'])
    steady_rate=bool(steady_rate and abs(accepted['device_value_span_ms_mod32']/1000-accepted['receive_span_s'])<2)
    replay=args.directory/'asan-replay';replay.mkdir()
    binary=args.build/'sanitize/gateway'
    command=[str(binary),'--replay',str(args.directory/args.acceptance_name/'raw.bin'),
             '--boundary','stream','--db',str(replay/'history.sqlite'),'--output',str(replay/'replay')]
    run=subprocess.run(command,capture_output=True,text=True)
    save(replay/'BUILD.json',dict(command=command,exit_code=run.returncode,stderr=run.stderr,
                                binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                                build_source=built,evaluator_source=manifest))
    assert run.returncode==accepted['summary']['exit_code'] and not run.stderr,(run.returncode,run.stderr)
    live=[json.loads(line) for line in (args.directory/args.acceptance_name/'records.jsonl').read_text().splitlines()]
    offline=[json.loads(line) for line in (replay/'replay/records.jsonl').read_text().splitlines()]
    assert len(live)==len(offline)
    for actual,original in zip(offline,live):
        assert {k:v for k,v in actual.items() if k not in ('utc','elapsed_s')}=={
            k:v for k,v in original.items() if k not in ('utc','elapsed_s')}
    checks=check(args.directory);save(args.directory/'database-checks.json',checks)
    save(args.directory/'result.json',dict(passed=clean,steady_rate_passed=steady_rate,storage_checks_passed=True,acceptance_name=args.acceptance_name,accepted_frames=len(live),
        live_elapsed_s=accepted['summary']['elapsed_s'],
        settling_exit_code=summaries['settling-30s']['summary']['exit_code'],
        acceptance_exit_code=accepted['summary']['exit_code'],asan_replay_exit_code=run.returncode,checked_database_runs=checks['checked_runs'],
        source_snapshot=built['snapshot'],source_manifest_sha256=built['source_manifest_sha256']))
    print(json.dumps(dict(passed=clean,storage_checks_passed=True,accepted_frames=len(live),checked_database_runs=checks['checked_runs'])))


if __name__=='__main__':
    main()
