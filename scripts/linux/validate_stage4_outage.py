"""Ten-minute private broker outage with paced PTY input, SQLite and bounded observations.

Run to completion; progress.json and observations.jsonl allow separate read-only monitoring.
No physical serial device or system broker is touched.
"""
import argparse
from contextlib import closing
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pty
import signal
import sqlite3
import subprocess
import sys
import time
import unittest

SOURCE=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(SOURCE/'tests'))
from mqtt_support import Broker, rows, wait_for
from test_gateway import frame
from test_storage import compare_database
from process_audit import audit_process


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path);args=parser.parse_args()
    args.output.mkdir();out=args.output/'run';dbpath=args.output/'history.sqlite'
    manifest=json.loads((SOURCE/'SOURCE.json').read_text())
    built=json.loads((args.build/'SOURCE.json').read_text())
    for name,digest in built['files'].items():
        if name.startswith(('src/','include/')) or name=='Makefile':
            assert manifest['files'].get(name)==digest,name
    (args.output/'SOURCE.json').write_bytes((args.build/'SOURCE.json').read_bytes())
    (args.output/'EVALUATOR_SOURCE.json').write_bytes((SOURCE/'SOURCE.json').read_bytes())
    binary=args.build/'gateway'
    broker=Broker(args.output/'broker');master,slave=pty.openpty();proc=None
    started=time.monotonic();sequence=0;next_send=started;observed_at=0;observations=[];timeline=[]

    def mark(kind,**details):
        timeline.append(dict(kind=kind,utc=datetime.now(timezone.utc).isoformat(),monotonic_s=time.monotonic(),**details))
        (args.output/'timeline.json').write_text(json.dumps(timeline,indent=2)+'\n')

    def progress(phase):
        (args.output/'progress.tmp').write_text(json.dumps(dict(phase=phase,elapsed_s=time.monotonic()-started,
                                                             sequence=sequence,pid=proc.pid if proc else None))+'\n')
        (args.output/'progress.tmp').replace(args.output/'progress.json')

    def tick(phase):
        nonlocal sequence,next_send,observed_at
        assert proc.poll() is None,'gateway exited during outage'
        now=time.monotonic()
        if now>=next_send:
            sequence+=1;os.write(master,frame(seq=sequence,value_ms=sequence*1000))
            next_send=now+1
        if now>=observed_at:
            status=json.loads((out/'mqtt-status.json').read_text())
            storage=json.loads((out/'storage-status.json').read_text())
            fields=dict(line.split(':',1) for line in Path(f'/proc/{proc.pid}/status').read_text().splitlines() if ':' in line)
            with closing(sqlite3.connect(dbpath,timeout=.3)) as db:
                committed=db.execute('SELECT COUNT(*) FROM frames').fetchone()[0]
            item=dict(phase=phase,elapsed_s=now-started,seq_sent=sequence,sqlite_rows=committed,
                      rss_kib=int(fields['VmRSS'].split()[0]),threads=int(fields['Threads']),
                      cpu_ticks=Path(f'/proc/{proc.pid}/stat').read_text().split()[13:15],mqtt=status,storage=storage)
            observations.append(item)
            with (args.output/'observations.jsonl').open('a') as stream:
                stream.write(json.dumps(item)+'\n')
            assert status['pending']<=3 and status['in_flight']<=3 and status['high_water']<=6
            assert storage['dropped_full']==storage['failed_records']==storage['sqlite_error']==0
            progress(phase);observed_at=now+10
        time.sleep(.1)

    try:
        broker.start();subscriber=broker.subscribe('before')
        command=[str(binary),'--port',os.ttyname(slave),'--duration','700','--output',str(out),
                 '--db',str(dbpath),'--mqtt','--mqtt-port',str(broker.port),'--gateway-id','outage-01']
        (args.output/'BUILD.json').write_text(json.dumps(dict(command=command,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                                                           source_snapshot=built['snapshot']),indent=2)+'\n')
        with (args.output/'gateway-stderr.txt').open('wb') as stream:
            proc=subprocess.Popen(command,stderr=stream)
        wait_for(lambda:'"kind":"OPEN"' in (out/'events.jsonl').read_text())
        os.write(master,b'startup\r\n');tick('initial')
        wait_for(lambda:any(r['topic'].endswith('/telemetry') for r in rows(subscriber)))
        mark('initial_online')
        broker.stop();stopped_at=time.monotonic();mark('broker_stopped',broker_pid=broker.proc.pid)
        # Ten minutes starts after the private broker has actually exited.
        while time.monotonic()-stopped_at<600:
            tick('broker_down')
        outage_seconds=time.monotonic()-stopped_at;mark('outage_completed',actual_seconds=outage_seconds)
        final_outage_seq=sequence
        wait_for(lambda:len(rows(out/'records.jsonl'))==sequence)
        broker.start();restored=broker.subscribe('after');mark('broker_restarted')
        # Hold input stable: the reconnect snapshot must equal the current final frame.
        wait_for(lambda:any(r['topic'].endswith('/telemetry') for r in rows(restored)),timeout=35)
        recovered=[r['payload'] for r in rows(restored) if r['topic'].endswith('/telemetry')]
        assert all(r['rx_id']==final_outage_seq for r in recovered),recovered
        mark('current_snapshot_received',rx_id=final_outage_seq)
        for _ in range(25):
            tick('recovered')
        proc.terminate();assert proc.wait(timeout=6)==143;mark('gateway_sigterm_complete')
        summary=json.loads((out/'summary.json').read_text())
        assert summary['mqtt']['offline_acked'] and summary['mqtt']['recoveries']>=1
        assert summary['mqtt']['failures']>=1 and summary['mqtt']['received']==sequence==summary['valid_records']
        assert summary['storage']['committed']==sequence
        with closing(sqlite3.connect(dbpath)) as db:
            compare_database(unittest.TestCase(),db,out)
        during=[x for x in observations if x['phase']=='broker_down']
        assert len(during)>=55 and during[-1]['sqlite_rows']-during[0]['sqlite_rows']>=500
        assert all(b['sqlite_rows']>a['sqlite_rows'] for a,b in zip(during,during[1:]))
        assert max(x['rss_kib'] for x in during)-min(x['rss_kib'] for x in during)<16384
        record=audit_process(command,proc.returncode,(args.output/'gateway-stderr.txt').read_text(),143,'ten_minute_outage','exit',proc.pid)
        assert record['kind']=='expected_exit'
        result=dict(passed=True,scope='software integration: private PTY and broker, not real board',outage_seconds=outage_seconds,
                    received=sequence,committed=summary['storage']['committed'],first_recovery_rx_id=final_outage_seq,
                    mqtt=summary['mqtt'],rss_min_kib=min(x['rss_kib'] for x in during),rss_max_kib=max(x['rss_kib'] for x in during),
                    observations=len(observations),process=record,database_check='raw/records/points/events/counters/integrity/foreign keys passed')
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');progress('complete')
        print(json.dumps(result,indent=2))
    finally:
        if proc and proc.poll() is None:
            proc.terminate();proc.wait(timeout=6)
        broker.close();os.close(master);os.close(slave)


if __name__=='__main__':
    main()
