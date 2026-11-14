"""Stage 3 fault/signal/restart/retention acceptance, all in private PTYs and DBs."""
from contextlib import closing
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import pty
import signal
import sqlite3
import subprocess
import tempfile
import time
import unittest

from schema_v1 import SCHEMA_V1
from test_gateway import frame
from test_storage import compare_database
from process_audit import audit_file_child


def seed_v1(path, count=130):
    """Historical v1 data; migration must preserve it until retention explicitly removes it."""
    with closing(sqlite3.connect(path)) as db:
        db.executescript(SCHEMA_V1)
        db.execute("INSERT INTO runs(run_id,started_utc,monotonic_start,mode,output_dir) VALUES('old','2000-01-01T00:00:00Z',0,'stream','old-evidence')")
        for i in range(1,count+1):
            db.execute("INSERT INTO frames VALUES('old',?,0,'2000-01-01T00:00:00Z',0,0,2,?,1,'EVENT',22,0,0,4,2,'next')",(i,b'fixture'))
            for point in range(2):
                db.execute("INSERT INTO points VALUES('old',?,?,'SHT30',?,25000,-3,?,'VALID','OK',1000,1000,1,'current',1)",
                           (i,point,'HUMIDITY' if point else 'TEMPERATURE','PERCENT_RH' if point else 'DEG_C'))
        db.commit()


