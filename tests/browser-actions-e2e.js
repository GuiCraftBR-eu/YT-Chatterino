// Real extension in an isolated Chromium profile against simulated platform pages.
// Requires playwright, faye-websocket, and a Playwright Chromium installation.
const {chromium}=require('playwright'), Faye=require('faye-websocket');
const http=require('http'),fs=require('fs'),os=require('os'),path=require('path'),assert=require('assert');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'ytchat-actions-'));
const extension=path.join(temp,'extension'); fs.cpSync(path.resolve('browser-extension'),extension,{recursive:true});
const background=path.join(extension,'background.js');
fs.writeFileSync(background,fs.readFileSync(background,'utf8').replace('[37367, 37368]','[38484, 38485]'));
const events=[]; let bridge, kickVoted=false, kickMod=false;
const server=http.createServer();
server.on('upgrade',(request,socket,body)=>{
  if(!Faye.isWebSocket(request))return socket.destroy();
  const connection=new Faye(request,socket,body);bridge=connection;
  connection.on('message',event=>{const data=JSON.parse(event.data);if(data.type==='hello')connection.send('{"type":"ready"}');else events.push(data);});
});
const waitFor=async(predicate,ms=16000)=>{const end=Date.now()+ms;while(Date.now()<end){const value=predicate();if(value)return value;await new Promise(r=>setTimeout(r,50));}throw Error('Timed out: '+JSON.stringify(events.slice(-4)));};
const react=process.env.REACT_TEST_BUNDLE ? fs.readFileSync(process.env.REACT_TEST_BUNDLE,'utf8') : '';
const twitch=react ? `<!doctype html><div id="react-root"></div><script>${react}</script>` : `<!doctype html><div class="chat-room" data-test-selector="chat-room-component-layout">
<div class="prediction-checkout-details-header"><strong>Will it happen?</strong><p>Open</p></div>
<div data-test-selector="prediction-summary-outcome__title">Yes</div><div data-test-selector="prediction-summary-outcome__title">No</div>
<button data-test-selector="prediction-checkout-active-footer__input-type-toggle">Custom</button>
<div id="fields" hidden><div class="custom-prediction-button"><input type="number"><button>Vote</button></div>
<div class="custom-prediction-button"><input type="number"><button>Vote</button></div></div></div>
<script>window.votes=[];document.querySelector('[data-test-selector="prediction-checkout-active-footer__input-type-toggle"]').onclick=()=>document.querySelector('#fields').hidden=false;
document.querySelectorAll('.custom-prediction-button').forEach((field,index)=>field.querySelector('button').onclick=()=>window.votes.push({index,amount:field.querySelector('input').value}));</script>`;
const lexical=process.env.LEXICAL_TEST_BUNDLE ? fs.readFileSync(process.env.LEXICAL_TEST_BUNDLE,'utf8') : '';
const kick=`<!doctype html><div><div><div><span data-testid="poll-title">Pick one</span></div></div>
<div data-testid="poll-option-list"><button role="radio" id="0" title="Yes" data-state="unchecked">Yes</button><button role="radio" id="1" title="No" data-state="unchecked">No</button></div></div>
<div data-testid="chat-input" contenteditable="true"></div><button id="send-message-button">Send</button>
<script>${lexical}</script><script>document.querySelectorAll('[role=radio]').forEach(button=>button.onclick=()=>{button.dataset.state='checked';fetch('/api/test-poll',{method:'POST'});});
document.querySelector('#send-message-button').onclick=()=>{window.command=window.kickEditorText?window.kickEditorText():document.querySelector('[data-testid=chat-input]').innerText;fetch('/api/test-role',{method:'POST'});};</script>`;
const video='Abcdefgh123';
const youtubeWatch=`<!doctype html><iframe src="https://www.youtube.com/live_chat?v=${video}"></iframe>`;
const youtubeChat=`<!doctype html><style>yt-live-chat-poll-renderer,yt-live-chat-poll-header-renderer,yt-live-chat-poll-choice,tp-yt-paper-item {display:block}</style>
<yt-live-chat-poll-renderer><yt-live-chat-poll-header-renderer><span id="poll-question">Pick a game?</span></yt-live-chat-poll-header-renderer>
<yt-live-chat-poll-choice><a id="endpoint"><tp-yt-paper-item id="content"><span id="label-text">Yes</span></tp-yt-paper-item></a></yt-live-chat-poll-choice>
<yt-live-chat-poll-choice><a id="endpoint"><tp-yt-paper-item id="content"><span id="label-text">No</span></tp-yt-paper-item></a></yt-live-chat-poll-choice></yt-live-chat-poll-renderer>
<script>document.querySelectorAll('yt-live-chat-poll-choice').forEach(node=>node.querySelector('#endpoint').onclick=()=>{
node.dispatchEvent(new CustomEvent('yt-service-request-sent',{bubbles:true,composed:true,detail:{endpoint:{sendLiveChatVoteEndpoint:{}}}}));
node.setAttribute('selected','');document.querySelectorAll('yt-live-chat-poll-choice').forEach(item=>item.setAttribute('has-voted',''));
node.dispatchEvent(new CustomEvent('yt-service-request-completed',{bubbles:true,composed:true}));});</script>`;
(async()=>{
  await new Promise(resolve=>server.listen(38484,'127.0.0.1',resolve));
  const ctx=await chromium.launchPersistentContext(path.join(temp,'profile'),{headless:true,channel:'chromium',args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`]});
  try {
    await ctx.route('https://www.twitch.tv/**',route=>route.fulfill({status:200,contentType:'text/html',body:twitch}));
    await ctx.route('https://kick.com/**',route=>{
      const url=new URL(route.request().url());
      if(url.pathname==='/api/test-poll'){kickVoted=true;return route.fulfill({status:200,body:'{}'});}
      if(url.pathname==='/api/test-role'){kickMod=true;return route.fulfill({status:200,body:'{}'});}
      if(url.pathname.endsWith('/polls'))return route.fulfill({status:200,contentType:'application/json',body:JSON.stringify({data:{poll:{title:'Pick one',has_voted:kickVoted,voted_option_id:kickVoted?0:null,options:[{id:0,label:'Yes'},{id:1,label:'No'}]}}})});
      if(url.pathname.endsWith('/users/Viewer'))return route.fulfill({status:200,contentType:'application/json',body:JSON.stringify({id:99,username:'Viewer',is_moderator:kickMod})});
      return route.fulfill({status:200,contentType:'text/html',body:kick});
    });
    await ctx.route('https://www.youtube.com/**',route=>route.fulfill({status:200,contentType:'text/html',body:route.request().url().includes('/live_chat?')?youtubeChat:youtubeWatch}));
    const tab=await ctx.newPage();await tab.goto('https://www.twitch.tv/felps');
    let state=await waitFor(()=>events.find(e=>e.url==='https://www.twitch.tv/felps'&&e.chat?.prediction?.state==='open'));
    let request='a5b27e91-a65a-4e23-85d6-d298f4c1ac12';
    bridge.send(JSON.stringify({type:'place-prediction',request,tab:state.tab,channel:'felps',question:'Will it happen?',choice:'outcome-1',label:'No',points:125}));
    let result=await waitFor(()=>events.find(e=>e.request===request));assert.equal(result.status,'uncertain');
    assert.deepEqual(await tab.evaluate(()=>window.votes),[{index:1,amount:'125'}]);
    await tab.goto('https://kick.com/felps');
    state=await waitFor(()=>events.find(e=>e.url==='https://kick.com/felps'&&e.chat?.poll?.state==='open'));
    request='b5b27e91-a65a-4e23-85d6-d298f4c1ac12';
    bridge.send(JSON.stringify({type:'vote-poll',platform:'kick',request,tab:state.tab,channel:'felps',question:'Pick one',choice:'0',label:'Yes'}));
    result=await waitFor(()=>events.find(e=>e.request===request));assert.equal(result.status,'confirmed');
    request='c5b27e91-a65a-4e23-85d6-d298f4c1ac12';
    bridge.send(JSON.stringify({type:'kick-role',request,tab:state.tab,channel:'felps',username:'Viewer',uid:'99',verb:'mod'}));
    result=await waitFor(()=>events.find(e=>e.request===request));
    if(result.status!=='confirmed') console.log('Kick editor:',await tab.evaluate(()=>({text:document.querySelector('[data-testid=chat-input]')?.innerText,editorText:window.kickEditorText?.(),command:window.command})));
    assert.equal(result.status,'confirmed');
    assert.equal(await tab.evaluate(()=>window.command),'/mod Viewer');
    await tab.goto('https://www.youtube.com/watch?v='+video);
    state=await waitFor(()=>events.find(e=>e.url==='https://www.youtube.com/watch?v='+video&&e.chat?.poll?.state==='open'));
    request='d5b27e91-a65a-4e23-85d6-d298f4c1ac12';
    bridge.send(JSON.stringify({type:'vote-poll',platform:'youtube',request,tab:state.tab,channel:video,question:'Pick a game?',choice:'option-0',label:'Yes'}));
    result=await waitFor(()=>events.find(e=>e.request===request));assert.equal(result.status,'confirmed');
    console.log('PASS: real extension with simulated Twitch prediction, Kick poll and role, and YouTube chat frame');
  } finally {await ctx.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;server.close();});
