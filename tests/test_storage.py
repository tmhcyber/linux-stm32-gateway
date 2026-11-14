"""Offline/PTY lifecycle and fields; no physical UART."""
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import tempfile
import time
import unittest

from test_gateway import frame
from process_audit import audit_process, audit_child, stderr_once, ChildRuntimeError


def compare_database(test, db, out, run_id=None, allow_drops=False):
    records = [json.loads(line) for line in (out/'records.jsonl').read_text().splitlines()]
    raw = (out/'raw.bin').read_bytes()
    db.row_factory = sqlite3.Row
    if run_id is None:
        run_id = db.execute('SELECT run_id FROM runs ORDER BY rowid DESC LIMIT 1').fetchone()[0]
    rows = db.execute('SELECT * FROM frames WHERE run_id=? ORDER BY rx_id', (run_id,)).fetchall()
    if not allow_drops:
        test.assertEqual(len(rows), len(records))
    by_id = {r['rx_id']: r for r in records}
    for stored in rows:
        record = by_id[stored['rx_id']]
        for field in ('rx_id', 'utc', 'raw_start', 'raw_end', 'v', 'reason', 'flags', 'gap', 'coal', 'snap', 'n', 'sequence_relation'):
            test.assertEqual(stored[field], record[field], field)
        test.assertAlmostEqual(stored['elapsed_s'], record['elapsed_s'], places=5)
        test.assertEqual(stored['body'], raw[record['raw_start']:record['raw_end']-2])
        points = db.execute('SELECT * FROM points WHERE run_id=? AND rx_id=? ORDER BY point_index',
                            (run_id, record['rx_id'])).fetchall()
        test.assertEqual(len(points), 2)
        for index, (point, expected) in enumerate(zip(points, record['points'])):
            test.assertEqual(point['point_index'], index)
            for field, value in expected.items():
                test.assertEqual(point[field], value, field)
    test.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0], 'ok')
    test.assertEqual(db.execute('PRAGMA foreign_key_check').fetchall(), [])
    if db.execute('PRAGMA user_version').fetchone()[0] >= 2:
        main_events = [json.loads(line) for line in (out/'events.jsonl').read_text().splitlines()]
        events = db.execute('SELECT * FROM events WHERE run_id=? AND event_id>0 ORDER BY event_id', (run_id,)).fetchall()
        if not allow_drops:
            test.assertEqual(len(events),len(main_events))
        for event in events:
            expected = main_events[event['event_id']-1]
            for key in ('utc','kind','detail','raw_offset'):
                test.assertEqual(event[key],expected[key])
            test.assertAlmostEqual(event['elapsed_s'],expected['elapsed_s'],places=5)
        summary = json.loads((out/'summary.json').read_text())
        stats=summary['storage']
        test.assertEqual(summary['valid_records'],stats['submitted']+stats['dropped_full']+stats['rejected'])
        test.assertEqual(stats['submitted'],stats['committed']+stats['failed_records'])
        test.assertEqual(len(main_events),stats['event_submitted']+stats['event_dropped_full']+stats['event_rejected'])
        test.assertEqual(stats['event_submitted'],stats['event_committed']+stats['event_failed'])
        test.assertEqual(len(rows),stats['committed'])
        test.assertEqual(len(events),stats['event_committed'])
        run=db.execute('SELECT * FROM runs WHERE run_id=?',(run_id,)).fetchone()
        for key in ('committed','commits','last_rx_id','event_committed','worker_events_committed'):
            test.assertEqual(run[key],stats[key],key)
        test.assertEqual(stats['queued'],0);test.assertEqual(stats['in_flight'],0)
        test.assertLessEqual(stats['high_water'],256)
    return records


