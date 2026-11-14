"""Resume the same real-board window after an observer-only cadence change. Sleeps <=60s; samples via IPC, no UART reader."""
import argparse
from contextlib import closing
from datetime import datetime,timezone,timedelta
import hashlib
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import sys
import time
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests'));sys.path.insert(0,str(ROOT/'scripts/linux'))
from test_storage import compare_database
from analyze_window import analyze

def utc(): return datetime.now(timezone.utc).isoformat()
def stamp(t): return t.strftime('%Y-%m-%dT%H:%M:%SZ')
def main():
    p=argparse.ArgumentParser();p.add_argument('--release',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--seconds',required=True,type=int);p.add_argument('--subscriber-pid',type=int,required=True);a=p.parse_args();assert os.geteuid()==0 and a.seconds>0
    assert a.output.is_dir()
    assert not (a.output/'result.json').exists(),'cannot resume or overwrite a finalized window'
    assert not (a.output/'CADENCE-CHANGE.json').exists(),'this helper permits only one observer handoff'
    (a.output/'RESUME_EVALUATOR_SOURCE.json').write_bytes((ROOT/'SOURCE.json').read_bytes())
    unit='stm32-gateway.service';state=Path('/var/lib/stm32-gateway');sock='/run/stm32-gateway/control.sock'
    commands=[];observations=[];subscriber=None;started=False;result=dict(passed=False,requested_s=a.seconds,scope='real STM32/host CH340/VirtualBox UART2/ttyS1, production ordinary-user service')
    def run(command,check=True):
        r=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=35)
        record=dict(command=list(map(str,command)),exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr,utc=utc())
        commands.append(record)
        with (a.output/'commands.jsonl').open('a') as f: f.write(json.dumps(record)+'\n')
        if check: assert r.returncode==0,record
        return r
    def query(*args):
        r=run(['sudo','-u','stm32gw',a.release/'gatewayctl','--socket',sock,*args],False)
        assert r.returncode in (0,4),(args,r.returncode,r.stdout,r.stderr)
        return json.loads(r.stdout)
    def service():
        r=run(['systemctl','show',unit,'--property=MainPID,InvocationID,NRestarts,ActiveState,SubState,ExecMainStartTimestampMonotonic,User'])
        return dict(line.split('=',1) for line in r.stdout.splitlines())
    def observe():
        status=query('status');latest=query('latest');wall=datetime.now(timezone.utc)
        history=query('history','--from',stamp(wall-timedelta(seconds=10)),'--until',stamp(wall),'--limit','10')
        identity=service();pid=status['pid']
        process_status=Path(f'/proc/{pid}/status').read_text()
        roles={p.name:(p/'comm').read_text().strip() for p in Path(f'/proc/{pid}/task').iterdir()}
        proc=dict(status=process_status,stat=Path(f'/proc/{pid}/stat').read_text(),fd_count=len(list(Path(f'/proc/{pid}/fd').iterdir())),roles=roles)
        row=dict(utc=utc(),monotonic_s=time.monotonic(),status=status,latest=latest,history=history,service=identity,process=proc,
                 raw_bytes=(parent/'run/raw.bin').stat().st_size)
        with (a.output/'observations.jsonl').open('a') as f: f.write(json.dumps(row)+'\n')
        observations.append(row)
        print(json.dumps(dict(utc=row['utc'],uptime_s=round(status['uptime_s'],2),pid=pid,invocation=identity['InvocationID'],
                              frames=status['legal_frames'],committed=status['storage']['committed'],mqtt_ack=status['mqtt']['topics'][0]['pubacks'],
                              queue=status['storage']['queued'],in_flight=status['storage']['in_flight'],rss_kib=next(l.split()[1] for l in process_status.splitlines() if l.startswith('VmRSS:')))),flush=True)
        return row
    try:
        initial=json.loads((a.output/'START.json').read_text())
        raw=(a.output/'observations.jsonl').read_text()
        assert raw.endswith('\n'),'old observer must hand over at a complete line'
        observations[:]=[json.loads(line) for line in raw.splitlines()]
        first=observations[0];identity=initial['service'];gateway_id=initial['gateway_run_id'];run_id=initial['run_id']
        begin_utc=initial['utc'];begin_mono=initial['monotonic_s'];parent=Path(initial['evidence_parent'])
        actual_unit=(a.output/'ACTUAL-UNIT.service').read_bytes()
        now=service();assert now['ActiveState']=='active' and now['MainPID']==identity['MainPID'] and now['InvocationID']==identity['InvocationID']
        class AdoptedSubscriber:
            def __init__(self,pid): self.pid=pid
            def poll(self):
                try: os.kill(self.pid,0);return None
                except ProcessLookupError: return 0
            def terminate(self): os.kill(self.pid,signal.SIGTERM)
            def wait(self,timeout=5):
                deadline=time.monotonic()+timeout
                while self.poll() is None:
                    try:
                        state=Path(f'/proc/{self.pid}/stat').read_text().split(') ',1)[1].split()[0]
                        if state=='Z': return -15
                    except FileNotFoundError: return -15
                    if time.monotonic()>=deadline: raise TimeoutError('adopted subscriber did not exit')
                    time.sleep(.05)
                return -15
        assert 'mosquitto_sub' in Path(f'/proc/{a.subscriber_pid}/comm').read_text()
        assert os.readlink(f'/proc/{a.subscriber_pid}/fd/1')==str(a.output/'mqtt-subscription.jsonl')
        subscriber=AdoptedSubscriber(a.subscriber_pid)
        log=(a.output/'mqtt-subscription.jsonl').open('a');suberr=(a.output/'mqtt-subscription.stderr').open('a')
        started=True
        (a.output/'CADENCE-CHANGE.json').write_text(json.dumps(dict(utc=utc(),reason='User requested 60-second lightweight sampling to reduce overhead',old_interval_s=30,new_interval_s=60,gateway_pid=int(identity['MainPID']),invocation_id=identity['InvocationID'],original_start_utc=begin_utc,original_requested_s=initial['requested_s'],adopted_subscriber_pid=a.subscriber_pid,previous_samples=len(observations)),indent=2)+'\n')
        observe()
        last_progress=0
        while True:
            row=observations[-1];s=row['status'];uptime=s['uptime_s']
            assert row['service']['InvocationID']==identity['InvocationID'] and s['pid']==first['status']['pid'],'service restarted'
            assert row['service']['ActiveState']=='active' and row['service']['NRestarts']==identity['NRestarts']
            assert s['gateway_run_id']==gateway_id and s['storage']['run_id']==run_id
            assert len(row['process']['roles'])==3 and sorted(row['process']['roles'].values())==['gateway','gw-mqtt','gw-storage']
            assert s['uid']!=0 and int(row['service']['MainPID'])==s['pid']
            assert s['parse_framing_errors']==s['open_errors']==s['link_errors']==0
            assert not s['storage']['failures'] and not s['mqtt']['failures']
            assert not s['storage']['dropped_full'] and not s['storage']['failed_records']
            if uptime>=20:
                assert s['legal_frames']>last_progress and s['legal_frame_age_s']<5,'no continuing legal telemetry'
                assert row['latest']['code']==0 and row['history']['code']==0
                last_progress=s['legal_frames']
            if len(observations)>1:
                assert row['monotonic_s']-observations[-2]['monotonic_s']<90,'monotonic observation gap/suspend'
                wall_gap=(datetime.fromisoformat(row['utc'])-datetime.fromisoformat(observations[-2]['utc'])).total_seconds()
                assert 0<=wall_gap<90,'UTC observation gap/clock jump/suspend'
            if uptime>=a.seconds: break
            wait=min(60.,a.seconds-uptime)
            time.sleep(max(.05,wait));observe()
        run(['systemctl','stop',unit]);started=False
        stop_utc=utc();stop_mono=time.monotonic();time.sleep(.3)
        subscriber.terminate();subscriber.wait(timeout=5);subscriber=None;log.close();suberr.close()
        assert Path('/etc/systemd/system/stm32-gateway.service').read_bytes()==actual_unit
        assert not Path(sock).exists();assert run(['fuser','/dev/ttyS1'],False).returncode==1
        out=parent/'run';summary=json.loads((out/'summary.json').read_text());result['summary']=summary
        assert summary['signal']==15 and summary['exit_code']==143 and not summary['complete']
        assert summary['elapsed_s']>=a.seconds and not summary['io_error'] and not summary['silent_count']
        assert not summary['overlong'] and not summary['link_errors'] and not summary['open_errors'] and not any(summary['parse_errors'].values())
        stats=summary['storage'];mqtt=summary['mqtt']
        for key in ('dropped_full','rejected','failed_records','event_dropped_full','event_rejected','event_failed','diagnostic_dropped','failures','diagnostic_io_error','sqlite_error'):
            assert not stats[key],(key,stats[key])
        assert stats['submitted']==stats['committed']==summary['valid_records']
        assert mqtt['offline_acked'] and not mqtt['failures'] and not mqtt['format_errors'] and not mqtt['diagnostic_io_error']
        assert mqtt['received']==summary['valid_records'] and mqtt['topics'][0]['pubacks']==summary['valid_records'] and mqtt['topics'][0]['overwritten']==0
        with closing(sqlite3.connect(state/'history.sqlite')) as db: compare_database(unittest.TestCase(),db,out,run_id=run_id)
        records={r['rx_id']:r for r in map(json.loads,(out/'records.jsonl').read_text().splitlines())}
        mqtt_ids=set()
        for line in (a.output/'mqtt-subscription.jsonl').read_text().splitlines():
            msg=json.loads(line);payload=msg['payload']
            if payload.get('gateway_run_id')!=gateway_id or not msg['topic'].endswith('/telemetry'): continue
            record=records[payload['rx_id']];assert {k:payload[k] for k in record}==record
            assert msg['qos']==1;mqtt_ids.add(payload['rx_id'])
        assert mqtt_ids==set(records)
        checked_queries=0
        for row in observations:
            for r in [row['latest'].get('record')]+row['history'].get('records',[]):
                if not r or r['run_id']!=run_id: continue
                expected=records[r['rx_id']]
                assert {k:r[k] for k in expected}==expected
                checked_queries+=1
        assert checked_queries>0
        analysis=analyze(out);(a.output/'analysis.json').write_text(json.dumps(analysis,indent=2)+'\n')
        journal=run(['journalctl','_SYSTEMD_INVOCATION_ID='+identity['InvocationID'],'--no-pager','-o','short-iso-precise']).stdout
        (a.output/'journal.txt').write_text(journal)
        assert 'SERVICE_EXEC' in journal and 'event=START' in journal and 'event=EXIT detail=143' in journal
        result.update(passed=True,start_utc=begin_utc,stop_utc=stop_utc,observer_monotonic_s=stop_mono-begin_mono,
                      service=identity,stable_invocation=True,run_evidence=str(parent),observations=len(observations),checked_query_records=checked_queries,
                      records=len(records),mqtt_unique_records=len(mqtt_ids),database_check='all raw/fields/points/events/counters/integrity/foreign keys passed',
                      binary_sha256=hashlib.sha256((a.release/'gateway').read_bytes()).hexdigest(),final_service=service())
    except BaseException as error:
        result['error']=repr(error)
        raise
    finally:
        if started: run(['systemctl','stop',unit],False)
        if subscriber and subscriber.poll() is None: subscriber.terminate();subscriber.wait(timeout=5)
        result['ended_utc']=utc();(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(dict(passed=result['passed'],requested_s=a.seconds,error=result.get('error'))),flush=True)
if __name__=='__main__': main()
