// Exercise request routing through both app/OBS sockets without a browser account.
const assert=require('assert'), fs=require('fs'), vm=require('vm');
let listener, removed, resolveVote, rejectVote;
const sockets=[], calls=[];
class Socket {
  static OPEN=1;
  constructor(url){this.url=url;this.readyState=1;this.sent=[];sockets.push(this);}
  send(raw){this.sent.push(JSON.parse(raw));}
  close(){this.readyState=3;}
}
const api={runtime:{getURL:p=>'chrome-extension://test/'+p,onMessage:{addListener:f=>listener=f}},
  tabs:{onRemoved:{addListener:f=>removed=f},onUpdated:{addListener:()=>{}},
    sendMessage:(tab,message)=>{calls.push({tab,message});return new Promise((resolve,reject)=>{resolveVote=resolve;rejectVote=reject;});},
    update:()=>Promise.resolve({windowId:1})},windows:{update:()=>Promise.resolve()}};
const context={URL,Map,Set,Number,WebSocket:Socket,chrome:api,crypto:{randomUUID:()=>"a5b27e91-a65a-4e23-85d6-d298f4c1ac12"},setTimeout:()=>1,clearTimeout:()=>{}};
vm.createContext(context);vm.runInContext(fs.readFileSync('browser-extension/background.js','utf8'),context);
const sender={tab:{id:7},frameId:0,url:'https://www.twitch.tv/felps'};
const poll={text:'Which game?',state:'open',choices:[{id:'choice-1',label:'Yes'}]};
listener({type:'live-state',url:sender.url,chat:{available:true,poll}},sender,()=>{});
const app=sockets[0],obs=sockets[1];
for(const socket of sockets){socket.onopen();socket.onmessage({data:'{"type":"ready"}'});}
const send=(socket,fields)=>socket.onmessage({data:JSON.stringify({type:'vote-poll',tab:7,channel:'felps',question:'Which game?',choice:'choice-1',label:'Yes',...fields})});
const request='b5b27e91-a65a-4e23-85d6-d298f4c1ac12';
(async()=>{
  send(app,{request,channel:'wrong'});
  assert.equal(app.sent.at(-1).status,'tab_closed');assert.equal(calls.length,0);
  send(app,{request,choice:'not-a-choice'});
  assert.equal(app.sent.at(-1).status,'unavailable');assert.equal(calls.length,0);
  send(app,{request});send(app,{request});
  assert.equal(calls.length,1);assert.equal(calls[0].tab,7);assert.equal(calls[0].message.type,'vote-poll');
  const obsBefore=obs.sent.length;
  resolveVote({status:'confirmed'});await Promise.resolve();await Promise.resolve();
  assert.equal(app.sent.at(-1).status,'confirmed');assert.equal(obs.sent.length,obsBefore);
  const request2='c5b27e91-a65a-4e23-85d6-d298f4c1ac12';
  send(app,{request:request2});removed(7);rejectVote(new Error('tab closed'));
  await Promise.resolve();await Promise.resolve();assert.equal(app.sent.at(-1).type,'closed');
  console.log('PASS: vote routing, duplicate requests, wrong channel, and closed tab');
})().catch(error=>{console.error(error);process.exitCode=1;});
