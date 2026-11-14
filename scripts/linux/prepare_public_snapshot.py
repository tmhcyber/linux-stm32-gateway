"""Copy a checkout into a new source snapshot with SHA256 build provenance."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import uuid


def prepare(output):
    root = Path(__file__).resolve().parents[2]
    output = output.resolve()
    if output.exists() or output.is_relative_to(root):
        raise ValueError('output must be a new directory outside this checkout')
    folders = ('src', 'include', 'tests', 'scripts', 'config', 'fixtures', 'examples')
    names = ['Makefile', 'README.md', '.gitignore', '.gitattributes']
    names += [p.relative_to(root).as_posix() for folder in folders
              for p in (root / folder).rglob('*') if p.is_file()
              and '__pycache__' not in p.parts and p.suffix != '.pyc']
    files = {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
             for name in sorted(names)}
    git = subprocess.run(['git', '-C', str(root), 'rev-parse', 'HEAD'],
                         capture_output=True, text=True)
    status = subprocess.run(['git', '-C', str(root), 'status', '--porcelain'],
                            capture_output=True, text=True)
    listing = ''.join(f'{sha}  {name}\n' for name, sha in files.items())
    manifest = dict(snapshot=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
                    + '-' + uuid.uuid4().hex[:8], files=files,
                    source_manifest_sha256=hashlib.sha256(listing.encode()).hexdigest(),
                    git_commit=git.stdout.strip() if git.returncode == 0 else 'exported-source',
                    git_dirty=bool(status.stdout) if status.returncode == 0 else True)
    output.mkdir(parents=True)
    for name in files:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
    text = json.dumps(manifest, indent=2) + '\n'
    (output / 'SOURCE.json').write_text(text)
    build = Path.home() / 'project/build' / manifest['snapshot']
    build.mkdir(parents=True)
    (build / 'SOURCE.json').write_text(text)
    return dict(source=str(output), build=str(build), snapshot=manifest['snapshot'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    print(json.dumps(prepare(parser.parse_args().output), indent=2))
