// Narrow independent Node TLS observations for frames hidden by node:http2.
import tls from 'node:tls';
import assert from 'node:assert/strict';
import {setTimeout as delay} from 'node:timers/promises';
export class RawClient {
 constructor(socket,ack){
  this.socket=socket;this.frames=[];this.pending=Buffer.alloc(0);this.token=0;
  socket.on('error',()=>{});
  socket.on('data',bytes=>{
   this.pending=Buffer.concat([this.pending,bytes]);
   while(this.pending.length>=9){const size=this.pending.readUIntBE(0,3);if(this.pending.length<size+9)break;
    const frame={type:this.pending[3],flags:this.pending[4],stream:this.pending.readUInt32BE(5)&2147483647,payload:this.pending.subarray(9,9+size)};
    this.frames.push(frame);assert.ok(this.frames.length<8192);this.pending=this.pending.subarray(size+9);
    if(ack&&frame.type===4&&frame.flags===0)this.send(4,1,0,Buffer.alloc(0));
   }
  });
  socket.write(Buffer.concat([Buffer.from('PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'),this.frameBytes(4,0,0,Buffer.alloc(0))]));
 }
 static async connect(port,options,ack=true){const socket=tls.connect({host:'127.0.0.1',port:Number(port),...options,ALPNProtocols:['h2']});socket.on('error',()=>{});await new Promise((ok,no)=>{socket.once('secureConnect',ok);socket.once('error',no)});assert.equal(socket.alpnProtocol,'h2');return new RawClient(socket,ack)}
 frameBytes(type,flags,stream,payload){const head=Buffer.alloc(9);head.writeUIntBE(payload.length,0,3);head[3]=type;head[4]=flags;head.writeUInt32BE(stream,5);return Buffer.concat([head,payload])}
 send(type,flags,stream,payload){this.socket.write(this.frameBytes(type,flags,stream,payload))}
 openingBytes(stream,path='/installed',method='GET',flags=5){
  const text=value=>Buffer.concat([Buffer.from([value.length]),Buffer.from(value)]);
  const block=Buffer.concat([Buffer.from([method==='POST'?131:130,135,1]),text('a.example'),Buffer.from([4]),text(path)]);
  return this.frameBytes(1,flags,stream,block);
 }
 opening(stream,path='/installed',method='GET',flags=5){this.socket.write(this.openingBytes(stream,path,method,flags))}
 async until(predicate,timeout=3000){const end=performance.now()+timeout;while(!predicate()){assert.ok(performance.now()<end,'raw H2 observation deadline: '+JSON.stringify(this.frames.map(f=>({type:f.type,flags:f.flags,stream:f.stream,payload:f.payload.toString('hex')}))));await delay(1)}}
 async ping(){const token=Buffer.alloc(8);token.writeUInt32BE(++this.token,4);this.send(6,0,0,token);await this.until(()=>this.frames.some(f=>f.type===6&&f.flags===1&&f.payload.equals(token)))}
 reset(stream){const code=Buffer.alloc(4);code.writeUInt32BE(8);this.send(3,0,stream,code)}
 close(){this.socket.destroy()}
}
