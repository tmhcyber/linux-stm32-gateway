"""Capture a Linux serial byte stream without sending application data.

This stage-1 tool records raw bytes and bounded CRLF lines. It does not perform
the existing protocol's complete semantic validation or implement MQTT.
"""

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import select
import signal
import sys
import time


@dataclass
class LineFramer:
    max_payload: int = 509
    state: str = 'seeking'
    buffer: bytearray = field(default_factory=bytearray)
    previous_cr: bool = False
    startup_discards: int = 0
    overlong_lines: int = 0
    resyncs: int = 0

    def feed(self, chunk):
        lines = []
        for byte in chunk:
            if self.state != 'synced':
                if self.previous_cr and byte == 10:
                    if self.state == 'seeking':
                        self.startup_discards += 1
                    else:
                        self.resyncs += 1
                    self.state = 'synced'
                    self.previous_cr = False
                else:
                    self.previous_cr = byte == 13
                continue
            self.buffer.append(byte)
            if self.buffer.endswith(b'\r\n'):
                lines.append(bytes(self.buffer[:-2]))
                self.buffer.clear()
            elif len(self.buffer) - int(byte == 13) > self.max_payload:
                self.overlong_lines += 1
                self.buffer.clear()
                self.state = 'discarding'
                self.previous_cr = byte == 13
        return lines


def positive_seconds(value):
    result = float(value)
    if not 0 < result < float('inf'):
        raise argparse.ArgumentTypeError('duration must be finite and positive')
    return result


def capture(args):
    import termios
    import fcntl

    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    # Keep the artifact contract even when opening the serial port fails.
    (output / 'raw.bin').touch(exist_ok=False)
    (output / 'complete-lines.txt').touch(exist_ok=False)
    started = time.monotonic()
    summary = {
        'started_at_utc': datetime.now(timezone.utc).isoformat(),
        'port': args.port, 'resolved_port': os.path.realpath(args.port),
        'baud': 115200, 'format': '8N1', 'flow_control': 'none',
        'requested_duration_s': args.duration,
        'input_flushed_before_capture': args.discard_buffered,
        'host': os.uname().sysname, 'architecture': os.uname().machine,
        'bytes_received': 0, 'complete_lines': 0,
        'v1_candidate_lines': 0, 'non_ascii_lines': 0,
        'protocol_validation': 'framing and v1 prefix only; semantics not checked',
        'exit_reason': 'duration_elapsed',
        'signal': None, 'exit_code': None, 'link_ended': False,
    }
    framer = LineFramer()
    descriptor = None
    saved_attributes = None
    failure = None
    received_signal = None

    def stop(signum, _frame):
        nonlocal received_signal
        if received_signal is None:
            received_signal = signum

    saved_handlers = {sig: signal.signal(sig, stop)
                      for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        descriptor = os.open(args.port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
        fcntl.ioctl(descriptor, termios.TIOCEXCL)
        saved_attributes = termios.tcgetattr(descriptor)
        attributes = termios.tcgetattr(descriptor)
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] = termios.CLOCAL | termios.CREAD | termios.CS8
        attributes[3] = 0
        attributes[4] = termios.B115200
        attributes[5] = termios.B115200
        attributes[6][termios.VMIN] = 0
        attributes[6][termios.VTIME] = 0
        termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
        if args.discard_buffered:
            termios.tcflush(descriptor, termios.TCIFLUSH)
        poller = select.poll()
        poller.register(descriptor, select.POLLIN | select.POLLHUP | select.POLLERR)
        deadline = time.monotonic() + args.duration
        with (output / 'raw.bin').open('wb') as raw, (output / 'complete-lines.txt').open('wb') as complete:
            while received_signal is None and time.monotonic() < deadline:
                remaining_ms = max(0, int((deadline - time.monotonic()) * 1000))
                for _, events in poller.poll(min(100, remaining_ms)):
                    ended = bool(events & (select.POLLERR | select.POLLHUP | select.POLLNVAL))
                    # HUP can accompany POLLIN. Drain available tail bytes first,
                    # bounded to 256 KiB so a faulty endpoint cannot trap us.
                    for _ in range(64 if ended else 1):
                        try:
                            chunk = os.read(descriptor, 4096)
                        except BlockingIOError:
                            break
                        except OSError:
                            if ended:
                                break
                            raise
                        if not chunk:
                            break
                        raw.write(chunk)
                        summary['bytes_received'] += len(chunk)
                        for line in framer.feed(chunk):
                            complete.write(line + b'\r\n')
                            summary['complete_lines'] += 1
                            if not line.isascii():
                                summary['non_ascii_lines'] += 1
                            elif line.startswith(b'v=1;') and line.endswith(b';'):
                                summary['v1_candidate_lines'] += 1
                    if ended:
                        summary['link_ended'] = True
                        raise OSError('serial device disconnected or reported an error')
    except KeyboardInterrupt:
        received_signal = signal.SIGINT
    except (OSError, termios.error) as exc:
        summary['exit_reason'] = 'error'
        summary['error'] = str(exc)
        failure = exc
    finally:
        for sig, handler in saved_handlers.items():
            signal.signal(sig, handler)
        if descriptor is not None:
            if saved_attributes is not None:
                try:
                    termios.tcsetattr(descriptor, termios.TCSANOW, saved_attributes)
                except (OSError, termios.error):
                    pass
            try:
                fcntl.ioctl(descriptor, termios.TIOCNXCL)
            except OSError:
                pass
            os.close(descriptor)
        summary.update({
            'finished_at_utc': datetime.now(timezone.utc).isoformat(),
            'elapsed_s': round(time.monotonic() - started, 3),
            'startup_discards': framer.startup_discards,
            'overlong_lines': framer.overlong_lines,
            'resyncs': framer.resyncs,
        })
        if received_signal is not None:
            summary['exit_reason'] = 'interrupted'
            summary['signal'] = signal.Signals(received_signal).name
            exit_code = 128 + received_signal
        elif failure:
            exit_code = 1
        else:
            exit_code = 0 if summary['v1_candidate_lines'] else 2
        summary['exit_code'] = exit_code
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(summary, indent=2))
    return exit_code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--duration', type=positive_seconds, default=30)
    parser.add_argument('--discard-buffered', action='store_true',
                        help='discard pre-existing tty input before the measured window; recorded in summary')
    parser.add_argument('--output', required=True,
                        help='new output directory; existing captures are never overwritten')
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('run this tool inside Ubuntu Linux')
    try:
        return capture(args)
    except OSError as exc:
        print(f'capture failed: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