@unittest.skipUnless(os.name == 'posix' and os.environ.get('GATEWAY'), 'Linux make test required')
class StorageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.binary = os.environ['GATEWAY']
        self.path = self.root/'history.sqlite'

    def replay(self, data, name='out', boundary='aligned', expected=0):
        source = self.root/(name+'.bin'); source.write_bytes(data)
        out = self.root/name
        result = subprocess.run([self.binary, '--replay', str(source), '--boundary', boundary,
                                 '--output', str(out), '--db', str(self.path)], capture_output=True, timeout=10)
        record=audit_process(result.args,result.returncode,result.stderr,expected,self.id(),'replay:'+name)
        if record['kind']!='expected_exit':
            raise ChildRuntimeError(record)
        self.assertEqual(result.returncode, expected, result.stderr)
        return out, json.loads((out/'summary.json').read_text())

    def test_replay_all_fields_batch_drain_and_reopen_append(self):
        data = frame(seq=0, quality='NOT_AVAILABLE', status='NACK', value_ms=0,
                     status_ms=20, temperature=0, humidity=0)
        data += frame()+frame(reason='HEARTBEAT', flags='0x00000000')
        data += frame(quality='STALE', status='NACK', status_ms=2000)
        data += frame(quality='OFFLINE', status='TIMEOUT', status_ms=4000)
        data += b''.join(frame(seq=i, value_ms=i*1000) for i in range(2, 20))
        out, summary = self.replay(data)
        self.assertEqual(summary['valid_records'], 23)
        stats = summary['storage']
        expected=dict(capacity=256, submitted=23, dropped_full=0, rejected=0,
                      committed=23, last_rx_id=23, failed_records=0, sqlite_error=0)
        self.assertEqual({key:stats[key] for key in expected},expected)
        self.assertGreaterEqual(stats['commits'],3)
        with sqlite3.connect(self.path) as db:
            compare_database(self, db, out)
            self.assertEqual(db.execute('SELECT DISTINCT session FROM frames').fetchall()[0][0], 0)
            run = dict(db.execute('SELECT * FROM runs').fetchone())
            self.assertEqual(run['committed'],23); self.assertEqual(run['exit_code'],0)
            self.assertIsNotNone(run['ended_utc'])
            self.assertEqual(db.execute('PRAGMA journal_mode').fetchone()[0], 'delete')
        second, _ = self.replay(frame(seq=20), name='second')
        # A fresh process opens the existing DB, appends a unique run and starts rx_id at 1.
        with sqlite3.connect(self.path) as db:
            compare_database(self, db, second)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM runs').fetchone()[0],2)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM frames').fetchone()[0],24)

    def test_stream_boundary_errors_and_tail_keep_stage2_counts(self):
        data = b'fragment\r\n'+frame()+b'invalid\r\n'+frame(seq=2)+b'partial'
        out, summary = self.replay(data, boundary='stream', expected=1)
        self.assertEqual(summary['sync_discards'],1)
        self.assertEqual(summary['discarded_bytes'],10)
        self.assertEqual(summary['pending_bytes'],7)
        self.assertEqual(summary['storage']['committed'],2)
        self.assertEqual(summary['parse_errors']['SYNTAX'],1)
        with sqlite3.connect(self.path) as db:
            compare_database(self, db, out)
            self.assertEqual(db.execute('SELECT exit_code FROM runs').fetchone()[0],1)

    def test_empty_eof_closes_thread_and_database(self):
        out, summary = self.replay(b'', expected=2)
        self.assertGreaterEqual(summary['storage']['commits'],1)  # START/EXIT and STORAGE_READY
        with sqlite3.connect(self.path) as db:
            compare_database(self, db, out)
            self.assertEqual(tuple(db.execute('SELECT exit_code,complete FROM runs').fetchone()),(2,1))

    def start_pty(self):
        import pty
        master, slave = pty.openpty()
        self.addCleanup(os.close,master); self.addCleanup(os.close,slave)
        out = self.root/'pty'
        proc = subprocess.Popen([self.binary,'--port',os.ttyname(slave),'--duration','30',
                                 '--output',str(out),'--db',str(self.path)], stderr=subprocess.PIPE)
        def stop():
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=5)  # Never use kill as a checkpoint exit.
            try:
                audit_child(proc,self.id())
            finally:
                proc.stderr.close()
        self.addCleanup(stop)
        deadline = time.monotonic()+3
        while not (out/'events.jsonl').exists() or '"kind":"OPEN"' not in (out/'events.jsonl').read_text():
            if proc.poll() is not None:
                audit_child(proc,self.id(),phase='startup',startup=True)
            self.assertLess(time.monotonic(),deadline)
            time.sleep(.01)
        return proc, master, out

    def test_monotonic_one_second_commit_visible_before_exit(self):
        proc, master, out = self.start_pty()
        sent_at = time.monotonic()
        os.write(master,b'fragment\r\n'+frame())
        time.sleep(.2)
        with sqlite3.connect(self.path) as db:
            count=db.execute('SELECT COUNT(*) FROM frames').fetchone()[0]
        # A VM can deschedule the observer beyond the entire commit interval.
        # Check actual elapsed time, not the requested sleep duration.
        first_probe_elapsed=time.monotonic()-sent_at
        if first_probe_elapsed<.95:
            self.assertEqual(count,0)
        if count:
            self.assertGreaterEqual(first_probe_elapsed,.95)
        deadline = time.monotonic()+3
        seen = 0
        while time.monotonic()<deadline:
            with sqlite3.connect(self.path) as db:
                seen = db.execute('SELECT COUNT(*) FROM frames').fetchone()[0]
            if seen:
                break
            self.assertIsNone(proc.poll()); time.sleep(.02)
        if seen != 1:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=5)
            self.fail('timer commit missing: '+stderr_once(proc).decode()+
                      (out/'summary.json').read_text())
        self.assertGreaterEqual(time.monotonic()-sent_at,.95)
        self.assertIsNone(proc.poll())  # Timer committed a sub-10 batch while receiver was alive.
        proc.send_signal(signal.SIGTERM)
        self.assertEqual(proc.wait(timeout=5),143,stderr_once(proc))
        summary = json.loads((out/'summary.json').read_text())
        self.assertGreaterEqual(summary['storage']['commits'],1)
        with sqlite3.connect(self.path) as db:
            compare_database(self, db, out)
            self.assertEqual(db.execute('SELECT DISTINCT session FROM frames').fetchone()[0],1)

    def test_basic_sigint_drains_underfull_batch(self):
        proc, master, out = self.start_pty()
        os.write(master,b'\r\n'+frame()+frame(seq=2)+frame(seq=3))
        deadline=time.monotonic()+2
        while len((out/'records.jsonl').read_text().splitlines())<3:
            self.assertLess(time.monotonic(),deadline);time.sleep(.01)
        proc._expected_exit=130;proc.send_signal(signal.SIGINT)
        self.assertEqual(proc.wait(timeout=5),130,stderr_once(proc))
        summary=json.loads((out/'summary.json').read_text())
        self.assertFalse(summary['complete'])
        self.assertEqual(summary['storage']['committed'],3)
        with sqlite3.connect(self.path) as db:
            compare_database(self,db,out)

    def test_refuse_unowned_database_without_modifying_it(self):
        with sqlite3.connect(self.path) as db:
            db.execute('CREATE TABLE precious(value TEXT)')
            db.execute("INSERT INTO precious VALUES('keep')")
        before=self.path.read_bytes()
        _, summary=self.replay(frame(),expected=1)
        self.assertEqual(summary['valid_records'],0)
        self.assertNotEqual(summary['storage']['sqlite_error'],0)
        self.assertEqual(self.path.read_bytes(),before)


if __name__ == '__main__':
    unittest.main()
