"""Bounded AF_UNIX integration tests, private PTYs/databases only."""
import json
import os
from pathlib import Path
import pty
import socket
import sqlite3
import subprocess
import tempfile
import time
import unittest
from test_gateway import frame
from process_audit import audit_child, audit_process, ChildRuntimeError

@unittest.skipUnless(os.name=='posix' and os.environ.get('GATEWAY'),'Linux make test required')
class IpcTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.binary=os.environ['GATEWAY']
        self.ctl=str(Path(self.binary).with_name('gatewayctl'))
        self.path=self.root/'history.sqlite';self.sock=self.root/'control.sock';self.proc=None
        self.master,self.slave=pty.openpty()
        self.addCleanup(os.close,self.master);self.addCleanup(os.close,self.slave)
        self.addCleanup(self.stop)

    def start(self,db=True,extra=()):
        args=[self.binary,'--port',os.ttyname(self.slave),'--continuous','--output',str(self.root/'run'),'--socket',str(self.sock)]
        if db: args+=['--db',str(self.path)]
        self.proc=subprocess.Popen(args+list(extra),stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        for _ in range(150):
            if self.proc.poll() is not None:
                audit_child(self.proc,self.id(),startup=True)
            if self.sock.exists():
                try:
                    if self.call('status')[0]==0: return
                except (ConnectionError,OSError): pass
            time.sleep(.02)
        self.fail('IPC startup timeout')

    def stop(self):
        if self.proc:
            if self.proc.poll() is None: self.proc.terminate()
            self.proc.wait(timeout=10);audit_child(self.proc,self.id());self.proc=None

    def connection(self):
        s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.settimeout(8);s.connect(str(self.sock));self.addCleanup(s.close);return s

    def call(self,*args,expected=None):
        p=subprocess.run([self.ctl,'--socket',str(self.sock),*args],capture_output=True,timeout=10)
        if expected is not None:
            r=audit_process(p.args,p.returncode,p.stderr,expected,self.id(),'ctl')
            if r['kind']!='expected_exit': raise ChildRuntimeError(r)
        return p.returncode,json.loads(p.stdout or p.stderr)

    def line(self,s):
        data=b''
        while not data.endswith(b'\n'):
            part=s.recv(65536)
            if not part: raise EOFError(data)
            data+=part
        return json.loads(data)

    def feed(self,count=1,start=1):
        os.write(self.master,b'sync\r\n')
        for i in range(start,start+count): os.write(self.master,frame(seq=i,value_ms=i*1000))

    def wait_frames(self,n):
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            code,status=self.call('status')
            if code==0 and status['legal_frames']>=n: return status
            time.sleep(.025)
        self.fail('no frame progress')

    def seed(self,count=30):
        src=self.root/'seed.bin';src.write_bytes(frame())
        for i in range(2):
            p=subprocess.run([self.binary,'--replay',str(src),'--boundary','aligned','--output',str(self.root/f'seed{i}'),'--db',str(self.path)],capture_output=True,timeout=10)
            r=audit_process(p.args,p.returncode,p.stderr,0,self.id(),'seed')
            if r['kind']!='expected_exit': raise ChildRuntimeError(r)
        with sqlite3.connect(self.path) as db:
            runs=[r[0] for r in db.execute('select run_id from runs order by run_id')]
            for run in runs:
                for rx in range(2,count+1):
                    db.execute('insert into frames select run_id,?,session,utc,elapsed_s,raw_start,raw_end,body,v,reason,flags,gap,coal,snap,n,sequence_relation from frames where run_id=? and rx_id=1',(rx,run))
                    db.execute('insert into points select run_id,?,point_index,src,id,value,scale,unit,quality,status,value_ms,status_ms,seq,value_kind,is_current from points where run_id=? and rx_id=1',(rx,run))
            stamp=db.execute('select utc from frames limit 1').fetchone()[0]
            db.execute('update frames set utc=?',(stamp,))
        return stamp

    def history(self,stamp,limit=7,cursor=None):
        args=['history','--from',stamp,'--until',stamp,'--limit',str(limit)]
        if cursor: args+=['--cursor',cursor]
        return self.call(*args)

    def test_status_latest_no_db_and_cli_codes(self):
        self.assertEqual(subprocess.run([self.ctl,'--help'],capture_output=True).returncode,0)
        self.assertEqual(self.call('status')[0],3)
        self.assertEqual(self.call('history','--from','wrong')[0],2)
        self.start(db=False)
        status=self.call('status',expected=0)[1]
        self.assertIsNone(status['storage']);self.assertIsNone(status['legal_frame_age_s'])
        self.assertEqual(self.call('latest',expected=4)[1]['error'],'no_data')
        self.assertEqual(self.history('2026-11-12T00:00:00Z')[0],6)
        self.feed();self.wait_frames(1)
        latest=self.call('latest',expected=0)[1]
        self.assertEqual(latest['record']['points'][0]['scale'],-3)
        self.assertTrue(latest['transport_fresh']);self.assertEqual(len(latest['gateway_run_id']),32)

    def test_segmented_pipeline_half_close_and_bad_requests(self):
        self.start();s=self.connection()
        for part in (b'1 sta',b'tus\n1 lat',b'est\n'): s.sendall(part)
        s.shutdown(socket.SHUT_WR)
        f=s.makefile('rb');self.addCleanup(f.close)
        self.assertEqual(json.loads(f.readline())['code'],0)
        self.assertEqual(json.loads(f.readline())['code'],4)
        for request in (b'2 status\n',b'1 status\x00\n',b'x'*514,b'1 history a b 0 -\n',b'1 history 2026-02-30T00:00:00Z 2026-11-12T00:00:00Z 1 -\n'):
            with self.subTest(request=request[:60]):
                c=self.connection();c.sendall(request);self.assertEqual(self.line(c)['code'],2);c.close()

    def test_history_duplicate_time_cross_run_order_boundaries_and_retired_cursor(self):
        stamp=self.seed();self.start();all_rows=[];cursor=None
        while True:
            code,page=self.history(stamp,cursor=cursor)
            if not all_rows: self.assertEqual(code,0)
            all_rows+=page['records'];cursor=page['next_cursor']
            if not page['more']: break
        keys=[(r['utc'],r['run_id'],r['rx_id']) for r in all_rows]
        self.assertEqual(len(keys),60);self.assertEqual(keys,sorted(set(keys)))
        self.assertEqual(len({r['run_id'] for r in all_rows}),2)
        self.assertEqual(self.history(stamp,cursor=cursor)[0],4)
        first=self.history(stamp,limit=1)[1];retired=first['next_cursor']
        with sqlite3.connect(self.path) as db:
            run=first['records'][0]['run_id'];rx=first['records'][0]['rx_id']
            db.execute('delete from points where run_id=? and rx_id=?',(run,rx))
            db.execute('delete from frames where run_id=? and rx_id=?',(run,rx))
        after=self.history(stamp,limit=1000,cursor=retired)[1]
        self.assertEqual(len(after['records']),59)
        self.assertEqual(self.history('2000-01-01T00:00:00Z')[0],4)
        self.assertEqual(self.history(stamp,limit=1001)[0],2)

    def test_slow_readers_timeouts_pressure_and_write_progress(self):
        stamp=self.seed(600);self.start();slow=[]
        for _ in range(6):
            s=self.connection();s.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,1024)
            s.sendall(f'1 history {stamp} {stamp} 1000 -\n'.encode());slow.append(s)
        self.feed(100);status=self.wait_frames(100)
        self.assertLessEqual(status['storage']['high_water'],256)
        time.sleep(5.3)
        for s in slow: s.close()
        status=self.call('status')[1]
        self.assertGreaterEqual(status['ipc']['timed_out'],1)
        deadline=time.monotonic()+5
        while status['storage']['committed']<100 and time.monotonic()<deadline:
            time.sleep(.1);status=self.call('status')[1]
        self.assertEqual(status['storage']['committed'],100)
        self.assertEqual(status['storage']['dropped_full'],0)
        self.assertEqual(self.call('latest')[1]['record']['rx_id'],100)
        code,page=self.history(stamp,limit=1000)
        if code==5 and os.environ.get('GATEWAY_TSAN_MODE')=='1':
            self.assertEqual(page['error'],'query_timeout')
            self.assertEqual(self.history(stamp,limit=10)[0],0)
        else:
            self.assertEqual(code,0);self.assertEqual(page['count'],1000);self.assertTrue(page['more'])
        self.stop();self.assertFalse(self.sock.exists())

    def test_stale_file_active_instance_and_socket_ownership(self):
        self.sock.write_text('keep')
        args=[self.binary,'--port',os.ttyname(self.slave),'--duration','.1','--output',str(self.root/'denied'),'--socket',str(self.sock)]
        p=subprocess.run(args,capture_output=True,timeout=5)
        self.assertEqual(p.returncode,1);self.assertEqual(self.sock.read_text(),'keep');self.sock.unlink()
        stale=socket.socket(socket.AF_UNIX);stale.bind(str(self.sock));stale.close()
        self.start();self.assertEqual(self.sock.stat().st_mode&0o777,0o600)
        p=subprocess.run([*args[:-3],str(self.root/'other'),'--socket',str(self.sock)],capture_output=True,timeout=5)
        self.assertEqual(p.returncode,1);self.assertEqual(self.call('status')[0],0)

    def test_empty_db_incomplete_and_idle_timeout(self):
        self.start();self.assertEqual(self.history('2026-11-12T00:00:00Z')[0],4)
        s=self.connection();s.sendall(b'1 sta');s.shutdown(socket.SHUT_WR)
        self.assertEqual(self.line(s)['code'],2)
        idle=self.connection();time.sleep(5.3);self.assertEqual(idle.recv(1),b'')

    def test_client_request_timeout(self):
        fake=socket.socket(socket.AF_UNIX);fake.bind(str(self.sock));fake.listen(1);self.addCleanup(fake.close)
        code,result=self.call('--timeout','.1','status')
        self.assertEqual(code,5);self.assertEqual(result['error'],'timeout')

    def test_disconnected_query_never_reaches_new_client(self):
        stamp=self.seed(200);self.start()
        for _ in range(24):
            s=self.connection();s.sendall(f'1 history {stamp} {stamp} 100 -\n'.encode());s.close()
            code,result=self.call('latest')
            self.assertIn(code,(3,4,7))
            if code==4: self.assertEqual(result['error'],'no_data')
        time.sleep(1.2);self.assertEqual(self.call('status')[0],0)
    def test_busy_queries_expire_without_poisoning_storage(self):
        stamp=self.seed(50);self.start()
        with sqlite3.connect(self.path,timeout=2) as db:
            db.execute('BEGIN EXCLUSIVE')
            started=time.monotonic();code,page=self.history(stamp)
            self.assertIn(code,(5,6));self.assertLess(time.monotonic()-started,3)
            db.rollback()
        self.feed(3);status=self.wait_frames(3)
        self.assertEqual(status['legal_frames'],3)
        self.assertEqual(self.history(stamp)[0],0)

    def test_saturated_clients_and_fragment_deadline(self):
        self.start();clients=[self.connection() for _ in range(8)]
        time.sleep(.2)
        extra=self.connection();extra.sendall(b'1 status\n')
        try: self.assertEqual(extra.recv(1),b'')
        except ConnectionResetError: pass
        self.feed(5);time.sleep(.2)
        for s in clients: s.close()
        time.sleep(.2);status=self.wait_frames(5)
        self.assertGreaterEqual(status['ipc']['rejected'],1)
        fragment=self.connection();fragment.sendall(b'1 stat');time.sleep(5.3)
        self.assertEqual(fragment.recv(1),b'')
    def test_latest_keeps_source_quality_and_receive_age_separate(self):
        self.start();self.feed();self.wait_frames(1)
        first=self.call('latest')[1]
        os.write(self.master,frame(seq=1,quality='STALE',status='NACK',value_ms=1000,status_ms=2000))
        self.wait_frames(2);stale=self.call('latest')[1]
        p=stale['record']['points'][0]
        self.assertEqual(p['seq'],1);self.assertEqual(p['value_ms'],1000)
        self.assertEqual(p['value_kind'],'last_valid');self.assertFalse(p['is_current'])
        self.assertTrue(stale['transport_fresh']);self.assertGreater(stale['record']['rx_id'],first['record']['rx_id'])
        time.sleep(15.4)
        old=self.call('latest')[1];status=self.call('status')[1]
        self.assertFalse(old['transport_fresh']);self.assertGreaterEqual(old['legal_frame_age_s'],15)
        self.assertEqual(status['source_quality'],'STALE');self.assertEqual(status['serial'],'silent')
        self.assertEqual(old['record']['utc'],stale['record']['utc'])
