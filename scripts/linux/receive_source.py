"""Receive one verified immutable snapshot. Never extract into project/evidence."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile


def receive(stream, project):
    files = {}
    with tarfile.open(fileobj=stream, mode='r|') as tar:
        for member in tar:
            path = PurePosixPath(member.name)
            if (not member.isfile() or path.as_posix() != member.name or member.name == '.' or
                    path.is_absolute() or '..' in path.parts or
                    any(part in {'.local', '.git', 'evidence', 'build', '__pycache__'} for part in path.parts) or
                    member.name in files or member.size > 10 * 1024 * 1024):
                raise ValueError(f'unsafe source entry: {member.name}')
            files[member.name] = tar.extractfile(member).read()
    manifest = json.loads(files.pop('SOURCE.json'))
    snapshot = manifest['snapshot']
    if not re.fullmatch(r'[0-9]{8}T[0-9]{6}Z-[a-f0-9]{8}', snapshot):
        raise ValueError('invalid snapshot ID')
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    digest = hashlib.sha256(''.join(f'{value}  {name}\n' for name, value in hashes.items()).encode()).hexdigest()
    if hashes != manifest['files'] or digest != manifest['source_manifest_sha256']:
        raise ValueError('source file list or SHA256 mismatch')
    project = project.resolve()
    for name in ('source', 'build'):
        target = project / name
        if target.is_symlink():
            raise ValueError(f'managed directory cannot be a symlink: {target}')
        target.mkdir(parents=True, exist_ok=True)
    source = project / 'source' / snapshot
    source.mkdir(exist_ok=False)
    for name, data in files.items():
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    encoded = (json.dumps(manifest, indent=2) + '\n').encode()
    (source / 'SOURCE.json').write_bytes(encoded)
    written = {path.relative_to(source).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in sorted(source.rglob('*')) if path.is_file() and path != source / 'SOURCE.json'}
    if written != hashes:
        raise ValueError('on-disk source file list or SHA256 mismatch')
    build = project / 'build' / snapshot
    build.mkdir(exist_ok=False)
    (build / 'SOURCE.json').write_bytes(encoded)
    tools = {}
    for tool, args in {'python': ['python3', '--version'], 'gcc': ['gcc', '--version'],
                       'make': ['make', '--version'], 'pkg-config': ['pkg-config', '--version']}.items():
        try:
            result = subprocess.run(args, capture_output=True, text=True)
            tools[tool] = (result.stdout or result.stderr).splitlines()[0]
        except (OSError, IndexError):
            tools[tool] = 'unavailable'
    receipt = {'snapshot': snapshot, 'git_commit': manifest['git_commit'],
               'git_dirty': manifest['git_dirty'], 'source_manifest_sha256': digest,
               'files_verified': len(hashes), 'source_dir': str(source),
               'build_dir': str(build), 'guest_tools': tools}
    (build / 'RECEIPT.json').write_text(json.dumps(receipt, indent=2) + '\n')
    current = project / 'current'
    if current.exists() and not current.is_symlink():
        raise ValueError('project/current already exists and is not a managed symlink')
    temporary = project / ('.current-' + snapshot)
    temporary.symlink_to(source, target_is_directory=True)
    temporary.replace(current)
    return receipt


if __name__ == '__main__':
    print(json.dumps(receive(sys.stdin.buffer, Path.home() / 'project'), indent=2))
