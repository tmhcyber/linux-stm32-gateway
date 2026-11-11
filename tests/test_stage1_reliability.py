"""Linux regressions for process outcomes and read-only readiness failures."""
import contextlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/linux/capture_serial.py'
sys.path.insert(0, str(SCRIPT.parent))
import capture_serial


@unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux')
class ProcessTests(unittest.TestCase):
    def run_capture(self, action=None, duration='0.3', port=None):
        import pty
        import termios
        master, slave = pty.openpty()
        try:
            with tempfile.TemporaryDirectory() as temp:
                output = Path(temp) / 'capture'
                process = subprocess.Popen([sys.executable, str(SCRIPT), '--port',
                                            port or os.ttyname(slave), '--duration', duration,
                                            '--output', str(output)],
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    if not port:
                        deadline = time.monotonic() + 5
                        while termios.tcgetattr(slave)[3] & termios.ICANON:
                            if process.poll() is not None or time.monotonic() > deadline:
                                self.fail('capture did not configure PTY')
                            time.sleep(0.01)
                    if action:
                        master = action(process, master)
                    stdout, stderr = process.communicate(timeout=6)
                    summary = json.loads((output / 'summary.json').read_text())
                    self.assertEqual(process.returncode, summary['exit_code'], (stdout, stderr))
                    return summary, (output / 'raw.bin').read_bytes(), (output / 'complete-lines.txt').read_bytes()
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()
        finally:
            if master is not None:
                os.close(master)
            os.close(slave)

    def test_no_input_returns_two(self):
        summary, raw, lines = self.run_capture()
        self.assertEqual((summary['exit_code'], summary['exit_reason'], raw, lines),
                         (2, 'duration_elapsed', b'', b''))

    def test_open_failure_returns_one_with_artifacts(self):
        summary, raw, lines = self.run_capture(port='/nonexistent/stage1-device')
        self.assertEqual((summary['exit_code'], summary['exit_reason']), (1, 'error'))
        self.assertEqual((raw, lines), (b'', b''))

    def test_interrupts_preserve_partial_records_and_exit_code(self):
        for sig in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=sig):
                data = b'partial\r\nv=1;reason=EVENT;\r\n'

                def interrupt(process, master):
                    os.write(master, data)
                    time.sleep(0.15)
                    process.send_signal(sig)
                    return master

                summary, raw, lines = self.run_capture(interrupt, '10')
                self.assertEqual(summary['exit_code'], 128 + sig)
                self.assertEqual(summary['exit_reason'], 'interrupted')
                self.assertEqual(summary['signal'], sig.name)
                self.assertEqual(summary['v1_candidate_lines'], 1)
                self.assertLess(summary['elapsed_s'], 3)
                self.assertEqual(raw, data)
                self.assertEqual(lines, b'v=1;reason=EVENT;\r\n')

    def test_pty_link_end_returns_one(self):
        def disconnect(process, master):
            os.write(master, b'\r\nv=1;\r\n')
            time.sleep(0.15)
            os.close(master)
            return None
        summary, raw, _ = self.run_capture(disconnect, '10')
        self.assertEqual(summary['exit_code'], 1)
        self.assertTrue(summary['link_ended'])
        self.assertIn(b'v=1;', raw)

    def test_simultaneous_pollin_hup_drains_tail(self):
        import pty
        master, slave = pty.openpty()
        try:
            with tempfile.TemporaryDirectory() as temp:
                poller = unittest.mock.Mock()
                poller.poll.return_value = [(slave, capture_serial.select.POLLIN | capture_serial.select.POLLHUP)]
                data = b'\r\nv=1;tail=yes;\r\n'
                args = SimpleNamespace(port=os.ttyname(slave), duration=1, discard_buffered=False,
                                       output=str(Path(temp) / 'capture'))
                with patch.object(capture_serial.select, 'poll', return_value=poller), \
                     patch.object(capture_serial.os, 'read', side_effect=[data[:8], data[8:], BlockingIOError()]), \
                     contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(capture_serial.capture(args), 1)
                self.assertEqual((Path(args.output) / 'raw.bin').read_bytes(), data)
                self.assertEqual((Path(args.output) / 'complete-lines.txt').read_bytes(), data[2:])
        finally:
            os.close(master)
            os.close(slave)

    def test_daily_configuration_needs_no_sudo_and_sets_8n1(self):
        import pty
        import termios
        master, slave = pty.openpty()
        try:
            # An attempted sudo call fails even on the passwordless dedicated VM.
            with tempfile.TemporaryDirectory() as temp:
                sudo = Path(temp) / 'sudo'
                sudo.write_text('#!/bin/sh\nexit 99\n')
                sudo.chmod(0o755)
                subprocess.run(['bash', str(ROOT / 'scripts/linux/configure_serial.sh'), os.ttyname(slave)],
                               env={**os.environ, 'PATH': temp + ':' + os.environ['PATH']},
                               check=True, capture_output=True)
            attrs = termios.tcgetattr(slave)
            self.assertEqual(attrs[4:6], [termios.B115200, termios.B115200])
            self.assertEqual(attrs[2] & termios.CSIZE, termios.CS8)
            self.assertFalse(attrs[2] & (termios.PARENB | termios.CSTOPB | termios.CRTSCTS))
            self.assertFalse(attrs[0] & (termios.IXON | termios.IXOFF | termios.IXANY))
            self.assertFalse(attrs[3] & (termios.ICANON | termios.ECHO | termios.ECHONL |
                                         termios.IEXTEN | termios.ISIG))
        finally:
            os.close(master)
            os.close(slave)


