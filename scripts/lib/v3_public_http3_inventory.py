"""Exact basic HTTP/3 product predicate, independent of fixture success."""
import hashlib
from pathlib import Path
from v3_package_matrix import check_dependencies, check_linkage, check_scenarios

CLIENTS = {'aioquic':'1.3.0','quic-go':'v0.55.0'}
BASIC = dict(authenticated_get_trailers=dict(body='hello native h3\n'), unknown_route={},
             incremental_upload_request_trailers=dict(body='incremental installed native h3 upload'),
             tls_peer_metadata={}, parallel_running_handler={}, parallel_suspended_upload={},
             reset_stream_local_once=dict(cancellation_entries=1), transport_stream_capacity_recovery=dict(requests=24))
ONESHOTS = {'post-auth':'POST_AUTH_H3_REJECTED','unsupported-alpn':'ALPN_H3_REJECTED','listener-storage-limit':'LIMIT_H3_UNBOUND','no-udp':'PREBIND_H3_REJECTED no-udp','no-tls':'PREBIND_H3_REJECTED no-tls',
            'no-h3':'PREBIND_H3_REJECTED no-h3','invalid-transport':'PREBIND_H3_REJECTED invalid-transport',
            'wrong-store':'PREBIND_H3_REJECTED wrong-store','psk':'PSK_H3_REJECTED',
            'rollback':'ROLLBACK_H3_UNBOUND rollback','rollback-reverse':'ROLLBACK_H3_UNBOUND rollback-reverse'}

WEBSOCKET = dict(text='public text', binary_hex='00ff62696e617279', fragmented_hex='667261676d656e7420e282ac20646f6e65', close_code=1000, terminal='FIN', settings_connect=1, subprotocol='harness.v1', sibling_body='hello native h3\n')

SEMANTICS = dict(one_shot_sources=dict(sources=2,reuse_status=409,closes_per_source=1),ordered_raw_target=dict(ordered_fields=['first','second'],ordered_trailers=['first','second'],raw_target='/items/../semantic?x=1'),
                 parameter_prefix_extension={},method_refusal={},hooks_errors=dict(committed_aborted=True),basic_authentication={},
                 digest_authentication=dict(replay_status=401),urlencoded_limits=dict(byte_limit_refused=True),multipart_files=dict(file_bytes=5),
                 response_definitions=dict(concurrent_cursors=3),file_borrowed_missing={},early_response={},body_admission_continue=dict(informational_status=100,body='continued admitted body'))
CREDENTIALS = dict(credential_rotation_held_handshake=dict(selected_old_certificate=True),sni_default_canonical=dict(hosts=2),session_resumption=dict(resumed=True),
                   zero_rtt_attempt_rejected=dict(early_data_accepted=False,early_handler_entries=0),acme_quic_isolation=dict(challenge_alpn_rejected=True),
                   credential_rotation_existing_new=dict(old_session_resumed=False,established_requests=3))

LIFECYCLE = dict(public_multipart_cancellation_cleanup=dict(partial_files=1,retained_files=0,terminal_code=0x10c),public_partial_response_lease_cancellation=dict(withheld_credit=4096,borrowed_live=0,file_closes=1,handler_returns=2,pending_handlers=0,terminal_code=0x10c),public_loss_reordering_recovery=dict(body_bytes=524288,fin_count=1,sibling_status=204,sibling_admitted_before_pressure=True),public_rfc9220_refusals=dict(cases=['invalid_mask','message_limit','pending_receive_reset'],terminal_code=0x10c,sibling_body='hello native h3\n'),public_flow_saturation_recovery=dict(cycles=3,resumed_bytes=131072,download_bytes=524288,ping_acks=192,terminal_code=0x10c),
                 public_rfc9220_writable_backpressure=dict(withheld_credit=4096,cancelled_pending_writable=1,handler_returns=2,terminal_code=0x10c,sibling_body='hello native h3\n',reset_before_stop_sending=True),
                 public_qpack_reset=dict(terminal_code=0x10c,accepted_trailer_completed=False,sibling_before_release=True),
                 public_qpack_initial=dict(terminal_code=0x10b,accepted_trailer_completed=False,sibling_before_release=True),
                 public_qpack_trailers=dict(terminal_code=0,accepted_trailer_completed=True,sibling_before_release=True),migration_copied_metadata_live_policy=dict(distinct_peer_ports=True,copied_peer_preserved=True,connection_identity_preserved=True,retransmitted_initial_decisions=1,rejected_tls_completions=0,refusal_code=0x10b),
                 public_qpack_blocked_unblock=dict(table_capacity=4096,blocked_limit=8,sibling_before_release=True,body='hello native h3\n'),
                 aggregate_clean=dict(listeners=4,protocols=['h1','h2','h3','rfc9220'],close_code=1001,drain_status=0,remaining=0,stable_wait=True,retained_ticket=True),
                 aggregate_deadline=dict(listeners=4,protocols=['h1','h2','h3','rfc9220'],close_code=1001,drain_status=1,remaining=6,stable_wait=True,retained_ticket=True),
                 aggregate_stalled_tls_deadline=dict(remaining=1,remote_error_code=12),
                 public_h3_handler_request_stop=dict(returned_promptly=True,remote_error_code=0x100),
                 public_quic_independent_stores=dict(foreign_ticket_resumed=False,other_store_preserved=True,existing_connection_preserved=True),
                 counted_h3_handler_drain_wait=dict(wait='would_deadlock'))

