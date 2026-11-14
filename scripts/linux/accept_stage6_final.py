"""Manual fixed-release service acceptance, also usable for an explicit short preflight."""
import argparse
from contextlib import closing
from datetime import datetime,timezone
import hashlib,json,os,signal,sqlite3,subprocess,sys,time,tomllib,unittest
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--release',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
 p.add_argument('--seconds',type=int,default=28800);a=p.parse_args();assert os.geteuid()==0 and 30<=a.seconds<=28800
 release=a.release.resolve(strict=True);manifest=json.loads((release/'FINAL.json').read_text())
 for name,digest in manifest['release_files'].items():assert sha(release/name)==digest,name
 assert sha(manifest['config'])==manifest['config_sha256']
 unit=Path('/etc/systemd/system/stm32-gateway.service');assert sha(unit)==manifest['unit_sha256']
 cfg=tomllib.loads(Path(manifest['config']).read_text());state=Path(cfg['state']);sock=cfg['socket']
 assert state.name=='stm32-gateway-stage6','use separate stage6 state, never migrate the old stage5 database'
 source=release/'source';sys.path[:0]=[str(source/'tests'),str(source/'scripts/linux')]
 from test_storage import compare_database
 from check_stage6_evidence import check
 a.output.mkdir();(a.output/'FINAL.json').write_bytes((release/'FINAL.json').read_bytes())
 result=dict(passed=False,scope='real board, pinned ordinary-user systemd service',requested_s=a.seconds,
             continuous_8h_passed=False,release=str(release),started_utc=datetime.now(timezone.utc).isoformat())
 sub=None;started=False;observations=[]
 def run(command,accepted=(0,)):
  r=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=35)
  with (a.output/'commands.jsonl').open('a') as f:f.write(json.dumps(dict(command=list(map(str,command)),exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr))+'\n')
  assert r.returncode in accepted,(command,r.returncode,r.stderr);return r
 def query(*args):
  r=run(['sudo','-u','stm32gw',release/'gatewayctl','--socket',sock,*args],(0,4));return json.loads(r.stdout)
 def service():
  s=run(['systemctl','show','stm32-gateway.service','-p','MainPID','-p','InvocationID','-p','NRestarts','-p','ActiveState','-p','User'])
  return dict(line.split('=',1) for line in s.stdout.splitlines())
 def interrupted(signum,frame):
  raise SystemExit(128+signum)
 for signum in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP):signal.signal(signum,interrupted)
 try:
  assert service()['ActiveState']=='inactive'
  assert run(['fuser','/dev/ttyS1'],(1,)).returncode==1
  # Explicit five-second warmup preserves upstream backlog as its own raw window.
  # It ends before the fixed service begins and is never counted into eight hours.
  warm=run([release/'gateway','--port','/dev/ttyS1','--duration','5','--discard-buffered','--output',a.output/'warmup'],(0,1))
  result['warmup_exit']=warm.returncode
  sublog=(a.output/'mqtt.jsonl').open('xb');suberr=(a.output/'mqtt.stderr.txt').open('xb')
  sub=subprocess.Popen(['mosquitto_sub','-h','127.0.0.1','-p',str(cfg['mqtt_port']),'-t','lab/#','-q','1','-F','%J'],stdout=sublog,stderr=suberr)
  time.sleep(.3);assert sub.poll() is None
  run(['systemctl','start','stm32-gateway.service']);started=True
  deadline=time.monotonic()+15;parent=None
  while time.monotonic()<deadline:
   latest=Path(sock).parent/'latest-run'
   if latest.exists() and Path(sock).exists():parent=Path(latest.read_text().strip());break
   time.sleep(.05)
  assert parent is not None,'launcher/IPC not ready'
  initial=service();gid=None;rid=None;first_diag_up=None
  while True:
   s=query('status');identity=service();now=time.monotonic()
   assert identity['InvocationID']==initial['InvocationID'] and identity['MainPID']==initial['MainPID']
   assert identity['ActiveState']=='active' and identity['NRestarts']==initial['NRestarts'] and s['uid']!=0
   roles=sorted(p.read_text().strip() for p in Path('/proc/'+str(s['pid'])+'/task').glob('*/comm'))
   assert roles==['gateway','gw-mqtt','gw-storage'],roles
   if gid is None:gid=s['gateway_run_id'];rid=s['storage']['run_id']
   assert s['gateway_run_id']==gid and s['storage']['run_id']==rid
   assert not s['parse_framing_errors'] and not s['open_errors'] and not s['link_errors']
   assert not s['storage']['failures'] and not s['mqtt']['failures']
   for k in ('mcu_dropped_full','mcu_rejected','mcu_failed','dropped_full','rejected','failed_records'):assert not s['storage'][k]
   d=s['diagnostic']
   if s['uptime_s']>=15:
    assert s['legal_frames']>0 and s['legal_frame_age_s']<5
    assert d['received']>0 and d['age_s']<10 and d['transport_fresh']
    v=d['latest'];assert v['monitor_valid']==1 and v['health']==1 and not v['active'] and not v['latched']
    assert v['fault_status']==0 and not v['pc_valid']
    if first_diag_up is None:first_diag_up=v['up_ms']
   wall=datetime.now(timezone.utc);latest=query('latest')
   row=dict(utc=wall.isoformat(),monotonic_s=now,status=s,latest=latest,service=identity,roles=roles)
   if observations:
    prev=observations[-1];assert 0<=now-prev['monotonic_s']<90
    assert 0<=(wall-datetime.fromisoformat(prev['utc'])).total_seconds()<90
    if s['uptime_s']>=20:assert s['legal_frames']>prev['status']['legal_frames'] and d['received']>prev['status']['diagnostic']['received']
   observations.append(row)
   with (a.output/'observations.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
   print(json.dumps(dict(uptime_s=round(s['uptime_s'],2),telemetry=s['legal_frames'],diagnostics=d['received'],telemetry_committed=s['storage']['committed'],diagnostic_committed=s['storage']['mcu_committed'])),flush=True)
   if s['uptime_s']>=a.seconds:break
   time.sleep(min(60.,a.seconds-s['uptime_s']))
  run(['systemctl','stop','stm32-gateway.service']);started=False;time.sleep(.3)
  sub.terminate();sub.wait(timeout=5);sub=None;sublog.close();suberr.close()
  out=parent/'run';summary=json.loads((out/'summary.json').read_text());result['summary']=summary
  assert summary['elapsed_s']>=a.seconds and summary['exit_code']==143 and summary['signal']==15
  assert not summary['complete'] and not summary['silent_count'] and not summary['io_error']
  assert not summary['overlong'] and not any(summary['parse_errors'].values())
  assert not summary['sequence']['backward'] and not summary['sequence']['jump']
  stats=summary['storage'];mqtt=summary['mqtt']
  assert stats['submitted']==stats['committed']==summary['valid_records']
  assert stats['mcu_submitted']==stats['mcu_committed']==summary['diagnostic_records']
  for key in ('dropped_full','rejected','failed_records','mcu_dropped_full','mcu_rejected','mcu_failed','event_dropped_full','event_rejected','event_failed','failures','diagnostic_dropped','diagnostic_io_error','sqlite_error'):assert not stats[key],key
  assert mqtt['offline_acked'] and not mqtt['failures'] and not mqtt['format_errors']
  assert mqtt['topics'][0]['pubacks']==summary['valid_records'] and not mqtt['topics'][0]['overwritten']
  with closing(sqlite3.connect(state/'history.sqlite')) as db:compare_database(unittest.TestCase(),db,out,rid)
  reconciled=check(out,state/'history.sqlite')
  records={r['rx_id']:r for r in map(json.loads,(out/'records.jsonl').read_text().splitlines())}
  diagnostics=[json.loads(line) for line in (out/'diagnostics.jsonl').read_text().splitlines()]
  for before,after in zip(diagnostics,diagnostics[1:]):
   assert 0<((after['up_ms']-before['up_ms'])&0xFFFFFFFF)<0x80000000,'MCU uptime reset/backward candidate during acceptance'
  mqtt_ids=set();d_count=0
  for message in map(json.loads,(a.output/'mqtt.jsonl').read_text().splitlines()):
   v=message['payload']
   if v.get('gateway_run_id')!=gid:continue
   if message['topic'].endswith('/telemetry'):
    assert {k:v[k] for k in records[v['rx_id']]}==records[v['rx_id']];mqtt_ids.add(v['rx_id'])
   elif message['topic'].endswith('/device/state') and v.get('diagnostic',{}).get('latest'):
    d=v['diagnostic']['latest'];assert all(diagnostics[d['diagnostic_rx_id']-1][k]==value for k,value in d.items());d_count+=1
  assert mqtt_ids==set(records) and d_count>0
  for row in observations:
   d=row['status']['diagnostic']['latest']
   if d:assert d==diagnostics[d['diagnostic_rx_id']-1]
  span=(diagnostics[-1]['up_ms']-first_diag_up)&0xFFFFFFFF
  assert span>=max(0,(a.seconds-90)*1000),'MCU uptime span does not cover the window'
  assert sha(unit)==manifest['unit_sha256'] and sha(manifest['config'])==manifest['config_sha256']
  result.update(passed=True,continuous_8h_passed=a.seconds==28800,run_evidence=str(parent),run_id=rid,gateway_run_id=gid,
   stable_service=initial,observations=len(observations),telemetry=len(records),diagnostics=len(diagnostics),
   mqtt_unique_records=len(mqtt_ids),mqtt_diagnostic_states=d_count,reconciliation=reconciled,final_service=service())
 except BaseException as error:result['error']=repr(error);raise
 finally:
  if started:subprocess.run(['systemctl','stop','stm32-gateway.service'])
  if sub and sub.poll() is None:sub.terminate();sub.wait(timeout=5)
  result['ended_utc']=datetime.now(timezone.utc).isoformat();(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
  print(json.dumps(dict(passed=result['passed'],continuous_8h_passed=result['continuous_8h_passed'],error=result.get('error'))),flush=True)
if __name__=='__main__':main()