@unittest.skipUnless(sys.platform.startswith('linux'), 'requires Bash')
class EnvironmentTests(unittest.TestCase):
    def check(self, scope, *, device='/dev/null', active='inactive', package=True,
              broker=True, listener='127.0.0.1:1883', ss_ok=True):
        with tempfile.TemporaryDirectory() as temp:
            commands = {
                'systemctl': ('if [ "$1" = is-active ] && [ "$2" = --quiet ]; then exit ' +
                              ('0' if broker else '3') + '; fi\necho ' + active + '\nexit 3'),
                'pkg-config': 'exit ' + ('0' if package else '1'),
                'ss': (f'echo "LISTEN 0 100 {listener} 0.0.0.0:*"' if listener else ':') +
                      '\nexit ' + ('0' if ss_ok else '1'),
                'mosquitto_pub': 'exit 0', 'mosquitto_sub': 'exit 0', 'sqlite3': 'exit 0',
            }
            for name, code in commands.items():
                path = Path(temp) / name
                path.write_text('#!/bin/sh\n' + code + '\n')
                path.chmod(0o755)
            result = subprocess.run(['bash', str(ROOT / 'scripts/linux/check_environment.sh'), scope, device],
                                    env={**os.environ, 'PATH': temp + ':' + os.environ['PATH']},
                                    capture_output=True, text=True)
            return result.returncode, result.stdout + result.stderr

    def test_serial_requirements(self):
        self.assertEqual(self.check('serial')[0], 0)
        for options in ({'device': '/missing/device'}, {'active': 'active'}, {'active': ''}):
            with self.subTest(options=options):
                code, output = self.check('serial', **options)
                self.assertEqual(code, 1)
                self.assertIn('FAIL:', output)

    def test_storage_dependency_failure(self):
        self.assertEqual(self.check('storage')[0], 0)
        self.assertEqual(self.check('storage', package=False)[0], 1)

    def test_mqtt_failures(self):
        self.assertEqual(self.check('mqtt')[0], 0)
        for options in ({'package': False}, {'broker': False}, {'listener': ''},
                        {'listener': '0.0.0.0:1883'}, {'ss_ok': False}):
            with self.subTest(options=options):
                code, output = self.check('mqtt', **options)
                self.assertEqual(code, 1)
                self.assertIn('FAIL:', output)

    def test_explicit_scope_required(self):
        self.assertEqual(self.check('')[0], 2)


if __name__ == '__main__':
    unittest.main()
