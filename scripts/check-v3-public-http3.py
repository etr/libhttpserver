#!/usr/bin/env python3
"""Installed public-only H3 transport: independent clients and native loops."""
import socket
import argparse, asyncio, hashlib, importlib.metadata, importlib.util, json, os, shlex, shutil, ssl, sys, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/lib'))
from v3_package_commands import Commands, runtime_environment
from v3_package_integration import compile_probe
from v3_package_matrix import check_linkage
from v3_package_process import Consumer
from v3_public_http3_inventory import CLIENTS, ONESHOTS, validate_client, validate_matrix
from v3_public_http2_h2 import Client as H2Client

def require(value,message):
    if not value: raise ValueError(message)
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module

def scenarios(executable,loop,args,directory,env,fixtures,commands,client):
    rows=[]
    if args.tls=='no':
        output=commands([executable,fixtures,loop],directory,env)
        require(output.strip()=='TLS_OFF_H3_REJECTED','same-source TLS-off typed H3 rejection')
        return [],[dict(case='tls_off_h3_rejected',status='passed')]
    for name in CLIENTS:
        process_receipt=directory/(name+'-basic-process.json')
        with Consumer([str(executable),str(fixtures),loop],directory,env,process_receipt) as child:
            port=child.port()
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as malformed:
                for packet in (b'bad Initial',b'\xc0'+b'\x00'*63,b'\xc0'+b'\xff'*1199):
                    malformed.sendto(packet,('127.0.0.1',port))
            if name=='aioquic':
                result=dict(client=name,version=importlib.metadata.version(name),alpn='h3',verified=True,
                            quic_version=1,tls_version='TLSv1.3',early_data=False, retry_count=1,
                            scenarios=asyncio.run(client.basic(port,fixtures)))
            else:
                output=commands([args.quic_go,'-matrix','public','-port',str(port),'-ca',fixtures/'root.pem'],directory,env,timeout=20)
                result=json.loads(output.strip().splitlines()[-1])
            validate_client(result); child.stop()
        process=json.loads(process_receipt.read_text())
        require('H3_ACCEPT_TOTAL 1 0' in process['stderr'],'malformed Initial reached connection admission')
        require(process['exit']==0 and 'H3_STOP ' in process['stderr'],'public owner shutdown absent')
        if loop=='external': require('INACTIVE_DRIVER_REJECTED' in process['stderr'],'inactive external readiness driver')
        rows.append(dict(**result,process_receipt=str(process_receipt)))
    checks=[dict(case=name+'_bad_initial_listener_recovery',status='passed') for name in CLIENTS]
    for mode,expected in ONESHOTS.items():
        output=commands([executable,fixtures,loop,mode],directory,env)
        require(output.strip()==expected,'prebind/rollback predicate '+mode)
        checks.append(dict(case=mode,status='passed'))
    for family,mode in [('policy','policy'),('timeouts','timeouts'),('failed-handshake','basic'),('stalled','stalled'),('destruction','destroy')]:
        process_receipt=directory/(family+'-process.json')
        with Consumer([str(executable),str(fixtures),loop,mode],directory,env,process_receipt) as child:
            port=child.port()
            if family=='policy': observations=asyncio.run(client.rejection(port,fixtures))
            elif family=='timeouts': observations=asyncio.run(client.timeout_recovery(port,fixtures))
            elif family=='failed-handshake': observations=asyncio.run(client.failed_handshake(port,fixtures))
            elif family=='stalled': observations=asyncio.run(client.stalled_handshake(port,fixtures))
            else: observations=asyncio.run(client.active_stop(port,fixtures,child.stop))
            if family!='destruction':child.stop()
        process=json.loads(process_receipt.read_text())
        if family=='stalled':require('H3_ACCEPT_TOTAL 2 0' in process['stderr'] and 'H3_TLS_COMPLETED 1' in process['stderr'],'stalled handshake retired before recovery')
        if family=='policy':require('POLICY_REFUSAL ' in process['stderr'],'admission hook before TLS')
        if family=='destruction':require('H3_DESTROYED ' in process['stderr'],'active destruction completion')
        checks.extend(observations)
    process_receipt=directory/'quic-go-destruction-process.json'
    with Consumer([str(executable),str(fixtures),loop,'destroy'],directory,env,process_receipt) as server:
        port=server.port()
        with Consumer([str(args.quic_go),'-matrix','public-stop','-port',str(port),'-ca',str(fixtures/'root.pem')],directory,env,directory/'quic-go-active-process.json') as independent:
            require(independent.line()=='H3_ACTIVE','independent active stream absent')
            server.stop()
            result=json.loads(independent.line());require(result.get('status')=='passed' and result.get('client')=='quic-go','independent destruction witness')
            require(independent.process.wait(timeout=5)==0,'independent destruction process failed')
        checks.append(dict(case='quic_go_active_stream_server_destruction',status='passed'))
    for name in CLIENTS:
        with Consumer([str(executable),str(fixtures),loop,'mixed'],directory,env,directory/(name+'-mixed-process.json')) as child:
            port=child.port();mixed_tcp(port,fixtures)
            if name=='aioquic':asyncio.run(client.basic(port,fixtures))
            else:
                result=json.loads(commands([args.quic_go,'-matrix','public','-port',str(port),'-ca',fixtures/'root.pem'],directory,env,timeout=20).strip().splitlines()[-1])
                validate_client(result)
            child.stop()
        checks.append(dict(case=name+'_mixed_tcp_udp_same_port',status='passed'))
    return rows,checks

