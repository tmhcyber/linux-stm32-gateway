"""Build verified Windows snapshot, run C receiver, attach build provenance.

Use an absolute --output path whose parent exists. Output must be new.
The child owns output creation; the wrapper only adds provenance to that new run.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    args, receiver = parser.parse_known_args()
    source = Path(__file__).resolve().parents[2]
    manifest = json.loads((source/'SOURCE.json').read_text())
    for name, digest in manifest['files'].items():
        if hashlib.sha256((source/name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'snapshot modified: {name}')
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError(output)
    build = Path.home()/'project'/'build'/manifest['snapshot']
    command = ['make', f'BUILD={build}']
    result = subprocess.run(command, cwd=source, capture_output=True, text=True)
    if result.returncode:
        sys.stderr.write(result.stdout+result.stderr)
        return result.returncode
    binary = build/'gateway'
    provenance = dict(source=manifest, build_command=command,
                      build_log=result.stdout+result.stderr,
                      binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                      gcc=subprocess.check_output(['gcc','--version'], text=True),
                      uname=os.uname()._asdict() if hasattr(os.uname(), '_asdict') else list(os.uname()),
                      started_utc=datetime.now(timezone.utc).isoformat())
    command = [str(binary), *receiver, '--output',str(output)]
    provenance['command'] = command
    child = subprocess.Popen(command)
    for sig in (signal.SIGINT,signal.SIGTERM):
        signal.signal(sig,lambda s, f: child.send_signal(s) if child.poll() is None else None)
    while not output.is_dir() and child.poll() is None:
        time.sleep(.01)
    if output.is_dir():
        (output/'BUILD.json').write_text(json.dumps(provenance,indent=2)+'\n')
        (output/'SOURCE.json').write_bytes((source/'SOURCE.json').read_bytes())
    code = child.wait()
    if output.is_dir():
        (output/'runner-result.json').write_text(json.dumps(dict(exit_code=code, ended_utc=datetime.now(timezone.utc).isoformat()))+'\n')
    return code


if __name__ == '__main__':
    sys.exit(main())
