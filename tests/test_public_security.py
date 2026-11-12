"""Public-evidence guard and new-VM authentication policy regressions."""
import base64
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / 'scripts/windows'
sys.path.insert(0, str(SCRIPTS))
import check_public_history
import prepare_seed


class PublicGuardTests(unittest.TestCase):
    def test_reject_machine_identifiers_and_private_artifacts(self):
        samples = [
            ('evidence/devices.json', b'{"InstanceId":"USB device unique instance"}'),
            ('evidence/devices.json', b'{"InstanceId":"BTH' + b'ENUM address"}'),
            ('README.md', b'local file C:' + bytes([92]) + b'Users' + bytes([92]) + b'example'),
            ('.local/example.key', b'private material'),
            ('evidence/host-preflight.json', b'{"Volumes":[]}'),
        ]
        for path, data in samples:
            with self.subTest(path=path):
                self.assertTrue(check_public_history.inspect_blob(path, data))

    def test_accept_redacted_metadata_and_normal_telemetry(self):
        self.assertEqual(check_public_history.inspect_blob('evidence/device.json',
                         b'{"InstanceId":"<REDACTED_DEVICE_INSTANCE>"}'), [])
        self.assertEqual(check_public_history.inspect_blob('evidence/raw.bin',
                         b'v=1;reason=EVENT;p0.id=TEMPERATURE;\r\n'), [])


class SeedPolicyTests(unittest.TestCase):
    def test_actual_encrypted_identity_is_accepted(self):
        with tempfile.TemporaryDirectory() as temp:
            key = Path(temp) / 'ephemeral-test-key'
            subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N',
                            'test-only-ephemeral-passphrase', '-f', str(key)],
                           check=True, capture_output=True)
            prepare_seed.require_encrypted_identity(key)

    def test_unencrypted_key_is_rejected(self):
        magic = b'openssh-key-v1\0'
        with tempfile.TemporaryDirectory() as temp:
            key = Path(temp) / 'test-key'
            payload = magic + struct.pack('>I', 4) + b'none'
            key.write_bytes(b'header\n' + base64.b64encode(payload) + b'\nfooter\n')
            with self.assertRaisesRegex(ValueError, 'encrypted OpenSSH identity'):
                prepare_seed.require_encrypted_identity(key)

    def test_malformed_key_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            key = Path(temp) / 'test-key'
            key.write_bytes(b'not a key\n')
            with self.assertRaises(ValueError):
                prepare_seed.require_encrypted_identity(key)

    def test_sudo_hash_stays_private_and_plaintext_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            local = Path(temp) / '.local'
            local.mkdir()
            hashed = local / 'sudo.hash'
            value = '$6$' + 'test-salt' + '$' + 'A' * 86
            hashed.write_text(value, encoding='ascii')
            # Salt characters are checked, too.
            with self.assertRaises(ValueError):
                prepare_seed.read_sudo_hash(hashed, local)
            value = '$6$' + 'testsalt' + '$' + 'A' * 86
            hashed.write_text(value, encoding='ascii')
            self.assertEqual(prepare_seed.read_sudo_hash(hashed, local), value)
            outside = Path(temp) / 'outside.hash'
            outside.write_text(value, encoding='ascii')
            with self.assertRaises(ValueError):
                prepare_seed.read_sudo_hash(outside, local)
            hashed.write_text('plaintext-is-not-a-password-hash', encoding='ascii')
            with self.assertRaises(ValueError):
                prepare_seed.read_sudo_hash(hashed, local)

    def test_sudo_requires_password_while_ssh_password_login_stays_disabled(self):
        config = prepare_seed.cloud_config('public-key-placeholder', 'private-hash-placeholder')
        self.assertFalse(config['ssh_pwauth'])
        self.assertTrue(config['disable_root'])
        user = config['users'][0]
        self.assertIn('PASSWD:', user['sudo'])
        self.assertNotIn('NOPASSWD', user['sudo'])
        self.assertFalse(user['lock_passwd'])
        self.assertEqual(user['passwd'], 'private-hash-placeholder')
        ssh_policy = config['write_files'][0]['content']
        self.assertIn('PasswordAuthentication no', ssh_policy)
        self.assertIn('KbdInteractiveAuthentication no', ssh_policy)
        self.assertIn('PermitRootLogin no', ssh_policy)


if __name__ == '__main__':
    unittest.main()
