// Independent Node http2 client for installed native HTTP/2 witnesses.
import http2 from 'node:http2';
import https from 'node:https';
import http from 'node:http';
import netTls from 'node:tls';
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {setTimeout as delay} from 'node:timers/promises';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {RawClient} from './v3_http2_raw.mjs';
let [port, fixtures, mode='basic', loop='managed'] = process.argv.slice(2);
assert.equal(process.versions.node,'24.15.0');
const watchdog=setTimeout(()=>{console.error(JSON.stringify({error:'Node public H2 observation deadline',rows,stdout,stderr,connectionErrors}));process.exit(1)},15000);
let server, stderr='', stdout='', exit, serverLines=[], lineWaiters=[];
function serverLine(){return serverLines.length?Promise.resolve(serverLines.shift()):new Promise(ok=>lineWaiters.push(ok))}
async function command(text){server.stdin.write(text+'\n');return serverLine()}
if (!/^\d+$/.test(port)) {
 const executable=port;
 const serverMode=mode==='timeouts'||mode==='header'||mode==='websocket'?'timeouts':mode.endsWith('-limit')||['h2-only','h1-bound','mixed','mtls'].includes(mode)?mode:'both';
 server=spawn(executable,[fixtures,loop,serverMode]);
 const lines=createInterface({input:server.stdout});
 lines.on('line',line=>{assert.ok(serverLines.length<256);if(lineWaiters.length)lineWaiters.shift()(line);else serverLines.push(line)});
 server.stdout.on('data',data=>{stdout+=data;assert.ok(stdout.length<65536)});
 server.stderr.on('data',data=>{stderr+=data;assert.ok(stderr.length<65536)});
 exit=new Promise(ok=>server.once('exit',(code,signal)=>ok({code,signal})));
 port=await serverLine();assert.match(port,/^\d+$/);
}
const tls={ca:fs.readFileSync(fixtures+'/root.pem'),servername:'a.example',rejectUnauthorized:true};
if(mode==='mtls'){tls.cert=fs.readFileSync(fixtures+'/client-valid-chain.pem');tls.key=fs.readFileSync(fixtures+'/client-valid-key.pem')}
const c=http2.connect(`https://127.0.0.1:${port}`,{...tls,paddingStrategy:mode==='flow-padded'?http2.constants.PADDING_STRATEGY_MAX:0});
const connectionErrors=[];
c.on('error',error=>connectionErrors.push(String(error)));
c.on('goaway',(code,last)=>connectionErrors.push({goaway:code,last}));
await new Promise((ok,no)=>{c.once('connect',ok);c.once('error',no)});
assert.equal(c.alpnProtocol,'h2');
const rows=[];
const pass=(name,observed={})=>rows.push({case:name,status:'passed',...observed});
function request(path,{method='GET',headers={},open=false,trailers=false,paused=false}={}) {
 const s=c.request({':path':path,':method':method,...headers},{endStream:!open,waitForTrailers:trailers});
 const r={body:Buffer.alloc(0),headers:{},trailers:{},raw:[],rawTrailers:[],ended:false};
 s.on('response',(h,f,raw)=>{r.headers=h;r.raw=raw});
 s.on('trailers',(h,f,raw)=>{r.trailers=h;r.rawTrailers=raw});
 if(paused)s.pause();else s.on('data',b=>{r.body=Buffer.concat([r.body,b])});
 s.on('error',()=>{});
 const done=new Promise(ok=>{s.once('close',()=>{r.reset=s.rstCode;r.ended=true;ok(r)})});
 return {s,r,done};
}
async function get(path,status=200,headers={}){const q=request(path,{headers});const r=await q.done;assert.equal(r.headers[':status'],status,path);return r;}
async function until(check){const end=performance.now()+3000;while(!check()){assert.ok(performance.now()<end,'entry/progress deadline');await delay(1)}}
async function counts(){return (await get('/counts')).body.toString().split(' ').map(Number)}
async function entered(before){const end=performance.now()+3000;while((await counts())[0]===before){assert.ok(performance.now()<end,'handler entry deadline')}}
async function ping(){await new Promise((ok,no)=>c.ping((e)=>e?no(e):ok()))}
function mask(op,payload){const key=Buffer.from([1,2,3,4]);return Buffer.concat([Buffer.from([128|op,128|payload.length]),key,Buffer.from(payload.map((v,i)=>v^key[i%4]))])}
function pairs(raw,key){const values=[];for(let i=0;i<raw.length;i+=2)if(raw[i]===key)values.push(raw[i+1]);return values}
try {
 if(mode==='basic') {
  const hookBefore=server?(await command('stats')).split(' ').map(Number):undefined;
  const h1=await new Promise((ok,no)=>https.get({hostname:'127.0.0.1',port,path:'/installed',...tls,ALPNProtocols:['http/1.1']},r=>{let body='';r.on('data',b=>body+=b);const protocol=r.socket.alpnProtocol;r.on('end',()=>ok([r.statusCode,body,protocol]))}).on('error',no));
  assert.deepEqual(h1,[200,'installed-v3-h2-ok\n','http/1.1']);
  assert.equal((await get('/installed')).body.toString(),'installed-v3-h2-ok\n');pass('alpn_route_parity',{alpn:'h2',h1_alpn:'http/1.1'});
  if(server){const end=performance.now()+3000;let after;do{after=(await command('stats')).split(' ').map(Number);assert.ok(performance.now()<end)}while(after[2]!==hookBefore[2]+2);assert.equal(after[1],hookBefore[1]+2)}pass('hook_policy_parity',{requests:2,completions:2});
  const body=request('/body',{method:'POST',headers:{'x-order':['one','two']},open:true,trailers:true});
  body.s.write('first-');pass('header_admit_incremental');
  assert.equal((await get('/installed')).body.toString(),'installed-v3-h2-ok\n');await ping();assert.equal(body.r.ended,false);pass('slow_upload_healthy_stream',{ping_ack:true});
  body.s.on('wantTrailers',()=>body.s.sendTrailers({'x-trailer':['end-one','end-two']}));body.s.end('second');
  let r=await body.done;assert.equal(r.body.toString(),'first-second');assert.deepEqual(pairs(r.raw,'x-order'),['one','two']);assert.deepEqual(pairs(r.rawTrailers,'x-trailer'),['end-one','end-two']);
  for(const name of ['ordered_headers_trailers','request_body_trailers','response_body_trailers'])pass(name);
  for(const [data,status,name] of [['12345678',200,'route_sync_at_cap'],['123456789',413,'route_sync_over_cap']]){const q=request('/sync',{method:'POST',open:true});q.s.end(data);r=await q.done;assert.equal(r.headers[':status'],status);assert.equal(r.body.toString(),status===200?data:'');pass(name)}
  const rejected=request('/reject',{method:'POST',open:true});await until(()=>!!rejected.r.headers[':status']);assert.equal(rejected.r.headers[':status'],403);rejected.s.close(http2.constants.NGHTTP2_CANCEL);await rejected.done;pass('header_reject_before_body');
  for(const [path,release,name] of [['/suspend','/resume','suspended_stream_control_progress'],['/running','/release-running','running_handler_does_not_own_wire_domain']]){const before=(await counts())[0];const q=request(path);await entered(before);assert.equal(q.r.ended,false);await get('/installed');await ping();await get(release,204);assert.equal((await q.done).headers[':status'],204);pass(name,{held_entered:true,ping_ack:true});if(path==='/suspend')pass('header_suspend_resume')}
  const before=(await counts())[0], held=request('/suspend');await entered(before);held.s.close(http2.constants.NGHTTP2_CANCEL);await held.done;await get('/installed');let signal;const deadline=performance.now()+3000;do{signal=(await get('/resume',204)).headers['x-signal'];assert.ok(performance.now()<deadline)}while(signal!=='2');for(const name of ['suspension_reset_late_signal','reset_stream_local_cancel','late_signal_after_retirement'])pass(name,{late_signal:'cancelled'});
  assert.equal((await get('/source')).body.toString(),'source-data');assert.notEqual((await request('/source-error').done).reset,0);await get('/installed');pass('streaming_source_end_error');
  const overlays=['one','two'].map(v=>request('/definition',{headers:{'x-overlay':v}}));for(let i=0;i<2;i++){r=await overlays[i].done;assert.equal(r.body.toString(),'reused-definition');assert.equal(r.headers['x-overlay'],['one','two'][i]);assert.equal(r.trailers['x-end'],'definition')}pass('response_definition_overlay_isolation');
  r=await get('/meta',204);assert.equal(r.headers['x-protocol'],'h2');assert.equal(r.headers['x-peer'],'127.0.0.1');assert.equal(r.headers['x-client-verified'],'no');pass('tls_peer_metadata');
  await get('/missing',404);await get('/throw',500);await get('/installed');pass('custom_errors_exception_containment');
  await until(()=>c.remoteSettings.enableConnectProtocol);const ws=request('/ws',{method:'CONNECT',headers:{':protocol':'websocket','sec-websocket-version':'13'},open:true});await until(()=>ws.r.headers[':status']);assert.equal(ws.r.headers[':status'],200);ws.s.write(mask(1,Buffer.from('hello')));await get('/installed');await ping();await until(()=>ws.r.body.length>=7);assert.deepEqual(ws.r.body,Buffer.from([129,5,104,101,108,108,111]));pass('rfc8441_echo_parallel',{ping_ack:true});
  const closes=(await counts())[3];ws.s.write(mask(8,Buffer.from([3,232])));await until(()=>ws.s.readableEnded);ws.s.end();await ws.done;const end=performance.now()+3000;while((await counts())[3]===closes)assert.ok(performance.now()<end);assert.equal((await counts())[3],closes+1);pass('rfc8441_close_once',{close_count:1});
 } else if(mode==='timeouts') {
  const before=(await counts())[0], held=request('/suspend');await entered(before);await ping();const started=performance.now();assert.equal((await held.done).reset,8);assert.ok(performance.now()-started<2000);await get('/installed');pass('suspension_timeout');
  const body=request('/body',{method:'POST',open:true});await ping();assert.equal((await body.done).reset,8);await get('/installed');pass('body_idle_timeout_local');
  const slow=request('/large',{paused:true});await until(()=>slow.r.headers[':status']);await get('/installed');await ping();assert.equal((await slow.done).reset,8);pass('slow_response_healthy_stream',{ping_ack:true});pass('write_idle_timeout_local');
 } else if(mode==='running-reset') {
  const before=await counts(),q=request('/running');await entered(before[0]);q.s.close(http2.constants.NGHTTP2_CANCEL);await q.done;await ping();assert.equal((await counts())[5],before[5]);await get('/installed');await get('/release-running',204);const end=performance.now()+3000;while((await counts())[5]===before[5])assert.ok(performance.now()<end);assert.equal((await counts())[5],before[5]+1);pass('reset_running_resume_retains_frame',{destroyed:1});
 } else if(mode==='drain-finish'||mode==='drain-timeout') {
  const finish=mode==='drain-finish', before=(await counts())[0], held=request('/suspend');
  await entered(before);
  let acceptedBody;
  if(finish){const bodyBefore=(await counts())[0];acceptedBody=request('/body',{method:'POST',open:true});acceptedBody.s.write('accepted-drain');await entered(bodyBefore)}
  let ws,closes;if(!finish){await until(()=>c.remoteSettings.enableConnectProtocol);ws=request('/ws',{method:'CONNECT',headers:{':protocol':'websocket','sec-websocket-version':'13'},open:true});await until(()=>ws.r.headers[':status']);assert.equal(ws.r.headers[':status'],200);closes=Number((await command('stats')).split(' ')[3])}
  const goaway=[];c.on('goaway',(code,last)=>goaway.push({code,last}));
  const draining=request('/handler-drain',{headers:finish?{'x-finish':'yes'}:{}});
  const r=await draining.done;assert.equal(r.headers[':status'],204);assert.equal(r.headers['x-begin'],'0');assert.equal(r.headers['x-wait'],'9');
  if(server)assert.equal(await serverLine(),'HANDLER_DRAIN_RETURNED');
  await until(()=>goaway.length>=2);assert.deepEqual(goaway[0],{code:0,last:2147483647});assert.deepEqual(goaway.at(-1),{code:0,last:draining.s.id});
  pass('handler_drain_ack');pass('counted_handler_wait_rejected',{outcome:'would_deadlock'});
  if(finish){assert.equal((await held.done).headers[':status'],204);acceptedBody.s.end();assert.equal((await acceptedBody.done).body.toString(),'accepted-drain');pass('goaway_finishes_accepted',{accepted_upload_held_through_goaway_barrier:true})}
  else {
   assert.throws(()=>c.request({':path':'/installed'}));pass('goaway_accept_cutoff',{cutoff:draining.s.id});
   const started=performance.now();await new Promise(ok=>c.once('close',ok));assert.ok(performance.now()-started<3000);pass('drain_deadline_without_waiter');if(server){const end=performance.now()+3000;let counts;do{counts=(await command('stats')).split(' ').map(Number);assert.ok(performance.now()<end)}while(counts[4]||counts[5]||counts[1]!==counts[2]);assert.equal(counts[3],closes+1)}pass('drain_cancels_requests_ws',{close_count:1});
  }
 } else if(mode==='flow'||mode==='flow-padded') {
  const before=(await counts())[0], held=request('/held-body',{method:'POST',open:true});await entered(before);await ping();
  const baseline=c.state.remoteWindowSize, bytes=mode==='flow'?16384:4096;
  await new Promise((ok,no)=>held.s.write(Buffer.alloc(bytes,112),e=>e?no(e):ok()));await ping();
  assert.equal(c.state.remoteWindowSize,baseline-bytes,'credit before application consume');
  if(mode==='flow'){await get('/resume',204);held.s.end();assert.equal((await held.done).headers['x-consumed'],String(bytes));await ping();assert.equal(c.state.remoteWindowSize,baseline);pass('consumed_credit_exact',{consumed:bytes})}
  else {held.s.close(http2.constants.NGHTTP2_CANCEL);await held.done;await ping();await until(()=>c.state.remoteWindowSize===baseline);await ping();assert.equal(c.state.remoteWindowSize,baseline);pass('padded_data_credit_exact',{padding_strategy:'max'});pass('reset_unread_credit_once',{unread:bytes})}
 } else if(mode==='queued') {
  const raw=await RawClient.connect(port,tls);await raw.ping();
  const before=Number((await command('stats')).split(' ')[0]);
  for(const id of [1,3,5])raw.opening(id,'/running');
  const end=performance.now()+3000;while(Number((await command('stats')).split(' ')[0])<before+3){assert.ok(performance.now()<end);await delay(1)}
  const code=Buffer.alloc(4);code.writeUInt32BE(8);
  raw.socket.write(Buffer.concat([raw.openingBytes(7,'/running'),raw.frameBytes(3,0,7,code)]));await raw.ping();
  assert.equal(Number((await command('stats')).split(' ')[0]),before+3);assert.equal(await command('release'),'RELEASED');
  for(const id of [1,3,5])await raw.until(()=>raw.frames.some(f=>f.type===1&&f.stream===id&&(f.flags&1)));
  raw.close();await get('/installed');pass('reset_queued_resume_never_enters',{queued_entry_count:0});
 } else if(mode==='disconnect') {
  const before=(await command('stats')).split(' ').map(Number);request('/suspend');request('/body',{method:'POST',open:true});
  const end=performance.now()+3000;while(Number((await command('stats')).split(' ')[0])<before[0]+2){assert.ok(performance.now()<end);await delay(1)}
  // A connection can briefly have no projected I/O between completion and
  // rearm. Capture its actual registration before disconnecting; the listener
  // alone cannot prove a retired connection generation is inert.
  let captured;
  do{await ping();captured=Number((await command('snapshot')).split(' ')[1]);assert.ok(performance.now()<end,'connection readiness capture deadline')}while(loop==='external'&&captured<2);
  c.destroy();let after;do{after=(await command('stats')).split(' ').map(Number);assert.ok(performance.now()<end);await delay(1)}while(after[4]||after[5]||after[1]!==after[2]);
  assert.equal(after[1]-before[1],2);assert.equal(after[2]-before[2],2);assert.equal(await command('stale'),'STALE_GENERATIONS_INERT');pass('disconnect_cancels_connection_streams',{cancelled:2});
 } else if(mode.endsWith('-limit')) {
  await get('/installed');
  if(mode==='stream-limit'){
   const raw=await RawClient.connect(port,tls);try{
    raw.opening(1,'/suspend');raw.opening(3,'/body','POST',4);await raw.ping();raw.opening(5);
    await raw.until(()=>raw.frames.some(f=>f.type===3&&f.stream===5));assert.equal(raw.frames.find(f=>f.type===3&&f.stream===5).payload.readUInt32BE(),7);await raw.ping();
    raw.reset(1);raw.reset(3);await raw.ping();raw.opening(7);await raw.until(()=>raw.frames.some(f=>f.type===0&&f.stream===7));
    assert.equal(Buffer.concat(raw.frames.filter(f=>f.type===0&&f.stream===7).map(f=>f.payload)).toString(),'installed-v3-h2-ok\n');pass('stream_limit_control_progress',{ping_ack:true});
   }finally{raw.close()}
  } else if(mode==='connection-limit'||mode==='timer-limit'){
   const other=http2.connect(`https://127.0.0.1:${port}`,tls);other.on('error',()=>{});let connected=false;other.on('connect',()=>{connected=true});await new Promise(ok=>other.once('close',ok));assert.equal(connected,false);await get('/installed');await ping();c.destroy();
   const end=performance.now()+3000;while(Number((await command('stats')).split(' ')[4])){assert.ok(performance.now()<end);await delay(1)}
   const recovered=http2.connect(`https://127.0.0.1:${port}`,tls);recovered.on('error',()=>{});await new Promise((ok,no)=>{recovered.once('connect',ok);recovered.once('error',no)});let body='';const s=recovered.request({':path':'/installed'});s.on('data',b=>body+=b);await new Promise(ok=>s.once('end',ok));assert.equal(body,'installed-v3-h2-ok\n');recovered.destroy();pass(mode==='connection-limit'?'listener_connection_limit_recovery':'timer_limit_retirement');
  } else if(mode==='body-limit'){
   const held=[];for(let i=0;i<8;i++){held.push(request('/body',{method:'POST',open:true}));await ping();if(held.some(q=>q.r.reset))break}
   await until(()=>held.some(q=>q.r.reset));await ping();for(const q of held)q.s.close();for(const q of held)await q.done;await get('/installed');pass('body_limit_bounded',{attempted:held.length});
  } else if(mode==='output-limit'){
   c.settings({initialWindowSize:0});await new Promise(ok=>c.once('localSettings',ok));const held=Array.from({length:8},()=>request('/large',{paused:true}));
   await until(()=>held.some(q=>q.r.reset));assert.ok(Number((await command('stats')).split(' ')[6])<=131072);await ping();for(const q of held)q.s.close();for(const q of held)await q.done;c.settings({initialWindowSize:65535});await new Promise(ok=>c.once('localSettings',ok));await get('/installed');pass('output_limit_control_progress',{ping_ack:true});
  } else if(mode==='header-limit'){
   const errors=[];c.on('goaway',code=>errors.push(code));const overflow=request('/installed',{headers:{'x-large':'z'.repeat(4096)}});const r=await overflow.done;assert.ok(r.reset||errors.includes(9));pass('header_limit_bounded',{bytes:4096});
  }
  c.destroy();const end=performance.now()+3000;let state;do{state=(await command('stats')).split(' ').map(Number);assert.ok(performance.now()<end);await delay(1)}while(state[4]||state[5]||state[6]);pass('capacity_returns_to_baseline',{connections:0,streams:0,output_bytes:0});
 } else if(mode==='websocket'){
  await until(()=>c.remoteSettings.enableConnectProtocol);
  const invalid=request('/ws',{method:'CONNECT',headers:{':protocol':'websocket','sec-websocket-version':'12'},open:true});
  await until(()=>invalid.r.headers[':status']);assert.equal(invalid.r.headers[':status'],426);invalid.s.close();await invalid.done;pass('rfc8441_invalid_negotiation');
  c.settings({initialWindowSize:0});await new Promise(ok=>c.once('localSettings',ok));
  const ws=request('/ws',{method:'CONNECT',headers:{':protocol':'websocket','sec-websocket-version':'13'},open:true});await until(()=>ws.r.headers[':status']);assert.equal(ws.r.headers[':status'],200);
  const before=Number((await command('stats')).split(' ')[3]);
  for(let i=0;i<64;i++)ws.s.write(mask(2,Buffer.alloc(120,105)));
  await get('/meta',204);await ping();assert.ok(Number((await command('stats')).split(' ')[6])<=4194304);pass('rfc8441_backpressure',{frames:64,ping_ack:true});
  assert.equal((await ws.done).reset,8);await get('/meta',204);
  const end=performance.now()+3000;while(Number((await command('stats')).split(' ')[3])===before){assert.ok(performance.now()<end);await delay(1)}
  assert.equal(Number((await command('stats')).split(' ')[3]),before+1);pass('rfc8441_reset_deadline',{close_count:1});
 } else if(mode==='header'||mode==='settings'){
  await ping();
  const raw=await RawClient.connect(port,tls,mode!=='settings');try{
   if(mode==='header'){await raw.ping();raw.opening(1,'/installed','GET',1)}
   const started=performance.now();await raw.until(()=>raw.frames.some(f=>f.type===7),mode==='settings'?12000:3000);
   const f=raw.frames.find(f=>f.type===7);assert.equal(f.payload.readUInt32BE(4),mode==='settings'?4:0);assert.ok(performance.now()-started<(mode==='settings'?12000:2000));pass(mode+'_timeout_idle');
  }finally{raw.close()}
 } else if(mode==='stop'){
  request('/handler-stop');assert.equal(await command('ack'),'HANDLER_STOP_RETURNED');pass('handler_stop_ack');
 } else if(mode==='mixed'){
  const ports=(await command('ports')).split(' ').map(Number);assert.equal(new Set(ports).size,3);await get('/installed');
  const other=http2.connect(`https://127.0.0.1:${ports[1]}`,tls);other.on('error',()=>{});
  await new Promise((ok,no)=>{other.once('connect',ok);other.once('error',no)});assert.equal(other.alpnProtocol,'h2');
  const q=other.request({':path':'/installed'});let body='';q.on('data',b=>body+=b);await new Promise((ok,no)=>{q.once('end',ok);q.once('error',no)});assert.equal(body,'installed-v3-h2-ok\n');other.destroy();
  const plain=await new Promise((ok,no)=>http.get({hostname:'127.0.0.1',port:ports[2],path:'/installed'},r=>{let data='';r.on('data',b=>data+=b);r.on('end',()=>ok([r.statusCode,data]))}).on('error',no));assert.deepEqual(plain,[200,'installed-v3-h2-ok\n']);pass('mixed_h1_h2_listeners');
 } else if(mode==='cipher'){
  await get('/installed');const before=(await command('stats')).split(' ').map(Number);
  await new Promise((ok,no)=>{const peer=netTls.connect({host:'127.0.0.1',port,...tls,ALPNProtocols:['h2'],minVersion:'TLSv1.2',maxVersion:'TLSv1.2',ciphers:'ECDHE-RSA-AES128-SHA256'},()=>peer.write(Buffer.concat([Buffer.from('PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'),Buffer.from([0,0,0,4,0,0,0,0,0])])));peer.on('data',()=>no(Error('forbidden H2 CBC produced protocol bytes')));peer.on('error',()=>{});peer.once('close',ok)});
  const after=(await command('stats')).split(' ').map(Number);assert.deepEqual(after.slice(1,3),before.slice(1,3));await get('/installed');pass('h2_forbidden_cipher_before_hooks');
 } else if(mode==='h2-only'){await get('/installed');pass('h2_only_alpn')
 } else if(mode==='h1-bound') {throw Error('h1-bound requires an H1 client entry')
 } else if(mode==='mtls'){const r=await get('/meta',204);assert.equal(r.headers['x-client-verified'],'yes');pass('h2_mtls_metadata')
 } else if(mode==='policy'){
  await get('/installed');assert.equal(await command('deny'),'POLICY deny');const r=await request('/installed').done;assert.ok(r.reset||r.headers[':status']===403);assert.equal(await command('allow'),'POLICY allow');await get('/installed');pass('h2_peer_policy_live_and_accept');assert.equal(await command('rotate'),'ROTATE 0');await get('/installed');pass('h2_live_policy_publication');
 } else {throw Error('unknown Node witness mode: '+mode)}
 c.destroy();
 if(server){server.stdin.end('stop\n');const result=await exit;assert.equal(result.code,0,stderr);assert.equal(result.signal,null);if(mode==='drain-finish')assert.match(stderr,/RETAINED_TICKET 0 0/);if(mode==='drain-timeout')assert.match(stderr,/RETAINED_TICKET 1 [1-9]\d*/);if(mode.startsWith('drain-'))pass('ticket_after_server_destruction')}
 console.log(JSON.stringify({client:'node:http2',version:process.versions.node,nghttp2:process.versions.nghttp2,alpn:'h2',scenarios:rows,server:server?{executable:process.argv[2],stdout,stderr,exit:0}:undefined}));
} catch(error){console.error(JSON.stringify({mode,connectionErrors,server_stdout:stdout,server_stderr:stderr}));throw error}
finally {c.destroy();if(server&&server.exitCode===null)server.kill();clearTimeout(watchdog)}
