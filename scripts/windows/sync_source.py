"""Send a small allowlisted source snapshot over tar + SSH, including dirty work."""
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRS = {'scripts', 'tests', 'docs', 'src', 'include', 'config'}
SUFFIXES = {'.py', '.sh', '.ps1', '.md', '.c', '.h', '.gdb', '.toml', '.yaml', '.yml', '.service'}
ROOT_FILES = {'README.md', '.gitattributes', '.gitignore', 'Makefile', 'CMakeLists.txt'}
EXCLUDED = {'.local', '.git', 'evidence', 'build', '__pycache__'}


def source_paths():
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT)
    result = []
    for name in sorted(set(names.decode('utf-8').split('\0')) - {''}):
        path = Path(name)
        if EXCLUDED.intersection(path.parts):
            continue
        if name in ROOT_FILES or (path.parts[0] in SOURCE_DIRS and
                                  (path.suffix in SUFFIXES or path.name in ROOT_FILES)):
            full = ROOT / path
            if full.is_symlink():
                raise ValueError(f'source symlinks are not supported: {name}')
            if full.is_file():
                result.append(name)
    return result


def main():
    files = {name: (ROOT / name).read_bytes() for name in source_paths()}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
    listing = ''.join(f'{digest}  {name}\n' for name, digest in hashes.items()).encode()
    manifest_hash = hashlib.sha256(listing).hexdigest()
    snapshot = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    status = subprocess.check_output(['git', 'status', '--porcelain=v1', '-uall'], cwd=ROOT).decode('utf-8')
    manifest = {
        'snapshot': snapshot, 'git_commit': subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'git_dirty': bool(status), 'git_status': status,
        'created_at_utc': datetime.now(timezone.utc).isoformat(),
        'source_manifest_sha256': manifest_hash, 'files': hashes,
        'host_tools': {'python': sys.version, 'git': subprocess.check_output(
            ['git', '--version'], text=True).strip(), 'transport': 'Python tarfile + OpenSSH',
            'ssh': subprocess.run(['ssh', '-V'], capture_output=True, text=True, check=True).stderr.strip()},
    }
    local = ROOT / '.local' / 'sync' / snapshot
    local.mkdir(parents=True)
    (local / 'SOURCE.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    archive = local / 'source.tar'
    with tarfile.open(archive, 'w') as tar:
        for name, data in {**files, 'SOURCE.json': (local / 'SOURCE.json').read_bytes()}.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            tar.addfile(info, io.BytesIO(data))
    # Binary stdin avoids PowerShell's text pipeline corrupting the tar stream.
    receiver = (ROOT / 'scripts/linux/receive_source.py').read_text(encoding='utf-8')
    command = 'python3 -c ' + shlex.quote(receiver)
    with archive.open('rb') as stream:
        result = subprocess.run(['ssh', '-F', str(ROOT / '.local/ssh_config'),
                                 'linux-stm32', command], stdin=stream,
                                capture_output=True, check=True)
    receipt = json.loads(result.stdout)
    if receipt['source_manifest_sha256'] != manifest_hash or receipt['files_verified'] != len(files):
        raise ValueError('remote receipt does not match local source manifest')
    (local / 'RECEIPT.json').write_bytes(result.stdout)
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