def mixed_tcp(port,fixtures):
    context=ssl.create_default_context(cafile=str(fixtures/'root.pem'))
    context.set_alpn_protocols(['http/1.1'])
    with context.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=3),server_hostname='a.example') as peer:
        require(peer.selected_alpn_protocol()=='http/1.1','mixed TLS TCP H1 ALPN')
        peer.sendall(b'GET /hello HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n')
        data=bytearray()
        while chunk:=peer.recv(4096):
            data.extend(chunk);require(len(data)<=65536,'mixed H1 response bound')
        require(b'hello native h3\n' in data and b'native-fin' in data,'mixed H1 route/trailer')
    client=H2Client(port,fixtures)
    try:
        result=client.get('/hello')
        require(result['body']==b'hello native h3\n','mixed TLS TCP H2 route')
    finally:client.close()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('prefix','provider-prefix','build','output'): parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--source',type=Path,default=ROOT)
    parser.add_argument('--tls',choices=('yes','no'),required=True)
    parser.add_argument('--compiler',default='/usr/bin/clang++-18')
    parser.add_argument('--quic-go',type=Path,required=True)
    parser.add_argument('--sanitize',default='')
    args=parser.parse_args(); args.output=args.output.resolve(); args.output.mkdir(parents=True,exist_ok=False)
    prefix,source,build,provider=[p.resolve() for p in (args.prefix,args.source,args.build,args.provider_prefix)]
    commands=Commands(args.output); env=runtime_environment(os.environ,prefix,provider,[])
    env.update(PKG_CONFIG_PATH='',PKG_CONFIG_LIBDIR=str(prefix/'lib/pkgconfig'))
    receipt=dict(version=1,predicate='installed-public-http3',tls=args.tls,prefix=str(prefix),source=str(source),build=str(build),
                 cells=[],command=sys.argv,environment={k:env.get(k) for k in ('ASAN_OPTIONS','UBSAN_OPTIONS','TSAN_OPTIONS','LD_LIBRARY_PATH')})
    try:
        audit=load(source/'scripts/audit-v3-installed-package.py','audit')
        client=load(source/'test/installed/v3_http3_aioquic.py','public_h3_client') if args.tls=='yes' else None
        receipt['revision']=commands(['git','rev-parse','HEAD'],source,env).strip()
        receipt['branch']=commands(['git','branch','--show-current'],source,env).strip()
        receipt['compiler']=commands(shlex.split(args.compiler)+['--version'],source,env)
        receipt['python']=dict(executable=sys.executable,version=sys.version,ssl=ssl.OPENSSL_VERSION,sha256=digest(Path(sys.executable)))
        if args.tls=='yes':
            require(importlib.metadata.version('aioquic')==CLIENTS['aioquic'],'aioquic pin')
            receipt['clients']=dict(quic_go=dict(executable=str(args.quic_go),sha256=digest(args.quic_go),identity=json.loads(commands([args.quic_go,'--identity'],source,env))),aioquic=dict(version='1.3.0'))
            import aioquic
            receipt['clients']['aioquic']['source_hashes']={str(p):digest(p) for p in Path(aioquic.__file__).parent.rglob('*') if p.is_file() and p.suffix in ('.py','.so')}
            receipt['provider']={str(p):digest(p) for pattern in ('libssl.so*','libcrypto.so*','libssl.a','libcrypto.a') for p in (provider/'lib').glob(pattern) if p.is_file()}
        inputs=sorted(p for p in (source/'src').rglob('*') if p.suffix in ('.cpp','.hpp'))
        inputs += [source/p for p in ('src/Makefile.am','test/Makefile.am','configure.ac','test/installed/v3_http3_consumer.cpp','test/installed/v3_http3_aioquic.py','test/integ/http3-client-go/public.go','scripts/check-v3-public-http3.py','scripts/lib/v3_public_http3_inventory.py')]
        inputs += sorted((source/'test/integ/http3-client-go').glob('*.go')) + [source/'test/integ/http3-client-go/go.mod',source/'test/integ/http3-client-go/go.sum']
        receipt['source_hashes']={str(p.relative_to(source)):digest(p) for p in inputs}
        receipt['source_identity']=hashlib.sha256(json.dumps(receipt['source_hashes'],sort_keys=True).encode()).hexdigest()
        receipt['library_hashes']={str(p):digest(p) for p in (prefix/'lib').glob('libhttpserver.*') if p.is_file()}
        require(Path(commands(['pkg-config','--variable=pcfiledir','libhttpserver'],args.output,env).strip()).resolve()==prefix/'lib/pkgconfig','installed pkg-config identity')
        cflags=shlex.split(commands(['pkg-config','--cflags','libhttpserver'],args.output,env))
        require(not audit.consumer_flag_violations(cflags,(source,build)),'compile flag contamination')
        directory=Path(tempfile.mkdtemp(prefix='task196-installed-')); receipt['consumer_directory']=str(directory)
        for name in ('test/installed/v3_http3_consumer.cpp','examples/v3_host_poll.hpp'): shutil.copyfile(source/name,directory/Path(name).name)
        fixtures=directory/'fixtures';shutil.copytree(source/'test/data/tls_credentials',fixtures)
        receipt['fixture_hashes']={p.name:digest(p) for p in fixtures.iterdir() if p.is_file()}
        for linkage in ('shared','static'):
            libraries=shlex.split(commands(['pkg-config','--libs']+(['--static'] if linkage=='static' else [])+['libhttpserver'],directory,env))
            if linkage=='static':libraries=[str(prefix/'lib/libhttpserver.a') if f=='-lhttpserver' else f for f in libraries]
            libraries += ['-pthread','-Wl,-rpath,'+str(prefix/'lib'),'-Wl,-rpath,'+str(provider/'lib')]+shlex.split(args.sanitize)
            executable=directory/linkage
            provenance=compile_probe(args.compiler,[directory/'v3_http3_consumer.cpp'],cflags+shlex.split(args.sanitize),libraries,executable,commands,env,source,build,prefix,False)
            graph=audit.dependency_graph([str(executable)]);check_linkage(graph,linkage,prefix)
            require(not audit.graph_violations(graph,'strict',args.tls,'aggregate',audit.load_policy(),provider),'dependency graph violations')
            for loop in ('managed','external'):
                out=args.output/(linkage+'-'+loop);out.mkdir()
                cell=dict(linkage=linkage,loop=loop,compile=provenance,dependencies=graph);receipt['cells'].append(cell)
                cell['clients'],cell['configuration']=scenarios(executable,loop,args,out,env,fixtures,commands,client)
                cell['status']='passed';print('PASS installed H3',args.tls,linkage,loop,flush=True)
        receipt['status']='passed'
        if args.tls=='yes':validate_matrix(receipt)
    except BaseException as error:
        receipt.update(status='failed',error=str(error));raise
    finally:(args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')

if __name__=='__main__':main()
