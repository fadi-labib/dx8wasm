// SPDX-License-Identifier: GPL-3.0-only
// End-to-end test of the ported MP transport (no engine). Two browser contexts:
//  1) host + join over a room key -> MQTT signaling -> WebRTC DataChannel (peer up)
//  2) DATAGRAM round-trip through gxNet using the SAME calls WebUDP.cpp makes:
//     Bind(handle,vip,port) / Write(handle,dstIP,dstPort,ptr,len) / Read(...).
//     gxNet.send/recv marshal via HEAPU8/HEAP32 — we stand in a scratch ArrayBuffer
//     for the wasm heap, so this exercises the exact byte path the engine will use.
import { createServer } from 'node:http';
import { statSync, createReadStream } from 'node:fs';
import { join } from 'node:path';
import { chromium } from 'playwright';
const WEBMP = '/home/fla/projects/personal/generals-dx8wasm/web-mp';
const PAGE = `<!doctype html><meta charset=utf-8><title>mp-test</title>
<script src="/mp/netbridge.js"></script><script src="/mp/signaling.js"></script><script src="/mp/lobby.js"></script>
<script>
// Stand-in for the wasm heap so gxNet.send/recv (bare HEAPU8/HEAP32) work without the engine.
var __buf=new ArrayBuffer(4096); window.HEAPU8=new Uint8Array(__buf); window.HEAP32=new Int32Array(__buf);
window.__peers=0;window.__status='';window.__ready=false;
(function b(){if(!window.gxLobby)return setTimeout(b,40);
 gxLobby.onPeers=n=>window.__peers=n;gxLobby.onStatus=t=>window.__status=t;window.__ready=true;})();
// WebUDP-style helpers driving the real gxNet:
window.udpBind=function(port){ return gxNet.bind(1, gxNet.myVip>>>0, port>>>0); };
window.udpWrite=function(dstIP,dstPort,str){ // write str at ptr=100, send it
 var enc=new TextEncoder().encode(str); HEAPU8.set(enc,100); gxNet.send(1, dstIP>>>0, dstPort>>>0, 100, enc.length); };
window.udpRead=function(){ // read into ptr=200, srcIP at 300, srcPort at 304
 var n=gxNet.recv(1, 200, 128, 300, 304); if(n<=0) return null;
 return { text:new TextDecoder().decode(HEAPU8.subarray(200,200+n)), srcIP:(HEAP32[300>>2]>>>0), srcPort:HEAP32[304>>2] }; };
</script>`;
const srv = createServer((q,s)=>{ const p=q.url.split('?')[0];
  if(p==='/'){ s.writeHead(200,{'Content-Type':'text/html'}); s.end(PAGE); return; }
  if(p.startsWith('/mp/')){ const f=join(WEBMP,p.slice(4)); try{const st=statSync(f);s.writeHead(200,{'Content-Type':'text/javascript','Content-Length':st.size});createReadStream(f).pipe(s);}catch{s.writeHead(404).end();} return; }
  s.writeHead(404).end();
}).listen(8141,'127.0.0.1');
await new Promise(r=>setTimeout(r,400));
let browser; for (const o of [{channel:'chrome'},{executablePath:'/usr/bin/google-chrome'},{}]) { try{ browser=await chromium.launch({headless:true,args:['--no-sandbox'],...o}); break; }catch{} }
const KEY='WXYZ-2345', PORT=8088, BCAST=0xFFFFFFFF;
const mk = async () => { const c=await browser.newContext(); const pg=await c.newPage(); await pg.goto('http://127.0.0.1:8141/',{waitUntil:'load'}); await pg.waitForFunction('window.__ready===true',{timeout:8000}); return pg; };
const A = await mk(), B = await mk();
console.log('A hosts, B joins', KEY);
await A.evaluate(k=>gxLobby.host(k), KEY);
await new Promise(r=>setTimeout(r,1500));
await B.evaluate(k=>gxLobby.join(k), KEY);
let connected=false;
for (let t=2;t<=24;t+=2){ await new Promise(r=>setTimeout(r,2000));
  const pa=await A.evaluate(()=>window.__peers), pb=await B.evaluate(()=>window.__peers);
  console.log(`  t=${t}s peers A=${pa} B=${pb}`);
  if(pa>=1&&pb>=1){ connected=true; break; } }
if(!connected){ console.log('FAIL: peers never connected'); await browser.close(); srv.close(); process.exit(1); }
console.log('PEER LINK UP. Now datagram round-trip (WebUDP Bind/Write/Read)...');
// Both bind the game port; A broadcasts (LAN host-discovery pattern) -> B reads.
await A.evaluate(p=>udpBind(p), PORT);
await B.evaluate(p=>udpBind(p), PORT);
await new Promise(r=>setTimeout(r,500));
await A.evaluate(([d,p])=>udpWrite(d,p,'GX-HELLO-FROM-HOST'), [BCAST,PORT]);
let got=null;
for(let i=0;i<20;i++){ await new Promise(r=>setTimeout(r,250)); got=await B.evaluate(()=>udpRead()); if(got) break; }
console.log('  B received:', JSON.stringify(got));
// Reverse: B unicasts to A's vip (10.77.0.1).
await B.evaluate(p=>udpWrite(0x0A4D0001, p, 'GX-REPLY-FROM-JOINER'), PORT); // 10.77.0.1
let got2=null;
for(let i=0;i<20;i++){ await new Promise(r=>setTimeout(r,250)); got2=await A.evaluate(()=>udpRead()); if(got2) break; }
console.log('  A received:', JSON.stringify(got2));
const ok = got && got.text==='GX-HELLO-FROM-HOST' && got2 && got2.text==='GX-REPLY-FROM-JOINER';
console.log(ok ? '\n*** SUCCESS: full transport verified — peer link + bidirectional datagrams ***'
               : '\n*** DATAGRAM FAIL ***');
await browser.close(); srv.close(); process.exit(ok?0:1);
