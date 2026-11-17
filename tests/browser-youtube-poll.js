// Fixture follows YouTube's public live-chat poll renderer and choice template.
const assert=require('assert'), fs=require('fs'), vm=require('vm');
function fixture(config={}) {
  let listener, clicks=0; const handlers=new Map(), reports=[];
  const visible={getClientRects:()=>[{}]};
  const attrs=[new Set(),new Set()];
  if(config.voted){attrs[0].add('selected'); for(const value of attrs)value.add('has-voted');}
  const root={...visible,addEventListener:(name,fn)=>handlers.set(name,fn),removeEventListener:name=>handlers.delete(name)};
  const nodes=['Yes','No'].map((label,index)=>({...visible,hasAttribute:name=>attrs[index].has(name),
    querySelector:selector=>selector==='#label-text' ? {innerText:label} : selector==='#content' ?
      {hasAttribute:name=>name==='disabled' && !!config.disabled} : selector==='#endpoint' ? {...visible,click(){
        ++clicks; handlers.get('yt-service-request-sent')?.({detail:{endpoint:config.unrelated ? {} : {sendLiveChatVoteEndpoint:{}}}}); attrs[index].add('selected');
        if(!config.noResults)for(const value of attrs)value.add('has-voted');
        if(config.error) handlers.get('yt-service-request-error')?.();
        else if(!config.noCompletion)handlers.get('yt-service-request-completed')?.();
      }} : null}));
  root.querySelector=selector=>selector.includes('#poll-question') ? {innerText:config.question || 'Which game?'} : null;
  root.querySelectorAll=selector=>selector==='yt-live-chat-poll-choice' ? nodes : [];
  const api={runtime:{id:'extension',onMessage:{addListener:fn=>listener=fn},sendMessage:msg=>{reports.push(msg);return Promise.resolve();}}};
  const context={chrome:api,URL,Promise,location:{hostname:'www.youtube.com',pathname:'/live_chat',href:'https://www.youtube.com/live_chat?v=Abcdefgh123'},
    document:{querySelectorAll:selector=>selector==='yt-live-chat-poll-renderer' && !config.absent ? [root] : []},
    getComputedStyle:()=>({visibility:'visible'}),setInterval:()=>0,setTimeout:fn=>setTimeout(fn,1)};
  vm.createContext(context);vm.runInContext(fs.readFileSync('browser-extension/youtube-chat.js','utf8'),context);
  return {reports, clicks:()=>clicks, state:()=>reports[0].poll?.state,
    async vote(overrides={}){return new Promise(resolve=>{
      const handled=listener({type:'vote-poll',platform:'youtube',channel:'Abcdefgh123',question:'Which game?',choice:'option-0',label:'Yes',...overrides},
        {id:'extension'},resolve);assert.equal(handled,true);
    });}, reject(overrides={}){return listener({type:'vote-poll',platform:'youtube',channel:'Abcdefgh123',...overrides},{id:'extension'},()=>{});}};
}
(async()=>{
  let test=fixture(); assert.equal(test.state(),'open'); assert.equal(test.reports[0].type,'youtube-chat-state');
  assert.equal((await test.vote()).status,'confirmed');assert.equal(test.clicks(),1);
  test=fixture({voted:true}); assert.equal((await test.vote()).status,'already_voted');assert.equal(test.clicks(),0);
  test=fixture({disabled:true});assert.equal((await test.vote()).status,'unavailable');assert.equal(test.clicks(),0);
  test=fixture({absent:true});assert.equal((await test.vote()).status,'ended');assert.equal(test.clicks(),0);
  test=fixture();assert.equal((await test.vote({question:'Old'})).status,'changed');assert.equal(test.clicks(),0);
  test=fixture();assert.equal((await test.vote({choice:'option-1',label:'Yes'})).status,'unavailable');assert.equal(test.clicks(),0);
  test=fixture();assert.equal(test.reject({channel:'abcdefgh123'}),undefined);assert.equal(test.clicks(),0);
  test=fixture({error:true});assert.equal((await test.vote()).status,'uncertain');assert.equal(test.clicks(),1);
  test=fixture({noCompletion:true});assert.equal((await test.vote()).status,'uncertain');assert.equal(test.clicks(),1);
  test=fixture({unrelated:true});assert.equal((await test.vote()).status,'uncertain');assert.equal(test.clicks(),1);
  test=fixture({noResults:true});assert.equal((await test.vote()).status,'uncertain');assert.equal(test.clicks(),1);
  console.log('PASS: YouTube poll vote, video identity, server events, stale and uncertain cases');
})().catch(error=>{console.error(error);process.exitCode=1;});