def validate_family(rows, expected):
    check_scenarios(rows,expected)
    for row in rows:
        for key,value in expected[row['case']].items():
            if type(row.get(key)) is not type(value) or row[key]!=value: raise ValueError('wrong family observation: '+key)
        if row['case'].startswith('public_qpack_') and (type(row.get('encoder_bytes')) is not int or not 0<row['encoder_bytes']<=8192):
            raise ValueError('missing actual withheld encoder instructions')
        if row['case']=='public_flow_saturation_recovery':
            from resource_limits_probe import validate_window
            samples=row.get('windows',[])
            if type(row.get('normal_allocator_only')) is not bool or len(samples)!=6:raise ValueError('missing actual saturation windows')
            warm=row.get('warm_seconds')
            if type(warm) not in (int,float) or not 0 < warm <= (3 if row['normal_allocator_only'] else 8):
                raise ValueError('missing bounded actual warm transfer time')
            for sample in samples:
                try:
                    if row['normal_allocator_only']:validate_window(sample)
                    else:assert sample['wall_seconds']>=.99 and sample['cpu_seconds']/sample['wall_seconds']<=.2
                except (AssertionError,KeyError,TypeError) as error:raise ValueError('invalid process resource window') from error
        if row['case']=='migration_copied_metadata_live_policy' and (type(row.get('validated_challenges')) is not int or row['validated_challenges']<1):
            raise ValueError('missing independent path validation')
        if row['case']=='zero_rtt_attempt_rejected' and (type(row.get('attempted_packets')) is not int or row['attempted_packets']<1):
            raise ValueError('missing genuine early-data packet attempt')
        if row['case']=='credential_rotation_held_handshake' and (type(row.get('held_finished_bytes')) is not int or not 0 < row['held_finished_bytes'] <= 32768):
            raise ValueError('missing actually withheld handshake flight')
        if row['case']=='public_loss_reordering_recovery':
            for key in ('client_drops','server_drops','reordered_pairs','forwarded_datagrams'):
                if type(row.get(key)) is not int or not 0 < row[key] <= 8192:
                    raise ValueError('missing bounded actual impairment observation: '+key)
            seconds=row.get('recovery_seconds')
            if type(row.get('instrumented')) is not bool or type(seconds) not in (int,float) or not 0 < seconds <= (12 if row['instrumented'] else 6):
                raise ValueError('missing bounded actual impairment recovery time')
        if row['case']=='public_rfc9220_writable_backpressure' and (type(row.get('resumed_messages')) is not int or not 2 <= row['resumed_messages'] <= 16):
            raise ValueError('missing ordered resumed WebSocket messages')


