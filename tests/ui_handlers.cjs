// DOM-handler regression harness, not a substitute for actual browser/visual QA.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const base=process.argv[2]||'http://127.0.0.1:8766';
class Element {
 constructor(id){this.id=id;this.textContent='';this.style={};this.disabled=false;this.value=id==='mode'?'0':'';this.open=false;this.files=[];this.classList={add(){}};this.handlers={};this.parts={};}
 addEventListener(type,fn){this.handlers[type]=fn;}replaceChildren(){}append(){}focus(){}click(){}setAttribute(){}getBoundingClientRect(){return {left:0,top:0,width:1200,height:760};}
 querySelector(selector){return this.parts[selector] ||= new Element(selector);}getContext(){return new Proxy({},{get(){return ()=>{}}});}showModal(){this.open=true;}close(){this.open=false;}
}
const elements=new Map(),get=id=>{if(!elements.has(id))elements.set(id,new Element(id));return elements.get(id);};
const doc={getElementById:get,addEventListener(){},createElement:()=>new Element(''),createTextNode:s=>s};
const win={addEventListener(){}};
const context=vm.createContext({document:doc,window:win,console,fetch:(url,opts)=>fetch(base+url,opts),requestAnimationFrame(){},setTimeout:()=>0,clearTimeout(){},Blob,URL});
vm.runInContext(fs.readFileSync(require('node:path').join(__dirname,'../web/app.js'),'utf8'),context);
const read=async()=>await (await fetch(base+'/api/state')).json();
const wait=async fn=>{for(let i=0;i<100;i++){if(fn())return;await new Promise(r=>setTimeout(r,10));}throw Error('test timeout');};
(async()=>{
 await wait(()=>get('connection').textContent==='C++ 引擎在线');
 await get('reset').onclick();let before=await read();
 vm.runInContext("keys.add('KeyD');keys.add('Space')",context);await get('step').onclick();let after=await read();
 assert.equal(after.robots[0].x,before.robots[0].x+55,'single-step captures held movement');assert.equal(after.robots[0].shots,1,'single-step captures held fire');
 let replay=await (await fetch(base+'/api/replay')).json();await get('reset').onclick();
 let release;const file={size:100,text:()=>new Promise(resolve=>release=resolve)};
 const operation=get('replay-file').onchange({target:{files:[file],value:''}});
 await wait(()=>typeof release==='function');
 for(const id of ['play','step','reset','export','import','mode'])assert.equal(get(id).disabled,true,`${id} disabled during import`);
 await get('play').onclick();assert.equal(vm.runInContext('playing',context),false,'busy play handler cannot restart simulation');
 release(JSON.stringify(replay));await operation;
 assert.equal((await read()).hash,replay.final_hash,'replay accepted');assert.equal(vm.runInContext('playing',context),false,'import remains paused');
 for(const id of ['play','step','reset','export','import','mode'])assert.equal(get(id).disabled,false,`${id} restored after import`);
 const bad={...replay,final_hash:(replay.final_hash^1)>>>0};await get('replay-file').onchange({target:{files:[{size:100,text:async()=>JSON.stringify(bad)}],value:''}});
 assert.equal((await read()).hash,replay.final_hash,'corrupt replay leaves match unchanged');assert.match(get('toast').textContent,/hash mismatch/);
 get('scope-open').onclick();assert.equal(get('scope').open,true);get('scope-close').onclick();assert.equal(get('scope').open,false);
 console.log('PASS DOM-handler/API regression: held-key single-step, import busy guard, paused import, hash corruption atomicity, modal open/close. Real browser rendering remains unverified.');
})().catch(error=>{console.error(error);process.exitCode=1;});
