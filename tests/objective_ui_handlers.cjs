// Real loopback API + stub DOM regression. This is NOT browser rendering or visual QA.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const base=process.argv[2]||'http://127.0.0.1:8766';
class Element {
 constructor(id){this.id=id;this.textContent='';this.style={};this.disabled=false;this.value=({'mode':'0','scenario':'sandbox','simulation-speed':'1','boundary-policy':'strict-reject-unadjudicated-v1','credit-strategy':'disabled'})[id]||'';this.open=false;this.hidden=false;this.files=[];this.className='';this.classList={add(){}};this.handlers={};this.parts={};this.children=[];this.attributes={};}
 addEventListener(type,fn){this.handlers[type]=fn;}replaceChildren(...nodes){this.children=nodes;}append(...nodes){this.children.push(...nodes);}focus(){}click(){this.clicked=(this.clicked||0)+1;}setAttribute(key,value){this.attributes[key]=String(value);}getBoundingClientRect(){return{left:0,top:0,width:1200,height:760};}
 querySelector(selector){return this.parts[selector] ||= new Element(selector);}getContext(){return new Proxy({},{get(){return()=>{}}});}showModal(){this.open=true;}close(){this.open=false;}
}
const elements=new Map(),get=id=>{if(!elements.has(id))elements.set(id,new Element(id));return elements.get(id);};
const documentEvents={},windowEvents={},created=[],calls=[];
const document={getElementById:get,addEventListener(type,fn){documentEvents[type]=fn;},createElement:tag=>{const node=new Element('');node.tagName=tag.toUpperCase();created.push(node);return node;},createTextNode:text=>String(text)};
const window={addEventListener(type,fn){windowEvents[type]=fn;}};
let gate=null,failure=null;
function deferNext(url){let release,start;const started=new Promise(resolve=>start=resolve),hold=new Promise(resolve=>release=resolve);gate={url,start,hold};return{started,release};}
function failNext(url){failure=url;}
const context=vm.createContext({document,window,console,fetch:async(url,opts)=>{
 calls.push({url,data:opts?.body?JSON.parse(opts.body):null});
 if(failure===url){failure=null;return{ok:false,status:400,json:async()=>({error:'Injected boundary rejection; batch unchanged.'})};}
 const held=gate?.url===url?gate:null;if(held)gate=null;
 const response=await fetch(base+url,opts);if(held){held.start();await held.hold;}return response;
},requestAnimationFrame(){},setTimeout:()=>0,clearTimeout(){},Blob,URL});
const run=source=>vm.runInContext(source,context);
run(fs.readFileSync(path.join(__dirname,'../web/app.js'),'utf8'));
const read=async(scope='sandbox')=>await(await fetch(`${base}/api/${scope==='objective'?'objective/':''}state`)).json();
const replay=async(scope='sandbox')=>await(await fetch(`${base}/api/${scope==='objective'?'objective/':''}replay`)).json();
const wait=async predicate=>{for(let i=0;i<300;i++){if(predicate())return;await new Promise(resolve=>setTimeout(resolve,10));}throw Error(`Timed out. ${get('toast').textContent}`);};
const choose=async(value)=>{get('scenario').value=value;await get('scenario').onchange();};
const mode=value=>{get('mode').value=String(value);get('mode').onchange();};
const selectFile=async data=>get('replay-file').onchange({target:{files:[{size:JSON.stringify(data).length,text:async()=>JSON.stringify(data)}],value:''}});
const busyIds=['play','step','reset','export','import','mode','scenario','boundary-policy','credit-strategy','objective-apply','simulation-speed','advance-second','advance-ten'];
const assertBusy=expected=>{for(const id of busyIds)assert.equal(get(id).disabled,expected,`${id} busy=${expected}`);};
const allText=node=>[node.textContent,...node.children.map(child=>typeof child==='string'?child:allText(child))].join(' ');
(async()=>{
 await wait(()=>get('connection').textContent==='C++ 引擎在线');
 // Each suite owns its fixture. Earlier HTTP tests may intentionally leave scoring enabled.
 const fixtureResponse=await fetch(base+'/api/objective/reset',{method:'POST',headers:{'Content-Type':'application/json','X-Sandbox-Client':'1'},body:JSON.stringify({boundary_policy:'strict-reject-unadjudicated-v1',credit_strategy:'disabled'})});
 assert.equal(fixtureResponse.ok,true,'explicit objective fixture reset succeeds');
 const fixture=await fixtureResponse.json();assert.equal(fixture.objective.boundary_policy,'strict-reject-unadjudicated-v1');assert.equal(fixture.objective.credit_strategy,'disabled');
 await get('reset').onclick();
 // A mode change waits for an in-flight native mutation and commits metadata/state together.
 const delayed=deferNext('/api/step');const advancing=run('advance(1,[1000,0,10000,3800,1])');await delayed.started;
 const switching=choose('objective');assertBusy(true);assert.equal(run('scenario'),'sandbox');
 await get('play').onclick();assert.equal(run('playing'),false);
 delayed.release();await advancing;await switching;
 assert.equal(run('scenario'),'objective');assert.equal(run('state.profile'),'objective-experimental-v1');assert.equal(run('meta.profile'),'objective-experimental-v1');assertBusy(false);
 assert.equal(get('objective-controls').hidden,false);assert.equal(get('red-robot-card').hidden,true);assert.equal(get('blue-robot-card').hidden,true);assert.match(get('fire-help').textContent,/禁用/);
 assert.equal(get('mode').value,'1','objective starts with useful demo policy');
 const sandboxState=await read(),sandboxReplay=await replay();assert.equal(sandboxState.tick,1);
 await get('reset').onclick();assert.equal(get('credit-strategy').value,'disabled');assert.equal(get('boundary-policy').value,'strict-reject-unadjudicated-v1');
 mode(0);const before=await read('objective');run("keys.add('KeyD');keys.add('Space');mouseDown=true");await get('step').onclick();let after=await read('objective');
 assert.equal(after.robots[0].x,before.robots[0].x+55,'held-key objective single-step moves');assert.equal(after.robots[0].shots,0,'space and mouse cannot fire in objective mode');assert.equal(calls.at(-1).data.action[4],0);
 // Brief physical demo must visibly reach capture without requiring a 420-second wait.
 await get('reset').onclick();mode(1);await get('advance-ten').onclick();after=await read('objective');
 assert.equal(after.tick,600);assert.equal(after.objective.zones.filter(zone=>zone.completed).length,2);assert.equal(after.objective.score,null);assert.equal(get('red-score').textContent,'未启用');
 assert.match(allText(get('objective-zones')),/临时控制锁存/);assert.match(allText(get('objective-zones')),/检测有/);assert.match(allText(get('objective-zones')),/逻辑激活/);
 assert.match(allText(get('events')),/红方.*锁存/);assert.match(allText(get('events')),/7\.050s/,'capture log uses native7050ms completion, not10s response time');assert.match(allText(get('events')),/9\.067s/);run('draw()');
 const objectiveReplay=await replay('objective');assert.equal(objectiveReplay.profile,'objective-experimental-v1');
 // A failed target load leaves old mode/meta/state unchanged and paused.
 const oldHash=run('state.hash');failNext('/api/meta');await choose('sandbox');assert.equal(run('scenario'),'objective');assert.equal(run('state.hash'),oldHash);assert.equal(run('meta.profile'),'objective-experimental-v1');assert.equal(run('playing'),false);assert.equal(get('scenario').value,'objective');
 // File-picker snapshot survives a mode change; stale file selection never routes to the other engine.
 await get('import').onclick();await choose('sandbox');const importCount=calls.filter(call=>call.url.endsWith('/import')).length;
 await selectFile(objectiveReplay);assert.match(get('toast').textContent,/状态已变化/);assert.equal(calls.filter(call=>call.url.endsWith('/import')).length,importCount);assert.equal((await read()).hash,sandboxState.hash);
 await choose('objective');await selectFile(sandboxReplay);assert.match(get('toast').textContent,/其他模式/);assert.equal(calls.filter(call=>call.url.endsWith('/import')).length,importCount);
 // Reset invalidates an open file-picker even if the selected mode did not change.
 await get('import').onclick();await get('reset').onclick();await selectFile(objectiveReplay);assert.match(get('toast').textContent,/状态已变化/);assert.equal((await read('objective')).tick,0);
 // Delayed file read acquires every busy barrier; programmatic clicks cannot switch/restart.
 let releaseFile;const importOperation=get('replay-file').onchange({target:{files:[{size:100,text:()=>new Promise(resolve=>releaseFile=resolve)}],value:''}});
 await wait(()=>!!releaseFile);assertBusy(true);assert.equal(get('replay-status').hidden,false);assert.match(get('replay-status').textContent,/正在读取并校验/);await get('play').onclick();await choose('sandbox');await get('advance-ten').onclick();assert.equal(run('scenario'),'objective');assert.equal(run('playing'),false);
 const importGate=deferNext('/api/objective/import');releaseFile(JSON.stringify(objectiveReplay));await importGate.started;assertBusy(true);assert.match(get('replay-status').textContent,/正在逐帧校验/);importGate.release();await importOperation;assertBusy(false);assert.match(get('replay-status').textContent,/已校验 600 帧/);assert.equal((await read('objective')).hash,objectiveReplay.final_hash);assert.equal(run('playing'),false);
 const corrupted={...objectiveReplay,final_hash:(objectiveReplay.final_hash^1)>>>0};await selectFile(corrupted);assert.equal((await read('objective')).hash,objectiveReplay.final_hash);assert.match(get('toast').textContent,/hash mismatch/i);assert.match(get('replay-status').textContent,/校验失败/);
 // Successful objective export uses the native objective replay identity, never sandbox endpoint/data.
 await get('export').onclick();assert.equal(calls.at(-1).url,'/api/objective/replay');assert.ok(created.some(node=>node.download==='tactics-objective-tick-600.json'));
 // Config changes stop playback; applying config resets atomically and exposes exact identity.
 get('boundary-policy').value='ENGINEERING-NOT-OFFICIAL-timer-before-input-v1';get('boundary-policy').onchange();
 get('credit-strategy').value='EXPERIMENTAL-end-of-second-v1';get('credit-strategy').onchange();assert.match(get('config-status').textContent,/尚未应用/);
 await get('objective-apply').onclick();assert.match(get('active-boundary').textContent,/ENGINEERING-NOT-OFFICIAL/);assert.match(get('active-credit').textContent,/EXPERIMENTAL-end-of-second/);assert.equal((await read('objective')).tick,0);
 await get('advance-ten').onclick();await get('advance-ten').onclick();after=await read('objective');assert.deepEqual(after.objective.score,[13,33]);assert.equal(String(get('red-score').textContent),'13');assert.equal(String(get('blue-score').textContent),'33');
 // Simulate backpressure at12x. No physical tick is skipped and outstanding time is retained.
 await get('reset').onclick();get('simulation-speed').value='12';get('simulation-speed').onchange();
 const speedGate=deferNext('/api/objective/step');run('setPlaying(true);lastTime=1000;loop(1100)');await speedGate.started;
 run('loop(1200)');assert.ok(run('accumulator')>=1.19,'in-flight time remains queued');speedGate.release();await wait(()=>run('inFlight===null'));
 run('loop(1300)');await wait(()=>run('inFlight===null'));run('setPlaying(false)');assert.equal((await read('objective')).tick,216,'0.3 wall seconds at12x processes216 native ticks');
 // API rejection must pause, preserve state, and never silently change strict/credit config.
 const rejectBefore=await read('objective');failNext('/api/objective/step');run('setPlaying(true)');await run('advance(60)');assert.equal(run('playing'),false);assert.equal((await read('objective')).hash,rejectBefore.hash);assert.match(get('toast').textContent,/boundary rejection/);
 run("keys.add('KeyD');mouseDown=true;setPlaying(true)");windowEvents.blur();assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);assert.equal(run('mouseDown'),false);
 // Reach exact real420s through bounded native steps, including terminal display.
 await get('reset').onclick();mode(1);for(let i=0;i<42;i++)await get('advance-ten').onclick();after=await read('objective');
 assert.equal(after.tick,25200);assert.equal(after.objective.elapsed_ms,420000);assert.equal(after.objective.result.outcome,'blue-wins');assert.equal(run('playing'),false);assert.equal(get('overlay').hidden,false);assert.match(get('objective-phase').textContent,/蓝方胜出/);assert.equal(get('time').textContent,'07:00.0');
 // Full420s replay survives UI export/import, including formatting larger than the sandbox limit.
 const fullReplay=await replay('objective'),fullPretty=JSON.stringify(fullReplay,null,1);assert.ok(fullPretty.length<4000000);
 await get('export').onclick();assert.ok(created.some(node=>node.download==='tactics-objective-tick-25200.json'));
 await get('reset').onclick();await get('replay-file').onchange({target:{files:[{size:fullPretty.length,text:async()=>fullPretty}],value:''}});
 assert.equal((await read('objective')).hash,fullReplay.final_hash);assert.equal(run('state.tick'),25200);assert.equal(run('playing'),false);assert.match(get('toast').textContent,/25200/);
 console.log(`Full420s objective replay UI roundtrip: ${fullPretty.length} bytes,25200 frames.`);
 // Returning to sandbox restores its independent state and weapon behavior.
 await choose('sandbox');assert.equal((await read()).hash,sandboxState.hash);assert.equal(get('objective-controls').hidden,true);assert.equal(get('red-robot-card').hidden,false);assert.equal(get('mode').value,'0');
 run("keys.add('Space')");await get('step').onclick();assert.equal((await read()).robots[0].shots,1,'combat cooldown leaves original shot count unchanged after one extra tick');
 console.log('PASS objective DOM/real API: mode isolation, in-flight switch, failed switch rollback, held movement/no fire, capture in10s, latched-control display, strict/default-off configuration, wrong/stale replay isolation, delayed import busy guards, objective replay hash validation, export identity, explicit scoring,12x backpressure/no skipped ticks, error pause, blur release,420s terminal and sandbox preservation. Real browser rendering remains unverified.');
})().catch(error=>{console.error(error);process.exitCode=1;});
