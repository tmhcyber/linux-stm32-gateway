"""Private systemd failure/restart test; never uses the physical UART."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import pty
import pwd
import signal
import sqlite3
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests'))
from test_gateway import frame

def main():
    p=argparse.ArgumentParser();p.add_argument('--release',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    assert os.geteuid()==0;a.output.mkdir();os.chmod(a.output,0o755)
    (a.output/'EVALUATOR_SOURCE.json').write_bytes((ROOT/'SOURCE.json').read_bytes())
    name='stm32-gateway-stage5-test.service';unit=Path('/etc/systemd/system')/name
    config=Path('/etc/stm32-gateway-stage5-test.toml');state=Path('/var/lib/stm32-gateway-stage5-test-'+a.output.name)
    runtime=Path('/run/stm32-gateway-stage5-test');fault=Path('/etc/stm32-gateway-stage5-fault.py');assert not unit.exists() and not config.exists() and not state.exists()
    account=pwd.getpwnam('stm32gw');master,slave=pty.openpty();os.fchown(slave,account.pw_uid,account.pw_gid)
    port=os.ttyname(slave);commands=[]
    def run(*command,check=True):
        r=subprocess.run(command,capture_output=True,text=True,timeout=40)
        commands.append(dict(command=command,code=r.returncode,stdout=r.stdout,stderr=r.stderr,utc=datetime.now(timezone.utc).isoformat()))
        (a.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if check: assert r.returncode==0,commands[-1]
        return r
    def status():
        r=run('sudo','-u','stm32gw',str(a.release/'gatewayctl'),'--socket',str(runtime/'control.sock'),'status',check=False)
        return json.loads(r.stdout) if r.returncode==0 else None
    def wait_for(test,seconds=12):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=test()
            if value: return value
            time.sleep(.1)
        raise AssertionError('bounded test wait expired')
    try:
        text=(a.release/'unit.service').read_text().replace('/etc/stm32-gateway.toml',str(config)).replace('StateDirectory=stm32-gateway\n','StateDirectory='+state.name+'\n').replace('RuntimeDirectory=stm32-gateway\n','RuntimeDirectory=stm32-gateway-stage5-test\n')
        unit.write_text(text)
        config.write_text(f'port = "{port}"\nstate = "{state}"\nsocket = "{runtime}/control.sock"\nmqtt = false\nmqtt_port = 1883\ngateway_id = "stage5-private"\ndevice_id = "test"\n')
        run('systemd-analyze','verify',str(unit));run('systemctl','daemon-reload');run('systemctl','start',name)
        first=wait_for(status);assert first['uid']==account.pw_uid and first['uid']!=0
        os.write(master,b'sync\r\n'+b''.join(frame(seq=i,value_ms=i*1000) for i in range(1,21)))
        def committed():
            s=status();return s if s and s['storage']['committed']==20 else None
        first=wait_for(committed)
        before=run('systemctl','show',name,'--property=MainPID,InvocationID,NRestarts,User,Group,StateDirectory,RuntimeDirectory,TimeoutStopUSec').stdout
        stop_start=time.monotonic();run('systemctl','stop',name);first_stop_elapsed=time.monotonic()-stop_start
        assert not (runtime/'control.sock').exists()
        assert not fault.exists()
        fault.write_text('from pathlib import Path\nimport sys,time\np=Path('+repr(str(state/'fault-once'))+')\nif not p.exists():\n p.write_text("controlled ExecStartPost failure")\n time.sleep(1)\n sys.exit(1)\n')
        unit.write_text(text.replace('[Service]\n','[Service]\nExecStartPost=/usr/bin/python3 '+str(fault)+'\n'))
        run('systemctl','daemon-reload')
        failed_start=run('systemctl','start',name,check=False);assert failed_start.returncode!=0
        def restarted():
            s=status();return s if s and s['pid']!=first['pid'] and s['uptime_s']>.2 else None
        second=wait_for(restarted,18);assert second['gateway_run_id']!=first['gateway_run_id'] and second['storage']['run_id']!=first['storage']['run_id']
        os.write(master,b'sync\r\n'+b''.join(frame(seq=i,value_ms=i*1000) for i in range(21,31)))
        def committed2():
            s=status();return s if s and s['storage']['committed']==10 else None
        second=wait_for(committed2)
        after=run('systemctl','show',name,'--property=MainPID,InvocationID,NRestarts,User,Group').stdout
        stop_start=time.monotonic();run('systemctl','stop',name);stop_elapsed=time.monotonic()-stop_start
        assert not (runtime/'control.sock').exists()
        with sqlite3.connect(state/'history.sqlite') as db:
            assert db.execute('pragma integrity_check').fetchone()[0]=='ok'
            rows=db.execute('select run_id,count(*) from frames group by run_id').fetchall()
            assert sorted(n for _,n in rows)==[10,20],rows
            assert db.execute('select count(distinct output_dir) from runs').fetchone()[0]==3
            runs=db.execute('select run_id,output_dir,ended_utc,signal from runs').fetchall()
            assert sorted(r[3] or 0 for r in runs)==[15,15,15]
        (a.output/'TEST-UNIT.service').write_bytes(unit.read_bytes())
        journal=run('journalctl','-u',name,'--no-pager','-o','short-iso-precise').stdout
        (a.output/'journal.txt').write_text(journal)
        assert 'SERVICE_EXEC' in journal and 'event=START' in journal and 'event=EXIT detail=143' in journal
        result=dict(passed=True,first=first,second=second,before=before,after=after,stop_elapsed_s=stop_elapsed,first_stop_elapsed_s=first_stop_elapsed,db_runs=runs,frames=rows,
                    scope='private PTY/unit, controlled ExecStartPost subprocess failure causes systemd service failure and automatic restart; C receives normal TERM; normal stop then SQLite re-open')
        (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(passed=True,stop_elapsed_s=stop_elapsed)))
    finally:
        run('systemctl','stop',name,check=False)
        unit.unlink(missing_ok=True);config.unlink(missing_ok=True);fault.unlink(missing_ok=True);run('systemctl','daemon-reload');run('systemctl','reset-failed',name,check=False)
        os.close(master);os.close(slave)
        # Keep private state/evidence intact for later audit; no unit/process remains.
if __name__=='__main__': main()
