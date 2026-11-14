"""Reject private artifacts and machine identifiers anywhere in pushed history."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def inspect_blob(path, data):
    problems = []
    parts = Path(path).parts
    if any(part in {'.local', '.ssh', '.private'} for part in parts) or path.endswith(
            ('.pem', '.key', '.iso', '.vdi', '.vmdk', '.dmp', '.bundle',
             '.sqlite', '.sqlite-wal', '.sqlite-shm', '.db', '.db-wal', '.db-shm', '.axf')):
        problems.append('private artifact path')
    for name, expression in {
        'private key': rb'-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----',
        'access token': rb'gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-(?:proj-)?[A-Za-z0-9_-]{24,}',
        'password hash': rb'\$6\$(?:rounds=[0-9]+\$)?[./A-Za-z0-9]{1,16}\$[./A-Za-z0-9]{86}',
        'credential URL': rb'https?://[^\s/:@]+:[^\s/@]+@',
    }.items():
        if re.search(expression, data):
            problems.append(name)
    if path == 'README.md' or path.startswith(('docs/', 'evidence/')):
        for name, expression in {
            'personal Windows directory': rb'(?i)[A-Z]:[\\/]+Users[\\/]+(?!<)[^\\/\s`"\x27]+',
            'Bluetooth enumeration identifier': rb'BTHENUM',
            'device MAC address': rb'(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}',
            'report/device UUID': rb'(?i)\b[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}\b',
            'kernel address': rb'(?i)0xffff[0-9a-f]{12}|\bffff[0-9a-f]{4}`[0-9a-f]{8}\b',
        }.items():
            if re.search(expression, data):
                problems.append(name)
        if path.endswith('.json'):
            try:
                value = json.loads(data.decode('utf-8-sig'))
            except (ValueError, UnicodeError):
                problems.append('invalid evidence JSON')
            else:
                def check(item):
                    if isinstance(item, dict):
                        for key, entry in item.items():
                            if key in {'InstanceId', 'PNPDeviceID'} and '<REDACTED' not in str(entry):
                                problems.append('unredacted device instance')
                            if path.endswith('/host-preflight.json') and key in {'Volumes', 'MemoryBytes', 'Cpu', 'BuildNumber'}:
                                problems.append('unnecessary host inventory')
                            check(entry)
                    elif isinstance(item, list):
                        for entry in item:
                            check(entry)
                check(value)
    return sorted(set(problems))


def audit(repo, refs):
    commits = subprocess.check_output(['git', 'rev-list', *refs], cwd=repo, text=True).splitlines()
    blobs = {}
    for commit in commits:
        tree = subprocess.check_output(['git', 'ls-tree', '-rz', commit], cwd=repo)
        for entry in tree.split(b'\0'):
            if not entry:
                continue
            metadata, name = entry.split(b'\t', 1)
            _mode, kind, sha = metadata.decode().split()
            if kind == 'blob':
                blobs.setdefault(sha, set()).add(name.decode('utf-8'))
    findings = []
    for sha, paths in blobs.items():
        data = subprocess.check_output(['git', 'cat-file', 'blob', sha], cwd=repo)
        for path in paths:
            problems = inspect_blob(path, data)
            if problems:
                findings.append({'file': path, 'problems': problems})
    return {'commits_checked': len(set(commits)), 'blobs_checked': len(blobs), 'findings': findings}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', default='.')
    parser.add_argument('--ref', action='append')
    parser.add_argument('--pre-push', action='store_true')
    parser.add_argument('--install-hook', action='store_true')
    args = parser.parse_args()
    if args.install_hook:
        override = subprocess.run(['git', 'config', '--get', 'core.hooksPath'], cwd=args.repo,
                                  capture_output=True, text=True)
        if override.returncode == 0:
            parser.error('core.hooksPath is customized; install the guard there without overwriting existing hooks')
        hooks = Path(subprocess.check_output(['git', 'rev-parse', '--git-path', 'hooks'],
                                            cwd=args.repo, text=True).strip())
        if not hooks.is_absolute():
            hooks = Path(args.repo).resolve() / hooks
        hooks.mkdir(parents=True, exist_ok=True)
        hook = hooks / 'pre-push'
        if hook.exists():
            parser.error('pre-push hook already exists; refusing to overwrite it')
        shutil.copyfile(__file__, hooks / 'public-history-guard.py')
        hook.write_text('#!/bin/sh\nexec python "$(git rev-parse --git-path hooks/public-history-guard.py)" '
                        '--repo "$(git rev-parse --show-toplevel)" --pre-push\n', encoding='utf-8', newline='\n')
        hook.chmod(0o755)
        print('Installed local public-history pre-push guard.')
        return 0
    refs = args.ref or ['HEAD']
    if args.pre_push:
        refs = [line.split()[1] for line in sys.stdin if line.strip() and set(line.split()[1]) != {'0'}]
        if not refs:
            return 0
    result = audit(args.repo, refs)
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 1 if result['findings'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
