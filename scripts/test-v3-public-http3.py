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
                    compile=dict(private_test_headers=False,dependencies=['/prefix/include/httpserver.hpp']),example_compile=dict(private_test_headers=False,dependencies=['/prefix/include/httpserver.hpp']),dependencies={},
                    clients=[self.client(name) for name in inventory.CLIENTS],configuration=[dict(case=c,status='passed', **(dict(terminal='remote_connection_closed',error_code=0x100) if c == 'quic_go_active_stream_server_destruction' else dict(**inventory.LIFECYCLE[c],**(dict(encoder_bytes=175) if c.startswith('public_qpack_') else dict(normal_allocator_only=True,warm_seconds=.01,windows=[dict(cpu_seconds=.01,wall_seconds=1,rss_before=100,rss_after=100)]*6) if c=='public_flow_saturation_recovery' else dict(validated_challenges=1) if c=='migration_copied_metadata_live_policy' else dict(client_drops=1,server_drops=1,reordered_pairs=1,forwarded_datagrams=10,instrumented=False,recovery_seconds=.01) if c=='public_loss_reordering_recovery' else dict(resumed_messages=5) if c=='public_rfc9220_writable_backpressure' else {})) if c in inventory.LIFECYCLE else inventory.WEBSOCKET if c.endswith('_rfc9220_wire_corpus') else dict(scenarios=[dict(case=name,status='passed',**details) for name,details in inventory.SEMANTICS.items()]) if c.endswith('_semantic_parity') else dict(scenarios=[dict(case=name,status='passed',**details,**(dict(attempted_packets=1) if name=='zero_rtt_attempt_rejected' else dict(held_finished_bytes=1200) if name=='credential_rotation_held_handshake' else {})) for name,details in inventory.CREDENTIALS.items()]) if c=='quic_credentials' else dict(scenarios=[dict(case=('require' if c=='mtls-require' else 'request')+'_'+str(identity),status='passed',rejected=identity=='client-other' or (c=='mtls-require' and identity is None)) for identity in (None,'client-valid','client-other')]) if c in ('mtls-require','mtls-request') else {})) for c in inventory.CONFIGURATION])
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

    def test_family_payloads_and_nested_inventory_are_required(self):
        with patch.object(inventory, 'check_linkage'):
            for family, mutation in (
                ('h3_semantic_parity', lambda row: next(r for r in row['scenarios'] if r['case']=='ordered_raw_target').update(raw_target='wrong')),
                ('quic_credentials', lambda row: row.update(scenarios=[])),
                ('mtls-require', lambda row: row.update(scenarios=[]))):
                receipt=self.matrix();row=next(r for r in receipt['cells'][0]['configuration'] if r['case']==family)
                mutation(row)
                with self.assertRaises(ValueError):inventory.validate_matrix(receipt)

    def test_nested_credentials_preserve_installed_prefix_for_all_cells(self):
        receipt=self.matrix()
        for cell in receipt['cells']:
            cell['compile']['dependencies']=['/prefix/include/httpserver/server/native_server.hpp']
        with patch.object(inventory,'check_linkage'):
            inventory.validate_matrix(receipt)

    def test_resource_and_blocked_terminal_observations_are_required(self):
        expected={'public_flow_saturation_recovery':dict(cycles=3,resumed_bytes=131072,download_bytes=524288,ping_acks=192,terminal_code=0x10c,normal_allocator_only=True)}
        row=dict(case='public_flow_saturation_recovery',status='passed',**expected['public_flow_saturation_recovery'],windows=[])
        with self.assertRaises(ValueError):inventory.validate_family([row],expected)
        expected={'public_qpack_reset':dict(terminal_code=0x10c,accepted_trailer_completed=False,sibling_before_release=True)}
        row=dict(case='public_qpack_reset',status='passed',**expected['public_qpack_reset'])
        with self.assertRaises(ValueError):inventory.validate_family([row],expected)

    def test_destruction_requires_decoded_remote_terminal(self):
        with patch.object(inventory, 'check_linkage'):
            for observation in ({}, dict(terminal='timeout', error_code=0),
                                dict(terminal='remote_stream_cancelled', error_code=0x102),
                                dict(terminal='remote_connection_closed', error_code=0x102)):
                receipt = self.matrix()
                row = next(r for r in receipt['cells'][0]['configuration']
                           if r['case'] == 'quic_go_active_stream_server_destruction')
                row.pop('terminal'); row.pop('error_code'); row.update(observation)
                with self.assertRaises(ValueError): inventory.validate_matrix(receipt)

    def test_impairment_requires_actual_loss_and_reordering(self):
        expected={'public_loss_reordering_recovery':dict(body_bytes=524288,fin_count=1,sibling_body='hello native h3\n')}
        valid=dict(instrumented=False,recovery_seconds=.01,case='public_loss_reordering_recovery',status='passed',**expected['public_loss_reordering_recovery'],client_drops=1,server_drops=1,reordered_pairs=1,forwarded_datagrams=10)
        inventory.validate_family([valid],expected)
        for key in ('client_drops','server_drops','reordered_pairs','forwarded_datagrams'):
            with self.subTest(key=key):
                row=dict(valid);row[key]=0
                with self.assertRaises(ValueError):inventory.validate_family([row],expected)

    def test_pending_writable_requires_ordered_resumed_messages(self):
        expected={'public_rfc9220_writable_backpressure':dict(cancelled_pending_writable=1,handler_returns=2)}
        row=dict(case='public_rfc9220_writable_backpressure',status='passed',resumed_messages=0,**expected['public_rfc9220_writable_backpressure'])
        with self.assertRaises(ValueError):inventory.validate_family([row],expected)

    def test_installed_example_requires_public_header_provenance(self):
        for provenance in (None, dict(private_test_headers=True, dependencies=['/source/src/httpserver.hpp'])):
            receipt = self.matrix()
            for cell in receipt['cells']:
                if provenance is None:
                    cell.pop('example_compile', None)
                else:
                    cell['example_compile'] = provenance
            with patch.object(inventory, 'check_linkage'), patch.object(inventory, 'check_dependencies'):
                with self.assertRaises(ValueError): inventory.validate_matrix(receipt)

    def test_saturation_records_bounded_actual_warmup(self):
        expected={'public_flow_saturation_recovery':dict(cycles=3)}
        row=dict(case='public_flow_saturation_recovery',status='passed',cycles=3,
                 normal_allocator_only=False,windows=[dict(cpu_seconds=.01,wall_seconds=1)]*6)
        with self.assertRaises(ValueError): inventory.validate_family([row],expected)

    def test_impairment_records_bounded_actual_recovery_time(self):
        expected={'public_loss_reordering_recovery':dict(body_bytes=524288)}
        row=dict(case='public_loss_reordering_recovery',status='passed',body_bytes=524288,
                 client_drops=1,server_drops=1,reordered_pairs=1,forwarded_datagrams=2000)
        with self.assertRaises(ValueError): inventory.validate_family([row],expected)

if __name__=='__main__': unittest.main()
