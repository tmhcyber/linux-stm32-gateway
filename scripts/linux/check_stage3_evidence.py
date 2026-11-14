"""Read-only post-exit check of frames, both event origins and durable commit positions.

Run against a completed validation/board directory. Does not repair or change DB rows.
SQLite may recover a hot journal when opening a deliberately terminated test DB.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
from check_stage6_evidence import check as check_mcu_diagnostics


def check(root):
    results=[]
    for path in sorted(root.rglob('summary.json')):
        out=path.parent
        summary=json.loads(path.read_text())
        if 'storage' not in summary:
            continue
        stats=summary['storage'];run_id=stats['run_id']
        if not run_id:
            continue  # Initialization failures have no opened application run.
        database=out.parent/'history.sqlite'
        if not database.exists():
            raise FileNotFoundError(database)
        records={r['rx_id']:r for r in map(json.loads,(out/'records.jsonl').read_text().splitlines())}
        main={i:r for i,r in enumerate(map(json.loads,(out/'events.jsonl').read_text().splitlines()),1)}
        diagnostics={r['event_id']:r for r in map(json.loads,(out/'storage-events.jsonl').read_text().splitlines())}
        with closing(sqlite3.connect(database)) as db:
            db.row_factory=sqlite3.Row
            assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
            assert not db.execute('PRAGMA foreign_key_check').fetchall()
            rows=db.execute('SELECT * FROM frames WHERE run_id=? ORDER BY rx_id',(run_id,)).fetchall()
            points=db.execute('SELECT count(*) FROM points WHERE run_id=?',(run_id,)).fetchone()[0]
            events=db.execute('SELECT * FROM events WHERE run_id=?',(run_id,)).fetchall()
            stored_main=stored_worker=0
            for event in events:
                if event['event_id']>0:
                    expected=main[event['event_id']];stored_main+=1
                else:
                    expected=diagnostics[event['event_id']];stored_worker+=1
                for field in ('utc','kind','detail','raw_offset'):
                    assert event[field]==expected[field],(path,event['event_id'],field)
                assert abs(event['elapsed_s']-expected['elapsed_s'])<=.000001
            raw=(out/'raw.bin').read_bytes()
            for row in rows:
                expected=records[row['rx_id']]
                for field in ('utc','raw_start','raw_end','v','reason','flags','gap','coal','snap','n','sequence_relation'):
                    assert row[field]==expected[field],(path,row['rx_id'],field)
                assert abs(row['elapsed_s']-expected['elapsed_s'])<=.000001
                assert row['body']==raw[row['raw_start']:row['raw_end']-2]
                stored_points=db.execute('SELECT * FROM points WHERE run_id=? AND rx_id=? ORDER BY point_index',(run_id,row['rx_id'])).fetchall()
                assert len(stored_points)==2
                for stored,point in zip(stored_points,expected['points']):
                    for field,value in point.items():
                        assert stored[field]==value,(path,row['rx_id'],field)
            assert len(rows)==stats['committed']
            assert points==len(rows)*2
            assert stored_main==stats['event_committed']
            assert stored_worker==stats['worker_events_committed']
            assert len(diagnostics)==stored_worker+stats['diagnostic_dropped']
            assert len(records)==stats['submitted']+stats['dropped_full']+stats['rejected']
            assert stats['submitted']==stats['committed']+stats['failed_records']
            assert len(main)==stats['event_submitted']+stats['event_dropped_full']+stats['event_rejected']
            assert stats['event_submitted']==stats['event_committed']+stats['event_failed']
            assert stats['high_water']<=256 and stats['queued']==stats['in_flight']==0
            run=db.execute('SELECT * FROM runs WHERE run_id=?',(run_id,)).fetchone()
            for field in ('committed','event_committed','worker_events_committed','last_rx_id','commits'):
                assert run[field]==stats[field],(path,field)
            if run['ended_utc'] is not None:
                assert run['exit_code']==summary['exit_code'],path
            mcu=check_mcu_diagnostics(out,database) if 'diagnostic_records' in summary else None
            results.append(dict(path=str(out),run_id=run_id,frames=len(rows),points=points,mcu_diagnostics=mcu,
                                main_events=stored_main,worker_events=stored_worker,stats=stats,
                                finalized_in_database=run['ended_utc'] is not None))
    assert results,'no finalized storage evidence found'
    return dict(passed=True,checked_runs=len(results),runs=results,
                checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    print(json.dumps(check(args.directory),indent=2))
