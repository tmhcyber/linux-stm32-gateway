"""Read-only reconciliation of ended MCU diagnostic runs, independently of C parsers."""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
from analyze_window import analyze

FIELDS='d up_ms reset_captured reset_raw reset_reasons fault_status fault_type frame_valid frame_reject pc cfsr hfsr monitor_valid health active observed watchdog_initialized watchdog latched'.split()


def check(out,database):
    result=analyze(out);assert result['finalized'],out
    assert result['summary_counts_match'] and not result['diagnostic_mismatches'],out
    summary=result['summary'];s=summary['storage'];run=s['run_id'];assert run
    records=[json.loads(line) for line in (out/'diagnostics.jsonl').read_text().splitlines()]
    assert len(records)==s['mcu_submitted']+s['mcu_dropped_full']+s['mcu_rejected']
    assert s['mcu_submitted']==s['mcu_committed']+s['mcu_failed']
    assert s['queued']==s['in_flight']==0 and s['high_water']<=256
    with closing(sqlite3.connect(database.resolve().as_uri()+'?mode=ro',uri=True)) as db:
        db.row_factory=sqlite3.Row
        assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
        assert not db.execute('PRAGMA foreign_key_check').fetchall()
        stored=db.execute('SELECT * FROM diagnostics WHERE run_id=? ORDER BY diagnostic_rx_id',(run,)).fetchall()
        assert len(stored)==s['mcu_committed']
        for row in stored:
            r=records[row['diagnostic_rx_id']-1]
            assert r['run_id']==run and r['gateway_run_id']==summary['gateway_run_id']
            for key in FIELDS+['run_id','gateway_run_id','utc','session','raw_start','raw_end']:
                assert row[key]==r[key],(out,r['diagnostic_rx_id'],key)
            assert row['body']==r['body'].encode('ascii')
            assert abs(row['elapsed_s']-r['elapsed_s'])<.000001
        persisted=db.execute('SELECT * FROM runs WHERE run_id=?',(run,)).fetchone()
        assert persisted['mcu_committed']==s['mcu_committed']
        if persisted['ended_utc']:
            for key in ('mcu_submitted','mcu_dropped_full','mcu_rejected','mcu_failed'):assert persisted[key]==s[key]
    inspected=0
    for path in out.parent.glob('*-status.json'):
        message=json.loads(path.read_text());d=message.get('diagnostic')
        if not d or not d['latest']:continue
        original=records[d['latest']['diagnostic_rx_id']-1]
        assert d['latest']==original,(path,'IPC diagnostic fields')
        assert d['received']<=len(records);inspected+=1
    for path in out.parent.glob('broker/*.jsonl'):
        for message in map(json.loads,path.read_text().splitlines()):
            if not message['topic'].endswith('/state'):continue
            d=message['payload'].get('diagnostic')
            if not d or not d['latest']:continue
            original=records[d['latest']['diagnostic_rx_id']-1]
            for key,value in d['latest'].items():assert value==original[key],(path,key)
            assert d['received']<=len(records);assert len(json.dumps(message['payload']))<4096;inspected+=1
    return dict(passed=True,run_id=run,diagnostics=len(records),committed=len(stored),display_snapshots=inspected,
                telemetry_records=result['records'],summary=s,
                checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--db',type=Path,required=True)
    a=p.parse_args();print(json.dumps(check(a.output,a.db),indent=2))
