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
    sendMessage:(tab,message,options)=>{calls.push({tab,message,options});return new Promise((resolve,reject)=>{resolveVote=resolve;rejectVote=reject;});},
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
  const duplicate='d5b27e91-a65a-4e23-85d6-d298f4c1ac12';
  send(obs,{request:duplicate}); assert.equal(obs.sent.at(-1).status,'unavailable');
  send(app,{request:duplicate,choice:'wrong'}); assert.equal(app.sent.at(-1).status,'unavailable');
  send(obs,{request:duplicate}); assert.equal(obs.sent.at(-1).status,'unavailable'); assert.equal(calls.length,1);
  const obsBefore=obs.sent.length;
  resolveVote({status:'confirmed'});await Promise.resolve();await Promise.resolve();
  assert.equal(app.sent.at(-1).status,'confirmed');assert.equal(obs.sent.length,obsBefore);
  const kickSender={tab:{id:8},frameId:0,url:'https://kick.com/felps'};
  const kickPoll={text:'Which game?',state:'open',choices:[{id:'0',label:'Yes'}]};
  listener({type:'live-state',url:kickSender.url,chat:{available:true,poll:kickPoll}},kickSender,()=>{});
  send(app,{request:duplicate,tab:8,platform:'twitch',choice:'0'});
  assert.equal(app.sent.at(-1).status,'tab_closed'); assert.equal(calls.length,1);
  send(app,{request:duplicate,tab:8,platform:'kick',choice:'0'});
  assert.equal(calls.length,2); assert.equal(calls[1].message.platform,'kick');
  resolveVote({status:'confirmed'});await Promise.resolve();await Promise.resolve();
  assert.equal(app.sent.at(-1).status,'confirmed');
  const video='Abcdefgh123', watch='https://www.youtube.com/watch?v='+video;
  listener({type:'live-state',url:watch},{tab:{id:9,url:watch},frameId:0,url:watch},()=>{});
  const frame={tab:{id:9,url:watch},frameId:3,url:'https://www.youtube.com/live_chat?v='+video};
  const youtubePoll={text:'Which game?',state:'open',choices:[{id:'option-0',label:'Yes'}]};
  listener({type:'youtube-chat-state',url:'https://www.youtube.com/live_chat?v=abcdefgh123',poll:youtubePoll},frame,()=>{});
  send(app,{request:duplicate,tab:9,platform:'youtube',channel:video,choice:'option-0'});
  assert.equal(app.sent.at(-1).status,'tab_closed'); assert.equal(calls.length,2);
  listener({type:'youtube-chat-state',url:frame.url,poll:youtubePoll},frame,()=>{});
  listener({type:'live-state',url:watch},{tab:{id:9,url:watch},frameId:0,url:watch},()=>{});
  send(app,{request:duplicate,tab:9,platform:'youtube',channel:'abcdefgh123',choice:'option-0'});
  assert.equal(app.sent.at(-1).status,'tab_closed'); assert.equal(calls.length,2);
  send(app,{request:duplicate,tab:9,platform:'youtube',channel:video,choice:'option-0'});
  assert.equal(calls.length,3);assert.equal(calls[2].options.frameId,3);assert.equal(calls[2].message.platform,'youtube');
  resolveVote({status:'confirmed'});await Promise.resolve();await Promise.resolve();
  assert.equal(app.sent.at(-1).status,'confirmed');
  const prediction={text:'Will it happen?',state:'open',choices:[{id:'outcome-0',label:'Yes'},{id:'outcome-1',label:'No'}]};
  listener({type:'live-state',url:sender.url,chat:{available:true,poll,prediction}},sender,()=>{});
  send(app,{type:'place-prediction',request:duplicate,question:'Will it happen?',choice:'outcome-0',label:'Wrong',points:50});
  assert.equal(app.sent.at(-1).status,'unavailable'); assert.equal(calls.length,3);
  send(app,{type:'place-prediction',request:duplicate,question:'Will it happen?',choice:'outcome-0',label:'Yes',points:50});
  assert.equal(calls.length,4); assert.equal(calls[3].message.points,50);
  send(obs,{type:'place-prediction',request:'e5b27e91-a65a-4e23-85d6-d298f4c1ac12',question:'Will it happen?',choice:'outcome-0',label:'Yes',points:50});
  assert.equal(obs.sent.at(-1).status,'unavailable'); assert.equal(calls.length,4);
  resolveVote({status:'needs_confirmation'});await Promise.resolve();await Promise.resolve();
  assert.equal(app.sent.at(-1).status,'needs_confirmation');
  send(app,{type:'kick-role',request:duplicate,tab:8,channel:'felps',username:'Viewer',uid:'99',verb:'mod'});
  assert.equal(calls.length,5);assert.equal(calls[4].message.uid,'99');assert.equal(calls[4].message.verb,'mod');
  send(obs,{type:'kick-role',request:'f5b27e91-a65a-4e23-85d6-d298f4c1ac12',tab:8,channel:'felps',username:'Viewer',uid:'99',verb:'unmod'});
  assert.equal(obs.sent.at(-1).status,'unavailable');assert.equal(calls.length,5);
  resolveVote({status:'confirmed'});await Promise.resolve();await Promise.resolve();
  assert.equal(app.sent.at(-1).status,'confirmed');
  const request2='c5b27e91-a65a-4e23-85d6-d298f4c1ac12';
  send(app,{request:request2});removed(7);rejectVote(new Error('tab closed'));
  await Promise.resolve();await Promise.resolve();
  assert(app.sent.some(message=>message.type==='closed' && message.tab===7));
  assert.equal(app.sent.at(-1).status,'uncertain');
  console.log('PASS: vote routing, duplicate requests, wrong channel, and closed tab');
})().catch(error=>{console.error(error);process.exitCode=1;});
