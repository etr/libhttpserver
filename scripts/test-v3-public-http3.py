#!/usr/bin/env python3
"""Fail-closed installed public HTTP/3 witness contracts."""
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import v3_public_http3_inventory as inventory

class Contracts(unittest.TestCase):
    def client(self, name='aioquic'):
        return dict(client=name, version=inventory.CLIENTS[name], alpn='h3', verified=True,
                    quic_version=1, tls_version='TLSv1.3', early_data=False, retry_count=1,
                    scenarios=[dict(case=case, status='passed', **details) for case, details in inventory.BASIC.items()])

    def test_authenticated_exact_independent_receipts(self):
        for client in inventory.CLIENTS:
            inventory.validate_client(self.client(client))

    def test_empty_duplicate_omitted_unknown_failed_case_rejected(self):
        for mutate in (lambda r:r['scenarios'].clear(), lambda r:r['scenarios'].pop(),
                       lambda r:r['scenarios'].append(r['scenarios'][0]),
                       lambda r:r['scenarios'].append(dict(case='fake',status='passed')),
                       lambda r:r['scenarios'][0].update(status='failed')):
            value=self.client(); mutate(value)
            with self.assertRaises(ValueError): inventory.validate_client(value)

    def test_exit_zero_wrong_payload_and_unverified_or_fixture_identity_rejected(self):
        for key,value in [('client','private-fixture'),('version','0'),('verified',False),('alpn','hq-interop'),
                          ('retry_count',0),('quic_version',2),('tls_version','TLSv1.2'),('early_data',True)]:
            receipt=self.client(); receipt[key]=value
            with self.assertRaises(ValueError): inventory.validate_client(receipt)
        receipt=self.client(); receipt['scenarios'][0]['body']='wrong'
        with self.assertRaises(ValueError): inventory.validate_client(receipt)

    def matrix(self):
        return dict(predicate='installed-public-http3',status='passed',tls='yes',prefix='/prefix',source='/source',build='/build',
                    library_hashes={},source_hashes={},cells=[dict(linkage=linkage,loop=loop,status='passed',
                    compile=dict(private_test_headers=False,dependencies=['/prefix/include/httpserver.hpp']),dependencies={},
                    clients=[self.client(name) for name in inventory.CLIENTS],configuration=[dict(case=c,status='passed') for c in inventory.CONFIGURATION])
                    for linkage in ('shared','static') for loop in ('managed','external')])

    def test_exact_cells_clients_and_public_header_provenance(self):
        with patch.object(inventory,'check_linkage'):
            inventory.validate_matrix(self.matrix())
            for mutate in (lambda r:r['cells'].pop(), lambda r:r['cells'].append(r['cells'][0]),
                           lambda r:r['cells'][0]['configuration'].pop(), lambda r:r['cells'][0]['clients'].pop(),
                           lambda r:r['cells'][0]['clients'].append(r['cells'][0]['clients'][0]),
                           lambda r:r['cells'][0]['compile'].update(private_test_headers=True),
                           lambda r:r['cells'][0]['compile'].update(dependencies=['/source/src/httpserver.hpp'])):
                receipt=self.matrix(); mutate(receipt)
                with self.assertRaises(ValueError): inventory.validate_matrix(receipt)
        with patch.object(inventory,'check_linkage',side_effect=ValueError('wrong library')):
            with self.assertRaises(ValueError): inventory.validate_matrix(self.matrix())

if __name__=='__main__': unittest.main()
