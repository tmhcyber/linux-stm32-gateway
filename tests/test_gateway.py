"""Linux integration tests; no physical serial port is touched."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


def frame(seq=1, quality='VALID', status='OK', value_ms=1000, status_ms=None,
          temperature=25000, humidity=50000, **header):
    if status_ms is None:
        status_ms = value_ms
    values = dict(v='1', reason='EVENT', flags='0x00000016', gap='0', coal='0', snap='4', n='2')
    values.update({k: str(v) for k, v in header.items()})
    for i, (name, unit, value) in enumerate((('TEMPERATURE', 'DEG_C', temperature),
                                           ('HUMIDITY', 'PERCENT_RH', humidity))):
        for k, v in dict(src='SHT30', id=name, value=value, scale=-3, unit=unit,
                         quality=quality, status=status, value_ms=value_ms,
                         status_ms=status_ms, seq=seq).items():
            values[f'p{i}.{k}'] = str(v)
    return ''.join(f'{k}={v};' for k, v in values.items()).encode() + b'\r\n'


@unittest.skipUnless(os.name == 'posix' and os.environ.get('GATEWAY'), 'Linux make test required')
class GatewayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.binary = os.environ['GATEWAY']

    def tearDown(self):
        self.tmp.cleanup()

    def read(self, name='out'):
        out = self.root / name
        return json.loads((out/'summary.json').read_text()), [json.loads(s) for s in (out/'records.jsonl').read_text().splitlines()]

    def replay(self, data, boundary='aligned', name='out', expected=0):
        source = self.root/(name+'.bin')
        source.write_bytes(data)
        run = subprocess.run([self.binary, '--replay', str(source), '--boundary', boundary,
                              '--output', str(self.root/name)], capture_output=True)
        self.assertEqual(run.returncode, expected, run.stderr)
        self.assertEqual((self.root/name/'raw.bin').read_bytes(), data)
        return self.read(name)

    def serial(self, duration=1, name='out', path=None):
        import pty
        if path is None:
            master, slave = pty.openpty()
            self.addCleanup(os.close, slave)
            self.addCleanup(lambda: os.close(master) if master in self.live else None)
            self.live.add(master)
            path = os.ttyname(slave)
        else:
            master = None
        proc = subprocess.Popen([self.binary, '--port', path, '--duration', str(duration),
                                 '--output', str(self.root/name)])
        self.addCleanup(lambda: proc.kill() if proc.poll() is None else None)
        deadline = time.monotonic()+3
        while not (self.root/name/'events.jsonl').exists() or 'OPEN' not in (self.root/name/'events.jsonl').read_text():
            if proc.poll() is not None or time.monotonic()>deadline:
                break
            time.sleep(.01)
        return proc, master

    live = set()

    def test_firmware_states_and_last_value_semantics(self):
        stream = frame(seq=0, quality='NOT_AVAILABLE', status='NOT_RUN', value_ms=0, temperature=0, humidity=0, flags='0x00000001')
        for status in ('INVALID_ARGUMENT', 'NACK', 'TIMEOUT', 'BUSY', 'BUS_ERROR', 'TEMPERATURE_CRC_ERROR', 'HUMIDITY_CRC_ERROR'):
            stream += frame(seq=0, quality='NOT_AVAILABLE', status=status, value_ms=0,
                            status_ms=215, temperature=0, humidity=0, flags='0x00000012')
        stream += frame(seq=0, quality='OFFLINE', status='NACK', value_ms=0, status_ms=3000, temperature=0, humidity=0)
        stream += frame()+frame(quality='STALE', status='NACK', status_ms=2000)
        stream += frame(quality='OFFLINE', status='TIMEOUT', status_ms=4000)
        stream += frame(seq=2, value_ms=5000, flags='0x0000005E')
        s, r = self.replay(stream)
        self.assertEqual(s['valid_records'], 13)
        self.assertTrue(all(p['value_kind']=='not_available' for row in r[:9] for p in row['points']))
        self.assertEqual(r[-2]['points'][0]['value_kind'], 'last_valid')
        self.assertTrue(r[-1]['points'][0]['is_current'])

    def test_strict_rejections_recover(self):
        good = frame()
        cases = {
            'NON_ASCII': good.replace(b'EVENT', b'EV\xffNT'),
            'SYNTAX': good.replace(b'v=1;', b'v=1;;'),
            'DUPLICATE_FIELD': good.replace(b'v=1;', b'v=1;v=1;'),
            'FAMILY': good.replace(b'v=1;', b'x=1;'),
            'UNKNOWN_FIELD': good.replace(b'reason=EVENT;', b'other=EVENT;'),
            'MISSING_FIELD': good.replace(b'v=1;', b''),
            'INTEGER_FORMAT': good.replace(b'gap=0;', b'gap=01;'),
            'VALUE_RANGE': good.replace(b'gap=0;', b'gap=4294967296;'),
            'ENUM': good.replace(b'quality=VALID', b'quality=WRONG'),
            'VERSION': good.replace(b'v=1;', b'v=2;'),
            'IDENTITY': good.replace(b'unit=DEG_C;', b'unit=PERCENT_RH;'),
            'HEADER': good.replace(b'snap=4;', b'snap=3;'),
            'QUALITY': good.replace(b'status=OK;', b'status=NOT_RUN;'),
            'BATCH': good.replace(b'p1.seq=1;', b'p1.seq=2;'),
        }
        for index, (error, data) in enumerate(cases.items()):
            with self.subTest(error=error):
                s, r = self.replay(data+good, name=str(index), expected=1)
                self.assertEqual(s['parse_errors'][error], 1)
                self.assertEqual(s['valid_records'], 1)

    def test_integer_and_semantic_edges(self):
        bad = [b'-0', b'+1', b' 1', b'1 ', b'1.0', b'01', b'', b'99999999999999999999999999999999999999999']
        for i, value in enumerate(bad):
            s, _ = self.replay(frame().replace(b'p0.value=25000;', b'p0.value='+value+b';'), name=str(i), expected=1)
            self.assertEqual(sum(s['parse_errors'].values()), 1)
        self.replay(frame(temperature=-45000, humidity=0)+frame(temperature=130000, humidity=100000), name='ends')
        for i, data in enumerate((frame(temperature=-45001),frame(humidity=100001),frame(seq=0),
                  frame(quality='NOT_AVAILABLE'),frame(quality='STALE',status='NOT_RUN'),
                  frame(reason='HEARTBEAT'),frame(flags='0x00000100'),frame(flags='0x0000001a'),
                  frame(flags='0x00000000'),frame(reason='HEARTBEAT',flags='0x00000000',gap=1))):
            self.replay(data, name='bad'+str(i), expected=1)
        self.replay(frame(reason='HEARTBEAT',flags='0x00000000',coal=99), name='heartbeat')

    def test_sync_overlong_raw_and_sequence(self):
        data = b'fragment\r\n'+frame(seq=4294967295, value_ms=4294967290)+frame(seq=1,value_ms=10)+frame(seq=1,value_ms=10)+frame(seq=5,value_ms=1010)+frame(seq=2,value_ms=2010)
        s, r = self.replay(data, 'stream')
        self.assertEqual(s['sync_discards'], 1)
        self.assertEqual(s['discarded_bytes'], 10)
        self.assertEqual(s['sequence'], dict(repeat=1,next=0,jump=1,wrap=1,backward=1,millis_wrap_candidates=1))
        self.assertEqual([x['sequence_relation'] for x in r], ['first','wrap','repeat','jump','backward'])
        s, _ = self.replay(b'X'*509+b'\r\n'+b'X'*510+b'\r\n'+frame(), name='limit', expected=1)
        self.assertEqual(s['overlong'], 1)
        self.assertEqual(s['parse_errors']['SYNTAX'], 1)
        self.assertEqual(s['discarded_bytes'], 512)
        for row in r:
            self.assertEqual(data[row['raw_start']:row['raw_end']][-2:], b'\r\n')

    def test_pty_byte_segmentation_multi_frame_and_settings(self):
        import termios
        proc, master = self.serial(2)
        attrs = termios.tcgetattr(master)
        self.assertEqual(attrs[4],termios.B115200)
        self.assertFalse(attrs[3] & (termios.ICANON|termios.ECHO|termios.ISIG|termios.IEXTEN))
        self.assertFalse(attrs[0] & (termios.IXON|termios.IXOFF|termios.IXANY))
        self.assertFalse(attrs[2] & (termios.PARENB|termios.CSTOPB|termios.CRTSCTS))
        self.assertEqual(attrs[2] & termios.CSIZE,termios.CS8)
        os.write(master,b'truncated\r')
        for b in b'\n'+frame():
            os.write(master,bytes([b]))
        os.write(master, frame(seq=2)+frame(seq=3))
        self.assertEqual(proc.wait(timeout=5),0)
        s, r = self.read()
        self.assertEqual(len(r),3)
        self.assertEqual(s['sync_discards'],1)
        self.assertTrue(s['complete'])

    def test_signal_preserves_partial_window(self):
        for sig in (signal.SIGINT,signal.SIGTERM):
            proc, master = self.serial(20,name=str(sig))
            os.write(master,b'fragment\r\n'+frame()+b'unfinished')
            time.sleep(.1); proc.send_signal(sig)
            self.assertEqual(proc.wait(timeout=3),128+sig)
            s, r = self.read(str(sig))
            self.assertFalse(s['complete']); self.assertEqual(s['signal'],sig)
            self.assertEqual(s['pending_bytes'],10); self.assertEqual(len(r),1)

    def test_hup_and_open_failure_summary_retry(self):
        proc, master = self.serial(2)
        os.write(master,b'\r\n'+frame()); time.sleep(.1)
        os.close(master); self.live.remove(master)
        self.assertEqual(proc.wait(timeout=5),1)
        s, r = self.read(); self.assertGreaterEqual(s['link_errors'],1)
        self.assertEqual(len(r),1)
        missing = self.root/'missing'
        run = subprocess.Popen([self.binary,'--port',str(self.root/'absent'),'--duration','60','--output',str(missing)])
        self.addCleanup(lambda: run.send_signal(signal.SIGTERM) if run.poll() is None else None)
        deadline=time.monotonic()+30
        while not (missing/'events.jsonl').exists() or (missing/'events.jsonl').read_text().count('"kind":"RETRY_SECONDS"')<3:
            self.assertIsNone(run.poll());self.assertLess(time.monotonic(),deadline);time.sleep(.02)
        run.send_signal(signal.SIGTERM);self.assertEqual(run.wait(timeout=5),143)
        s, _ = self.read('missing'); self.assertGreaterEqual(s['open_errors'],3)
        delays = [x['detail'] for x in map(json.loads,(missing/'events.jsonl').read_text().splitlines()) if x['kind']=='RETRY_SECONDS']
        self.assertEqual(delays[:3],[1,2,4]);self.assertEqual(len(delays),s['open_errors'])

    def test_pty_reconnect_resync_and_restore(self):
        import pty
        import termios
        master1, slave1 = pty.openpty()
        master2, slave2 = pty.openpty()
        self.addCleanup(os.close,slave1); self.addCleanup(os.close,slave2)
        self.addCleanup(os.close,master2)
        original=termios.tcgetattr(slave2)
        port=self.root/'port'; port.symlink_to(os.ttyname(slave1))
        proc, _ = self.serial(3.3,path=str(port))
        os.write(master1,b'fragment\r\n'+frame()+b'old-partial')
        time.sleep(.1)
        os.close(master1)
        port.unlink(); port.symlink_to(os.ttyname(slave2))
        deadline=time.monotonic()+2
        while (self.root/'out/events.jsonl').read_text().count('"kind":"OPEN"')<2:
            self.assertLess(time.monotonic(),deadline)
            time.sleep(.02)
        os.write(master2,b'new-fragment\r\n'+frame(seq=2))
        self.assertEqual(proc.wait(timeout=5),1)  # Historical link error remains visible.
        summary, records=self.read()
        self.assertEqual(summary['opens'],2)
        self.assertEqual(summary['sync_discards'],2)
        self.assertEqual(summary['abandoned_bytes'],11)
        self.assertEqual(len(records),2)
        self.assertEqual(termios.tcgetattr(slave2),original)

    def test_silent_recovers_only_on_legal_telemetry(self):
        proc, master=self.serial(40)
        os.write(master,b'\r\n')
        deadline=time.monotonic()+30
        events_path=self.root/'out/events.jsonl'
        while '"kind":"SILENT"' not in events_path.read_text():
            self.assertIsNone(proc.poll())
            self.assertLess(time.monotonic(),deadline)
            os.write(master,b'invalid\r\n'); time.sleep(.2)
        os.write(master,frame())
        while '"kind":"TELEMETRY_RESUMED"' not in events_path.read_text():
            self.assertLess(time.monotonic(),deadline);time.sleep(.01)
        proc.send_signal(signal.SIGTERM)
        self.assertEqual(proc.wait(timeout=5),143)
        summary, records=self.read()
        self.assertEqual(summary['silent_count'],1)
        self.assertFalse(summary['silent'])
        self.assertEqual(len(records),1)
        events=[json.loads(x)['kind'] for x in (self.root/'out/events.jsonl').read_text().splitlines()]
        self.assertLess(events.index('SILENT'),events.index('TELEMETRY_RESUMED'))

    def test_invalid_data_does_not_refresh_silent_deadline(self):
        proc, master = self.serial(40)
        os.write(master,b'\r\n')
        deadline=time.monotonic()+30
        while '"kind":"SILENT"' not in (self.root/'out/events.jsonl').read_text():
            self.assertIsNone(proc.poll());self.assertLess(time.monotonic(),deadline)
            os.write(master,b'invalid\r\n'); time.sleep(.2)
        proc.send_signal(signal.SIGTERM);self.assertEqual(proc.wait(timeout=5),143)
        s, _ = self.read(); self.assertEqual(s['silent_count'],1)
        self.assertTrue(s['silent'])

    def test_empty_partial_and_no_overwrite(self):
        s, _ = self.replay(b'unfinished',expected=2)
        self.assertEqual(s['pending_bytes'],10)
        existing = (self.root/'out'/'summary.json').read_bytes()
        run = subprocess.run([self.binary,'--replay',str(self.root/'out.bin'),'--boundary','aligned','--output',str(self.root/'out')],capture_output=True)
        self.assertEqual(run.returncode,1)
        self.assertEqual((self.root/'out'/'summary.json').read_bytes(),existing)


if __name__=='__main__':
    unittest.main()
