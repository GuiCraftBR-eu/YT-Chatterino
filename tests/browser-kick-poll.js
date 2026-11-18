// Kick's public poll component uses poll-title, poll-option-list, and radio
// buttons whose id/title are the option id/label. No live account is used.
const assert = require('assert'), fs = require('fs'), vm = require('vm');

function fixture(config = {}) {
  let listener, clicks = 0, requests = 0, selected = config.voted ? 1 : -1;
  const visible = {getClientRects:()=>[{}]};
  const options = ['aa', 'aaa'].map((label, i) => ({...visible, disabled:!!config.disabled,
    getAttribute:name => name === 'id' ? String(i) : name === 'title' ? label :
      name === 'data-state' ? (selected === i ? 'checked' : 'unchecked') : null,
    click(){ ++clicks; selected = i; }}));
  const list = {...visible, querySelectorAll:selector => selector === 'button[role="radio"]' ? options : []};
  const root = {querySelectorAll:selector => selector === '[data-testid="poll-option-list"]' ? (config.collapsed ? [] : [list]) :
    selector === '[data-testid="poll-option-list"] button[role="radio"]' ? options : []};
  const title = {...visible, innerText:config.question || 'aa', parentElement:{parentElement:{parentElement:root}}};
  const api = {runtime:{id:'test-extension',onMessage:{addListener:fn=>listener=fn},sendMessage:()=>Promise.resolve()}};
  const context = {chrome:api, URL, WeakSet, AbortController, Date, Promise,
    location:{hostname:'kick.com',pathname:'/guicraft',href:'https://kick.com/guicraft'},
    document:{querySelectorAll:selector => selector === '[data-testid="poll-title"]' && !config.absent ? [title] : [],querySelector:()=>null},
    getComputedStyle:()=>({visibility:'visible'}), setInterval:()=>0,
    setTimeout:(fn,ms)=>setTimeout(fn,ms === 900 ? 1000 : 1), clearTimeout,
    fetch:async (url, settings) => {
      ++requests;
      assert.equal(vm.runInContext('kickPollSnapshot().state', context),'unavailable');
      assert.equal(url,'/api/v2/channels/guicraft/polls');
      assert.equal(settings.credentials,'same-origin'); assert.equal(settings.cache,'no-store');
      if (config.networkError) throw new Error('offline');
      return {ok:true,json:async()=>({data:{poll:{title:config.serverQuestion || 'aa',
        has_voted:!config.unconfirmed, voted_option_id:config.serverChoice ?? 0,
        options:[{id:0,label:'aa'},{id:1,label:'aaa'}]}}})};
    }};
  vm.createContext(context);
  vm.runInContext(fs.readFileSync('browser-extension/content.js','utf8'),context);
  return {async vote(overrides={}) {
    return new Promise(resolve=>{
      const handled=listener({type:'vote-poll',platform:'kick',channel:'guicraft',question:'aa',choice:'0',label:'aa',...overrides},
        {id:'test-extension'},resolve);
      assert.equal(handled,true);
    });
  }, counts:()=>({clicks,requests}), snapshot:()=>vm.runInContext('kickPollSnapshot()',context)};
}

(async()=>{
  let test=fixture();
  assert.equal(test.snapshot().state,'open');
  assert.equal((await test.vote()).status,'confirmed');
  assert.deepEqual(test.counts(),{clicks:1,requests:1});
  assert.equal(test.snapshot().state,'voted');
  test=fixture({voted:true}); assert.equal((await test.vote()).status,'already_voted'); assert.equal(test.counts().clicks,0);
  test=fixture({disabled:true}); assert.equal((await test.vote()).status,'unavailable'); assert.equal(test.counts().clicks,0);
  test=fixture({collapsed:true}); assert.equal((await test.vote()).status,'unavailable'); assert.equal(test.counts().clicks,0);
  test=fixture({absent:true}); assert.equal((await test.vote()).status,'ended'); assert.equal(test.counts().clicks,0);
  test=fixture(); assert.equal((await test.vote({question:'old poll'})).status,'changed'); assert.equal(test.counts().clicks,0);
  test=fixture(); assert.equal((await test.vote({choice:'1',label:'aa'})).status,'unavailable'); assert.equal(test.counts().clicks,0);
  test=fixture({unconfirmed:true}); assert.equal((await test.vote()).status,'uncertain'); assert.equal(test.counts().clicks,1);
  test=fixture({networkError:true}); assert.equal((await test.vote()).status,'uncertain'); assert.equal(test.counts().clicks,1);
  test=fixture({serverQuestion:'new poll'}); assert.equal((await test.vote()).status,'uncertain'); assert.equal(test.counts().clicks,1);
  test=fixture({serverChoice:1}); assert.equal((await test.vote()).status,'uncertain'); assert.equal(test.counts().clicks,1);
  console.log('PASS: Kick radio vote, server confirmation, stale/disabled/already-voted and uncertain cases');
})().catch(error=>{console.error(error);process.exitCode=1;});
