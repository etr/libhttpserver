"""Exact inventories for the installed public HTTP/2 acceptance predicates."""
from pathlib import Path
import hashlib
from package_matrix import check_dependencies, check_scenarios, check_linkage

BASIC = ['alpn_route_parity', 'hook_policy_parity', 'header_admit_incremental', 'slow_upload_healthy_stream',
         'ordered_headers_trailers', 'request_body_trailers', 'response_body_trailers',
         'route_sync_at_cap', 'route_sync_over_cap', 'header_reject_before_body',
         'suspended_stream_control_progress', 'header_suspend_resume',
         'running_handler_does_not_own_wire_domain', 'suspension_reset_late_signal',
         'reset_stream_local_cancel', 'late_signal_after_retirement', 'streaming_source_end_error',
         'response_definition_overlay_isolation', 'tls_peer_metadata',
         'custom_errors_exception_containment', 'rfc8441_echo_parallel', 'rfc8441_close_once']
COMMON = {'basic': BASIC,
          'timeouts': ['suspension_timeout', 'body_idle_timeout_local', 'slow_response_healthy_stream', 'write_idle_timeout_local'],
          'running-reset': ['reset_running_resume_retains_frame'],
          'drain-finish': ['handler_drain_ack', 'counted_handler_wait_rejected', 'goaway_finishes_accepted', 'ticket_after_server_destruction'],
          'drain-timeout': ['handler_drain_ack', 'counted_handler_wait_rejected', 'goaway_accept_cutoff', 'drain_deadline_without_waiter', 'drain_cancels_requests_ws', 'ticket_after_server_destruction']}
NODE = dict(COMMON, websocket=['rfc8441_invalid_negotiation','rfc8441_backpressure','rfc8441_reset_deadline'], flow=['consumed_credit_exact'], **{'flow-padded': ['padded_data_credit_exact', 'reset_unread_credit_once']},
            queued=['reset_queued_resume_never_enters'], disconnect=['disconnect_cancels_connection_streams'],
            header=['header_timeout_idle'], settings=['settings_timeout_idle'],
            policy=['h2_peer_policy_live_and_accept', 'h2_live_policy_publication'],
            mtls=['h2_mtls_metadata'], stop=['handler_stop_ack'], mixed=['mixed_h1_h2_listeners'],
            cipher=['h2_forbidden_cipher_before_hooks'], **{'h2-only': ['h2_only_alpn']})
PYTHON = dict(COMMON, flow=['consumed_credit_exact', 'padded_data_credit_exact', 'reset_unread_credit_once'],
              queued=['reset_queued_resume_never_enters'], disconnect=['disconnect_cancels_connection_streams'],
              header=['header_timeout_idle'], settings=['settings_timeout_idle'])
for mode, case in [('connection-limit', 'listener_connection_limit_recovery'), ('timer-limit', 'timer_limit_retirement'),
                   ('stream-limit', 'stream_limit_control_progress'), ('header-limit', 'header_limit_bounded'),
                   ('body-limit', 'body_limit_bounded'), ('output-limit', 'output_limit_control_progress')]:
    PYTHON[mode] = [case, 'capacity_returns_to_baseline']
    NODE[mode] = [case, 'capacity_returns_to_baseline']


POLICY = {'h2-only': ['h2_only_alpn'], 'h1-bound': ['h2_live_policy_rejects_disabled'],
          'mixed': ['mixed_h1_h2_listeners'], 'mtls': ['h2_mtls_metadata_and_refusal'],
          'policy': ['h2_peer_policy_live_and_accept', 'h2_live_policy_publication'],
          'cipher': ['h2_forbidden_cipher_before_hooks'], 'stop': ['handler_stop_ack'],
          'websocket': ['rfc8441_invalid_negotiation', 'rfc8441_bounded_backpressure_reset', 'rfc8441_backpressure', 'rfc8441_reset_deadline']}

ONESHOTS = {'disabled': ('DISABLED_H2_PREBIND', 'disabled_h2_prebind'),
            'psk-reject': ('PSK_H2_REJECTED', 'psk_h2_rejected'),
            'rollback': ('ROLLBACK_H2_UNBOUND', 'partial_bind_rollback_h2')}


def validate_client(receipt, client, family):
    expected = ('node:http2', '24.15.0') if client == 'node' else ('hyper-h2', '4.4.1')
    if (receipt.get('client'), receipt.get('version')) != expected or receipt.get('alpn') != 'h2':
        raise ValueError('independent public client/ALPN identity differs')
    if client == 'node' and not receipt.get('nghttp2'):
        raise ValueError('missing Node nghttp2 identity')
    check_scenarios(receipt.get('scenarios', []), (NODE if client == 'node' else PYTHON | POLICY)[family])


def validate_public(public, included, prefix, source, build):
    if public is not True:
        raise ValueError('a public capability requires an installed public-only consumer')
    check_dependencies(included, prefix, source, build, private=False)


def validate_matrix(receipt):
    if receipt.get('predicate') != 'installed-public-http2' or receipt.get('status') != 'passed' or receipt.get('tls') != 'yes':
        raise ValueError('public H2 predicate needs a successful installed TLS-on matrix')
    cells = receipt.get('cells', [])
    if len(cells) != 4 or {(c.get('linkage'), c.get('loop')) for c in cells} != {
            (linkage, loop) for linkage in ('shared', 'static') for loop in ('managed', 'external')}:
        raise ValueError('incomplete installed public H2 cells')
    prefix, source, build = [Path(receipt[key]) for key in ('prefix', 'source', 'build')]
    for name, digest in receipt['library_hashes'].items():
        if hashlib.sha256(Path(name).read_bytes()).hexdigest() != digest:
            raise ValueError('installed H2 library changed since receipt')
    for name, digest in receipt['source_hashes'].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != digest:
            raise ValueError('H2 source changed since receipt: ' + name)
    for cell in cells:
        if cell.get('status') != 'passed':
            raise ValueError('failed public H2 cell')
        validate_public(cell['compile'].get('private_test_headers') is False, cell['compile']['dependencies'], prefix, source, build)
        check_linkage(cell['dependencies'], cell['linkage'], prefix)
        if cell['loop'] == 'external':
            witnesses = cell.get('readiness', [])
            if len(witnesses) != 2 or {r.get('client') for r in witnesses} != {'hyper-h2', 'node:http2'} or any(
                    r.get('case') != 'external_stale_generation_after_close' or r.get('stale_events', 0) < 2 for r in witnesses):
                raise ValueError('missing external stale connection-generation evidence')
        observed = cell['families']
        for row in observed:
            client, family = row.get('client'), row.get('family')
            if client not in ('hyper-h2', 'node:http2', None):
                raise ValueError('unknown public H2 client')
            required = NODE if client == 'node:http2' else PYTHON | POLICY if client == 'hyper-h2' else ONESHOTS
            if family not in required:
                raise ValueError('unknown public H2 family')
        for client, required in [('hyper-h2', PYTHON | POLICY), ('node:http2', NODE), (None, ONESHOTS)]:
            families = [r['family'] for r in observed if r.get('client') == client]
            if set(families) != set(required) or len(families) != len(required):
                raise ValueError('missing/duplicate public H2 client families')
            for row in observed:
                if row.get('client') != client:
                    continue
                if client is None:
                    check_scenarios(row['scenarios'], [ONESHOTS[row['family']][1]])
                else:
                    validate_client(row, 'python' if client == 'hyper-h2' else 'node', row['family'])