@unittest.skipUnless(os.name=='posix' and os.environ.get('GATEWAY_FAULT'), 'Linux make test required')
class StorageFailureTests(unittest.TestCase):
    def setUp(self):
        if os.environ.get('STAGE3_EVIDENCE'):
            self.root=Path(os.environ['STAGE3_EVIDENCE'])/self._testMethodName
            self.root.mkdir(parents=True)
        else:
            tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup);self.root=Path(tmp.name)
        self.path=self.root/'history.sqlite'
        self.control=self.root/'control.txt';self.control.write_text('')
        self.processes=[]
        self.addCleanup(self.stop_all)

    def stop_all(self):
        errors=[]
        if any(proc.poll() is None for proc,_,_,_ in self.processes):
            # Failed assertions must not leave a slow/failing test-only VFS enabled during cleanup.
            (self.root/'control-before-cleanup.txt').write_bytes(self.control.read_bytes())
            self.configure()
        for proc, master, slave, stderr in self.processes:
            try:
                if proc.poll() is None:
                    proc.send_signal(signal.SIGTERM);proc.wait(timeout=8)
                audit_file_child(proc,stderr.name,self.id())
            except Exception as error:
                errors.append(error)
            finally:
                if master>=0:
                    os.close(master)
                os.close(slave);stderr.close()
        if errors:
            raise ExceptionGroup('child process cleanup/outcome failures',errors)

    def configure(self, **options):
        temporary=self.control.with_suffix('.tmp')
        temporary.write_text(''.join(f'{k}={v}\n' for k,v in options.items()))
        temporary.replace(self.control)

    def wait(self, predicate, timeout=6):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            try:
                value=predicate()
                if value:
                    return value
            except (sqlite3.OperationalError,FileNotFoundError,json.JSONDecodeError):
                pass
            time.sleep(.02)
        self.fail('timed out; artifacts: '+str(self.root))

    def start(self, name='run', fault=False):
        master,slave=pty.openpty();out=self.root/name
        stderr=(self.root/(name+'-stderr.txt')).open('wb')
        env=dict(os.environ,GATEWAY_STORAGE_TEST_CONTROL=str(self.control))
        binary=os.environ['GATEWAY_FAULT' if fault else 'GATEWAY']
        command=[binary,'--port',os.ttyname(slave),'--duration','60',
                 '--output',str(out),'--db',str(self.path)]
        proc=subprocess.Popen(command,stderr=stderr,env=env)
        proc._expected_exit=143
        self.processes.append((proc,master,slave,stderr))
        (self.root/(name+'-command.json')).write_text(json.dumps(command,indent=2)+'\n')
        def ready():
            if proc.poll() is not None:
                audit_file_child(proc,stderr.name,self.id(),startup=True)
            return '"kind":"OPEN"' in (out/'events.jsonl').read_text()
        self.wait(ready)
        self.assertIsNone(proc.poll())
        return proc,master,out

    def count(self, table='frames', run_id=None):
        with closing(sqlite3.connect(self.path,timeout=.1)) as db:
            if run_id is None:
                return db.execute('SELECT COUNT(*) FROM '+table).fetchone()[0]
            return db.execute('SELECT COUNT(*) FROM '+table+' WHERE run_id=?',(run_id,)).fetchone()[0]

    @staticmethod
    def status(out):
        return json.loads((out/'storage-status.json').read_text())

    def received(self, out, number):
        return self.wait(lambda: len((out/'records.jsonl').read_text().splitlines())==number)

    def stop(self, proc, out, sig=signal.SIGTERM, allow_drops=False):
        proc._expected_exit=128+sig
        proc.send_signal(sig);self.assertEqual(proc.wait(timeout=8),128+sig)
        summary=json.loads((out/'summary.json').read_text())
        self.assertFalse(summary['complete'])
        self.assertEqual(summary['storage']['state'],'CLOSED')
        with closing(sqlite3.connect(self.path)) as db:
            compare_database(self,db,out,allow_drops=allow_drops)
        return summary['storage']

    def test_slow_writer_full_queue_continues_receiving(self):
        self.configure(delay_ms=100)
        proc,master,out=self.start(fault=True)
        data=b'fragment\r\n';os.write(master,data);trace=[]
        before=time.monotonic()
        for wave in range(10):
            block=b''.join(frame(seq=i,value_ms=i*1000) for i in range(wave*150+1,(wave+1)*150+1))
            data+=block;os.write(master,block);time.sleep(.2)
            trace.append(self.status(out))
        self.received(out,1500)
        reception=time.monotonic()-before
        self.configure()
        stats=self.stop(proc,out,allow_drops=True)
        # FIFO/counters prove reception continued past saturation without waiting for all writes.
        # Absolute VM wall-time thresholds conflate scheduler pauses with queue backpressure.
        self.assertGreater(stats['dropped_full'],0)
        self.assertEqual(stats['high_water'],256)
        self.assertEqual(stats['failed_records'],0)
        self.assertEqual(stats['failures'],0)
        self.assertEqual((out/'raw.bin').read_bytes(),data)
        (self.root/'measurements.json').write_text(json.dumps(dict(receive_seconds=reception,trace=trace,stats=stats),indent=2)+'\n')

    def test_busy_recovers_without_duplicates_and_resumes_new_frames(self):
        proc,master,out=self.start()
        with closing(sqlite3.connect(self.path,timeout=3,isolation_level=None)) as lock:
            lock.execute('BEGIN IMMEDIATE')
            os.write(master,b'\r\n'+b''.join(frame(seq=i) for i in range(1,24)))
            self.received(out,23)
            self.wait(lambda: self.status(out)['state']=='DEGRADED')
            lock.execute('ROLLBACK')
        self.wait(lambda: self.status(out)['recoveries']>0)
        self.wait(lambda: self.count()==23)
        os.write(master,frame(seq=24));self.received(out,24)
        stats=self.stop(proc,out)
        self.assertGreater(stats['busy_retries'],0);self.assertGreater(stats['recoveries'],0)
        self.assertEqual(stats['sqlite_error'],0);self.assertEqual(stats['committed'],24)
        self.assertEqual(stats['last_sqlite_error']&255,sqlite3.SQLITE_BUSY)
        diagnostics=(out/'storage-events.jsonl').read_text()
        self.assertIn('STORAGE_DEGRADED',diagnostics);self.assertIn('STORAGE_RECOVERED',diagnostics)

    def test_persistent_busy_shutdown_is_bounded_and_counts_failed_batch(self):
        proc,master,out=self.start()
        with closing(sqlite3.connect(self.path,timeout=3,isolation_level=None)) as lock:
            lock.execute('BEGIN IMMEDIATE')
            os.write(master,b'\r\n'+b''.join(frame(seq=i) for i in range(1,24)))
            self.received(out,23);self.wait(lambda:self.status(out)['state']=='DEGRADED')
            before=time.monotonic();proc.send_signal(signal.SIGTERM)
            self.assertEqual(proc.wait(timeout=6),143)
            self.assertLess(time.monotonic()-before,5)
            lock.execute('ROLLBACK')
        summary=json.loads((out/'summary.json').read_text())
        self.assertGreater(summary['storage']['failed_records'],0)
        with closing(sqlite3.connect(self.path)) as db:
            compare_database(self,db,out,allow_drops=True)
            self.assertIsNone(db.execute('SELECT ended_utc FROM runs WHERE run_id=?',
                                        (summary['storage']['run_id'],)).fetchone()[0])
        # Restart can append after an incomplete run; existing committed data remains readable.
        proc2,master2,out2=self.start('restart')
        os.write(master2,b'\r\n'+frame(seq=50));self.received(out2,1)
        self.stop(proc2,out2)

    def test_sqlite_full_from_actual_page_limit_recovers(self):
        proc,master,out=self.start(fault=True)
        self.configure(max_pages=1)
        os.write(master,b'\r\n'+b''.join(frame(seq=i) for i in range(1,81)))
        self.received(out,80)
        self.wait(lambda:self.status(out)['last_sqlite_error']&255==sqlite3.SQLITE_FULL)
        self.configure(max_pages=100000)
        self.wait(lambda:self.count()==80)
        self.wait(lambda:self.status(out)['recoveries']>0)
        stats=self.stop(proc,out)
        self.assertEqual(stats['last_sqlite_error']&255,sqlite3.SQLITE_FULL)
        self.assertEqual(stats['sqlite_error'],0)
        self.assertEqual(stats['failed_records'],0)

    def test_vfs_write_ioerror_recovers(self):
        proc,master,out=self.start(fault=True)
        self.configure(write_error=sqlite3.SQLITE_IOERR_WRITE)
        os.write(master,b'\r\n'+frame()+frame(seq=2)+frame(seq=3))
        self.received(out,3)
        self.wait(lambda:self.status(out)['last_sqlite_error']&255==sqlite3.SQLITE_IOERR)
        self.configure()
        self.wait(lambda:self.count()==3)
        self.wait(lambda:self.status(out)['recoveries']>0)
        stats=self.stop(proc,out)
        self.assertEqual(stats['sqlite_error'],0)
        self.assertEqual(stats['last_sqlite_error']&255,sqlite3.SQLITE_IOERR)

    def test_int_and_term_drain_backlog(self):
        for sig in (signal.SIGINT,signal.SIGTERM):
            with self.subTest(signal=sig):
                self.configure(delay_ms=100)
                proc,master,out=self.start(str(sig),fault=True)
                os.write(master,b'\r\n'+b''.join(frame(seq=i) for i in range(1,201)))
                self.received(out,200)
                stats=self.stop(proc,out,sig=sig)
                self.assertEqual(stats['committed'],200);self.assertEqual(stats['dropped_full'],0)

    def test_signals_without_input_close_worker(self):
        for sig in (signal.SIGINT,signal.SIGTERM):
            with self.subTest(signal=sig):
                proc,_,out=self.start(str(sig))
                stats=self.stop(proc,out,sig=sig)
                self.assertEqual(stats['committed'],0)
                self.assertGreaterEqual(stats['event_committed'],3)

    def test_signal_masks_route_termination_only_to_main(self):
        proc,_,out=self.start()
        bits=(1<<(signal.SIGINT-1))|(1<<(signal.SIGTERM-1))
        tasks=list((Path('/proc')/str(proc.pid)/'task').iterdir())
        if not os.environ.get('GATEWAY_TSAN_MODE'):
            self.assertEqual(len(tasks),2)
        masks={};workers=[]
        for task in tasks:
            fields=dict(line.split(':',1) for line in (task/'status').read_text().splitlines() if ':' in line)
            mask=int(fields['SigBlk'].strip(),16);masks[task.name]=mask
            name=(task/'comm').read_text().strip()
            if int(task.name)==proc.pid:
                self.assertEqual(mask&bits,0)
            elif name=='gw-storage':
                workers.append(task.name);self.assertEqual(mask&bits,bits)
        self.assertEqual(len(workers),1,'identify exactly one application storage worker, regardless of TSan helpers')
        (self.root/'signal-masks.json').write_text(json.dumps(masks,indent=2)+'\n')
        self.stop(proc,out)

    def test_parse_overlong_and_disconnect_events_round_trip(self):
        proc,master,out=self.start()
        data=b'fragment\r\n'+frame()+b'invalid\r\n'+b'X'*510+b'\r\n'+frame(seq=2)
        os.write(master,data);self.received(out,2)
        self.wait(lambda:'OVERLONG' in (out/'events.jsonl').read_text())
        os.close(master)
        self.processes=[(p,-1 if m==master else m,s,e) for p,m,s,e in self.processes]
        self.wait(lambda:'LINK_POLL_FLAGS' in (out/'events.jsonl').read_text())
        stats=self.stop(proc,out)
        self.assertEqual(stats['committed'],2)
        self.assertEqual((out/'raw.bin').read_bytes(),data)

    def test_silent_and_resumed_events_persist(self):
        proc,master,out=self.start()
        os.write(master,b'\r\n')
        deadline=time.monotonic()+30
        while '"kind":"SILENT"' not in (out/'events.jsonl').read_text():
            self.assertLess(time.monotonic(),deadline);self.assertIsNone(proc.poll())
            os.write(master,b'invalid\r\n');time.sleep(.2)
        os.write(master,frame());self.received(out,1)
        self.wait(lambda:'TELEMETRY_RESUMED' in (out/'events.jsonl').read_text())
        self.stop(proc,out)

    def test_sigkill_preserves_commits_and_restart_reads_them(self):
        proc,master,out=self.start()
        os.write(master,b'\r\n'+b''.join(frame(seq=i) for i in range(1,11)))
        self.received(out,10);self.wait(lambda:self.count()==10)
        os.write(master,frame(seq=11)+frame(seq=12)+frame(seq=13))
        self.received(out,13);self.assertEqual(self.count(),10)
        # Intentional abnormal-exit test on this private child, never a normal close substitute.
        proc._expected_exit=-signal.SIGKILL
        proc.send_signal(signal.SIGKILL);self.assertEqual(proc.wait(timeout=3),-signal.SIGKILL)
        self.assertFalse((out/'summary.json').exists())
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
            old_run=db.execute('SELECT run_id,committed,last_rx_id,ended_utc FROM runs').fetchone()
            self.assertEqual(old_run[1:3],(10,10));self.assertIsNone(old_run[3])
            self.assertEqual(db.execute('SELECT COUNT(*) FROM points').fetchone()[0],20)
        proc2,master2,out2=self.start('restart')
        os.write(master2,b'\r\n'+frame(seq=20));self.received(out2,1);self.stop(proc2,out2)
        self.assertEqual(self.count(run_id=old_run[0]),10)
        self.assertEqual(self.count(),11)

    def test_migrate_v1_preserves_recent_history(self):
        seed_v1(self.path,count=1)
        recent=datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
        with closing(sqlite3.connect(self.path)) as db:
            db.execute('UPDATE frames SET utc=?',(recent,));db.commit()
        proc,master,out=self.start()
        os.write(master,b'\r\n'+frame(seq=4));self.received(out,1);self.stop(proc,out)
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0],3)
            self.assertEqual(db.execute("SELECT body FROM frames WHERE run_id='old'").fetchone()[0],b'fixture')
            self.assertEqual(db.execute("SELECT COUNT(*) FROM points WHERE run_id='old'").fetchone()[0],2)

    def test_sigkill_inside_sqlite_transaction_rolls_back(self):
        proc,master,out=self.start(fault=True)
        os.write(master,b'\r\n'+b''.join(frame(seq=i) for i in range(1,11)))
        self.received(out,10);self.wait(lambda:self.count()==10)
        self.configure(pause_sync_ms=1500)
        os.write(master,b''.join(frame(seq=i) for i in range(11,31)))
        self.wait(lambda:self.control.with_suffix('.txt.sync').exists())
        proc._expected_exit=-signal.SIGKILL
        proc.send_signal(signal.SIGKILL);self.assertEqual(proc.wait(timeout=3),-signal.SIGKILL)
        self.assertFalse((out/'summary.json').exists())
        # A fresh SQLite connection performs hot-journal recovery if required.
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
            self.assertEqual(db.execute('SELECT count(*) FROM frames').fetchone()[0],10)
            self.assertEqual(db.execute('SELECT committed FROM runs').fetchone()[0],10)
            self.assertEqual(db.execute('SELECT count(*) FROM points').fetchone()[0],20)
        self.configure()
        proc2,master2,out2=self.start('restart')
        os.write(master2,b'\r\n'+frame(seq=40));self.received(out2,1);self.stop(proc2,out2)
        self.assertEqual(self.count(),11)

    def test_retention_batched_startup_and_monotonic_periodic_schedule(self):
        seed_v1(self.path,count=130)
        self.configure(retention_ms=50)
        proc,_,out=self.start(fault=True)
        self.wait(lambda:self.count(run_id='old')==0)
        # Add more expired data after initial cleanup; the next monotonic tick must remove it.
        with closing(sqlite3.connect(self.path,timeout=3)) as db:
            db.execute("INSERT INTO runs(run_id,started_utc,monotonic_start,mode,output_dir) VALUES('later','2000-01-01T00:00:00Z',0,'stream','fixture')")
            db.execute("INSERT INTO events VALUES('later',1,'2000-01-01T00:00:00Z',0,0,0,'OLD',0)")
            db.commit()
        self.wait(lambda:self.count('events',run_id='later')==0)
        stats=self.stop(proc,out)
        self.assertEqual(stats['retention_frames'],130)
        self.assertEqual(stats['retention_points'],260)
        self.assertGreaterEqual(stats['retention_events'],1)
        self.assertGreaterEqual(stats['retention_runs'],2)
        self.assertGreaterEqual(stats['retention_batches'],3)


if __name__=='__main__':
    unittest.main()
