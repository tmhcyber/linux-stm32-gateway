"""Independent post-exit check of saved ten-minute evidence, preserving runner failures.

The initial runner expected 580 commits between observations at ~10 and ~592s;
579 is valid for paced input and scheduling. This evaluator verifies continuous
progress, actual outage timestamps, every saved frame/point/event and recovery.
It never rewrites original logs, SOURCE, runner result or the database.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
import unittest

SOURCE=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(SOURCE/'tests'))
from mqtt_support import rows
from test_storage import compare_database


def check(root):
    out=root/'run'
    timeline={r['kind']:r for r in json.loads((root/'timeline.json').read_text())}
    outage=timeline['outage_completed']['monotonic_s']-timeline['broker_stopped']['monotonic_s']
    assert outage>=600
    assert timeline['broker_restarted']['monotonic_s']>=timeline['outage_completed']['monotonic_s']
    assert timeline['gateway_sigterm_complete']['monotonic_s']>timeline['current_snapshot_received']['monotonic_s']
    summary=json.loads((out/'summary.json').read_text());records=rows(out/'records.jsonl')
    assert summary['exit_code']==143 and summary['signal']==15 and not summary['complete']
    assert summary['valid_records']==len(records)==summary['storage']['committed']==summary['mqtt']['received']
    assert len(records)>500 and all(b['points'][0]['seq']==a['points'][0]['seq']+1 for a,b in zip(records,records[1:]))
    mqtt=summary['mqtt'];assert mqtt['offline_acked'] and mqtt['recoveries']>=1 and mqtt['failures']>=1
    assert mqtt['pending']==mqtt['in_flight']==0 and mqtt['high_water']<=6
    during=[r for r in rows(root/'observations.jsonl') if r['phase']=='broker_down']
    assert len(during)>=55 and during[-1]['sqlite_rows']-during[0]['sqlite_rows']>=500
    assert all(b['sqlite_rows']>a['sqlite_rows'] for a,b in zip(during,during[1:]))
    for r in during:
        assert not r['mqtt']['connected'] and r['mqtt']['pending']<=3 and r['mqtt']['in_flight']<=3
        assert r['storage']['dropped_full']==r['storage']['sqlite_error']==r['storage']['failed_records']==0
    rss=[r['rss_kib'] for r in during];assert max(rss)-min(rss)<16384
    expected=timeline['current_snapshot_received']['rx_id']
    after=rows(root/'broker/after.jsonl');telemetry=[r for r in after if r['topic'].endswith('/telemetry')]
    assert telemetry and telemetry[0]['payload']['rx_id']==expected
    assert all(r['payload']['rx_id']>=expected for r in telemetry)
    for r in telemetry:
        original=records[r['payload']['rx_id']-1]
        assert {k:r['payload'][k] for k in original}==original
        assert r['qos']==1 and r['retain']==0
    assert any(r['payload'].get('status')=='offline' for r in after)
    with closing(sqlite3.connect(root/'history.sqlite')) as db:
        compare_database(unittest.TestCase(),db,out)
    return dict(passed=True,evaluation='post-exit independent check; original runner outputs retained',
                scope='PTY software integration, not board',outage_seconds=outage,frames=len(records),points=len(records)*2,
                first_recovery_rx_id=expected,rss_min_kib=min(rss),rss_max_kib=max(rss),observations=len(during),
                observed_commit_progress=during[-1]['sqlite_rows']-during[0]['sqlite_rows'],mqtt=mqtt,
                database_check='all raw/fields/points/events/counters/integrity/foreign keys passed',
                checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                evaluator_snapshot=json.loads((SOURCE/'SOURCE.json').read_text())['snapshot'])


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);args=parser.parse_args()
    result=check(args.directory)
    with (args.directory/'post-exit-check.json').open('x') as stream:
        stream.write(json.dumps(result,indent=2)+'\n')
    (args.directory/'POST_CHECK_SOURCE.json').write_bytes((SOURCE/'SOURCE.json').read_bytes())
    print(json.dumps(result,indent=2))
