// Fixtures follow Twitch's public prediction checkout components. They never
// access a Twitch account or submit a real prediction.
const assert=require('assert'), fs=require('fs'), vm=require('vm');
function fixture(config={}) {
  let listener, custom=false, detail=-1, submissions=[], promptClicks=0;
  const visible={getClientRects:()=>[{}]};
  const labels=config.multi ? ['First','Second','Third'] : ['Yes','No'];
  const header={...visible,firstElementChild:{innerText:config.question || 'Will it happen?'}};
  class FakeInput {
    constructor(){this._value=config.dirty ? '50' : '';this.disabled=!!config.disabled;}
    getClientRects(){return [{}];} get value(){return this._value;} set value(v){this._value=v;}
    dispatchEvent(){if(config.changeOnInput)header.firstElementChild.innerText='Changed';}
  }
  const fields=labels.map((label,index)=>{
    const input=new FakeInput();
    const submit={...visible,disabled:!!config.disabled,innerText:'Vote',getAttribute:()=>null,
      click(){submissions.push({index,amount:input.value});}};
    return {...visible,querySelector:s=>s==='input[type="number"]' ? input : s==='button' ? submit : null};
  });
  const outcomes=labels.map((label,index)=>({...visible,innerText:label,click(){detail=index;}}));
  const toggle={...visible,disabled:false,click(){custom=true;if(config.changeOnToggle)header.firstElementChild.innerText='Changed';}};
  const prompt={...visible,click(){++promptClicks;}};
  const document={querySelector:selector=>selector.includes('prediction-checkout-active-footer__input-type-toggle') ?
      (config.multi && detail<0 ? null : toggle) : null,
    querySelectorAll:selector=>{
      if(selector==='.prediction-checkout-details-header')return config.absent ? [] : [header];
      if(selector.includes('prediction-summary-outcome__title'))return config.multi ? [] : outcomes;
      if(selector.includes('multi-option-summary-outcome__title'))return config.multi && detail<0 ? outcomes : [];
      if(selector.includes('multi-option-prediction-details__title'))return detail<0 ? [] : [{...visible,innerText:labels[detail]}];
      if(selector==='.custom-prediction-button')return !custom ? [] : config.multi ? [fields[detail]] : fields;
      if(selector.includes('prediction-terms-step__accept-button'))return config.terms && submissions.length ? [prompt] : [];
      return [];
    }};
  const api={runtime:{id:'extension',onMessage:{addListener:fn=>listener=fn},sendMessage:()=>Promise.resolve()}};
  const context={chrome:api,URL,WeakSet,Node:{TEXT_NODE:3,ELEMENT_NODE:1},HTMLInputElement:FakeInput,
    Event:class{constructor(type,options){this.type=type;this.options=options;}},
    location:{hostname:'www.twitch.tv',pathname:'/felps',href:'https://www.twitch.tv/felps'},
    document,getComputedStyle:()=>({visibility:'visible'}),setInterval:()=>0,setTimeout:fn=>setTimeout(fn,1),Date,Promise};
  vm.createContext(context);vm.runInContext(fs.readFileSync('browser-extension/content.js','utf8'),context);
  return {async predict(overrides={}) {return new Promise(resolve=>{
    const handled=listener({type:'place-prediction',channel:'felps',question:'Will it happen?',choice:'outcome-0',label:labels[0],points:25,...overrides},
      {id:'extension'},resolve);assert.equal(handled,true);
  });},submissions:()=>submissions,promptClicks:()=>promptClicks};
}
(async()=>{
  let test=fixture();assert.equal((await test.predict()).status,'uncertain');
  assert.deepEqual(test.submissions(),[{index:0,amount:'25'}]);
  test=fixture({multi:true});assert.equal((await test.predict({choice:'outcome-2',label:'Third',points:400})).status,'uncertain');
  assert.deepEqual(test.submissions(),[{index:2,amount:'400'}]);
  test=fixture({terms:true});assert.equal((await test.predict()).status,'needs_confirmation');assert.equal(test.promptClicks(),0);
  test=fixture({changeOnToggle:true});assert.equal((await test.predict()).status,'changed');assert.equal(test.submissions().length,0);
  test=fixture({changeOnInput:true});assert.equal((await test.predict()).status,'changed');assert.equal(test.submissions().length,0);
  test=fixture({disabled:true});assert.equal((await test.predict()).status,'unavailable');assert.equal(test.submissions().length,0);
  test=fixture({dirty:true});assert.equal((await test.predict()).status,'unavailable');assert.equal(test.submissions().length,0);
  test=fixture();assert.equal((await test.predict({points:250001})).status,'unavailable');assert.equal(test.submissions().length,0);
  test=fixture();assert.equal((await test.predict({choice:'outcome-1',label:'Yes'})).status,'unavailable');assert.equal(test.submissions().length,0);
  console.log('PASS: Twitch prediction amounts, outcome identity, stale/disabled controls, and manual confirmation');
})().catch(error=>{console.error(error);process.exitCode=1;});
