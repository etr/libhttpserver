#!/usr/bin/env python3
"""Fail-closed public HTTP/2 scenario and provenance contracts."""
import sys
import unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import v3_public_http2_inventory as inventory


class InventoryTests(unittest.TestCase):
    def receipt(self):
        return dict(client='node:http2', version='24.15.0', nghttp2='1.68', alpn='h2',
                    scenarios=[dict(case=name, status='passed') for name in inventory.NODE['basic']])

    def test_complete_independent_traffic_receipt(self):
        inventory.validate_client(self.receipt(), 'node', 'basic')

    def test_missing_duplicate_unknown_failed_or_empty_traffic_fails(self):
        for mutate in (lambda r: r['scenarios'].clear(), lambda r: r['scenarios'].pop(),
                       lambda r: r['scenarios'].append(r['scenarios'][0]),
                       lambda r: r['scenarios'].append(dict(case='unknown', status='passed')),
                       lambda r: r['scenarios'][0].update(status='failed')):
            receipt = self.receipt(); mutate(receipt)
            with self.assertRaises(ValueError):
                inventory.validate_client(receipt, 'node', 'basic')

    def test_wrong_client_or_alpn_fails(self):
        for key, value in [('client', 'private-fixture'), ('version', '20.0'), ('alpn', 'http/1.1'), ('nghttp2', '')]:
            receipt = self.receipt(); receipt[key] = value
            with self.assertRaises(ValueError):
                inventory.validate_client(receipt, 'node', 'basic')

    def test_private_predicate_or_source_headers_cannot_claim_public_support(self):
        for public, included in [(False, ['/prefix/include/httpserver.hpp']),
                                 (True, ['/source/src/httpserver/detail/http2.hpp']),
                                 (True, ['/source/src/httpserver.hpp'])]:
            with self.assertRaises(ValueError):
                inventory.validate_public(public, included, Path('/prefix'), Path('/source'), Path('/build'))
        inventory.validate_public(True, ['/prefix/include/httpserver.hpp'], Path('/prefix'), Path('/source'), Path('/build'))

    def test_private_or_empty_matrix_is_rejected(self):
        for receipt in ({}, {'predicate': 'private-engine', 'status': 'passed'},
                        {'predicate': 'installed-public-http2', 'status': 'passed', 'tls': 'yes', 'cells': []}):
            with self.assertRaises(ValueError):
                inventory.validate_matrix(receipt)

    def test_matrix_rejects_unknown_or_missing_policy_families(self):
        policy = inventory.POLICY
        def receipt():
            families = []
            for client, version, required in [('hyper-h2', '4.4.1', inventory.PYTHON | policy),
                                              ('node:http2', '24.15.0', inventory.NODE)]:
                for family, cases in required.items():
                    families.append(dict(client=client, version=version, nghttp2='1.68', alpn='h2', family=family,
                                         scenarios=[dict(case=name, status='passed') for name in cases]))
            for family, case in [('disabled', 'disabled_h2_prebind'), ('psk-reject', 'psk_h2_rejected'), ('rollback', 'partial_bind_rollback_h2')]:
                families.append(dict(family=family, scenarios=[dict(case=case, status='passed')]))
            return dict(predicate='installed-public-http2', status='passed', tls='yes', prefix='/prefix', source='/source', build='/build',
                        library_hashes={}, source_hashes={}, cells=[dict(linkage=linkage, loop=loop, status='passed',
                        compile=dict(dependencies=['/prefix/include/httpserver.hpp'], private_test_headers=False), dependencies={}, readiness=[
                        dict(client=c, case='external_stale_generation_after_close', stale_events=2) for c in ('hyper-h2', 'node:http2')], families=[dict(r) for r in families])
                        for linkage in ('shared', 'static') for loop in ('managed', 'external')])
        with patch.object(inventory, 'check_linkage'):
            inventory.validate_matrix(receipt())
            for mutate in (lambda rows: rows.append(dict(client='hyper-h2', family='unknown', scenarios=[])),
                           lambda rows: rows.__setitem__(slice(None), [r for r in rows if r['family'] != 'cipher']),
                           lambda rows: rows.__setitem__(slice(None), [r for r in rows if r['family'] != 'rollback'])):
                value = receipt(); mutate(value['cells'][0]['families'])
                with self.assertRaises(ValueError):
                    inventory.validate_matrix(value)
            value = receipt(); value['cells'][0]['compile']['private_test_headers'] = True
            with self.assertRaises(ValueError):
                inventory.validate_matrix(value)


if __name__ == '__main__':
    unittest.main()
