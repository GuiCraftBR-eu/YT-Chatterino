// Lightweight bridge tests; run with node tests/browser-extension.js.
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
let onMessage, onRemoved, onUpdated, socket;
const actions=[];
class FakeSocket {
  static OPEN=1;
  constructor(url) { this.url=url; this.readyState=1; this.sent=[]; socket=this; }
  send(raw) { this.sent.push(JSON.parse(raw)); }
  close() { this.readyState=3; }
}
const api={
  runtime:{getURL:path=>'chrome-extension://extension/'+path,onMessage:{addListener:fn=>onMessage=fn}},
  tabs:{onRemoved:{addListener:fn=>onRemoved=fn},onUpdated:{addListener:fn=>onUpdated=fn},
    update:(tab,settings)=>{actions.push({type:'focus',tab,settings});return Promise.resolve({windowId:2});},
    sendMessage:(tab,message)=>{actions.push({type:'message',tab,message});return Promise.resolve();}},
  windows:{update:(id,settings)=>{actions.push({type:'window',id,settings});return Promise.resolve();}}
};
const context={URL,Map,Set,Number,WebSocket:FakeSocket,chrome:api,
  crypto:{randomUUID:()=>"a5b27e91-a65a-4e23-85d6-d298f4c1ac12"},setTimeout:()=>1,clearTimeout:()=>{}};
vm.createContext(context);
vm.runInContext(fs.readFileSync('browser-extension/background.js','utf8')+'\nthis.normalize=liveUrl;',context);
assert.equal(context.normalize('https://www.twitch.tv/popout/Felps/chat?private=secret'),'https://www.twitch.tv/felps');
assert.equal(context.normalize('https://www.twitch.tv.evil.test/felps'),'');
const sender={tab:{id:7},frameId:0,url:'https://www.twitch.tv/popout/felps/chat'};
onMessage({type:'live-state',url:sender.url,chat:{available:true,pin:{name:'Alice',text:'Hello',cookie:'secret'},
  poll:{text:'Question',options:'Yes · No'},access_token:'secret'}},sender,()=>{});
socket.onopen();socket.onmessage({data:'{"type":"ready"}'});
const state=socket.sent.find(x=>x.type==='live-state');
assert.equal(state.url,'https://www.twitch.tv/felps');
assert.equal(state.chat.pin.text,'Hello');assert.equal(state.chat.poll.options,'Yes · No');
assert(!JSON.stringify(state).includes('secret'));
const send=data=>socket.onmessage({data:JSON.stringify(data)});
send({type:'open-panel',tab:7,channel:'wrong',kind:'poll'});
send({type:'open-panel',tab:7,channel:'felps',kind:'vote'});
send({type:'open-panel',tab:9,channel:'felps',kind:'poll'});
assert.equal(actions.length,0);
send({type:'open-panel',tab:7,channel:'felps',kind:'poll'});
assert(actions.some(x=>x.type==='message'&&x.tab===7&&x.message.kind==='poll'));
assert(actions.every(x=>!['vote','predict','redeem'].includes(x.message?.type)));
onUpdated(7,{status:'loading'});
assert(socket.sent.some(x=>x.type==='closed'&&x.tab===7));
onRemoved(7);
console.log('PASS: browser bridge routing, channel validation, privacy, and navigation');