CONFIGURATION = list(LIFECYCLE) + ['quic_credentials','mtls-request','mtls-require','h1_semantic_parity','h2_semantic_parity','h3_semantic_parity'] + [name+suffix for name in CLIENTS for suffix in ('_rfc9220_wire_corpus','_example_rfc9220_wire_corpus')] + list(ONESHOTS) + [case for c in CLIENTS for case in (c+'_bad_initial_listener_recovery',c+'_mixed_tcp_udp_same_port')] + ['stalled_handshake_capacity_recovery','peer_policy_before_tls','header_timeout_and_recovery','failed_handshake_and_recovery','active_stream_server_destruction','quic_go_active_stream_server_destruction']

def validate_client(receipt):
    if receipt.get('client') not in CLIENTS or receipt.get('version') != CLIENTS[receipt['client']]:
        raise ValueError('independent pinned client identity absent')
    for key, value in dict(alpn='h3',verified=True,quic_version=1,tls_version='TLSv1.3',early_data=False,retry_count=1).items():
        if type(receipt.get(key)) is not type(value) or receipt[key] != value:
            raise ValueError('authenticated protocol witness differs: '+key)
    rows=receipt.get('scenarios',[])
    check_scenarios(rows,BASIC)
    for row in rows:
        for key,value in BASIC[row['case']].items():
            if row.get(key) != value: raise ValueError('wrong public route observation: '+key)

def validate_stop_observation(row):
    expected = {'remote_stream_cancelled': 0x10c, 'remote_connection_closed': 0x100,
                'remote_transport_closed': 0}
    terminal = row.get('terminal')
    if terminal not in expected or type(row.get('error_code')) is not int or row['error_code'] != expected[terminal]:
        raise ValueError('destruction lacks expected decoded remote terminal')

def validate_matrix(receipt):
    if (receipt.get('predicate'),receipt.get('status'),receipt.get('tls')) != ('installed-public-http3','passed','yes'):
        raise ValueError('public H3 requires successful installed TLS-on matrix')
    cells=receipt.get('cells',[])
    if len(cells)!=4 or {(c.get('linkage'),c.get('loop')) for c in cells}!={(a,b) for a in ('shared','static') for b in ('managed','external')}:
        raise ValueError('missing/duplicate installed H3 cells')
    prefix,source,build=[Path(receipt[k]) for k in ('prefix','source','build')]
    for name,digest in receipt['library_hashes'].items():
        if hashlib.sha256(Path(name).read_bytes()).hexdigest()!=digest: raise ValueError('installed library changed')
    for name,digest in receipt['source_hashes'].items():
        if hashlib.sha256((source/name).read_bytes()).hexdigest()!=digest: raise ValueError('covered source changed: '+name)
    for cell in cells:
        if cell.get('status')!='passed' or cell['compile'].get('private_test_headers') is not False:
            raise ValueError('failed/private product witness')
        check_dependencies(cell['compile']['dependencies'],prefix,source,build)
        example=cell.get('example_compile',{})
        if example.get('private_test_headers') is not False:
            raise ValueError('missing/private installed example provenance')
        check_dependencies(example['dependencies'],prefix,source,build)
        check_linkage(cell['dependencies'],cell['linkage'],prefix)
        check_scenarios(cell.get('configuration',[]),CONFIGURATION)
        for row in cell['configuration']:
            if row['case'] in LIFECYCLE:
                validate_family([row],{row['case']:LIFECYCLE[row['case']]})
            if row['case'].endswith('_semantic_parity'):
                validate_family(row.get('scenarios',[]),SEMANTICS)
            if row['case']=='quic_credentials': validate_family(row.get('scenarios',[]),CREDENTIALS)
            if row['case'] in ('mtls-request','mtls-require'):
                require=row['case']=='mtls-require';mode='require' if require else 'request'
                validate_family(row.get('scenarios',[]),{mode+'_'+str(identity):dict(rejected=identity=='client-other' or (require and identity is None)) for identity in (None,'client-valid','client-other')})
            if row['case'] == 'quic_go_active_stream_server_destruction': validate_stop_observation(row)
            if row['case'].endswith('_rfc9220_wire_corpus'):
                for key, value in WEBSOCKET.items():
                    if type(row.get(key)) is not type(value) or row[key] != value: raise ValueError('wrong RFC9220 wire observation: '+key)
        clients=cell.get('clients',[])
        if len(clients)!=2 or {r.get('client') for r in clients}!=set(CLIENTS): raise ValueError('missing/duplicate independent client')
        for client in clients: validate_client(client)
