"""One 60-second real UART/MQTT/SQLite window. No writes or retrying bad windows."""
import argparse
from contextlib import closing
from datetime import datetime, timezone
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import unittest

SOURCE=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(SOURCE/'tests'))
from mqtt_support import Broker, rows, wait_for
from test_storage import compare_database
from analyze_window import analyze


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--port',default='/dev/ttyS1');args=parser.parse_args();args.output.mkdir()
    commands=[]

    def run(command):
        p=subprocess.run(command,cwd=SOURCE,capture_output=True,text=True)
        commands.append(dict(command=list(map(str,command)),exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr,
                             ended_utc=datetime.now(timezone.utc).isoformat()))
        (args.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');return p

    # fuser exit 1 means no owner; other errors or output prevent opening the tty.
    p=run(['fuser',args.port]);assert p.returncode==1 and not p.stdout and not p.stderr,p
    for scope in ('serial','storage','mqtt'):
        p=run(['bash','scripts/linux/check_environment.sh',scope]+([args.port] if scope=='serial' else []));assert p.returncode==0
    p=run(['bash','scripts/linux/configure_serial.sh',args.port]);assert p.returncode==0
    p=run(['stty','-F',args.port,'-a']);assert p.returncode==0
    broker=Broker(args.output/'broker');out=args.output/'acceptance-60s';dbpath=args.output/'history.sqlite'
    try:
        broker.start();sub=broker.subscribe('board')
        p=run([sys.executable,'scripts/linux/run_gateway.py','--port',args.port,'--duration','60','--output',str(out),
               '--db',str(dbpath),'--mqtt','--mqtt-port',str(broker.port),'--gateway-id','board-01'])
        summary=json.loads((out/'summary.json').read_text());analysis=analyze(out)
        (args.output/'analysis.json').write_text(json.dumps(analysis,indent=2)+'\n')
        if summary['valid_records']==0:
            (args.output/'result.json').write_text(json.dumps(dict(passed=False,reason='environment: no legal telemetry; no automatic retry',summary=summary),indent=2)+'\n')
            return 1
        received=rows(out/'records.jsonl')
        messages=rows(sub);telemetry=[r for r in messages if r['topic'].endswith('/telemetry')]
        by_id={r['rx_id']:r for r in received}
        for message in telemetry:
            payload=message['payload'];record=by_id[payload['rx_id']]
            assert {k:payload[k] for k in record}==record
            assert message['qos']==1 and message['retain']==0
        assert {r['payload']['rx_id'] for r in telemetry}==set(by_id),'short steady board run should confirm each received frame'
        assert any(r['payload'].get('status')=='online' for r in messages)
        assert any(r['payload'].get('status')=='offline' for r in messages)
        late=broker.subscribe('after-board');wait_for(lambda:len(rows(late))>=2)
        assert all(r['retain']==1 for r in rows(late))
        with closing(sqlite3.connect(dbpath)) as db:
            compare_database(unittest.TestCase(),db,out)
        result=dict(passed=p.returncode==0 and analysis['clean_complete'] and summary['mqtt']['offline_acked'],
                    scope='real STM32 -> CH340 COM11 -> VirtualBox UART2 -> ttyS1 -> gateway -> private loopback broker',
                    valid_records=len(received),mqtt_messages=len(telemetry),committed=summary['storage']['committed'],
                    database_check='raw/fields/points/events/counters/integrity/foreign keys passed',mqtt=summary['mqtt'],
                    exit_code=p.returncode,source_snapshot=json.loads((out/'SOURCE.json').read_text())['snapshot'])
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
        return 0 if result['passed'] else 1
    finally:
        broker.close()
        run(['fuser',args.port]);run(['stty','-F',args.port,'-a'])


if __name__=='__main__':
    sys.exit(main())
