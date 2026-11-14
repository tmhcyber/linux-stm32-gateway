"""Observe files and /proc without opening the serial port or signalling it."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time


def observe(directory):
    lines=(directory/'records.jsonl').read_text().splitlines()
    records=[]
    for line in lines:
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            # An observation can race a write; retained raw/records are reconciled after exit.
            pass
    build=json.loads((directory/'BUILD.json').read_text())
    binary=build['command'][0]
    processes=[]
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            command=(proc/'cmdline').read_bytes().split(b'\0')
            if command[0].decode()==binary and str(directory).encode() in command:
                status=dict(line.split(':',1) for line in (proc/'status').read_text().splitlines() if ':' in line)
                processes.append(dict(pid=int(proc.name),rss=status.get('VmRSS','').strip(),
                                      threads=status.get('Threads','').strip()))
        except (OSError,UnicodeDecodeError,IndexError):
            pass
    return dict(observed_utc=datetime.now(timezone.utc).isoformat(),records=len(records),
                last_elapsed_s=records[-1]['elapsed_s'] if records else None,
                last_seq=records[-1]['points'][0]['seq'] if records else None,
                last_record_age_s=round(time.time()-(directory/'records.jsonl').stat().st_mtime,3),
                events=(directory/'events.jsonl').read_text().count('\n'),
                finalized=(directory/'summary.json').exists(),processes=processes)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('directory',type=Path)
    parser.add_argument('--log',type=Path)
    parser.add_argument('--interval',type=float,default=30)
    args=parser.parse_args()
    if args.interval<1 or args.interval>60:
        parser.error('interval must be 1..60 seconds')
    if args.log:
        with args.log.open('x') as stream:
            while True:
                result=observe(args.directory)
                print(json.dumps(result),flush=True)
                stream.write(json.dumps(result)+'\n'); stream.flush()
                if result['finalized']:
                    break
                time.sleep(args.interval)
    else:
        print(json.dumps(observe(args.directory),indent=2))
