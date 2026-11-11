"""Framing regression tests and a Linux-only PTY capture smoke test."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/linux/capture_serial.py'
spec = importlib.util.spec_from_file_location('capture_serial', SCRIPT)
capture_serial = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = capture_serial
spec.loader.exec_module(capture_serial)


class FramingTests(unittest.TestCase):
    def test_startup_partial_discard_and_split_delimiter(self):
        framer = capture_serial.LineFramer()
        self.assertEqual(framer.feed(b'partial\r'), [])
        self.assertEqual(framer.feed(b'\nv=1;\r'), [])
        self.assertEqual(framer.feed(b'\n'), [b'v=1;'])
        self.assertEqual(framer.startup_discards, 1)

    def test_multiple_lines_in_one_read(self):
        framer = capture_serial.LineFramer()
        self.assertEqual(framer.feed(b'\r\nA\r\nB\r\n'), [b'A', b'B'])

    def test_exact_boundary_then_overlong_resynchronization(self):
        framer = capture_serial.LineFramer(max_payload=5)
        self.assertEqual(framer.feed(b'\r\n12345\r\n'), [b'12345'])
        self.assertEqual(framer.feed(b'123456tail\r'), [])
        self.assertEqual(framer.feed(b'\nOK\r\n'), [b'OK'])
        self.assertEqual((framer.overlong_lines, framer.resyncs), (1, 1))

    def test_unterminated_input_has_bounded_buffer(self):
        framer = capture_serial.LineFramer()
        framer.feed(b'\r\n' + b'x' * 100_000)
        self.assertLessEqual(len(framer.buffer), 510)
        self.assertEqual(framer.state, 'discarding')

    @unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux PTY and termios')
    def test_capture_from_pty_saves_original_bytes(self):
        import pty
        master, slave = pty.openpty()
        port = os.ttyname(slave)
        try:
            with tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'capture'
                process = subprocess.Popen(
                    [sys.executable, str(SCRIPT), '--port', port,
                     '--duration', '1.5', '--output', str(output)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    time.sleep(0.3)
                    data = b'partial\r\nv=1;reason=EVENT;\r\n'
                    os.write(master, data)
                    stdout, stderr = process.communicate(timeout=6)
                    self.assertEqual(process.returncode, 0, (stdout, stderr))
                    self.assertEqual((output / 'raw.bin').read_bytes(), data)
                    summary = json.loads((output / 'summary.json').read_text())
                    self.assertEqual(summary['v1_candidate_lines'], 1)
                    self.assertIn('semantics not checked', summary['protocol_validation'])
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
        finally:
            os.close(master)
            os.close(slave)


if __name__ == '__main__':
    unittest.main()
