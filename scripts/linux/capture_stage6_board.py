"""Finite real-board window, pinned binary, private DB/socket, complete post-exit checks."""
import argparse
from contextlib import closing
from datetime import datetime,timezone
import hashlib,json,os,signal,sqlite3,subprocess,sys,tempfile,time,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests'))
from test_storage import compare_database
from check_stage6_evidence import check
from analyze_window import analyze
def main():
 p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--seconds',type=int,required=True);p.add_argument('--scenario',choices=['normal','late','reset','fault'],required=True)
 a=p.parse_args();assert 15<=a.seconds<=180
 a.output.mkdir();result=dict(passed=False,scenario=a.scenario,scope='real STM32, host CH340/VirtualBox UART2/ttyS1; host route recorded separately',requested_s=a.seconds)
 command=[];proc=sub=None;runtime=tempfile.TemporaryDirectory(prefix='gw-s6-board-')
 def save(): (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 try:
  busy=subprocess.run(['sudo','-n','fuser','/dev/ttyS1'],capture_output=True);assert busy.returncode==1 and not busy.stdout
  sock=Path(runtime.name)/'control.sock';database=a.output/'history.sqlite';out=a.output/'run'
  ctl=a.binary.with_name('gatewayctl')
  def query(*args):
   q=subprocess.run([str(ctl),'--socket',str(sock),*args],capture_output=True,text=True,timeout=8)
   assert q.returncode in (0,4),(q.returncode,q.stdout,q.stderr);return json.loads(q.stdout)
  sublog=(a.output/'mqtt.jsonl').open('x');suberr=(a.output/'mqtt.stderr.txt').open('x')
  sub=subprocess.Popen(['mosquitto_sub','-h','127.0.0.1','-p','1883','-t','lab/#','-q','1','-F','%J'],stdout=sublog,stderr=suberr)
  time.sleep(.3);assert sub.poll() is None
  command=[str(a.binary),'--port','/dev/ttyS1','--duration',str(a.seconds),'--discard-buffered',
           '--db',str(database),'--socket',str(sock),'--mqtt','--gateway-id','gw-stage6','--device-id','stm32-stage6','--output',str(out)]
  (a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
  error=(a.output/'gateway.stderr.txt').open('xb');proc=subprocess.Popen(command,stderr=error)
  (a.output/'START.json').write_text(json.dumps(dict(utc=datetime.now(timezone.utc).isoformat(),pid=proc.pid,command=command))+'\n')
  observations=[];deadline=time.monotonic()+a.seconds+10
  while proc.poll() is None:
   if sock.exists():
    try:
     row=dict(utc=datetime.now(timezone.utc).isoformat(),monotonic_s=time.monotonic(),status=query('status'),latest=query('latest'))
     observations.append(row)
     with (a.output/'observations.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
    except (AssertionError,OSError) as e:
     if proc.poll() is None:raise
   if time.monotonic()>deadline:raise TimeoutError('finite window did not end')
   time.sleep(1)
  proc.wait(timeout=5);error.close();time.sleep(.2);sub.terminate();sub.wait(timeout=5);sublog.close();suberr.close();sub=None
  summary=json.loads((out/'summary.json').read_text());result['summary']=summary
  assert summary['complete'] and summary['elapsed_s']>=a.seconds
  assert summary['valid_records']>=a.seconds*.5 and summary['diagnostic_records']>=2
  assert not summary['io_error'] and not summary['open_errors'] and not summary['link_errors']
  if a.scenario in ('normal','late'):assert proc.returncode==0 and not summary['overlong'] and not any(summary['parse_errors'].values()) and not summary['silent_count']
  else:assert proc.returncode in (0,1),'unexpected runtime exit'
  s=summary['storage'];m=summary['mqtt']
  for key in ('mcu_dropped_full','mcu_rejected','mcu_failed','dropped_full','rejected','failed_records','event_dropped_full','event_rejected','event_failed','failures','diagnostic_dropped','diagnostic_io_error','sqlite_error'):assert not s[key],(key,s[key])
  assert not m['failures'] and not m['format_errors'] and m['offline_acked']
  with closing(sqlite3.connect(database)) as db:compare_database(unittest.TestCase(),db,out)
  reconciliation=check(out,database);result['diagnostic_reconciliation']=reconciliation
  diagnostics=[json.loads(line) for line in (out/'diagnostics.jsonl').read_text().splitlines()]
  records={r['rx_id']:r for r in map(json.loads,(out/'records.jsonl').read_text().splitlines())}
  run=summary['gateway_run_id'];mqtt_ids=set();mqtt_d=0;ipc_d=0
  for message in map(json.loads,(a.output/'mqtt.jsonl').read_text().splitlines()):
   payload=message['payload']
   if payload.get('gateway_run_id')!=run:continue
   if message['topic'].endswith('/telemetry'):
    expected=records[payload['rx_id']];assert {k:payload[k] for k in expected}==expected;mqtt_ids.add(payload['rx_id'])
   elif message['topic'].endswith('/device/state') and payload.get('diagnostic',{}).get('latest'):
    latest=payload['diagnostic']['latest'];expected=diagnostics[latest['diagnostic_rx_id']-1]
    assert all(expected[k]==v for k,v in latest.items());mqtt_d+=1
  # Late attachment can drain upstream backlog faster than the bounded MQTT mailbox.
  # Every published payload must match its source. The overwrite counter is not
  # an exact loss counter: offers can race a worker that already copied a slot.
  assert mqtt_ids.issubset(set(records));assert len(mqtt_ids)==m['topics'][0]['pubacks'];assert mqtt_d
  assert max(mqtt_ids)==max(records) and m['received']==len(records)
  assert m['pending']==m['in_flight']==0 and m['high_water']<=6
  if a.scenario=='normal':assert mqtt_ids==set(records) and not m['topics'][0]['overwritten']
  for row in observations:
   latest=row['status']['diagnostic']['latest']
   if latest:assert latest==diagnostics[latest['diagnostic_rx_id']-1];ipc_d+=1
  assert ipc_d
  for d in diagnostics[-2:]:assert d['monitor_valid']==1 and d['health']==1 and not d['active'] and not d['latched']
  if a.scenario=='late':assert diagnostics[0]['up_ms']>30000
  if a.scenario=='reset':assert any(b['up_ms']<a['up_ms'] for a,b in zip(diagnostics,diagnostics[1:])) and any(d['reset_reasons']&8 for d in diagnostics)
  if a.scenario=='fault':
   matches=[d for d in diagnostics if d['fault_status']==1 and d['fault_type']==4 and d['frame_valid']==1 and d['pc']==0x2001D000 and d['cfsr']&0x10000 and d['reset_reasons']&16]
   assert len(matches)>=2,'real UsageFault record and IWDG recovery not received repeatedly'
  result.update(passed=True,exit_code=proc.returncode,binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),
   records=len(records),diagnostics=len(diagnostics),mqtt_records=len(mqtt_ids),mqtt_not_published=len(records)-len(mqtt_ids),mqtt_overwrite_observations=m['topics'][0]['overwritten'],mqtt_diagnostic_states=mqtt_d,ipc_diagnostic_snapshots=ipc_d,
   first_diagnostic=diagnostics[0],last_diagnostic=diagnostics[-1],observations=len(observations))
  (a.output/'analysis.json').write_text(json.dumps(analyze(out),indent=2)+'\n')
 except BaseException as error:result['error']=repr(error);raise
 finally:
  if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=10)
  if sub and sub.poll() is None:sub.terminate();sub.wait(timeout=5)
  runtime.cleanup();save();print(json.dumps(dict(passed=result['passed'],scenario=a.scenario,error=result.get('error'))),flush=True)
if __name__=='__main__':main()
