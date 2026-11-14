"""Stage 6 is software simulation only: files, private PTYs/DBs and loopback MQTT."""
from collections import Counter
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import pty
import shutil
import sqlite3
import subprocess
import tempfile
import time
import unittest
from diagnostic_vectors import VECTORS, VECTORS_SHA256
from test_gateway import frame
from process_audit import audit_file_child, audit_process
from mqtt_support import Broker, wait_for, rows

FIELDS='d up_ms reset_captured reset_raw reset_reasons fault_status fault_type frame_valid frame_reject pc cfsr hfsr monitor_valid health active observed watchdog_initialized watchdog latched'.split()


def source_values(data):
    return {k:int(v,16 if v.startswith('0x') else 10) for k,v in
            (part.split('=',1) for part in data.decode().strip().split(';') if part)}


@unittest.skipUnless(os.name=='posix' and os.environ.get('GATEWAY'),'Linux make test required')
class DiagnosticTests(unittest.TestCase):
    def setUp(self):
        if os.environ.get('STAGE6_EVIDENCE'):
            self.root=Path(os.environ['STAGE6_EVIDENCE'])/self._testMethodName;self.root.mkdir(parents=True)
        else:
            temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);self.root=Path(temp.name)
        self.binary=os.environ['GATEWAY'];self.ctl=str(Path(self.binary).with_name('gatewayctl'))
        socket_dir=tempfile.TemporaryDirectory(prefix='gw-diag-');self.addCleanup(socket_dir.cleanup)
        self.db=self.root/'history.sqlite';self.sock=Path(socket_dir.name)/'control.sock';self.proc=None
        self.control=self.root/'control.txt';self.control.write_text('');self.master=self.slave=None
        self.addCleanup(self.cleanup)

    def cleanup(self):
        self.control.write_text('')
        if self.proc:
            if self.proc.poll() is None:self.proc.terminate()
            self.proc.wait(timeout=10);self.stderr.close()
            audit_file_child(self.proc,self.root/'process-stderr.txt',self.id())
            self.proc=None
        for fd in (self.master,self.slave):
            if fd is not None:os.close(fd)
        self.master=self.slave=None

    def run_command(self,command,expected=0,env=None):
        p=subprocess.run(command,capture_output=True,env=env,timeout=20)
        audit_process(command,p.returncode,p.stderr,expected,self.id(),'command')
        self.assertEqual(p.returncode,expected,p.stderr)
        return p

    def replay(self,data,name='run',expected=0,db=True):
        src=self.root/(name+'.bin');src.write_bytes(data)
        cmd=[self.binary,'--replay',str(src),'--boundary','aligned','--output',str(self.root/name)]
        if db:cmd+=['--db',str(self.db)]
        p=self.run_command(cmd,expected)
        (self.root/(name+'-stderr.txt')).write_bytes(p.stderr)
        return json.loads((self.root/name/'summary.json').read_text())

    def start(self,fault=False,mqtt=None):
        self.master,self.slave=pty.openpty();self.stderr=(self.root/'process-stderr.txt').open('wb')
        command=[os.environ['GATEWAY_FAULT'] if fault else self.binary,'--port',os.ttyname(self.slave),
                 '--continuous','--db',str(self.db),'--socket',str(self.sock),'--output',str(self.root/'run')]
        if mqtt:command+=['--mqtt','--mqtt-port',str(mqtt.port)]
        self.proc=subprocess.Popen(command,stderr=self.stderr,env=dict(os.environ,GATEWAY_STORAGE_TEST_CONTROL=str(self.control)))
        self.proc._expected_exit=143
        def ready():
            if self.proc.poll() is not None:audit_file_child(self.proc,self.root/'process-stderr.txt',self.id(),startup=True)
            return self.sock.exists() and (self.root/'run/events.jsonl').exists() and '"kind":"OPEN"' in (self.root/'run/events.jsonl').read_text()
        wait_for(ready)
        os.write(self.master,b'sync\r\n')

    def status(self):
        p=self.run_command([self.ctl,'--socket',str(self.sock),'status'])
        return json.loads(p.stdout)

    def stop(self,signal=15):
        self.proc._expected_exit=128+signal;self.proc.send_signal(signal);self.proc.wait(timeout=10)
        self.assertEqual(self.proc.returncode,128+signal)
        self.stderr.close();audit_file_child(self.proc,self.root/'process-stderr.txt',self.id());self.proc=None
        return json.loads((self.root/'run/summary.json').read_text())

    def check(self,out='run',drops=False):
        output=self.root/out;raw=(output/'raw.bin').read_bytes();records=rows(output/'diagnostics.jsonl')
        summary=json.loads((output/'summary.json').read_text());s=summary['storage']
        self.assertEqual(len(records),summary['diagnostic_records'])
        self.assertEqual(len(records),s['mcu_submitted']+s['mcu_dropped_full']+s['mcu_rejected'])
        self.assertEqual(s['mcu_submitted'],s['mcu_committed']+s['mcu_failed'])
        if not drops:self.assertEqual(len(records),s['mcu_committed'])
        for i,r in enumerate(records,1):
            piece=raw[r['raw_start']:r['raw_end']];values=source_values(piece)
            self.assertEqual(r['diagnostic_rx_id'],i);self.assertEqual(len(values),19)
            self.assertEqual({k:r[k] for k in FIELDS},values)
            self.assertEqual(r['body'].encode(),piece[:-2]);self.assertEqual(r['gateway_run_id'],summary['gateway_run_id'])
            self.assertEqual(r['pc_valid'],r['fault_status']==1 and r['frame_valid']==1)
        with closing(sqlite3.connect(self.db)) as db:
            db.row_factory=sqlite3.Row
            stored=db.execute('SELECT * FROM diagnostics WHERE run_id=? ORDER BY diagnostic_rx_id',(s['run_id'],)).fetchall()
            self.assertEqual(len(stored),s['mcu_committed'])
            for d in stored:
                original=records[d['diagnostic_rx_id']-1]
                self.assertEqual(original['run_id'],s['run_id'])
                for k in FIELDS+['gateway_run_id','utc','session','raw_start','raw_end']:self.assertEqual(d[k],original[k],k)
                self.assertEqual(d['run_id'],s['run_id']);self.assertEqual(d['body'],original['body'].encode())
                self.assertAlmostEqual(d['elapsed_s'],original['elapsed_s'],places=5)
            run=db.execute('SELECT * FROM runs WHERE run_id=?',(s['run_id'],)).fetchone()
            self.assertEqual(run['mcu_committed'],s['mcu_committed'])
            if run['ended_utc']:
                for k in ('mcu_submitted','mcu_dropped_full','mcu_rejected','mcu_failed'):self.assertEqual(run[k],s[k])
            self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
            self.assertFalse(db.execute('PRAGMA foreign_key_check').fetchall())
        return records

    def test_actual_c_vectors_pure_and_mixed_repeated_late(self):
        self.assertEqual(hashlib.sha256(b''.join(VECTORS)).hexdigest(),VECTORS_SHA256)
        s=self.replay(b''.join(VECTORS));r=self.check();self.assertEqual(s['valid_records'],0)
        self.assertEqual(len(r),len(VECTORS));self.assertEqual(r[7]['up_ms'],0)
        self.assertEqual(r[6]['pc'],0);self.assertFalse(r[6]['pc_valid'])
        s=self.replay(frame()+b''.join(VECTORS)+VECTORS[1]*3+frame(seq=2),name='mixed')
        self.check('mixed');self.assertEqual(s['valid_records'],2);self.assertEqual(s['diagnostic_records'],len(VECTORS)+3)
        # Starting later in the same periodic report still exposes the retained boot report.
        s=self.replay(VECTORS[1]*2,name='late');r=self.check('late')
        self.assertEqual(r[0]['pc'],r[1]['pc']);self.assertEqual(r[0]['fault_status'],1)
        self.assertEqual(s['sequence']['backward'],0)

    def test_strict_diagnostic_rejections_and_recovery(self):
        good=VECTORS[1];cases=[]
        def bad(a,b,error):cases.append((good.replace(a,b),error))
        bad(b'd=1;',b'd=2;','VERSION');bad(b'd=1;',b'x=1;','FAMILY')
        bad(b'd=1;',b'd=1;v=1;','FAMILY');bad(b'd=1;',b'd=1;d=1;','DUPLICATE_FIELD')
        bad(b'health=3;',b'other=3;','UNKNOWN_FIELD');bad(b'health=3;',b'','MISSING_FIELD')
        bad(b'health=3;',b'health=4;','ENUM');bad(b'frame_valid=1;',b'frame_valid=2;','ENUM')
        bad(b'health=3;',b'health=03;','INTEGER_FORMAT');bad(b'health=3;',b'health=-0;','INTEGER_FORMAT')
        bad(b'health=3;',b'health=+1;','INTEGER_FORMAT');bad(b'health=3;',b'health=4294967296;','VALUE_RANGE')
        bad(b'pc=0xFFFFFFFF;',b'pc=0xffffffff;','INTEGER_FORMAT')
        bad(b'active=0x000000FF;',b'active=0x00000100;','HEADER')
        bad(b'observed=0x000000FF;',b'observed=0x00000000;','HEADER')
        bad(b'latched=0x00070090;',b'latched=0x80070090;','HEADER')
        bad(b'reset_reasons=0x0000007F;',b'reset_reasons=0x00000001;','HEADER')
        bad(b'frame_reject=0x00000000;',b'frame_reject=0x00000008;','HEADER')
        bad(b'frame_valid=1;',b'frame_valid=0;','HEADER')
        bad(b'fault_status=1;',b'fault_status=0;','HEADER')
        bad(b'fault_type=4;',b'fault_type=0;','HEADER')
        bad(b'reset_captured=1;',b'reset_captured=0;','HEADER')
        bad(b'health=3;',b'health=\xff;','NON_ASCII');bad(b'health=3;',b'health=\x00;','SYNTAX')
        bad(b'health=3;',b'health=;','SYNTAX')
        stream=b''.join(data+good for data,_ in cases)+b'X'*510+b'\r\n'+good
        s=self.replay(stream,expected=1);self.check()
        expected=Counter(error for _,error in cases)
        self.assertEqual({k:v for k,v in s['parse_errors'].items() if v},dict(expected))
        self.assertEqual(s['diagnostic_records'],len(cases)+1);self.assertEqual(s['overlong'],1)

    def test_v2_private_copy_migration_and_rejection(self):
        # Frozen stage 5 v2 DDL is derived from that commit, never a deployed DB.
        from schema_v2 import SCHEMA_V2
        original=self.root/'v2-original.sqlite'
        with closing(sqlite3.connect(original)) as db:
            db.executescript(SCHEMA_V2)
            db.execute("INSERT INTO runs(run_id,started_utc,monotonic_start,mode,output_dir) VALUES('history','2099-01-01T00:00:00Z',0,'aligned','private')")
            db.execute("INSERT INTO frames VALUES('history',1,0,'2099-01-01T00:00:00Z',0,0,2,?,1,'EVENT',22,0,0,4,2,'first')",(frame()[:-2],))
            db.execute("INSERT INTO events VALUES('history',1,'2099-01-01T00:00:00Z',0,0,0,'PRIVATE',0)");db.commit()
        digest=hashlib.sha256(original.read_bytes()).hexdigest();shutil.copy2(original,self.db)
        self.replay(VECTORS[1]);self.check()
        with closing(sqlite3.connect(self.db)) as db:
            self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0],3)
            self.assertEqual(db.execute("SELECT body FROM frames WHERE run_id='history'").fetchone()[0],frame()[:-2])
            self.assertEqual(db.execute("SELECT kind FROM events WHERE run_id='history'").fetchone()[0],'PRIVATE')
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(),digest)
        for i,(application,version) in enumerate(((1196897073,99),(123,2),(1196897073,0))):
            with closing(sqlite3.connect(self.db)) as db:db.execute(f'PRAGMA application_id={application}');db.execute(f'PRAGMA user_version={version}');db.commit()
            before=hashlib.sha256(self.db.read_bytes()).hexdigest();self.replay(VECTORS[1],name=f'reject{i}',expected=1)
            self.assertEqual(hashlib.sha256(self.db.read_bytes()).hexdigest(),before)

    def test_ipc_late_diagnostic_only_silence_then_telemetry_recovery(self):
        self.start();self.assertIsNone(self.status()['diagnostic']['latest'])
        self.assertEqual(self.run_command([self.ctl,'--socket',str(self.sock),'latest'],4).returncode,4)
        # Cross-chunk CRLF and body fragments, entirely on a private PTY.
        for part in (VECTORS[1][:9],VECTORS[1][9:-1],VECTORS[1][-1:]):os.write(self.master,part)
        wait_for(lambda:self.status()['diagnostic']['received']==1)
        self.assertEqual(self.status()['legal_frames'],0)
        began=time.monotonic()
        while time.monotonic()-began<15.3:
            os.write(self.master,VECTORS[1]);time.sleep(.45)
        status=self.status();(self.root/'silent-status.json').write_text(json.dumps(status,indent=2))
        self.assertEqual(status['serial'],'silent');self.assertIsNone(status['legal_frame_age_s'])
        self.assertTrue(status['diagnostic']['transport_fresh']);self.assertEqual(status['diagnostic']['latest']['pc'],4294967295)
        self.assertEqual(status['sample_seq'],0);self.assertGreater(status['diagnostic']['received'],20)
        os.write(self.master,frame());wait_for(lambda:self.status()['legal_frames']==1)
        status=self.status();self.assertEqual(status['serial'],'receiving');self.assertEqual(status['diagnostic']['latest']['fault_status'],1)
        (self.root/'recovered-status.json').write_text(json.dumps(status,indent=2))
        self.stop();self.check()
        events=rows(self.root/'run/events.jsonl');self.assertEqual(sum(e['kind']=='SILENT' for e in events),1)
        self.assertEqual(sum(e['kind']=='TELEMETRY_RESUMED' for e in events),1)

    def test_queue_full_sql_failure_and_signal_accounting(self):
        self.start(fault=True);self.control.write_text('write_error=13\n')
        for _ in range(1000):os.write(self.master,VECTORS[1])
        wait_for(lambda:self.status()['diagnostic']['received']==1000)
        wait_for(lambda:self.status()['storage']['mcu_dropped_full']>0)
        # Keep failure active through normal signal drain; no test hooks in production.
        s=self.stop(signal=2);self.check(drops=True)
        self.assertGreater(s['storage']['mcu_failed'],0);self.assertGreater(s['storage']['failures'],0)
        self.assertEqual(s['storage']['queued'],0);self.assertEqual(s['storage']['in_flight'],0)
        self.assertEqual(s['valid_records'],0)

    def test_slow_storage_recovers_without_blocking_diagnostic_receive(self):
        self.start(fault=True);self.control.write_text('pause_sync_ms=80\n')
        for i in range(40):os.write(self.master,VECTORS[i%len(VECTORS)])
        wait_for(lambda:self.status()['diagnostic']['received']==40)
        self.assertLessEqual(self.status()['storage']['high_water'],256)
        self.control.write_text('');wait_for(lambda:self.status()['storage']['mcu_committed']==40)
        self.stop();self.check()

    def test_mqtt_diagnostic_state_and_old_telemetry_isolation(self):
        broker=Broker(self.root/'broker');self.addCleanup(broker.close);broker.start()
        messages=broker.subscribe('diagnostic');self.start(mqtt=broker)
        wait_for(lambda:self.status()['mqtt']['connected'])
        os.write(self.master,VECTORS[1]);os.write(self.master,frame())
        wait_for(lambda:self.status()['diagnostic']['received']==1 and self.status()['legal_frames']==1)
        def state():
            found=[r for r in rows(messages) if r['topic'].endswith('/state') and r['payload'].get('diagnostic',{}).get('received')==1]
            return found[-1] if found else None
        m=wait_for(state);latest=m['payload']['diagnostic']['latest']
        self.assertEqual({k:latest[k] for k in FIELDS},source_values(VECTORS[1]))
        self.assertTrue(latest['pc_valid']);self.assertEqual(m['qos'],1)
        wait_for(lambda:self.status()['mqtt']['topics'][0]['pubacks']>=1)
        self.assertLess(len(json.dumps(m['payload'])),4096)
        for r in rows(messages):
            if r['topic'].endswith('/telemetry'):self.assertEqual(r['payload']['v'],1);self.assertNotIn('d',r['payload'])
        s=self.stop();self.check();self.assertEqual(s['mqtt']['received'],1);self.assertEqual(s['mqtt']['format_errors'],0)

    def test_v2_migration_failure_is_atomic(self):
        from schema_v2 import SCHEMA_V2
        with closing(sqlite3.connect(self.db)) as db:
            db.executescript(SCHEMA_V2)
            db.execute('CREATE INDEX diagnostics_utc ON events(utc)');db.commit()
        self.replay(VECTORS[1],expected=1)
        with closing(sqlite3.connect(self.db)) as db:
            self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0],2)
            self.assertFalse(db.execute("SELECT name FROM sqlite_master WHERE name='diagnostics'").fetchall())
            self.assertFalse(any(c[1]=='mcu_committed' for c in db.execute('PRAGMA table_info(runs)')))

