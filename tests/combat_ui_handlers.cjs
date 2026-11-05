// Actual combat.js handlers with a mock DOM and live loopback HTTP/native C++.
// This does NOT verify browser layout, rendered pixels, pointer capture or download persistence.
'use strict';
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict'), path = require('node:path');
const base = process.argv[2] || 'http://127.0.0.1:8777';
const html = fs.readFileSync(path.join(__dirname,'../web/combat.html'),'utf8');
const drawing = [], created = [], blobs = [], revoked = [], calls = [], windowEvents = {}, documentEvents = {};
const ctx = new Proxy({}, {get(target,key) { return key in target ? target[key] : (...args) => drawing.push({method:key,args,stroke:target.strokeStyle,fill:target.fillStyle}); }});
class Element {
  constructor(id,tag='div') { this.id=id; this.tagName=tag.toUpperCase(); this.value=''; this.checked=false; this.textContent=''; this.className=''; this.hidden=false; this.disabled=false; this.children=[]; this.files=[]; this.width=1120; this.height=760; this.style={}; this.attributes={}; }
  append(...items) { this.children.push(...items); } replaceChildren(...items) { this.children=items; this.textContent=''; }
  remove() { this.removed=true; } click() { this.clicked=(this.clicked||0)+1; } getContext() { return ctx; }
  setAttribute(key,value) { this.attributes[key]=value; } setPointerCapture() {}
  getBoundingClientRect() { return {left:0,top:0,width:this.width,height:this.height}; }
}
const elements = new Map();
const get = id => { assert.ok(html.includes(`id="${id}"`),`HTML contains ${id}`); if (!elements.has(id)) elements.set(id,new Element(id)); return elements.get(id); };
const document = {getElementById:get,body:new Element('body'),hidden:false,addEventListener(type,callback) { documentEvents[type]=callback; },createElement(tag) { const item=new Element('',tag); created.push(item); return item; }};
const window = {addEventListener(type,callback) { windowEvents[type]=callback; }};
let gate=null, failure=null, failureStatus=400, drop=null, dropAfter=null, rewrite=null;
function deferred() { let release; const promise=new Promise(resolve=>release=resolve); return {promise,release}; }
function deferNext(url) { const started=deferred(),held=deferred(); gate={url,started,held}; return {started:started.promise,release:held.release}; }
const context=vm.createContext({document,window,console,TextEncoder,Blob,URL:{createObjectURL(blob) { blobs.push(blob); return `blob:combat-${blobs.length}`; },revokeObjectURL(url) { revoked.push(url); }},setTimeout(fn) { fn(); return 0; },requestAnimationFrame() {},fetch:async(url,options)=>{
  calls.push({url,text:options?.body,data:options?.body?(()=>{try{return JSON.parse(options.body);}catch{return null;}})():null});
  if (failure===url) { failure=null; const status=failureStatus; failureStatus=400; return {ok:false,status,json:async()=>({error:status<500?'Injected native rejection; unchanged.':'Injected server failure.'})}; }
  if (drop===url) { drop=null; throw Error('Injected uncertain connection loss'); }
  const hold=gate?.url===url?gate:null; if (hold) gate=null;
  let response=await fetch(base+url,options);
  if (dropAfter===url) { dropAfter=null; throw Error('Injected loss after native commit'); }
  if (rewrite?.url===url) { const change=rewrite; rewrite=null; const body=await response.json(); change.edit(body); response={ok:response.ok,status:response.status,json:async()=>body}; }
  if (hold) { hold.started.release(); await hold.held.promise; }
  return response;
}});
const run=source=>vm.runInContext(source,context);
const allText=element=>[element.textContent,...element.children.map(child=>typeof child==='string'?child:allText(child))].join(' ');
const wait=async predicate=>{for(let i=0;i<600;i++){if(predicate())return;await new Promise(resolve=>setTimeout(resolve,10));}throw Error(`Timed out: ${get('operation-status').textContent}`);};
const read=async route=>{const response=await fetch(base+route),body=await response.json();assert.ok(response.ok,JSON.stringify(body));return body;};
const post=async(route,data)=>{const response=await fetch(base+route,{method:'POST',headers:{'Content-Type':'application/json','X-Sandbox-Client':'1'},body:JSON.stringify(data)}),body=await response.json();assert.ok(response.ok,JSON.stringify(body));return body;};
const input=(code,down=true,target=null,repeat=false)=>windowEvents[down?'keydown':'keyup']({code,target,repeat,preventDefault(){}});
const choose=(index,kind)=>{get('source-'+index).value=kind;get('source-'+index).onchange();};
const perspective=index=>{get('observer').value=String(index);return get('observer').onchange();};
const selectFile=(text,size=Buffer.byteLength(text),name='developer-replay.json')=>get('replay-file').onchange({target:{files:[{name,size,text:async()=>text}],value:'chosen'}});
const assertBusy=()=>{for(const id of ['step','reset','import-replay','export-replay','source-0','source-1','observer'])assert.equal(get(id).disabled,true,`${id} pending-operation barrier`);};
const assertReady=()=>assert.equal(run('ready()'),true,get('operation-status').textContent);
const stepCalls=()=>calls.filter(call=>call.url==='/api/combat/step');
const move=async(code,count)=>{input(code);await run(`advance(${count})`);input(code,false);assertReady();};
(async()=>{
  const original={sandbox:await read('/api/state'),objective:await read('/api/objective/state'),layered:await read('/api/layered/state')};
  await post('/api/combat/reset',{observer:0});
  run(fs.readFileSync(path.join(__dirname,'../web/combat.js'),'utf8'));
  await wait(()=>run('ready() && !busy'));
  assert.equal(run('observer'),0);assert.equal(run('state.tick'),0);assert.equal(run('state.observation.enemy'),null);assert.equal(run('state.last_sources'),null);
  assert.deepEqual(JSON.parse(run('JSON.stringify(sources)')),['manual','policy']);assert.equal(get('map-veil').hidden,true);
  assert.equal(run('state.hash'),undefined);assert.equal(run('state.observation.self.shots'),undefined);assert.equal(run('state.observation.self.hits'),undefined);
  assert.match(allText(get('mode-metadata')),/非官方/);assert.match(allText(get('policy-metadata')),/trusted-local-native/);
  assert.ok(html.includes('combat_mode/example_policy.cpp'));assert.ok(html.includes('导出会暴露两台机器人的逐帧已解析动作'));assert.ok(html.includes('没有 CPU / 内存或安全隔离'));
  drawing.length=0;run('drawMap()');assert.equal(drawing.some(entry=>entry.method==='stroke'&&entry.stroke==='#ff9683'),false,'Hidden opponent is not drawn');
  // Both source choices are independent; the unselected manual robot stays neutral.
  choose(1,'manual');await get('reset').onclick();const start0=run('state.observation.self.x'),start1=(await read('/api/combat/state?observer=1')).observation.self.x;
  input('KeyD');input('Space');await get('step').onclick();
  assert.equal(run('state.observation.self.x'),start0+55);assert.equal(run('keys.size'),0);assert.equal(run('pointers.size'),0);
  const heldStep=stepCalls().at(-1).data;assert.equal(heldStep.observer,0);assert.equal(heldStep.controllers[0].action.dx,1000);assert.equal(heldStep.controllers[0].action.fire,1);
  assert.deepEqual(heldStep.controllers[1],{kind:'manual',action:{sdk_version:1,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}});
  assert.equal((await read('/api/combat/state?observer=1')).observation.self.x,start1);
  input('KeyD',true,null,true);input('Space',true,null,true);await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers[0].action.dx,0);assert.equal(stepCalls().at(-1).data.controllers[0].action.fire,0,'Released keys cannot autorepeat into the next step');
  input('KeyW',true,{tagName:'TEXTAREA'});input('Space',true,{tagName:'BUTTON'});await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers[0].action.dy,0);assert.equal(stepCalls().at(-1).data.controllers[0].action.fire,0);
  // Touch direction/fire and map aim go through the exact same manual action payload.
  get('drive-up').onpointerdown({pointerId:7,preventDefault(){}});get('fire').onpointerdown({pointerId:8,preventDefault(){}});await get('step').onclick();
  assert.equal(stepCalls().at(-1).data.controllers[0].action.dy,1000);assert.equal(stepCalls().at(-1).data.controllers[0].action.fire,1);assert.equal(run('pointers.size'),0);
  get('drive-up').onpointerdown({pointerId:9,preventDefault(){}});get('drive-up').onpointercancel({pointerId:9});assert.equal(run('pointers.size'),0);
  get('combat-map').onpointerdown({pointerId:10,button:0,clientX:900,clientY:100,preventDefault(){}});assert.equal(run('pointers.size'),1);const target=JSON.parse(run('JSON.stringify(aim)'));await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers[0].action.aim_x,target.x);assert.equal(stepCalls().at(-1).data.controllers[0].action.aim_y,target.y);assert.equal(run('aim'),null);
  get('combat-map').onpointerdown({pointerId:11,button:0,clientX:900,clientY:100,preventDefault(){}});get('combat-map').onlostpointercapture({pointerId:11});assert.equal(run('pointers.size'),0);
  // Source changes pause and release everything without a reset or request.
  input('KeyD');get('fire').onpointerdown({pointerId:12,preventDefault(){}});run('setPlaying(true)');const beforeSource=calls.length,beforeSourceTick=run('state.tick');choose(0,'policy');
  assert.equal(calls.length,beforeSource);assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);assert.equal(run('pointers.size'),0);assert.equal(run('aim'),null);assert.equal(run('state.tick'),beforeSourceTick);assert.equal(get('drive-up').disabled,true);
  choose(1,'policy');await run('advance(10)');assertReady();assert.equal(run('state.tick'),beforeSourceTick+10);assert.ok(stepCalls().at(-1).data.controllers.every(source=>source.kind==='policy'));assert.ok(run('state.last_sources.every(source => source.kind === "policy")'));
  input('KeyD');assert.equal(run('keys.size'),0);choose(0,'nonsense');assert.equal(run('sources[0]'),'policy');
  // Switching perspective reads only the chosen view, never a pair of observations.
  const perspectiveGate=deferNext('/api/combat/state?observer=1');const switching=perspective(1);await perspectiveGate.started;assertBusy();assert.equal(run('playing'),false);assert.equal(get('map-veil').hidden,false);
  drawing.length=0;run('drawMap()');assert.equal(drawing.some(entry=>entry.method==='arc'),false,'No old observer is drawn while loading another');
  const duringPerspective=calls.length;choose(0,'manual');await perspective(0);await get('step').onclick();await get('reset').onclick();assert.equal(calls.length,duringPerspective);assert.equal(run('observer'),1);assert.equal(run('sources[0]'),'policy');
  perspectiveGate.release();await switching;assertReady();assert.equal(run('state.observation.self_index'),1);assert.equal(run('state.observer'),1);
  choose(0,'manual');choose(1,'manual');await get('reset').onclick();const robot0=(await read('/api/combat/state?observer=0')).observation.self.x,robot1=run('state.observation.self.x');
  await move('KeyA',1);assert.equal(run('state.observation.self.x'),robot1-55);assert.equal((await read('/api/combat/state?observer=0')).observation.self.x,robot0);assert.equal(stepCalls().at(-1).data.controllers[0].action.dx,0);assert.equal(stepCalls().at(-1).data.controllers[1].action.dx,-1000);
  // Actual native movement takes the observer around cover and into enemy visibility.
  await perspective(0);await get('reset').onclick();await move('KeyW',30);await move('KeyD',110);
  assert.notEqual(run('state.observation.enemy'),null);assert.equal(run('state.observation.enemy.hp'),undefined);
  drawing.length=0;run('drawMap()');assert.ok(drawing.some(entry=>entry.method==='stroke'&&entry.stroke==='#ff9683'),'Only a returned visible opponent is drawn');
  input('Space');await get('step').onclick();assert.ok(run('state.observation.projectiles.length')>0);assert.ok(run('state.observation.projectiles.every(p => Object.keys(p).sort().join() === "vx,vy,x,y")'));
  await get('reset').onclick();assert.equal(run('state.observation.enemy'),null);drawing.length=0;run('drawMap()');assert.equal(drawing.some(entry=>entry.method==='stroke'&&entry.stroke==='#ff9683'),false,'No enemy ghost survives a hidden observation');
  // Live native policy decisions, explicit developer export and deterministic action-only import.
  choose(0,'policy');choose(1,'policy');await run('advance(120)');assertReady();const replay=await read('/api/combat/replay'),replayText=JSON.stringify(replay);
  assert.equal(replay.frames.length,120);assert.ok(replay.frames.every(frame=>frame.inputs.every(source=>source.kind==='policy')));
  await get('export-replay').onclick();assert.equal(blobs.length,1);assert.deepEqual(JSON.parse(await blobs[0].text()),replay);assert.equal(revoked.length,1);assert.ok(created.some(item=>item.download==='tactics-filtered-combat-developer-tick-120.json'&&item.clicked));assert.match(get('operation-status').textContent,/已请求下载.*双方已解析动作/);
  await run('advance(120)');assertReady();assert.equal(run('state.tick'),139);assert.equal(run('state.advanced'),19);assert.equal(run('state.terminal'),1);assert.equal(get('step').disabled,true);assert.match(get('operation-status').textContent,/终止/);
  await get('reset').onclick();const policyBeforeImport=stepCalls().length;await selectFile(replayText);assertReady();assert.equal(run('state.tick'),120);assert.equal(stepCalls().length,policyBeforeImport);assert.equal(run('playing'),false);assert.match(get('replay-status').textContent,/没有重新执行策略/);assert.equal((await read('/api/combat/replay')).final_hash,replay.final_hash);
  const hostileName='<img src=x onerror=alert(1)>.json';await selectFile(replayText,Buffer.byteLength(replayText),hostileName);assert.ok(get('replay-status').textContent.includes(hostileName));assert.ok(created.every(item=>!['IMG','SCRIPT'].includes(item.tagName)));
  const broken=structuredClone(replay);broken.final_hash=(broken.final_hash+1)>>>0;await selectFile(JSON.stringify(broken));assert.equal(run('state.tick'),120);assert.equal((await read('/api/combat/replay')).final_hash,replay.final_hash);assert.match(get('replay-status').textContent,/校验失败/);
  const duplicate=replayText.replace('"schema":','"schema":"duplicate","schema":');await selectFile(duplicate);assert.equal(calls.at(-1).text,`{"observer":0,"replay":${duplicate}}`);assert.match(get('replay-status').textContent,/校验失败/);
  const historical=structuredClone(replay);historical.frames.forEach(frame=>frame.inputs.forEach(source=>source.controller_id='historical-record-only'));await selectFile(JSON.stringify(historical));assertReady();assert.match(get('source-status').textContent,/historical-record-only.*未经认证/);assert.equal(run('meta.policy.controller_id'),'filtered-cover-v1');await get('step').onclick();assert.ok(stepCalls().at(-1).data.controllers.every(source=>source.controller_id==='filtered-cover-v1'));
  // Picker snapshots reject state, source and perspective changes while the picker is open.
  get('import-replay').onclick();choose(0,'manual');const beforeStale=calls.filter(call=>call.url==='/api/combat/import').length;await selectFile(replayText);assert.equal(calls.filter(call=>call.url==='/api/combat/import').length,beforeStale);assert.match(get('operation-status').textContent,/已变化/);
  get('import-replay').onclick();await perspective(1);const beforeOtherPerspective=calls.filter(call=>call.url==='/api/combat/import').length;await selectFile(replayText);assert.equal(calls.filter(call=>call.url==='/api/combat/import').length,beforeOtherPerspective);get('replay-file').oncancel();
  // File reads, imports, reset and export share one pending-operation barrier.
  const fileGate=deferred(),reading=get('replay-file').onchange({target:{files:[{size:replayText.length,text:()=>fileGate.promise}],value:''}});assertBusy();const duringRead=calls.length;choose(1,'manual');await perspective(0);await get('step').onclick();await get('reset').onclick();await get('export-replay').onclick();assert.equal(calls.length,duringRead);
  const importGate=deferNext('/api/combat/import');fileGate.release(replayText);await importGate.started;assertBusy();importGate.release();await reading;assertReady();assert.equal(run('observer'),1);
  const resetGate=deferNext('/api/combat/reset'),resetting=get('reset').onclick();await resetGate.started;assertBusy();resetGate.release();await resetting;
  const exportGate=deferNext('/api/combat/replay'),exporting=get('export-replay').onclick();await exportGate.started;assertBusy();const duringExport=calls.length;await get('reset').onclick();await get('step').onclick();choose(0,'policy');assert.equal(calls.length,duringExport);exportGate.release();await exporting;
  // Pausing an in-flight step is allowed; no competing mutations or relabeling are allowed.
  choose(0,'policy');choose(1,'policy');run('setPlaying(true)');const stepGate=deferNext('/api/combat/step'),stepping=run('advance(10)');await stepGate.started;assertBusy();const duringStep=calls.length;
  choose(0,'manual');await perspective(0);await get('reset').onclick();await get('export-replay').onclick();assert.equal(calls.length,duringStep);assert.equal(run('sources[0]'),'policy');get('play').onclick();assert.equal(run('playing'),false);stepGate.release();await stepping;assertReady();assert.equal(run('state.tick'),10);
  // Explicit rejection preserves the last good view and never silently switches to manual.
  const rejectedTick=run('state.tick');failure='/api/combat/step';run('setPlaying(true)');await run('advance(1)');assert.equal(run('playing'),false);assert.equal(run('state.tick'),rejectedTick);assertReady();assert.equal(run('sources[0]'),'policy');assert.match(get('operation-status').textContent,/不判负/);
  // Uncertain mutations are never retried: one state read establishes the actual outcome.
  const beforeLost=stepCalls().length;dropAfter='/api/combat/step';await run('advance(3)');assertReady();assert.equal(stepCalls().length,beforeLost+1);assert.equal(run('state.tick'),rejectedTick+3);assert.equal(run('playing'),false);assert.match(get('operation-status').textContent,/没有自动重试/);
  const beforeDropped=run('state.tick');drop='/api/combat/step';await run('advance(2)');assertReady();assert.equal(run('state.tick'),beforeDropped);assert.equal(run('playing'),false);
  failure='/api/combat/step';failureStatus=500;await run('advance(2)');assertReady();assert.equal(run('state.tick'),beforeDropped);
  // If the recovery read fails too, the old view stays blank and all control remains disabled.
  dropAfter='/api/combat/step';failure='/api/combat/state?observer=1';await run('advance(1)');assert.equal(run('ready()'),false);assert.equal(get('map-veil').hidden,false);assert.equal(get('reconnect').hidden,false);await get('reconnect').onclick();assertReady();
  // Wrong source, SDK, observer, advanced count and filtered payloads are not applied.
  for(const edit of [body=>{body.last_sources[0].controller_id='stale-controller';},body=>{body.last_sources[1].sdk_version=999;},body=>{body.last_sources[0].action=[0,0,0,0,0];},body=>{body.observer=0;},body=>{body.tick-=1;},body=>{body.advanced=0;},body=>{body.observation.self.hits=999;},body=>{body.observation.enemy={x:1000,y:1000,yaw:0,hp:100};},body=>{body.hash=123;}]){
    const tick=run('state.tick'),count=stepCalls().length;rewrite={url:'/api/combat/step',edit};await run('advance(1)');assertReady();assert.equal(run('state.tick'),tick+1);assert.equal(stepCalls().length,count+1);assert.equal(run('playing'),false);assert.match(get('operation-status').textContent,/没有自动重试/);assert.equal(run('state.hash'),undefined);assert.equal(run('state.observation.self.hits'),undefined);
  }
  // Invalid policy metadata is fixed text; explicitly selected manual control still works.
  rewrite={url:'/api/combat/meta',edit:body=>{body.policy_error='PRIVATE POLICY EXCEPTION SENTINEL';body.policy=null;}};await get('reconnect').onclick();assertReady();assert.equal(run('policyAvailable'),false);assert.equal(get('step').disabled,true);assert.ok(!allText(get('policy-metadata')).includes('PRIVATE POLICY EXCEPTION'));
  choose(0,'manual');choose(1,'manual');assert.equal(get('step').disabled,false);await get('step').onclick();assertReady();assert.ok(stepCalls().at(-1).data.controllers.every(source=>source.kind==='manual'));choose(1,'policy');assert.equal(run('sources[1]'),'manual');await get('reconnect').onclick();assert.equal(run('policyAvailable'),true);
  // Oversized files never reach the server; blur and backgrounding release all held inputs.
  const beforeOversize=calls.length;await selectFile('{}',12*1024*1024+1);assert.equal(calls.length,beforeOversize);assert.match(get('operation-status').textContent,/12 MiB/);
  input('KeyW');run('setPlaying(true)');windowEvents.blur();assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);run('setPlaying(true)');document.hidden=true;documentEvents.visibilitychange();assert.equal(run('playing'),false);document.hidden=false;
  // Long frames keep backlog but native batches are capped, not sped up in the browser.
  await get('reset').onclick();run('setPlaying(true); lastTime=1000; loop(6000)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),120);assert.ok(run('accumulator')>=180);run('loop(6000)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),240);run('pauseAndRelease()');
  assert.ok(stepCalls().every(call=>call.data.count>=1&&call.data.count<=120));
  // Terminal native state stops play and stepping, while reset/import/export stay available.
  await get('reset').onclick();while(run('state.terminal')===0){await run('advance(120)');assertReady();}
  assert.equal(run('state.tick'),7200);assert.equal(run('playing'),false);assert.equal(get('play').disabled,true);assert.equal(get('step').disabled,true);assert.equal(get('export-replay').disabled,false);assert.equal(run('state.observation.enemy'),null);
  // Another tab advancing cannot be exported under a stale visible frame count.
  await get('reset').onclick();const beforeForeignExport=blobs.length;await post('/api/combat/step',{observer:1,count:1,controllers:[{kind:'manual',action:{sdk_version:1,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}},{kind:'manual',action:{sdk_version:1,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}}]});await get('export-replay').onclick();assert.equal(blobs.length,beforeForeignExport);assert.equal(run('ready()'),false);assert.match(get('operation-status').textContent,/不一致/);await get('reconnect').onclick();assertReady();
  assert.equal((await read('/api/state')).hash,original.sandbox.hash);assert.equal((await read('/api/objective/state')).hash,original.objective.hash);assert.equal((await read('/api/layered/state')).hash,original.layered.hash);assert.ok(calls.every(call=>call.url.startsWith('/api/combat/')));
  console.log('PASS filtered combat DOM/live native HTTP: independent sources, held single-step before release, neutral unselected manual robot, mouse/touch input, selected observer only, native cover visibility without ghosts, policy execution, explicit developer replay export/import, safe literal labels, stale pickers, all busy guards, uncertain-response resync without retry, strict response validation, manual availability on invalid policy metadata, bounded batches, terminal stop and mode isolation. Real browser rendering/pointer capture/download persistence remain unverified.');
})().catch(error=>{console.error(error);process.exitCode=1;});
