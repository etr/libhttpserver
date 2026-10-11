"""Exact basic HTTP/3 product predicate, independent of fixture success."""
import hashlib
from pathlib import Path
from v3_package_matrix import check_dependencies, check_linkage, check_scenarios

CLIENTS = {'aioquic':'1.3.0','quic-go':'v0.55.0'}
BASIC = dict(authenticated_get_trailers=dict(body='hello native h3\n'), unknown_route={},
             incremental_upload_request_trailers=dict(body='incremental installed native h3 upload'),
             tls_peer_metadata={}, parallel_running_handler={}, parallel_suspended_upload={},
             reset_stream_local_once=dict(cancellation_entries=1))
ONESHOTS = {'listener-storage-limit':'LIMIT_H3_UNBOUND','no-udp':'PREBIND_H3_REJECTED no-udp','no-tls':'PREBIND_H3_REJECTED no-tls',
            'no-h3':'PREBIND_H3_REJECTED no-h3','invalid-transport':'PREBIND_H3_REJECTED invalid-transport',
            'wrong-store':'PREBIND_H3_REJECTED wrong-store','psk':'PSK_H3_REJECTED',
            'rollback':'ROLLBACK_H3_UNBOUND rollback','rollback-reverse':'ROLLBACK_H3_UNBOUND rollback-reverse'}

CONFIGURATION = list(ONESHOTS) + [case for c in CLIENTS for case in (c+'_bad_initial_listener_recovery',c+'_mixed_tcp_udp_same_port')] + ['stalled_handshake_capacity_recovery','peer_policy_before_tls','header_timeout_and_recovery','failed_handshake_and_recovery','active_stream_server_destruction','quic_go_active_stream_server_destruction']

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
        check_linkage(cell['dependencies'],cell['linkage'],prefix)
        check_scenarios(cell.get('configuration',[]),CONFIGURATION)
        clients=cell.get('clients',[])
        if len(clients)!=2 or {r.get('client') for r in clients}!=set(CLIENTS): raise ValueError('missing/duplicate independent client')
        for client in clients: validate_client(client)
