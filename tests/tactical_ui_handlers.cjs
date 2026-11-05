// Actual tactical.js handlers with a mock DOM and live loopback HTTP/native C++.
// This does NOT verify browser layout, rendered pixels, pointer capture or download persistence.
'use strict';
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict'), path = require('node:path');
const base = process.argv[2] || 'http://127.0.0.1:8777';
const html = fs.readFileSync(path.join(__dirname,'../web/tactical.html'),'utf8');
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
const context=vm.createContext({document,window,console,TextEncoder,Blob,URL:{createObjectURL(blob) { blobs.push(blob); return `blob:tactical-${blobs.length}`; },revokeObjectURL(url) { revoked.push(url); }},setTimeout(fn) { fn(); return 0; },requestAnimationFrame() {},fetch:async(url,options)=>{
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
const assertBusy=()=>{for(const id of ['step','advance-1s','advance-10s','speed','boundary-policy','credit-strategy','attack-damage-strategy','heat-strategy','heat-threshold-policy','reset','import-replay','export-replay','source-0','source-1','observer'])assert.equal(get(id).disabled,true,`${id} pending-operation barrier`);};
const assertReady=()=>assert.equal(run('ready()'),true,get('operation-status').textContent);
const stepCalls=()=>calls.filter(call=>call.url==='/api/tactical/step');
const move=async(code,count)=>{input(code);await run(`advance(${count})`);input(code,false);assertReady();};
(async()=>{
  const original={sandbox:await read('/api/state'),objective:await read('/api/objective/state'),layered:await read('/api/layered/state'),combat:await read('/api/combat/replay')};
  await post('/api/tactical/reset',{observer:0,config:{boundary_policy:'ENGINEERING-NOT-OFFICIAL-timer-before-input-v1',credit_strategy:'EXPERIMENTAL-end-of-second-v1',attack_damage_strategy:'EXPERIMENTAL-reconciled-projectile-hp-loss-v1',heat_strategy:'disabled',heat_threshold_policy:'strict-reject-unadjudicated'}});
  run(fs.readFileSync(path.join(__dirname,'../web/tactical.js'),'utf8'));
  await wait(()=>run('ready() && !busy'));
  assert.equal(run('observer'),0);assert.equal(run('state.tick'),0);assert.equal(run('state.observation.combat.enemy'),null);assert.equal(run('state.last_sources'),null);
  assert.deepEqual(JSON.parse(run('JSON.stringify(sources)')),['manual','policy']);assert.equal(get('map-veil').hidden,true);
  assert.equal(run('state.hash'),undefined);assert.equal(run('state.observation.combat.self.shots'),undefined);assert.equal(run('state.observation.combat.self.hits'),undefined);
  assert.match(allText(get('mode-metadata')),/非官方/);assert.match(allText(get('policy-metadata')),/trusted-local-native/);
  assert.ok(html.includes('tactical_mode/example_policy.cpp'));assert.ok(html.includes('导出会暴露两台机器人的逐帧已解析动作'));assert.ok(html.includes('没有 CPU / 内存或安全隔离'));
  assert.equal(run('state.config.preset'),'EXPERIMENTAL-custom-v3');
  assert.equal(run('state.observation.zones.length'),4);assert.deepEqual(JSON.parse(run('JSON.stringify(state.observation.score)')),[0,0]);
  assert.equal(get('remaining-time').textContent,'07:00');assert.equal(get('score-red').textContent,'0');assert.equal(get('objective-list').children.length,4);
  assert.ok(html.includes('不发布占领进度'));assert.ok(html.includes('公开目标信息不用于推测隐藏敌人'));assert.ok(html.includes('没有复活'));
  assert.ok(html.includes('2 s 停用宽限 / 5 s 占领'));assert.ok(html.includes('同时控制两个前哨：1 分 / s'));
  assert.ok(!Object.keys(run('state.observation')).includes('hash'));
  // Public terminal contract fixtures, not a claim that unequal surviving HP
  // is naturally reachable after equal attack in this equal-start/no-heal scene.
  run(`verifyResult({outcome:'red-wins',reason:'timeout-surviving-robot-hp'},25200,state.config,[0,0]);
       verifyResult({outcome:'unresolved-timeout-tie',reason:'missing-adjudicated-attack-damage'},25200,state.config,[0,0]);
       verifyResult({outcome:'unresolved-timeout-tie',reason:'missing-adjudicated-surviving-robot-hp'},25200,state.config,[0,0]);`);
  assert.throws(()=>run(`verifyResult({outcome:'red-wins',reason:'timeout-attack-damage'},25200,state.config,[0,1])`));
  assert.throws(()=>run(`verifyResult({outcome:'draw',reason:'timeout-equal',attack_damage:[0,0]},25200,state.config,[0,0])`));
  drawing.length=0;run('drawMap()');assert.equal(drawing.some(entry=>entry.method==='stroke'&&entry.stroke==='#ff9683'),false,'Hidden opponent is not drawn');
  // Both source choices are independent; the unselected manual robot stays neutral.
  choose(1,'manual');await get('reset').onclick();const start0=run('state.observation.combat.self.x'),start1=(await read('/api/tactical/state?observer=1')).observation.combat.self.x;
  input('KeyD');input('Space');await get('step').onclick();
  assert.equal(run('state.observation.combat.self.x'),start0+55);assert.equal(run('keys.size'),0);assert.equal(run('pointers.size'),0);
  const heldStep=stepCalls().at(-1).data;assert.equal(heldStep.observer,0);assert.equal(heldStep.controllers[0].action.dx,1000);assert.equal(heldStep.controllers[0].action.fire,1);
  assert.deepEqual(heldStep.controllers[1],{kind:'manual',action:{sdk_version:1,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}});
  assert.equal((await read('/api/tactical/state?observer=1')).observation.combat.self.x,start1);
  input('KeyD',true,null,true);input('Space',true,null,true);await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers[0].action.dx,0);assert.equal(stepCalls().at(-1).data.controllers[0].action.fire,0,'Released keys cannot autorepeat into the next step');
  input('KeyW',true,{tagName:'TEXTAREA'});input('Space',true,{tagName:'BUTTON'});await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers[0].action.dy,0);assert.equal(stepCalls().at(-1).data.controllers[0].action.fire,0);
  // Touch direction/fire and map aim go through the exact same manual action payload.
  get('drive-up').onpointerdown({pointerId:7,preventDefault(){}});get('fire').onpointerdown({pointerId:8,preventDefault(){}});await get('step').onclick();
  assert.equal(stepCalls().at(-1).data.controllers[0].action.dy,1000);assert.equal(stepCalls().at(-1).data.controllers[0].action.fire,1);assert.equal(run('pointers.size'),0);
  get('drive-up').onpointerdown({pointerId:9,preventDefault(){}});get('drive-up').onpointercancel({pointerId:9});assert.equal(run('pointers.size'),0);
  get('tactical-map').onpointerdown({pointerId:10,button:0,clientX:900,clientY:100,preventDefault(){}});assert.equal(run('pointers.size'),1);const target=JSON.parse(run('JSON.stringify(aim)'));await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers[0].action.aim_x,target.x);assert.equal(stepCalls().at(-1).data.controllers[0].action.aim_y,target.y);assert.equal(run('aim'),null);
  get('tactical-map').onpointerdown({pointerId:11,button:0,clientX:900,clientY:100,preventDefault(){}});get('tactical-map').onlostpointercapture({pointerId:11});assert.equal(run('pointers.size'),0);
  // Source changes pause and release everything without a reset or request.
  input('KeyD');get('fire').onpointerdown({pointerId:12,preventDefault(){}});run('setPlaying(true)');const beforeSource=calls.length,beforeSourceTick=run('state.tick');choose(0,'policy');
  assert.equal(calls.length,beforeSource);assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);assert.equal(run('pointers.size'),0);assert.equal(run('aim'),null);assert.equal(run('state.tick'),beforeSourceTick);assert.equal(get('drive-up').disabled,true);
  choose(1,'policy');await run('advance(10)');assertReady();assert.equal(run('state.tick'),beforeSourceTick+10);assert.ok(stepCalls().at(-1).data.controllers.every(source=>source.kind==='policy'));assert.ok(run('state.last_sources.every(source => source.kind === "policy")'));
  input('KeyD');assert.equal(run('keys.size'),0);choose(0,'nonsense');assert.equal(run('sources[0]'),'policy');
  // Switching perspective reads only the chosen view, never a pair of observations.
  const perspectiveGate=deferNext('/api/tactical/state?observer=1');const switching=perspective(1);await perspectiveGate.started;assertBusy();assert.equal(run('playing'),false);assert.equal(get('map-veil').hidden,false);
  drawing.length=0;run('drawMap()');assert.equal(drawing.some(entry=>entry.method==='arc'),false,'No old observer is drawn while loading another');
  const duringPerspective=calls.length;choose(0,'manual');await perspective(0);await get('step').onclick();await get('reset').onclick();assert.equal(calls.length,duringPerspective);assert.equal(run('observer'),1);assert.equal(run('sources[0]'),'policy');
  perspectiveGate.release();await switching;assertReady();assert.equal(run('state.observation.combat.self_index'),1);assert.equal(run('state.observer'),1);
  choose(0,'manual');choose(1,'manual');await get('reset').onclick();const robot0=(await read('/api/tactical/state?observer=0')).observation.combat.self.x,robot1=run('state.observation.combat.self.x');
  await move('KeyA',1);assert.equal(run('state.observation.combat.self.x'),robot1-55);assert.equal((await read('/api/tactical/state?observer=0')).observation.combat.self.x,robot0);assert.equal(stepCalls().at(-1).data.controllers[0].action.dx,0);assert.equal(stepCalls().at(-1).data.controllers[1].action.dx,-1000);
  // Actual native movement takes the observer around cover and into enemy visibility.
  await perspective(0);await get('reset').onclick();await move('KeyW',30);await move('KeyD',110);
  assert.notEqual(run('state.observation.combat.enemy'),null);assert.equal(run('state.observation.combat.enemy.hp'),undefined);
  drawing.length=0;run('drawMap()');assert.ok(drawing.some(entry=>entry.method==='stroke'&&entry.stroke==='#ff9683'),'Only a returned visible opponent is drawn');
  input('Space');await get('step').onclick();assert.ok(run('state.observation.combat.projectiles.length')>0);assert.ok(run('state.observation.combat.projectiles.every(p => Object.keys(p).sort().join() === "vx,vy,x,y")'));
  await get('reset').onclick();assert.equal(run('state.observation.combat.enemy'),null);drawing.length=0;run('drawMap()');assert.equal(drawing.some(entry=>entry.method==='stroke'&&entry.stroke==='#ff9683'),false,'No enemy ghost survives a hidden observation');
  // Live native policy decisions, explicit developer export and deterministic action-only import.
  choose(0,'policy');choose(1,'policy');await run('advance(120)');assertReady();const replay=await read('/api/tactical/replay'),replayText=JSON.stringify(replay);
  assert.equal(replay.frames.length,120);assert.ok(replay.frames.every(frame=>frame.inputs.every(source=>source.kind==='policy')));
  await get('export-replay').onclick();assert.equal(blobs.length,1);assert.deepEqual(JSON.parse(await blobs[0].text()),replay);assert.equal(revoked.length,1);assert.ok(created.some(item=>item.download==='tactics-tactical-developer-tick-120.json'&&item.clicked));assert.match(get('operation-status').textContent,/已请求下载.*双方已解析动作/);
  await run('advance(120)');assertReady();assert.equal(run('state.tick'),240);assert.equal(run('state.advanced'),120);assert.equal(run('state.terminal'),0);assert.equal(get('step').disabled,false);
  await get('reset').onclick();const policyBeforeImport=stepCalls().length;await selectFile(replayText);assertReady();assert.equal(run('state.tick'),120);assert.equal(stepCalls().length,policyBeforeImport);assert.equal(run('playing'),false);assert.match(get('replay-status').textContent,/没有重新执行策略/);assert.equal((await read('/api/tactical/replay')).final_hash,replay.final_hash);
  const hostileName='<img src=x onerror=alert(1)>.json';await selectFile(replayText,Buffer.byteLength(replayText),hostileName);assert.ok(get('replay-status').textContent.includes(hostileName));assert.ok(created.every(item=>!['IMG','SCRIPT'].includes(item.tagName)));
  const broken=structuredClone(replay);broken.final_hash=(broken.final_hash+1)>>>0;await selectFile(JSON.stringify(broken));assert.equal(run('state.tick'),120);assert.equal((await read('/api/tactical/replay')).final_hash,replay.final_hash);assert.match(get('replay-status').textContent,/校验失败/);
  const duplicate=replayText.replace('"schema":','"schema":"duplicate","schema":');await selectFile(duplicate);assert.equal(calls.at(-1).text,`{"observer":0,"replay":${duplicate}}`);assert.match(get('replay-status').textContent,/校验失败/);
  const historical=structuredClone(replay);historical.frames.forEach(frame=>frame.inputs.forEach(source=>source.controller_id='historical-record-only'));await selectFile(JSON.stringify(historical));assertReady();assert.match(get('source-status').textContent,/historical-record-only.*未经认证/);assert.equal(run('meta.policy.controller_id'),'public-capture-combat-v1');await get('step').onclick();assert.ok(stepCalls().at(-1).data.controllers.every(source=>source.controller_id==='public-capture-combat-v1'));
  // Picker snapshots reject state, source and perspective changes while the picker is open.
  get('import-replay').onclick();choose(0,'manual');const beforeStale=calls.filter(call=>call.url==='/api/tactical/import').length;await selectFile(replayText);assert.equal(calls.filter(call=>call.url==='/api/tactical/import').length,beforeStale);assert.match(get('operation-status').textContent,/已变化/);
  get('import-replay').onclick();await perspective(1);const beforeOtherPerspective=calls.filter(call=>call.url==='/api/tactical/import').length;await selectFile(replayText);assert.equal(calls.filter(call=>call.url==='/api/tactical/import').length,beforeOtherPerspective);get('replay-file').oncancel();
  // File reads, imports, reset and export share one pending-operation barrier.
  const fileGate=deferred(),reading=get('replay-file').onchange({target:{files:[{size:replayText.length,text:()=>fileGate.promise}],value:''}});assertBusy();const duringRead=calls.length;choose(1,'manual');await perspective(0);await get('step').onclick();await get('reset').onclick();await get('export-replay').onclick();assert.equal(calls.length,duringRead);
  const importGate=deferNext('/api/tactical/import');fileGate.release(replayText);await importGate.started;assertBusy();importGate.release();await reading;assertReady();assert.equal(run('observer'),1);
  const resetGate=deferNext('/api/tactical/reset'),resetting=get('reset').onclick();await resetGate.started;assertBusy();resetGate.release();await resetting;
  const exportGate=deferNext('/api/tactical/replay'),exporting=get('export-replay').onclick();await exportGate.started;assertBusy();const duringExport=calls.length;await get('reset').onclick();await get('step').onclick();choose(0,'policy');assert.equal(calls.length,duringExport);exportGate.release();await exporting;
  // Pausing an in-flight step is allowed; no competing mutations or relabeling are allowed.
  choose(0,'policy');choose(1,'policy');run('setPlaying(true)');const stepGate=deferNext('/api/tactical/step'),stepping=run('advance(10)');await stepGate.started;assertBusy();const duringStep=calls.length;
  choose(0,'manual');await perspective(0);await get('reset').onclick();await get('export-replay').onclick();assert.equal(calls.length,duringStep);assert.equal(run('sources[0]'),'policy');get('play').onclick();assert.equal(run('playing'),false);stepGate.release();await stepping;assertReady();assert.equal(run('state.tick'),10);
  // Explicit rejection preserves the last good view and never silently switches to manual.
  const rejectedTick=run('state.tick');failure='/api/tactical/step';run('setPlaying(true)');await run('advance(1)');assert.equal(run('playing'),false);assert.equal(run('state.tick'),rejectedTick);assertReady();assert.equal(run('sources[0]'),'policy');assert.match(get('operation-status').textContent,/不判负/);
  // Uncertain mutations are never retried: one state read establishes the actual outcome.
  const beforeLost=stepCalls().length;dropAfter='/api/tactical/step';await run('advance(3)');assertReady();assert.equal(stepCalls().length,beforeLost+1);assert.equal(run('state.tick'),rejectedTick+3);assert.equal(run('playing'),false);assert.match(get('operation-status').textContent,/没有自动重试/);
  const beforeDropped=run('state.tick');drop='/api/tactical/step';await run('advance(2)');assertReady();assert.equal(run('state.tick'),beforeDropped);assert.equal(run('playing'),false);
  failure='/api/tactical/step';failureStatus=500;await run('advance(2)');assertReady();assert.equal(run('state.tick'),beforeDropped);
  // If the recovery read fails too, the old view stays blank and all control remains disabled.
  dropAfter='/api/tactical/step';failure='/api/tactical/state?observer=1';await run('advance(1)');assert.equal(run('ready()'),false);assert.equal(get('map-veil').hidden,false);assert.equal(get('reconnect').hidden,false);await get('reconnect').onclick();assertReady();
  // Wrong source, SDK, observer, advanced count and filtered payloads are not applied.
  for(const edit of [body=>{body.last_sources[0].controller_id='stale-controller';},body=>{body.last_sources[1].sdk_version=999;},body=>{body.last_sources[0].action=[0,0,0,0,0];},body=>{body.observer=0;},body=>{body.tick-=1;},body=>{body.advanced=0;},body=>{body.observation_version=1;},body=>{body.observation.observation_version=1;},body=>{body.observation.combat.self.hits=999;},body=>{body.observation.combat.enemy={x:1000,y:1000,yaw:0,hp:100};},body=>{body.hash=123;},body=>{body.observation.zones[0].progress_ms=123;},body=>{body.observation.zones[0].detected=[true,false];},body=>{body.observation.zones[0].active=[true,false];},body=>{body.observation.zones[0].grace_deadline_ms=999;},body=>{body.observation.zones[0].completed_at_ms=123;},body=>{body.observation.occupancy=[[true,false]];},body=>{body.result.enemy_hp=0;},body=>{body.damage_admin={attack_damage:[100,0]};},body=>{body.observation.attack_damage=[100,0];},body=>{body.heat_admin={robots:[]};},body=>{body.observation.enemy_heat={heat_tenths:1};},body=>{body.observation.self_heat.video_locked=true;},body=>{body.observation.self_heat.heat_tenths=1;},body=>{body.observation.self_heat.firing_locked=true;},body=>{body.observation.combat.enemy={x:1000,y:1000,yaw:0,heat_tenths:1};},body=>{body.config.heat_threshold_policy='guess';},body=>{body.observation.combat.ledger=[];},body=>{body.result.attack_damage=[100,0];},body=>{body.config.attack_damage_strategy='nominal-hit-damage';},body=>{body.result.outcome='red-wins';body.result.reason='timeout-control';},body=>{body.observation.qualified_ms+=1;},body=>{body.observation.score=[-1,0];},body=>{body.observation.rate=[4,4];}]){
    const tick=run('state.tick'),count=stepCalls().length;rewrite={url:'/api/tactical/step',edit};await run('advance(1)');assertReady();assert.equal(run('state.tick'),tick+1);assert.equal(stepCalls().length,count+1);assert.equal(run('playing'),false);assert.match(get('operation-status').textContent,/没有自动重试/);assert.equal(run('state.hash'),undefined);assert.equal(run('state.observation.combat.self.hits'),undefined);
  }
  // Invalid policy metadata is fixed text; explicitly selected manual control still works.
  rewrite={url:'/api/tactical/meta',edit:body=>{body.policy_error='PRIVATE POLICY EXCEPTION SENTINEL';body.policy=null;}};await get('reconnect').onclick();assertReady();assert.equal(run('policyAvailable'),false);assert.equal(get('step').disabled,true);assert.ok(!allText(get('policy-metadata')).includes('PRIVATE POLICY EXCEPTION'));
  choose(0,'manual');choose(1,'manual');assert.equal(get('step').disabled,false);await get('step').onclick();assertReady();assert.ok(stepCalls().at(-1).data.controllers.every(source=>source.kind==='manual'));choose(1,'policy');assert.equal(run('sources[1]'),'manual');await get('reconnect').onclick();assert.equal(run('policyAvailable'),true);
  // Oversized files never reach the server; blur and backgrounding release all held inputs.
  const beforeOversize=calls.length;await selectFile('{}',48*1024*1024+1);assert.equal(calls.length,beforeOversize);assert.match(get('operation-status').textContent,/48 MiB/);
  input('KeyW');run('setPlaying(true)');windowEvents.blur();assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);run('setPlaying(true)');document.hidden=true;documentEvents.visibilitychange();assert.equal(run('playing'),false);document.hidden=false;
  // Long frames keep backlog but native batches are capped, not sped up in the browser.
  await get('reset').onclick();run('setPlaying(true); lastTime=1000; loop(21000)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),600);assert.ok(run('accumulator')>=600);run('loop(21000)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),1200);run('pauseAndRelease()');
  assert.ok(stepCalls().every(call=>call.data.count>=1&&call.data.count<=600));
  // Bounded 1 s / 10 s buttons capture held input once and still record every fixed tick.
  await perspective(0);await get('reset').onclick();choose(0,'manual');choose(1,'manual');
  input('KeyW');await get('advance-1s').onclick();assertReady();assert.equal(run('state.tick'),60);assert.equal(run('keys.size'),0);assert.equal(stepCalls().at(-1).data.count,60);assert.equal(stepCalls().at(-1).data.controllers[0].action.dy,1000);
  await get('advance-10s').onclick();assertReady();assert.equal(run('state.tick'),660);assert.equal(stepCalls().at(-1).data.count,600);
  assert.equal((await read('/api/tactical/replay')).frames.length,660);
  input('KeyD');run('setPlaying(true)');get('speed').value='60';get('speed').onchange();assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);assert.equal(run('speed'),60);
  await get('reset').onclick();run('setPlaying(true);lastTime=1000;loop(2000)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),600);assert.ok(run('accumulator')>=3000);run('pauseAndRelease()');get('speed').value='1';get('speed').onchange();
  console.log('PASS tactical controls, filtered payloads, replay barriers and bounded speed.');
  // Reset-only selectors never mutate the running preset until explicit reset.
  const presetTick=run('state.tick'),presetCalls=calls.length;get('credit-strategy').value='disabled';get('credit-strategy').onchange();
  assert.equal(calls.length,presetCalls);assert.equal(run('state.tick'),presetTick);assert.equal(run('state.observation.credits_enabled'),true);assert.match(get('operation-status').textContent,/下次重置/);
  get('boundary-policy').value='strict-reject-unadjudicated-v1';get('boundary-policy').onchange();await get('reset').onclick();assertReady();
  assert.equal(run('state.config.boundary_policy'),'strict-reject-unadjudicated-v1');assert.equal(run('state.observation.score'),null);assert.equal(get('score-red').textContent,'—');assert.match(get('credit-status').textContent,/积分关闭/);
  while(run('state.terminal')===0){const tick=run('state.tick');await get('advance-10s').onclick();assertReady();assert.ok(run('state.tick')>tick,get('operation-status').textContent);}
  console.log('PASS unscored 420 s run.');
  assert.equal(run('state.result.outcome'),'unscored-timeout');assert.match(get('match-result').textContent,/不裁决/);assert.match(get('credit-status').textContent,/积分关闭/);assert.ok(!get('credit-status').textContent.includes('攻击账本'));
  get('credit-strategy').value='EXPERIMENTAL-end-of-second-v1';get('credit-strategy').onchange();get('boundary-policy').value='ENGINEERING-NOT-OFFICIAL-timer-before-input-v1';get('boundary-policy').onchange();await get('reset').onclick();assertReady();
  // A living manual robot drives into the same rendered objective used by native capture.
  await move('KeyW',30);await move('KeyD',110);await get('advance-10s').onclick();assertReady();
  assert.equal(run('state.observation.zones[1].controller'),0);assert.equal(run('state.observation.zones[1].degree'),'temporary');
  assert.equal(run('state.observation.rate[0]'),1);assert.ok(run('state.observation.score[0]')>0);assert.equal(get('score-red').textContent,String(run('state.observation.score[0]')));
  assert.match(allText(get('objective-list').children[1]),/红方控制.*临时控制/);
  const ownBeforeFire=run('state.observation.combat.self.ammo');run('aim={x:10400,y:3800}');input('Space');await get('advance-10s').onclick();assertReady();
  assert.ok(run('state.observation.combat.self.ammo')<ownBeforeFire);assert.equal(run('state.terminal'),0,'Damage or disable never ends the match early');
  const other=await read('/api/tactical/state?observer=1');assert.equal(other.observation.combat.self.hp,0,'Same-scene manual combat disabled the opponent');
  await perspective(1);assert.equal(get('drive-up').disabled,true);assert.match(get('drive-note').textContent,/已失效/);drawing.length=0;run('drawMap()');assert.ok(drawing.some(entry=>entry.method==='fillText'&&entry.args[0]==='SELF 1 · DISABLED'));assert.ok(!drawing.some(entry=>entry.method==='fillText'&&entry.args[0].includes('VISIBLE · DISABLED')));const disabledPosition=run('state.observation.combat.self.x');input('KeyD');await get('advance-1s').onclick();assertReady();assert.equal(run('state.observation.combat.self.x'),disabledPosition);assert.equal(run('state.terminal'),0);assert.equal(get('step').disabled,false);
  await perspective(0);while(run('state.terminal')===0){const tick=run('state.tick');await get('advance-10s').onclick();assertReady();assert.ok(run('state.tick')>tick,get('operation-status').textContent);}
  assert.equal(run('state.result.outcome'),'red-wins');assert.match(get('match-result').textContent,/红方控制积分胜出/);assert.equal(get('remaining-time').textContent,'00:00');
  await get('export-replay').onclick();assertReady();const fullReplayText=await blobs.at(-1).text();assert.equal(JSON.parse(fullReplayText).frames.length,25200);
  await get('reset').onclick();const fullImportSteps=stepCalls().length;await selectFile(fullReplayText);assertReady();assert.equal(stepCalls().length,fullImportSteps);assert.equal(run('state.tick'),25200);assert.equal(run('state.result.outcome'),'red-wins');
  console.log('PASS same-scene capture, score, disable, 420 s control win and full-run action replay import/export.');
  // Terminal native state stops play and stepping, while reset/import/export stay available.
  await get('reset').onclick();while(run('state.terminal')===0){const tick=run('state.tick');await run('advance(600)');assertReady();assert.ok(run('state.tick')>tick,get('operation-status').textContent);}
  assert.equal(run('state.tick'),25200);assert.equal(run('playing'),false);assert.equal(get('play').disabled,true);assert.equal(get('step').disabled,true);assert.equal(get('export-replay').disabled,false);assert.equal(run('state.observation.combat.enemy'),null);assert.equal(run('state.result.outcome'),'draw');assert.equal(run('state.result.reason'),'timeout-equal');assert.match(get('match-result').textContent,/平局/);assert.match(get('credit-status').textContent,/数值不公开/);
  console.log('PASS default no-hit 420 s draw.');
  // Attack selector is reset-only; the old missing-ledger mode remains unresolved.
  const damageTick=run('state.tick'),damageCalls=calls.length;
  get('attack-damage-strategy').value='missing-adjudicated-attack-damage';get('attack-damage-strategy').onchange();
  assert.equal(calls.length,damageCalls);assert.equal(run('state.tick'),damageTick);assert.equal(run('state.config.attack_damage_strategy'),'EXPERIMENTAL-reconciled-projectile-hp-loss-v1');
  await get('reset').onclick();assertReady();assert.equal(run('state.config.attack_damage_strategy'),'missing-adjudicated-attack-damage');assert.equal(run('state.config.preset'),'EXPERIMENTAL-custom-v3');
  while(run('state.terminal')===0){const tick=run('state.tick');await get('advance-10s').onclick();assertReady();assert.ok(run('state.tick')>tick,get('operation-status').textContent);}
  assert.equal(run('state.result.outcome'),'unresolved-timeout-tie');assert.match(get('match-result').textContent,/未能裁决/);assert.match(get('credit-status').textContent,/缺失攻击账本策略/);
  await get('export-replay').onclick();const missingReplay=await blobs.at(-1).text();await get('reset').onclick();await selectFile(missingReplay);assertReady();assert.equal(run('state.result.outcome'),'unresolved-timeout-tie');assert.equal(get('attack-damage-strategy').value,'missing-adjudicated-attack-damage');
  console.log('PASS missing-ledger 420 s unresolved and replay.');
  // A native attack breaks a public-control tie without publishing damage totals.
  get('attack-damage-strategy').value='EXPERIMENTAL-reconciled-projectile-hp-loss-v1';get('attack-damage-strategy').onchange();await get('reset').onclick();assertReady();
  await move('KeyW',30);await move('KeyD',120);run('aim={x:10400,y:3800}');input('Space');await get('advance-10s').onclick();assertReady();
  assert.deepEqual(JSON.parse(run('JSON.stringify(state.observation.score)')),[0,0]);assert.equal(run('state.terminal'),0);assert.equal(run('state.damage_admin'),undefined);
  assert.equal((await read('/api/tactical/state?observer=1')).observation.combat.self.hp,0);
  while(run('state.terminal')===0){const tick=run('state.tick');await get('advance-10s').onclick();assertReady();assert.ok(run('state.tick')>tick,get('operation-status').textContent);}
  assert.equal(run('state.result.outcome'),'red-wins');assert.equal(run('state.result.reason'),'timeout-attack-damage');assert.match(get('match-result').textContent,/实验攻击伤害胜出/);assert.match(get('credit-status').textContent,/数值不公开/);
  assert.equal(run('state.damage_admin'),undefined);assert.equal(run('state.observation.attack_damage'),undefined);assert.equal(run('state.observation.combat.enemy?.hp'),undefined);
  await get('export-replay').onclick();const attackReplay=await blobs.at(-1).text();assert.equal(JSON.parse(attackReplay).damage_admin,undefined);await get('reset').onclick();await selectFile(attackReplay);assertReady();assert.equal(run('state.result.reason'),'timeout-attack-damage');
  console.log('PASS tied-control 420 s attack-damage win, privacy and full replay.');
  // Own heat telemetry never exposes enemy heat or blanks this top-down map.
  get('heat-strategy').value='EXPERIMENTAL-fixed-level1-cooling-priority-17mm-v1';get('heat-strategy').onchange();
  assert.equal(run('state.config.heat_strategy'),'disabled');assert.match(get('operation-status').textContent,/下次重置/);
  get('heat-threshold-policy').value='strict-reject-unadjudicated';get('heat-threshold-policy').onchange();await get('reset').onclick();assertReady();
  assert.equal(run('state.config.preset'),'EXPERIMENTAL-playable-engineering-v3');assert.equal(run('state.observation.self_heat.enabled'),true);
  assert.match(get('heat-note').textContent,/不额外扣 HP.*不计入攻击伤害/);assert.match(get('heat-note').textContent,/第一人称图传未模拟/);
  input('Space');await run('advance(51)');assertReady();const hotAmmo=run('state.observation.combat.self.ammo');
  assert.equal(run('state.observation.self_heat.firing_locked'),true);assert.equal(run('state.observation.self_heat.overheat_locked'),true);
  assert.match(get('heat-status').textContent,/冷却至 0/);assert.match(get('fire').textContent,/热锁/);assert.equal(get('drive-up').disabled,false);assert.equal(get('map-veil').hidden,true);
  drawing.length=0;run('drawMap()');assert.ok(drawing.some(entry=>entry.method==='fillText'&&entry.args[0].startsWith('SELF 0')));
  const hotY=run('state.observation.combat.self.y'),hotHP=run('state.observation.combat.self.hp');input('KeyW');input('Space');await get('step').onclick();assertReady();
  assert.equal(run('state.observation.combat.self.y'),hotY+55);assert.equal(run('state.observation.combat.self.ammo'),hotAmmo);assert.equal(stepCalls().at(-1).data.controllers[0].action.fire,1);
  input('Space');await get('advance-1s').onclick();assertReady();assert.equal(run('state.observation.combat.self.ammo'),hotAmmo);assert.equal(run('state.observation.combat.self.hp'),hotHP);
  input('Space');await run('advance(300)');assertReady();assert.ok(run('state.observation.combat.self.ammo')<hotAmmo,'Native held request resumes after thermal unlock');
  const heatReplay=await read('/api/tactical/replay');assert.ok(heatReplay.frames.every(frame=>frame.inputs[0].action[4]===1),'Replay records requested fire, including blocked ticks');
  assert.equal(heatReplay.observation_version,2);assert.equal(heatReplay.heat_admin,undefined);assert.equal(run('state.observation.enemy_heat'),undefined);assert.equal(run('state.observation.self_heat.video_locked'),undefined);
  await get('export-replay').onclick();const heatedText=await blobs.at(-1).text();await get('reset').onclick();await selectFile(heatedText);assertReady();
  assert.equal(run('state.tick'),heatReplay.frames.length);assert.equal((await read('/api/tactical/replay')).final_hash,heatReplay.final_hash);
  get('heat-threshold-policy').value='ENGINEERING-prose-greater-temporary-at-equality-v1';get('heat-threshold-policy').onchange();await get('reset').onclick();assertReady();assert.equal(run('state.config.heat_threshold_policy'),'ENGINEERING-prose-greater-temporary-at-equality-v1');
  get('heat-threshold-policy').value='ENGINEERING-flowchart-greater-equal-v1';get('heat-threshold-policy').onchange();await get('reset').onclick();assertReady();assert.equal(run('state.config.heat_threshold_policy'),'ENGINEERING-flowchart-greater-equal-v1');
  // Presentation fixture only: permanent lock is not naturally reached through
  // this immediate-gate playable profile. The standalone module tests reach it.
  const permanentPresentation=run(`(() => { const actual=state; state=JSON.parse(JSON.stringify(actual)); try {
    state.observation.self_heat={...state.observation.self_heat,heat_tenths:0,overheat_locked:false,permanent_latched:true,firing_locked:true};
    verifySelfHeat(state.observation.self_heat,state.config);renderHeat();updateControls();
    return {label:$('heat-status').textContent,mapVisible:$('map-veil').hidden,movementEnabled:!$('drive-up').disabled};
  } finally { state=actual;renderState();updateControls(); } })()`);
  assert.match(permanentPresentation.label,/永久热锁/);assert.equal(permanentPresentation.mapVisible,true);assert.equal(permanentPresentation.movementEnabled,true);
  get('heat-strategy').value='disabled';get('heat-strategy').onchange();get('heat-threshold-policy').value='strict-reject-unadjudicated';get('heat-threshold-policy').onchange();await get('reset').onclick();assertReady();
  assert.equal(run('state.observation.self_heat.enabled'),false);assert.equal(get('heat-value').textContent,'关闭');
  console.log('PASS native heat/cooling telemetry, movement during lock, held requested fire vs accepted shots, no extra HP loss, map retention, privacy, reset-only strategies and heat replay.');
  // Another tab advancing cannot be exported under a stale visible frame count.
  await get('reset').onclick();const beforeForeignExport=blobs.length;await post('/api/tactical/step',{observer:1,count:1,controllers:[{kind:'manual',action:{sdk_version:1,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}},{kind:'manual',action:{sdk_version:1,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}}]});await get('export-replay').onclick();assert.equal(blobs.length,beforeForeignExport);assert.equal(run('ready()'),false);assert.match(get('operation-status').textContent,/不一致/);await get('reconnect').onclick();assertReady();
  assert.equal((await read('/api/combat/replay')).final_hash,original.combat.final_hash);assert.equal((await read('/api/state')).hash,original.sandbox.hash);assert.equal((await read('/api/objective/state')).hash,original.objective.hash);assert.equal((await read('/api/layered/state')).hash,original.layered.hash);assert.ok(calls.every(call=>call.url.startsWith('/api/tactical/')));
  console.log('PASS integrated tactical DOM/live native HTTP: independent sources, held single-step before release, neutral unselected manual robot, mouse/touch input, selected observer only, native cover visibility without ghosts, policy execution, explicit developer replay export/import, safe literal labels, stale pickers, all busy guards, uncertain-response resync without retry, strict response validation, manual availability on invalid policy metadata, public objective ownership, capture-to-score, same-scene combat, no KO ending, 420-second tie/win/unscored outcomes, reset-only presets, fixed-tick accelerated batches, terminal stop and mode isolation. Real browser rendering/pointer capture/download persistence remain unverified.');
})().catch(error=>{console.error(error);process.exitCode=1;});
