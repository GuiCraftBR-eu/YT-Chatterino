// Runs the real browser extension against a locally supplied Twitch-shaped page.
// Requires playwright, faye-websocket and a Playwright Chromium installation.
const {chromium}=require('playwright');
const Faye=require('faye-websocket'), http=require('http'), fs=require('fs'), os=require('os'), path=require('path'), assert=require('assert');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'ytchat-poll-'));
const extension=path.join(temp,'extension');
fs.cpSync(path.resolve('browser-extension'),extension,{recursive:true});
const background=path.join(extension,'background.js');
fs.writeFileSync(background,fs.readFileSync(background,'utf8').replace('[37367, 37368]','[38482, 38483]'));
const events=[];
let bridge;
const server=http.createServer();
server.on('upgrade',(request,socket,body)=>{
  if(!Faye.isWebSocket(request))return socket.destroy();
  const connection=new Faye(request,socket,body);bridge=connection;
  connection.on('message',event=>{
    const data=JSON.parse(event.data);
    if(data.type==='hello')connection.send('{"type":"ready"}');else events.push(data);
  });
});
const page=`<!doctype html><section data-test-selector="chat-room-component-layout" class="chat-room">
<div class="community-highlight"><div data-test-selector="click-wrapper">
 <div data-test-selector="header-content"><p data-test-selector="header">Current Poll</p><strong>Which game?</strong></div></div>
 <div data-test-selector="expanded-content">
  <button role="progressbar" aria-label="Yes"><input type="radio" value="choice-1"><p data-test-selector="title">Yes</p><p data-test-selector="Count">0%</p></button>
  <button role="progressbar" aria-label="No"><input type="radio" value="choice-2"><p data-test-selector="title">No</p><p data-test-selector="Count">0%</p></button>
  <button aria-label="Vote" disabled>Vote</button><span id="result"></span>
 </div></div></section>
<script>
 const root=document.querySelector('.community-highlight');
 root.querySelectorAll('button[role=progressbar]').forEach(button=>button.onclick=()=>{
   root.querySelectorAll('input').forEach(input=>input.checked=false);
   button.querySelector('input').checked=true;root.querySelector('button[aria-label=Vote]').disabled=false;
 });
 root.querySelector('button[aria-label=Vote]').onclick=()=>{document.getElementById('result').textContent='Your vote was recorded';};
</script>`;
const waitFor=async(predicate,ms=15000)=>{const end=Date.now()+ms;while(Date.now()<end){if(predicate())return;await new Promise(r=>setTimeout(r,50));}throw Error('Timed out: '+JSON.stringify(events.slice(-3)));};
(async()=>{
 await new Promise(resolve=>server.listen(38482,'127.0.0.1',resolve));
 const ctx=await chromium.launchPersistentContext(path.join(temp,'profile'),{headless:true,channel:'chromium',
   args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`]});
 try{
  ctx.on('serviceworker',worker=>worker.on('console',message=>console.log('extension:',message.type(),message.text())));
  await ctx.route('https://www.twitch.tv/felps',route=>route.fulfill({status:200,contentType:'text/html',body:page}));
  const tab=await ctx.newPage();await tab.goto('https://www.twitch.tv/felps');
  console.log('Extension workers:',ctx.serviceWorkers().map(worker=>worker.url()));
  await waitFor(()=>events.some(e=>e.type==='live-state'&&e.chat?.poll?.state==='open'&&e.chat.poll.choices.length===2));
  const state=events.find(e=>e.chat?.poll?.state==='open');
  const send=(request,extra={})=>bridge.send(JSON.stringify({type:'vote-poll',request,tab:state.tab,channel:'felps',
    question:'Which game?',choice:'choice-1',label:'Yes',...extra}));
  const a='b5b27e91-a65a-4e23-85d6-d298f4c1ac12';send(a);
  await waitFor(()=>events.some(e=>e.type==='poll-result'&&e.request===a));
  if(events.find(e=>e.request===a).status!=='confirmed') console.log('Poll after vote:',await tab.locator('.community-highlight').innerText(),events.filter(e=>e.chat?.poll).slice(-1));
  assert.equal(events.find(e=>e.request===a).status,'confirmed');
  assert.equal(await tab.locator('input[value=choice-1]').isChecked(),true);
  await waitFor(()=>events.some(e=>e.chat?.poll?.state==='voted'));
  const b='c5b27e91-a65a-4e23-85d6-d298f4c1ac12';send(b);
  await waitFor(()=>events.some(e=>e.request===b));assert.equal(events.find(e=>e.request===b).status,'already_voted');
  const c='d5b27e91-a65a-4e23-85d6-d298f4c1ac12';send(c,{channel:'other'});
  await waitFor(()=>events.some(e=>e.request===c));assert.equal(events.find(e=>e.request===c).status,'tab_closed');
  await tab.close();await waitFor(()=>events.some(e=>e.type==='closed'&&e.tab===state.tab));
  console.log('PASS: actual extension, simulated Twitch DOM, free vote, acknowledgement, duplicate and closed tab');
 }finally{await ctx.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
