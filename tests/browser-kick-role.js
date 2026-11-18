// Verify Kick role commands without a Kick account or a real role change.
const assert=require('assert'),fs=require('fs'),vm=require('vm');
function fixture(config={}) {
  let listener, sends=0, insertions=0, requests=0, moderator=!!config.moderator;
  const visible={getClientRects:()=>[{}]};
  const editor={...visible,innerText:config.draft ? 'unsent message' : '',
    querySelector:selector=>config.emoteDraft && selector.includes('img, svg') ? {} : null,
    dispatchEvent:()=>true,focus(){document.activeElement=editor;}};
  const button={...visible,disabled:!!config.disabled,getAttribute:()=>null,click(){++sends;if(!config.noChange)moderator=config.remove ? false : true;}};
  const document={activeElement:null,querySelector:()=>null,createRange:()=>({selectNodeContents:()=>{},collapse:()=>{}}),
    querySelectorAll:s=>s==='[data-testid="chat-input"][contenteditable="true"]' ? [editor] : s==='#send-message-button' ? [button] : [],
    execCommand:(name,_ui,value)=>{if(name==='selectAll')return true;if(name==='delete'){editor.innerText='';return true;}
      assert.equal(name,'insertText');++insertions;editor.innerText=value;return !config.insertError;}};
  const api={runtime:{id:'extension',onMessage:{addListener:fn=>listener=fn},sendMessage:()=>Promise.resolve()}};
  const context={chrome:api,URL,WeakSet,AbortController,InputEvent:class{},Node:{TEXT_NODE:3,ELEMENT_NODE:1},
    window:{getSelection:()=>({removeAllRanges:()=>{},addRange:()=>{}})},
    location:{hostname:'kick.com',pathname:'/guicraft',href:'https://kick.com/guicraft'},document,
    getComputedStyle:()=>({visibility:'visible'}),setInterval:()=>0,setTimeout:(fn,ms)=>setTimeout(fn,ms===900?1000:1),clearTimeout,Date,Promise,
    fetch:async(url,options)=>{++requests;assert.equal(url,'/api/v2/channels/guicraft/users/Viewer');
      assert.equal(options.credentials,'same-origin');if(config.networkError)throw new Error('offline');
      return {ok:true,json:async()=>({id:config.wrongId?100:99,username:'Viewer',is_moderator:moderator})};}};
  vm.createContext(context);vm.runInContext(fs.readFileSync('browser-extension/content.js','utf8'),context);
  return {async change(overrides={}){return new Promise(resolve=>{
    const handled=listener({type:'kick-role',platform:'kick',channel:'guicraft',username:'Viewer',uid:'99',verb:'mod',...overrides},
      {id:'extension'},resolve);assert.equal(handled,true);
  });},counts:()=>({sends,insertions,requests}),draft:()=>editor.innerText};
}
(async()=>{
  let test=fixture();assert.equal((await test.change()).status,'confirmed');assert.equal(test.counts().sends,1);
  test=fixture({moderator:true,remove:true});assert.equal((await test.change({verb:'unmod'})).status,'confirmed');assert.equal(test.counts().sends,1);
  test=fixture({moderator:true});assert.equal((await test.change()).status,'already_set');assert.equal(test.counts().sends,0);
  test=fixture({wrongId:true});assert.equal((await test.change()).status,'changed');assert.equal(test.counts().sends,0);
  test=fixture({draft:true});assert.equal((await test.change()).status,'unavailable');assert.equal(test.draft(),'unsent message');assert.equal(test.counts().insertions,0);
  test=fixture({emoteDraft:true});assert.equal((await test.change()).status,'unavailable');assert.equal(test.counts().insertions,0);
  test=fixture({networkError:true});assert.equal((await test.change()).status,'unavailable');assert.equal(test.counts().sends,0);
  test=fixture({disabled:true});assert.equal((await test.change()).status,'unavailable');assert.equal(test.counts().sends,0);assert.equal(test.draft(),'');
  test=fixture();assert.equal((await test.change({verb:'vip'})).status,'uncertain');assert.equal(test.counts().sends,1);
  test=fixture({noChange:true});assert.equal((await test.change()).status,'uncertain');assert.equal(test.counts().sends,1);
  test=fixture();assert.equal((await test.change({username:'bad user'})).status,'unavailable');assert.equal(test.counts().sends,0);
  console.log('PASS: Kick role identity, existing roles, drafts, unconfirmed changes, and single submission');
})().catch(error=>{console.error(error);process.exitCode=1;});
