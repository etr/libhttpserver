#!/usr/bin/env node
// Node 24.15.0 uses nghttp2 for HTTP/2 and HPACK. Extended CONNECT is native.
import assert from 'node:assert/strict';
import http2 from 'node:http2';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { once } from 'node:events';

assert.equal(process.versions.node, '24.15.0');
const at = process.argv.indexOf('--fixture');
assert(at >= 0);
const fixture = spawn(process.argv[at + 1], [], { stdio: ['pipe', 'pipe', 'inherit'] });
const lines = createInterface({ input: fixture.stdout });
const queue = []; const waiters = [];
lines.on('line', line => { if (waiters.length) waiters.shift()(line); else queue.push(line); });
const deadline = (promise, label) => {
  let timer;
  return Promise.race([promise, new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(`${label} timeout`)), 6000);
  })]).finally(() => clearTimeout(timer));
};
const line = () => deadline(queue.length ? Promise.resolve(queue.shift()) : new Promise(resolve => waiters.push(resolve)), 'fixture line');
const command = value => fixture.stdin.write(`${value}\n`);
const stats = async () => {
  command('STATS'); const fields = (await line()).split(' '); assert.equal(fields.shift(), 'STATS');
  assert.equal(fields.length, 5); return fields.map(Number);
};
let client; let result;
try {
const ready = (await line()).split(' '); assert.equal(ready[0], 'READY');
client = http2.connect(`https://127.0.0.1:${ready[1]}`, {
  rejectUnauthorized: false, servername: 'a.example', settings: { initialWindowSize: 32 },
});
client.on('error', error => { console.error(error); });
const ping = () => deadline(new Promise((resolve, reject) => client.ping((error) => error ? reject(error) : resolve())), 'PING');
const request = (path, method = 'GET', endStream = true, extra = {}) => {
  const stream = client.request({ ':method': method, ':scheme': 'https', ':authority': 'a.example', ':path': path, ...extra }, { endStream });
  stream.on('error', error => { if (error.code !== 'ERR_HTTP2_STREAM_ERROR') console.error(error); });
  const response = deadline(once(stream, 'response'), `response ${path}`).then(([headers]) => headers[':status']);
  // A cancelled stream has no response; the scenario owns that result.
  response.catch(() => {});
  return { stream, response };
};
const complete = async obj => {
  obj.stream.resume(); await deadline(once(obj.stream, 'end'), 'stream completion');
  return obj.response;
};
const health = async () => assert.equal(await complete(request('/health')), 204);
const masked = (opcode, data) => {
  const mask = Buffer.from([1, 2, 3, 4]); assert(data.length < 126);
  return Buffer.concat([Buffer.from([128 | opcode, 128 | data.length]), mask, Buffer.from(data.map((x, i) => x ^ mask[i % 4]))]);
};
  await deadline(once(client, 'connect'), 'TLS connect');
  assert.equal(client.alpnProtocol, 'h2');
  if (!client.remoteSettings.enableConnectProtocol) await deadline(once(client, 'remoteSettings'), 'SETTINGS');
  assert.equal(client.remoteSettings.enableConnectProtocol, true);
  const held = request('/hold'); let heldResponded = false;
  held.response.then(() => { heldResponded = true; });
  await health(); await ping(); assert.equal(heldResponded, false);
  command('RELEASE'); assert.equal(await complete(held), 200);
  const reset = request('/upload-hold', 'POST', false);
  await ping(); const before = (await stats())[2];
  reset.stream.close(http2.constants.NGHTTP2_CANCEL);
  await health(); await ping(); assert.equal((await stats())[2], before + 1);
  await health(); await ping(); assert.equal((await stats())[2], before + 1);
  const uploads = Array.from({ length: 5 }, () => request('/upload-hold', 'POST', false, { 'content-length': '32768' }));
  for (const upload of uploads) upload.stream.end(Buffer.alloc(32768, 'U'));
  await ping(); await ping();
  let stalled;
  for (let round = 0; round < 64; ++round) {
    await ping(); stalled = await stats();
    if (stalled[4] === 65535) break;
  }
  assert.equal(stalled[3], 0); assert.equal(stalled[4], 65535);
  // A second round-trip proves receipt of all eligible DATA and no app consumption.
  await ping(); const again = await stats(); assert.equal(again[4], stalled[4]);
  command('UPLOAD');
  assert.deepEqual(await Promise.all(uploads.map(complete)), [200, 200, 200, 200, 200]);
  assert.equal((await stats())[3], 5 * 32768);
  const large = request('/large'); assert.equal(await large.response, 200);
  // Leave the readable stream paused. nghttp2 grants no further stream credit.
  await deadline(once(large.stream, 'readable'), 'small response DATA');
  await ping(); assert.equal(large.stream.readableLength, 32);
  await deadline(new Promise((resolve, reject) => client.settings({ initialWindowSize: 131072 }, error => error ? reject(error) : resolve())), 'response window grant');
  const received = []; large.stream.on('data', bytes => received.push(bytes)); large.stream.resume();
  await deadline(once(large.stream, 'end'), 'large response');
  assert.deepEqual(Buffer.concat(received), Buffer.alloc(131072, 'L'));
  const ws = request('/ws', 'CONNECT', false, { ':protocol': 'websocket', 'sec-websocket-version': '13' });
  assert.equal(await ws.response, 200);
  const bytes = []; let available = Buffer.alloc(0); const callbacks = [];
  ws.stream.on('data', chunk => { bytes.push(chunk); available = Buffer.concat(bytes); while (callbacks.length) callbacks.shift()(); });
  const waitBytes = async n => {
    while (available.length < n) await deadline(new Promise(resolve => callbacks.push(resolve)), 'WS DATA');
  };
  await health();
  let expected = Buffer.alloc(0);
  for (const [opcode, payload] of [[1, Buffer.from('hello')], [2, Buffer.from([0, 255])], [9, Buffer.from('ping')]]) {
    ws.stream.write(masked(opcode, payload));
    expected = Buffer.concat([expected, Buffer.from([128 | (opcode === 9 ? 10 : opcode), payload.length]), payload]);
    await waitBytes(expected.length); assert.deepEqual(available, expected);
  }
  const ended = deadline(once(ws.stream, 'end'), 'WS Close');
  ws.stream.end(masked(8, Buffer.from([3, 232]))); await ended;
  assert.deepEqual(available, Buffer.concat([expected, Buffer.from([136, 2, 3, 232])]));
  await health();
  result = { stack: 'node:http2', version: process.versions.node, nghttp2: process.versions.nghttp2,
    alpn: 'h2', scenarios: ['multiplexing', 'reset', 'upload_flow', 'response_flow', 'websocket'],
    upload_stalled_bytes: stalled[4], uploaded_bytes: 5 * 32768, response_stalled_bytes: 32,
    response_bytes: 131072, cancel_count: 1, ws_clean_close: true };
} finally {
  client?.destroy();
  if (fixture.exitCode === null) command('STOP');
  const status = fixture.exitCode === null ? await deadline(once(fixture, 'exit'), 'fixture exit').catch(async error => { fixture.kill(); await once(fixture, 'exit'); throw error; }) : [fixture.exitCode];
  assert.equal(status[0], 0); lines.close();
}
console.log(JSON.stringify(result));
