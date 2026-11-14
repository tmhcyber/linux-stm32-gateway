"""Independently reconcile stored receiver evidence; never edits original data."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def analyze(root):
    summary = json.loads((root/'summary.json').read_text()) if (root/'summary.json').exists() else None
    raw = (root/'raw.bin').read_bytes()
    rows = [json.loads(s) for s in (root/'records.jsonl').read_text().splitlines()]
    diagnostic_rows = [json.loads(s) for s in (root/'diagnostics.jsonl').read_text().splitlines()] if (root/'diagnostics.jsonl').exists() else []
    events = [json.loads(s) for s in (root/'events.jsonl').read_text().splitlines()]
    spans = []
    mismatches = []
    by_second = Counter()
    previous = None
    diagnostic_mismatches=[]
    for i,row in enumerate(diagnostic_rows,1):
        piece=raw[row['raw_start']:row['raw_end']]
        try:
            pairs=[s.split('=',1) for s in piece[:-2].decode('ascii').split(';') if s]
            values={k:int(v,16 if v.startswith('0x') else 10) for k,v in pairs}
            if (not piece.endswith(b'\r\n') or len(values)!=19 or len(pairs)!=19 or
                row['diagnostic_rx_id']!=i or row['body']!=piece[:-2].decode('ascii') or
                any(row[k]!=v for k,v in values.items()) or
                row['fault_record_valid']!=(values['fault_status']==1) or
                row['pc_valid']!=(values['fault_status']==1 and values['frame_valid']==1)):
                diagnostic_mismatches.append(i)
        except (ValueError,KeyError,UnicodeError):diagnostic_mismatches.append(i)
    for i, row in enumerate(rows):
        piece = raw[row['raw_start']:row['raw_end']]
        fields = dict(item.split('=',1) for item in piece[:-2].decode('ascii').split(';') if item)
        expected = dict(v=row['v'],reason=row['reason'],flags=f"0x{row['flags']:08X}",
                        gap=row['gap'],coal=row['coal'],snap=row['snap'],n=row['n'])
        for p, point in enumerate(row['points']):
            for key in ('src','id','value','scale','unit','quality','status','value_ms','status_ms','seq'):
                expected[f'p{p}.{key}'] = point[key]
        if not piece.endswith(b'\r\n') or fields != {k:str(v) for k,v in expected.items()} or row['rx_id']!=i+1:
            mismatches.append(i+1)
        if previous:
            spans.append(row['elapsed_s']-previous['elapsed_s'])
        by_second[int(row['elapsed_s'])] += 1
        previous = row
    result = dict(path=str(root), finalized=summary is not None, records=len(rows),
                  diagnostic_records=len(diagnostic_rows),diagnostic_mismatches=diagnostic_mismatches,
                  raw_bytes=len(raw), reconciliation_mismatches=mismatches,
                  first=rows[0] if rows else None,last=rows[-1] if rows else None,
                  max_records_in_one_second=max(by_second.values(),default=0),
                  max_receive_interval_s=max(spans,default=0),
                  events=events, summary=summary,
                  files_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file() and p.name not in ('analysis.json',)})
    if rows:
        result['device_value_span_ms_mod32'] = (rows[-1]['points'][0]['value_ms']-rows[0]['points'][0]['value_ms']) & 0xffffffff
        result['receive_span_s'] = rows[-1]['elapsed_s']-rows[0]['elapsed_s']
    if summary:
        accounted = sum(row['raw_end']-row['raw_start'] for row in rows+diagnostic_rows)
        result['summary_counts_match'] = summary['bytes']==len(raw) and summary['valid_records']==len(rows) and summary.get('diagnostic_records',0)==len(diagnostic_rows)
        # Full-frame rejection byte lengths are reconstructed from raw framing by the C replay tests;
        # raw bytes remain authoritative when startup streams are truncated.
        result['accepted_frame_bytes'] = accounted
        result['byte_accounting_match'] = (accounted+summary['discarded_bytes']+
            summary['abandoned_bytes']+summary['pending_bytes']==len(raw)) if not sum(summary['parse_errors'].values()) else None
        result['clean_complete'] = bool(summary['complete'] and summary['exit_code']==0 and
            not summary['overlong'] and not sum(summary['parse_errors'].values()) and
            not summary['open_errors'] and not summary['link_errors'] and
            not summary['silent_count'] and not summary['io_error'] and
            not mismatches and not diagnostic_mismatches and result['summary_counts_match'] and result['byte_accounting_match'])
        result['one_hour_pass'] = bool(result['clean_complete'] and summary['mode']=='serial' and
            summary['requested_duration_s']>=3600 and summary['elapsed_s']>=3600 and
            len(rows)>=3580 and result['max_records_in_one_second']<=2 and
            abs(result['device_value_span_ms_mod32']/1000-result['receive_span_s'])<2 and
            not summary['sequence']['backward'] and not summary['sequence']['jump'])
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    print(json.dumps(analyze(args.directory),indent=2))
