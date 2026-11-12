"""Create a local SSH identity and a NoCloud seed ISO for Ubuntu."""

import argparse
import base64
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.local' / 'python-deps'))


def require_encrypted_identity(key):
    """Read the OpenSSH container's cipher name without decrypting the key."""
    lines = key.read_bytes().splitlines()
    try:
        payload = base64.b64decode(b''.join(lines[1:-1]), validate=True)
        magic = b'openssh-key-v1\0'
        if not payload.startswith(magic):
            raise ValueError('not an OpenSSH key')
        size = struct.unpack('>I', payload[len(magic):len(magic) + 4])[0]
        cipher = payload[len(magic) + 4:len(magic) + 4 + size]
        supported = {b'aes128-ctr', b'aes192-ctr', b'aes256-ctr', b'aes128-cbc',
                     b'aes192-cbc', b'aes256-cbc', b'aes128-gcm@openssh.com',
                     b'aes256-gcm@openssh.com', b'chacha20-poly1305@openssh.com'}
        if cipher not in supported or len(cipher) != size:
            raise ValueError('SSH identity is not encrypted')
    except (ValueError, struct.error) as exc:
        raise ValueError('Use an encrypted OpenSSH identity; run ssh-keygen -p locally to set its passphrase.') from exc


def read_sudo_hash(path, local):
    if not path.resolve().is_relative_to(local.resolve()):
        raise ValueError('sudo password hash must stay inside the ignored .local directory')
    value = path.read_text(encoding='ascii').strip()
    if not re.fullmatch(r'\$6\$(?:rounds=[1-9][0-9]*\$)?[./A-Za-z0-9]{1,16}\$[./A-Za-z0-9]{86}', value):
        raise ValueError('Expected a SHA-512 crypt hash generated interactively by openssl passwd -6')
    return value


def cloud_config(public_key, sudo_hash):
    return {
        'hostname': 'linux-stm32',
        'manage_etc_hosts': True,
        'ssh_pwauth': False,
        'disable_root': True,
        'users': [{'name': 'ubuntu', 'groups': ['adm', 'sudo', 'dialout'],
                   'shell': '/bin/bash', 'lock_passwd': False,
                   'passwd': sudo_hash,
                   'sudo': 'ALL=(ALL:ALL) PASSWD: ALL',
                   'ssh_authorized_keys': [public_key]}],
        'write_files': [{
            'path': '/etc/ssh/sshd_config.d/00-local-key-only.conf',
            'owner': 'root:root', 'permissions': '0644',
            'content': 'PasswordAuthentication no\nKbdInteractiveAuthentication no\nPermitRootLogin no\n',
        }],
        'runcmd': [
            ['install', '-d', '-o', 'ubuntu', '-g', 'ubuntu', '/home/ubuntu/project'],
            ['install', '-d', '-o', 'ubuntu', '-g', 'ubuntu',
             '/home/ubuntu/project/source', '/home/ubuntu/project/build',
             '/home/ubuntu/project/evidence'],
            ['sh', '-c', "date -u +%FT%TZ > /var/tmp/stm32-stage1-cloud-init.done"],
            ['systemctl', 'reload', 'ssh.service'],
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=2222)
    parser.add_argument('--sudo-password-hash-file', type=Path,
                        default=ROOT / '.local/keys/ubuntu_sudo.sha512',
                        help='private SHA-512 crypt hash file, generated with openssl passwd -6')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('--port must be between 1 and 65535')
    local = ROOT / '.local'
    try:
        sudo_hash = read_sudo_hash(args.sudo_password_hash_file, local)
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    import pycdlib
    keys = local / 'keys'
    keys.mkdir(parents=True, exist_ok=True)
    key = keys / 'ubuntu_ed25519'
    if not key.exists():
        # Interactive prompt: a passphrase is never placed in argv or logs.
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519',
                        '-C', 'linux-stm32-local-vm', '-f', str(key)], check=True)
    try:
        require_encrypted_identity(key)
    except ValueError as exc:
        parser.error(str(exc))
    public_key = key.with_suffix('.pub').read_text(encoding='ascii').strip()
    config = cloud_config(public_key, sudo_hash)
    metadata = {'instance-id': 'linux-stm32-stage1-20261111',
                'local-hostname': 'linux-stm32'}
    seed_dir = local / 'seed'
    seed_dir.mkdir(exist_ok=True)
    user_data = '#cloud-config\n' + json.dumps(config, indent=2) + '\n'
    files = {'user-data': user_data.encode(),
             'meta-data': (json.dumps(metadata) + '\n').encode()}
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, joliet=3, rock_ridge='1.09', vol_ident='cidata')
    for name, data in files.items():
        (seed_dir / name).write_bytes(data)
        iso.add_fp(io.BytesIO(data), len(data),
                   iso_path='/' + name.upper().replace('-', '_') + ';1',
                   rr_name=name, joliet_path='/' + name)
    iso_path = seed_dir / 'seed.iso'
    iso.write(str(iso_path))
    iso.close()
    ssh_config = (
        'Host linux-stm32\n'
        '    HostName 127.0.0.1\n'
        f'    Port {args.port}\n'
        '    User ubuntu\n'
        f'    IdentityFile "{key.as_posix()}"\n'
        f'    UserKnownHostsFile "{(local / "known_hosts").as_posix()}"\n'
        '    StrictHostKeyChecking accept-new\n'
        '    IdentitiesOnly yes\n'
        '    ConnectTimeout 10\n'
    )
    (local / 'ssh_config').write_text(ssh_config, encoding='utf-8')
    print(json.dumps({'seed_iso': str(iso_path),
                      'ssh_config': str(local / 'ssh_config')}))


if __name__ == '__main__':
    main()
