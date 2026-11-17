// Test the content script's poll actions against a Twitch-shaped DOM, with no network.
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

function fixture(config = {}) {
  let listener, optionClicks = 0, voteClicks = 0, confirmed = false;
  const input = {value:'choice-1', id:'choice-1', checked:false};
  const title = {innerText:'Yes'};
  const header = {innerText:config.ended ? 'Poll results' : 'Current Poll'};
  const question = {innerText:config.question || 'Which game?'};
  const visible = {getClientRects:()=>[{}]};
  const vote = {...visible, disabled:true, innerText:config.paid ? 'Vote 100 points' : 'Vote',
    getAttribute:name=>name === 'aria-label' ? 'Vote' : null,
    click(){ ++voteClicks; if (config.confirm !== false) { confirmed = true; content.innerText = config.realConfirmation ? 'Voted!' : 'Your vote was recorded'; } }};
  const option = {...visible, disabled:false, getAttribute:name=>name === 'aria-label' ? 'Yes' : null,
    querySelector:selector=>selector === 'input[type="radio"]' ? input : selector.includes('title') ? title : null,
    click(){ ++optionClicks; input.checked = true; vote.disabled = !!config.disabled; }};
  const content = {innerText:config.optionVoted ? 'Voted!' : config.voted ? 'You have voted' : '',
    querySelectorAll:selector=>selector === 'button[role="progressbar"]' ? [option] :
      selector === 'input[type="radio"]' ? [input] : selector === 'button' ? [option,vote] :
      selector === 'p' && config.realConfirmation && confirmed ? [{...visible,innerText:'Voted!',closest:()=>null}] :
      selector === 'p' && config.optionVoted ? [{...visible,innerText:'Voted!',closest:()=>option}] : []};
  const root = {...visible, querySelector:selector=>selector.includes('header-content') && selector.includes('header"]') ? header :
      selector.includes('header-content') && selector.includes('strong') ? question :
      selector.includes('expanded-content') ? content : null};
  const api={runtime:{id:'test-extension',onMessage:{addListener:fn=>listener=fn},sendMessage:()=>Promise.resolve()}};
  const context={chrome:api,URL,WeakSet,Node:{TEXT_NODE:3,ELEMENT_NODE:1},
    location:{hostname:'www.twitch.tv',pathname:'/felps',href:'https://www.twitch.tv/felps'},
    getComputedStyle:()=>({visibility:'visible'}),
    document:{querySelectorAll:selector=>selector === '.community-highlight' && !config.absent ? [root] : [],
      querySelector:()=>null},
    setInterval:()=>0,setTimeout:(fn)=>setTimeout(fn,1),Date,Promise};
  vm.createContext(context);
  vm.runInContext(fs.readFileSync('browser-extension/content.js','utf8'),context);
  return {async vote(overrides = {}) {
    const message={type:'vote-poll',channel:'felps',question:'Which game?',choice:'choice-1',label:'Yes',...overrides};
    return await new Promise(resolve => {
      const keep=listener(message,{id:'test-extension'},resolve);
      assert.equal(keep,true);
    });
  }, counts:()=>({optionClicks,voteClicks}), state:()=>vm.runInContext('pollSnapshot(twitchEngagementCard("poll")).state',context)};
}

(async()=>{
  let test=fixture();
  assert.equal((await test.vote()).status,'confirmed');
  assert.deepEqual(test.counts(),{optionClicks:1,voteClicks:1});
  test=fixture({realConfirmation:true}); assert.equal((await test.vote()).status,'confirmed');
  test=fixture({optionVoted:true}); assert.equal(test.state(),'open');
  test=fixture({voted:true}); assert.equal((await test.vote()).status,'already_voted'); assert.equal(test.counts().voteClicks,0);
  test=fixture({ended:true}); assert.equal((await test.vote()).status,'ended'); assert.equal(test.counts().voteClicks,0);
  test=fixture({absent:true}); assert.equal((await test.vote()).status,'ended'); assert.equal(test.counts().voteClicks,0);
  test=fixture(); assert.equal((await test.vote({question:'Old poll'})).status,'changed'); assert.equal(test.counts().optionClicks,0);
  test=fixture(); assert.equal((await test.vote({choice:'wrong'})).status,'unavailable'); assert.equal(test.counts().voteClicks,0);
  test=fixture({disabled:true}); assert.equal((await test.vote()).status,'unavailable'); assert.equal(test.counts().voteClicks,0);
  test=fixture({paid:true}); assert.equal((await test.vote()).status,'unavailable'); assert.equal(test.counts().voteClicks,0);
  test=fixture({confirm:false}); assert.equal((await test.vote()).status,'uncertain'); assert.equal(test.counts().voteClicks,1);
  console.log('PASS: poll confirmation, stale/closed/already-voted/paid cases and uncertain results');
})().catch(error=>{console.error(error);process.exitCode=1;});
