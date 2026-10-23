#!/usr/bin/env python3
"""Real independent fixture generation and strict TLS handshakes; no server library."""
import os
from pathlib import Path
import shlex
import shutil
import ssl
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parent.parent


class TLSFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='v3-tls-fixtures-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name).resolve()
        cls.fixtures = cls.root / 'fixtures'
        shutil.copytree(SOURCE / 'test/data/tls_credentials', cls.fixtures)
        executable = cls.root / ('client.exe' if os.name == 'nt' else 'client')
        cls.executable = executable
        command = (shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++20']
                   + shlex.split(os.environ.get('V3_TLS_CFLAGS', ''))
                   + [str(SOURCE / 'test/installed/tls_client.cpp')]
                   + shlex.split(os.environ.get('V3_TLS_LIBS', '-lssl -lcrypto'))
                   + (['-lws2_32'] if os.name == 'nt' else []) + ['-o', str(executable)])
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
        subprocess.run([str(executable), 'fixtures', str(cls.fixtures)], check=True,
                       capture_output=True, text=True, timeout=10)

    def handshake(self, identity, host='a.example', trust='root', client=None):
        """Exchange real TLS records in memory, including certificate/hostname verification."""
        chain = self.root / (identity + '-chain.pem')
        chain.write_bytes((self.fixtures / (identity + '.pem')).read_bytes()
                          + ((self.fixtures / 'intermediate.pem').read_bytes()
                             if identity in ('a', 'b') else b''))
        server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server.load_cert_chain(str(chain), str(self.fixtures / (identity + '-key.pem')))
        if client:
            server.load_verify_locations(str(self.fixtures / 'client-root.pem'))
            server.verify_mode = ssl.CERT_REQUIRED
        context = ssl.create_default_context(cafile=str(self.fixtures / (trust + '.pem')))
        # Python 3.14 enables this by default; exercise the same policy on older Python.
        context.verify_flags |= ssl.VERIFY_X509_STRICT
        self.assertTrue(context.check_hostname)
        self.assertEqual(ssl.CERT_REQUIRED, context.verify_mode)
        if client:
            client_chain = self.root / (client + '-chain.pem')
            client_chain.write_bytes((self.fixtures / (client + '.pem')).read_bytes()
                                     + ((self.fixtures / 'client-intermediate.pem').read_bytes()
                                        if client != 'client-other' else b''))
            context.load_cert_chain(str(client_chain), str(self.fixtures / (client + '-key.pem')))
        inbound, outbound = [ssl.MemoryBIO(), ssl.MemoryBIO()], [ssl.MemoryBIO(), ssl.MemoryBIO()]
        peers = [context.wrap_bio(inbound[0], outbound[0], server_hostname=host),
                 server.wrap_bio(inbound[1], outbound[1], server_side=True)]
        done = [False, False]
        for _ in range(20):
            for index, peer in enumerate(peers):
                if not done[index]:
                    try:
                        peer.do_handshake()
                        done[index] = True
                    except ssl.SSLWantReadError:
                        pass
                inbound[1 - index].write(outbound[index].read())
            if all(done):
                peers[0].write(b'fixture-ok')
                inbound[1].write(outbound[0].read())
                self.assertEqual(b'fixture-ok', peers[1].read())
                return
        self.fail('TLS handshake did not complete within 20 record exchanges')

    def test_static_sni_chains_pass_strict_handshake(self):
        for name in ('a', 'b'):
            with self.subTest(identity=name):
                self.handshake(name, name + '.example')

    def test_rotation_chain_passes_strict_handshake(self):
        self.handshake('a-replacement')

    def test_wrong_hostname_is_rejected(self):
        for name in ('a', 'b', 'a-replacement'):
            with self.subTest(identity=name):
                with self.assertRaises(ssl.SSLCertVerificationError) as error:
                    self.handshake(name, 'wrong.example')
                self.assertEqual(62, error.exception.verify_code, str(error.exception))

    def test_untrusted_chain_is_rejected(self):
        for name in ('a', 'b', 'a-replacement'):
            with self.subTest(identity=name):
                with self.assertRaises(ssl.SSLCertVerificationError) as error:
                    self.handshake(name, name + '.example' if name == 'b' else 'a.example', 'client-root')
                self.assertIn(error.exception.verify_code, (20, 21), str(error.exception))

    def test_server_fixture_regeneration_preserves_leaf_material(self):
        regenerated = self.root / 'regenerated'
        shutil.copytree(SOURCE / 'test/data/tls_credentials', regenerated)
        result = subprocess.run([str(self.executable), 'regenerate-server', str(regenerated)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        for path in (SOURCE / 'test/data/tls_credentials').iterdir():
            if path.name not in ('root.pem', 'intermediate.pem', 'a.pem', 'b.pem'):
                self.assertEqual(path.read_bytes(), (regenerated / path.name).read_bytes(), path.name)
        for name in ('root', 'intermediate', 'a', 'b'):
            flags = ['-noout', '-dates', '-serial', '-subject', '-issuer']
            if name in ('a', 'b'):
                flags.append('-pubkey')
            def metadata(directory):
                return subprocess.run([os.environ.get('OPENSSL', 'openssl'), 'x509',
                                       '-in', str(directory / (name + '.pem'))] + flags,
                                      check=True, capture_output=True, timeout=5).stdout
            self.assertEqual(metadata(SOURCE / 'test/data/tls_credentials'), metadata(regenerated))
        self.fixtures = regenerated
        self.handshake('a')
        self.handshake('b', 'b.example')

    def test_valid_mtls_client_is_accepted(self):
        self.handshake('a', client='client-valid')

    def test_negative_mtls_clients_are_rejected(self):
        for client in ('client-other', 'client-expired', 'client-wrong-purpose'):
            with self.subTest(client=client):
                with self.assertRaises(ssl.SSLCertVerificationError) as error:
                    self.handshake('a', client=client)
                self.assertEqual({'client-other': 20, 'client-expired': 10,
                                  'client-wrong-purpose': 26}[client], error.exception.verify_code)


if __name__ == '__main__':
    unittest.main()
