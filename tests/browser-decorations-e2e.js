// Exercise the real extension against a simulated Twitch pinned message and Hype Train.
const {chromium}=require('playwright'), Faye=require('faye-websocket');
const http=require('http'),fs=require('fs'),os=require('os'),path=require('path'),assert=require('assert');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'ytchat-decorations-'));
const extension=path.join(temp,'extension');fs.cpSync(path.resolve('browser-extension'),extension,{recursive:true});
const background=path.join(extension,'background.js');
fs.writeFileSync(background,fs.readFileSync(background,'utf8').replace('[37367, 37368]','[38486, 38487]'));
const events=[],server=http.createServer();
server.on('upgrade',(req,socket,body)=>{
  if(!Faye.isWebSocket(req))return socket.destroy();
  const ws=new Faye(req,socket,body);
  ws.on('message',event=>{const data=JSON.parse(event.data);if(data.type==='hello')ws.send('{"type":"ready"}');else events.push(data);});
});
const page=`<!doctype html><section class="chat-room" data-test-selector="chat-room-component-layout">
<div><span class="chatter-name">Alice</span><div><span class="pinned-chat__message"> Hello <img alt="Kappa" src="https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/3.0"> and <img alt="Pog" src="https://cdn.betterttv.net/emote/abc/3x">!</span></div></div>
<div class="hypeTrainBanner--fixture"><button class="bannerButton--fixture" aria-controls="hype-detail" aria-label="Level 3 Hype Train">
<div role="progressbar" aria-label="Progress towards completing Hype Train level 3" aria-valuenow="50" aria-valuemax="100"></div><span role="timer">2:15 left</span></button></div>
</section>`;
const wait=async(fn)=>{const end=Date.now()+12000;while(Date.now()<end){const v=fn();if(v)return v;await new Promise(r=>setTimeout(r,50));}throw Error('Timed out: '+JSON.stringify(events.slice(-3)));};
(async()=>{
 await new Promise(resolve=>server.listen(38486,'127.0.0.1',resolve));
 const ctx=await chromium.launchPersistentContext(path.join(temp,'profile'),{headless:true,channel:'chromium',args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`]});
 try{
  await ctx.route('https://www.twitch.tv/**',route=>route.fulfill({status:200,contentType:'text/html',body:page}));
  await ctx.route('https://static-cdn.jtvnw.net/**',route=>route.abort());
  await ctx.route('https://cdn.betterttv.net/**',route=>route.abort());
  const tab=await ctx.newPage();await tab.goto('https://www.twitch.tv/felps');
  const state=await wait(()=>events.find(e=>e.chat?.hype?.text && e.chat?.pin?.native_emotes?.length===2));
  assert.equal(state.chat.pin.text,'Hello Kappa and Pog!');
  assert.equal(state.chat.pin.name,'Alice');
  assert.deepEqual(state.chat.pin.native_emotes.map(e=>state.chat.pin.text.slice(e.start,e.end)),['Kappa','Pog']);
  assert.equal(state.chat.hype.text,'Level 3 Hype Train');assert(state.chat.hype.options.includes('50/100'));
  const count=events.length;
  await tab.locator('[class*="hypeTrainBanner--"]').evaluate(node=>node.remove());
  await wait(()=>events.slice(count).find(e=>e.chat?.available&&!e.chat?.hype?.text));
  console.log('PASS: pinned emote spans and Hype Train appear and clear through the real extension');
 }finally{await ctx.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;server.close();});
