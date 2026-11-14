"""Bounded offline first-half evidence; no hardware, fault injection or long test."""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import unittest

SOURCE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOURCE/'tests'))
from test_storage import compare_database


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((SOURCE/'SOURCE.json').read_text())
    for name, digest in manifest['files'].items():
        if hashlib.sha256((SOURCE/name).read_bytes()).hexdigest() != digest:
            raise ValueError('snapshot changed: '+name)
    args.output.mkdir()  # refuse overwrite; database stays in this run directory
    original = [json.loads(line) for line in args.raw.with_name('records.jsonl').read_text().splitlines()]
    if len(original) < 123:
        raise ValueError('reference must contain at least 123 records')
    # Preserve original startup stream discard and seven bytes of a following partial frame.
    raw = args.raw.read_bytes()
    data = raw[:original[122]['raw_end']+7]
    fixture = args.output/'real-prefix.bin'; fixture.write_bytes(data)
    database = args.output/'history.sqlite'
    out = args.output/'replay'
    command = [str(args.binary),'--replay',str(fixture),'--boundary','stream',
               '--output',str(out),'--db',str(database)]
    result = subprocess.run(command,capture_output=True,text=True,timeout=20)
    summary = json.loads((out/'summary.json').read_text())
    check = unittest.TestCase()
    check.assertEqual(result.returncode,0,result.stderr)
    check.assertEqual(summary['valid_records'],123)
    check.assertEqual(summary['pending_bytes'],7)
    check.assertEqual(summary['storage']['submitted'],123)
    check.assertEqual(summary['storage']['committed'],123)
    check.assertGreaterEqual(summary['storage']['commits'],13)
    check.assertEqual(summary['storage']['last_rx_id'],123)
    check.assertEqual(summary['storage']['dropped_full'],0)
    check.assertEqual(summary['storage']['sqlite_error'],0)
    # New connection after the receiver has joined its worker and closed SQLite.
    with sqlite3.connect(database) as db:
        records = compare_database(check,db,out)
        run = dict(db.execute('SELECT * FROM runs').fetchone())
        check.assertIsNotNone(run['ended_utc'])
        check.assertEqual(run['exit_code'],0)
        check.assertEqual(run['committed'],123)
        check.assertEqual(db.execute('SELECT count(*) FROM points').fetchone()[0],246)
    for row, reference in zip(records,original[:123]):
        check.assertEqual({k:v for k,v in row.items() if k not in ('utc','elapsed_s')},
                          {k:v for k,v in reference.items() if k not in ('utc','elapsed_s')})
    (args.output/'SOURCE.json').write_bytes((SOURCE/'SOURCE.json').read_bytes())
    report = dict(passed=True,command=command,exit_code=result.returncode,stderr=result.stderr,
                  binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  original_raw=str(args.raw),original_raw_sha256=hashlib.sha256(raw).hexdigest(),
                  fixture_sha256=hashlib.sha256(data).hexdigest(),source_manifest_sha256=manifest['source_manifest_sha256'],
                  git_dirty=manifest['git_dirty'],sqlite_version=sqlite3.sqlite_version,
                  frames=123,points=246,commits=summary['storage']['commits'],sync_discards=summary['sync_discards'],
                  discarded_bytes=summary['discarded_bytes'],pending_bytes=7,run=run,
                  coverage='bounded real prefix; EOF batch/drain/reopen, not 1Hz timer or full-hour replay')
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
