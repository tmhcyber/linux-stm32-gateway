"""Create a local SSH identity and a NoCloud seed ISO for Ubuntu."""

import argparse
import io
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.local' / 'python-deps'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=2222)
    args = parser.parse_args()
    import pycdlib

    local = ROOT / '.local'
    keys = local / 'keys'
    keys.mkdir(parents=True, exist_ok=True)
    key = keys / 'ubuntu_ed25519'
    if not key.exists():
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '',
                        '-C', 'linux-stm32-local-vm', '-f', str(key)], check=True)
    public_key = key.with_suffix('.pub').read_text(encoding='ascii').strip()
    config = {
        'hostname': 'linux-stm32',
        'manage_etc_hosts': True,
        'ssh_pwauth': False,
        'disable_root': True,
        'users': [{'name': 'ubuntu', 'groups': ['adm', 'sudo', 'dialout'],
                   'shell': '/bin/bash', 'lock_passwd': True,
                   'sudo': 'ALL=(ALL) NOPASSWD:ALL',
                   'ssh_authorized_keys': [public_key]}],
        'package_update': True,
        'packages': ['build-essential', 'gdb', 'git', 'pkg-config', 'python3',
                     'usbutils', 'mosquitto', 'mosquitto-clients',
                     'libmosquitto-dev'],
        'runcmd': [
            ['install', '-d', '-o', 'ubuntu', '-g', 'ubuntu', '/home/ubuntu/project'],
            ['sh', '-c', "date -u +%FT%TZ > /var/tmp/stm32-stage1-cloud-init.done"],
        ],
    }
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
