// Actual roster.js handlers with a mock DOM and live loopback HTTP/native C++.
// This does NOT verify browser layout, rendered pixels, pointer capture or download persistence.
'use strict';
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict'), path = require('node:path');
const base = process.argv[2] || 'http://127.0.0.1:8789';
const html = fs.readFileSync(path.join(__dirname,'../web/roster.html'),'utf8');
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
const context=vm.createContext({document,window,console,TextEncoder,Blob,URL:{createObjectURL(blob) { blobs.push(blob); return `blob:roster-${blobs.length}`; },revokeObjectURL(url) { revoked.push(url); }},setTimeout(fn) { fn(); return 0; },requestAnimationFrame() {},fetch:async(url,options)=>{
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
const choose=(id,kind)=>{const control=run(`sourceControls.get(${JSON.stringify(id)})`);assert.ok(control,`Control for ${id}`);control.value=kind;control.onchange();};
const perspective=index=>{get('observer').value=String(index);return get('observer').onchange();};
const selectFile=(text,size=Buffer.byteLength(text),name='developer-replay.json')=>get('replay-file').onchange({target:{files:[{name,size,text:async()=>text}],value:'chosen'}});
const assertBusy=()=>{assert.ok(run('[...sourceControls.values()].every(c=>c.disabled)'));for(const id of ['step','advance-1s','advance-10s','speed','config-json','config-default','config-current','reset','import-replay','export-replay','observer'])assert.equal(get(id).disabled,true,`${id} pending-operation barrier`);};
const assertReady=()=>assert.equal(run('ready()'),true,get('operation-status').textContent);
const stepCalls=()=>calls.filter(call=>call.url==='/api/roster/step');
const move=async(code,count)=>{input(code);await run(`advance(${count})`);input(code,false);assertReady();};

const stateCopy=()=>JSON.parse(run('JSON.stringify(state)'));
const configure=config=>{get('config-json').value=JSON.stringify(config);get('config-json').oninput();};
const allManual=()=>{for(const robot of stateCopy().config.robots)choose(robot.id,'manual');};
const reset=async config=>{if(config)configure(config);await get('reset').onclick();assertReady();assert.equal(run('state.tick'),0);};
(async()=>{
  const original={sandbox:await read('/api/state'),objective:await read('/api/objective/state'),layered:await read('/api/layered/state'),combat:await read('/api/combat/replay'),tactical:await read('/api/tactical/replay')};
  const nativeMeta=await read('/api/roster/meta'),defaultConfig=structuredClone(nativeMeta.default_config);defaultConfig.heat_strategy='disabled';
  await post('/api/roster/reset',{observer:'red-1',config:defaultConfig});
  run(fs.readFileSync(path.join(__dirname,'../web/roster.js'),'utf8'));await wait(()=>run('ready()&&!busy'));
  assert.equal(run('observer'),'red-1');assert.equal(run('state.tick'),0);assert.equal(run('sources.size'),4);assert.equal(get('roster-controls').children.length,4);
  assert.equal(run('sources.get("red-1")'),'manual');assert.ok(run('[...sources].filter(([id])=>id!==observer).every(([,source])=>source==="policy")'));
  assert.equal(run('state.hash'),undefined);assert.equal(run('state.damage_admin'),undefined);assert.equal(run('state.heat_admin'),undefined);assert.equal(run('state.frames'),undefined);
  assert.ok(html.includes('全部机器人的逐帧已解析动作'));assert.ok(html.includes('没有 CPU / 内存或安全隔离'));assert.ok(html.includes('SYNTHETIC'));assert.ok(html.includes('roster_mode/policy_sdk.hpp'));
  assert.ok(html.includes('不发布占领进度'));assert.match(allText(get('mode-metadata')),/SYNTHETIC/);assert.match(allText(get('policy-metadata')),/trusted-local-native/);
  assert.equal(run('policyAvailable'),true);assert.equal(run('state.observation.self_id'),'red-1');assert.equal(get('remaining-time').textContent,'07:00');assert.equal(get('objective-list').children.length,4);
  const ids=stateCopy().config.robots.map(r=>r.id);
  allManual();const starts=Object.fromEntries(await Promise.all(ids.map(async id=>[id,(await read('/api/roster/state?observer='+id)).observation.self])));
  input('KeyD');input('Space');await get('step').onclick();assertReady();assert.equal(run('state.observation.self.x'),starts['red-1'].x+55);assert.equal(run('keys.size'),0);
  let request=stepCalls().at(-1).data;assert.equal(request.observer,'red-1');assert.deepEqual(request.controllers.map(c=>c.id),ids);assert.equal(request.controllers.find(c=>c.id==='red-1').action.dx,1000);assert.equal(request.controllers.find(c=>c.id==='red-1').action.fire,1);
  for(const controller of request.controllers.filter(c=>c.id!=='red-1'))assert.deepEqual(controller.action,{sdk_version:1,dx:0,dy:0,aim_x:0,aim_y:0,fire:0});
  for(const id of ids.filter(id=>id!=='red-1'))assert.equal((await read('/api/roster/state?observer='+id)).observation.self.x,starts[id].x);
  input('KeyD',true,null,true);input('Space',true,null,true);await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.dx,0);
  input('KeyW',true,{tagName:'TEXTAREA'});input('Space',true,{tagName:'BUTTON'});await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.dy,0);assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.fire,0);
  get('drive-up').onpointerdown({pointerId:7,preventDefault(){}});get('fire').onpointerdown({pointerId:8,preventDefault(){}});await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.dy,1000);assert.equal(run('pointers.size'),0);
  get('roster-map').onpointerdown({pointerId:10,button:0,clientX:900,clientY:100,preventDefault(){}});const aimed=JSON.parse(run('JSON.stringify(aim)'));await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.aim_x,aimed.x);assert.equal(run('aim'),null);
  get('drive-up').onpointerdown({pointerId:9,preventDefault(){}});get('drive-up').onpointercancel({pointerId:9});get('roster-map').onlostpointercapture({pointerId:10});assert.equal(run('pointers.size'),0);
  input('KeyW');run('setPlaying(true)');const sourceTick=run('state.tick'),sourceCalls=calls.length;choose('red-1','policy');assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);assert.equal(calls.length,sourceCalls);assert.equal(run('state.tick'),sourceTick);assert.equal(get('drive-up').disabled,true);choose('red-1','manual');
  // A selected perspective is the only dynamic view. Neither previous contacts nor public spawns are rendered.
  const pg=deferNext('/api/roster/state?observer=blue-2'),changing=perspective('blue-2');await pg.started;assertBusy();assert.equal(get('map-veil').hidden,false);drawing.length=0;run('drawMap()');assert.equal(drawing.some(d=>d.method==='arc'),false);
  const during=calls.length;choose('red-1','policy');await perspective('red-2');await get('reset').onclick();await get('step').onclick();get('config-default').onclick();assert.equal(calls.length,during);pg.release();await changing;assertReady();assert.equal(run('observer'),'blue-2');
  const x=run('state.observation.self.x');input('KeyA');await get('step').onclick();assert.equal(run('state.observation.self.x'),x-65);assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='blue-2').action.dx,-1000);assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.dx,0);
  drawing.length=0;run('drawMap()');const visibleIDs=stateCopy().observation.contacts.map(c=>c.id);for(const id of ids.filter(id=>id!=='blue-2'))assert.equal(drawing.some(d=>d.method==='fillText'&&d.args[0]===`VISIBLE ${id}`),visibleIDs.includes(id));
  // Drafts are reset-only; reordering never reassigns a source to another ID.
  choose('red-2','policy');const sourceMap=run('JSON.stringify([...sources])'),draftTick=run('state.tick'),draftCalls=calls.length,reordered=stateCopy().config;reordered.robots.reverse();configure(reordered);
  assert.equal(run('state.tick'),draftTick);assert.equal(calls.length,draftCalls);assert.match(get('operation-status').textContent,/草稿/);assert.equal(run('JSON.stringify([...sources])'),sourceMap);await reset();assert.equal(run('sources.get("red-2")'),'policy');assert.deepEqual(stateCopy().config.robots.map(r=>r.id),ids);
  const invalid=stateCopy().config;invalid.robots[0].id=invalid.robots[1].id;configure(invalid);const invalidCalls=calls.length;await get('reset').onclick();assert.equal(calls.length,invalidCalls);assert.equal(run('state.tick'),0);assert.match(get('operation-status').textContent,/草稿无效/);get('config-current').onclick();
  const invalidProfile=stateCopy().config;invalidProfile.robots[0].profile.radius_mm=99;configure(invalidProfile);await get('reset').onclick();assert.equal(calls.length,invalidCalls);get('config-current').onclick();
  // Reset forwards raw staged JSON, including duplicate object keys, for strict server rejection.
  for(const duplicateDraft of [JSON.stringify(stateCopy().config).replace('"heat_strategy":','"heat_strategy":"disabled","heat_strategy":'),JSON.stringify(stateCopy().config).replace('"max_hp":','"max_hp":100,"max_hp":')]){
    get('config-json').value=duplicateDraft;get('config-json').oninput();const prior=await read('/api/roster/replay');await get('reset').onclick();assertReady();assert.equal(calls.at(-1).text,`{"observer":"blue-2","config":${duplicateDraft}}`);assert.match(get('operation-status').textContent,/重置被拒绝/);assert.equal((await read('/api/roster/replay')).final_hash,prior.final_hash);get('config-current').onclick();
  }
  get('config-json').value=' '.repeat(65537)+JSON.stringify(stateCopy().config);get('config-json').oninput();const largeDraftCalls=calls.length;await get('reset').onclick();assert.equal(calls.length,largeDraftCalls);assert.match(get('operation-status').textContent,/64 KiB/);get('config-current').onclick();
  // Change to 1v1 with entirely different identities, then 4v4. New IDs stay manual.
  const pair=structuredClone(defaultConfig);pair.robots=pair.robots.filter(r=>r.id.endsWith('-1')).map(r=>({...r,id:r.team===0?'alpha':'omega'}));await reset(pair);assert.equal(run('sources.size'),2);assert.equal(run('observer'),'alpha');assert.ok(run('[...sources.values()].every(s=>s==="manual")'));
  const eight=structuredClone(defaultConfig);eight.robots=[];for(const team of [0,1])for(let i=0;i<4;i++)eight.robots.push({...structuredClone(defaultConfig.robots.find(r=>r.team===team&&r.id.endsWith('-1'))),id:`${team===0?'r':'b'}-${i+1}`,spawn:{x:team===0?1600:10400,y:1100+1800*i,yaw:team===0?0:128}});await reset(eight);assert.equal(run('sources.size'),8);assert.equal(get('roster-controls').children.length,8);await get('step').onclick();assert.equal(stepCalls().at(-1).data.controllers.length,8);assert.ok(stepCalls().at(-1).data.controllers.every(c=>c.kind==='manual'));
  await reset(defaultConfig);await perspective('red-1');allManual();
  // A structurally valid but non-initial tick0 response is never rendered as a successful reset.
  for(const edit of [b=>b.observation.self.x++,b=>b.observation.self.hp--,b=>b.observation.self.ammo--,b=>b.observation.self.cooldown=1,b=>b.observation.contacts=[],b=>b.observation.projectiles=[{x:1700,y:2900,vx:0,vy:0}],b=>b.observation.zones[0].degree='temporary']){
    rewrite={url:'/api/roster/reset',edit};const count=calls.filter(c=>c.url==='/api/roster/reset').length;await get('reset').onclick();assertReady();assert.equal(run('state.tick'),0);assert.equal(run('state.observation.self.x'),1600);assert.equal(run('state.observation.self.hp'),100);assert.equal(calls.filter(c=>c.url==='/api/roster/reset').length,count+1);assert.match(get('operation-status').textContent,/没有自动重试/);
  }
  // All policy calls remain native and produce one resolved ID-tagged input for every tick.
  for(const id of ids)choose(id,'policy');await run('advance(120)');assertReady();assert.equal(run('state.tick'),120);assert.ok(run('state.last_sources.every(s=>s.kind==="policy")'));
  const replay=await read('/api/roster/replay'),replayText=JSON.stringify(replay);assert.equal(replay.frames.length,120);assert.ok(replay.frames.every(f=>f.inputs.length===4&&f.inputs.every(input=>input.kind==='policy')));
  await get('export-replay').onclick();assert.equal(blobs.length,1);assert.deepEqual(JSON.parse(await blobs[0].text()),replay);assert.equal(revoked.length,1);assert.ok(created.some(i=>i.download==='tactics-roster-developer-tick-120.json'&&i.clicked));assert.match(get('operation-status').textContent,/已请求下载/);
  await reset();const beforeImport=stepCalls().length;await selectFile(replayText);assertReady();assert.equal(run('state.tick'),120);assert.equal(stepCalls().length,beforeImport);assert.equal(run('playing'),false);assert.equal((await read('/api/roster/replay')).final_hash,replay.final_hash);
  const hostile='<img src=x onerror=alert(1)>.json';await selectFile(replayText,Buffer.byteLength(replayText),hostile);assert.ok(get('replay-status').textContent.includes(hostile));assert.ok(created.every(i=>!['IMG','SCRIPT'].includes(i.tagName)));
  const broken=structuredClone(replay);broken.final_hash=(broken.final_hash+1)>>>0;await selectFile(JSON.stringify(broken));assertReady();assert.equal(run('state.tick'),120);assert.match(get('replay-status').textContent,/校验失败/);
  const duplicate=replayText.replace('"schema":','"schema":"duplicate","schema":');await selectFile(duplicate);assert.equal(calls.at(-1).text,`{"observer":"red-1","replay":${duplicate}}`);assert.match(get('replay-status').textContent,/校验失败/);
  const labels=structuredClone(replay);labels.frames.forEach(f=>f.inputs.forEach(i=>i.controller_id='historical-only'));await selectFile(JSON.stringify(labels));assertReady();assert.match(get('source-status').textContent,/historical-only.*未经认证/);await get('step').onclick();assert.ok(stepCalls().at(-1).data.controllers.every(c=>c.controller_id===nativeMeta.policy.controller_id));
  // Stale file picker generations, source/config edits, and delayed file reads all fail closed.
  for(const change of [()=>choose('red-1','manual'),()=>configure(stateCopy().config),()=>perspective('blue-1')]){
    get('import-replay').onclick();await change();const n=calls.length;await selectFile(replayText);assert.equal(calls.length,n);assert.match(get('operation-status').textContent,/已变化/);
  }
  get('replay-file').oncancel();
  const fileGate=deferred(),reading=get('replay-file').onchange({target:{files:[{name:'held.json',size:replayText.length,text:()=>fileGate.promise}],value:''}});assertBusy();const duringRead=calls.length,staged=run('stagedConfig');choose('red-1','policy');await perspective('red-1');configure({});assert.equal(run('stagedConfig'),staged);await get('reset').onclick();await get('export-replay').onclick();assert.equal(calls.length,duringRead);
  const ig=deferNext('/api/roster/import');fileGate.release(replayText);await ig.started;assertBusy();ig.release();await reading;assertReady();
  for(const [url,button] of [['/api/roster/reset','reset'],['/api/roster/replay','export-replay']]){const gate=deferNext(url),pending=get(button).onclick();await gate.started;assertBusy();const n=calls.length;await get('step').onclick();get('config-default').onclick();choose('red-1','policy');await get('import-replay').onclick();assert.equal(calls.length,n);gate.release();await pending;assertReady();}
  for(const id of ids)choose(id,'policy');run('setPlaying(true)');const sg=deferNext('/api/roster/step'),stepping=run('advance(10)');await sg.started;assertBusy();const stepN=calls.length;choose('red-1','manual');await get('reset').onclick();await perspective('red-1');assert.equal(calls.length,stepN);get('play').onclick();assert.equal(run('playing'),false);sg.release();await stepping;assertReady();
  // Unknown mutation outcomes are never retried. Only read-only resync is allowed.
  const rejected=run('state.tick');failure='/api/roster/step';await run('advance(1)');assertReady();assert.equal(run('state.tick'),rejected);assert.match(get('operation-status').textContent,/不判负/);
  const beforeLost=stepCalls().length;dropAfter='/api/roster/step';await run('advance(3)');assertReady();assert.equal(stepCalls().length,beforeLost+1);assert.equal(run('state.tick'),rejected+3);assert.match(get('operation-status').textContent,/没有自动重试/);
  const beforeDrop=run('state.tick');drop='/api/roster/step';await run('advance(2)');assertReady();assert.equal(run('state.tick'),beforeDrop);failure='/api/roster/step';failureStatus=500;await run('advance(2)');assertReady();assert.equal(run('state.tick'),beforeDrop);
  dropAfter='/api/roster/step';failure='/api/roster/state?observer=blue-1';failureStatus=500;await run('advance(1)');assert.equal(run('ready()'),false);assert.equal(get('map-veil').hidden,false);await get('reconnect').onclick();assertReady();
  // If reset changed IDs before its response was lost, recover via the default selected current ID.
  configure(pair);dropAfter='/api/roster/reset';const resetN=calls.filter(c=>c.url==='/api/roster/reset').length;await get('reset').onclick();assertReady();assert.equal(calls.filter(c=>c.url==='/api/roster/reset').length,resetN+1);assert.equal(run('observer'),'alpha');assert.ok(calls.some(c=>c.url==='/api/roster/state'));assert.match(get('operation-status').textContent,/没有自动重试/);
  await reset(defaultConfig);await perspective('red-1');allManual();
  // Fail-closed response matrix: no unknown keys, identities, hidden status, hashes or source remapping.
  const badResponses=[b=>b.hash=1,b=>b.frames=[],b=>b.damage_admin={},b=>b.heat_admin={},b=>b.build='x',b=>b.observer='blue-1',b=>b.tick--,b=>b.advanced=0,b=>b.observation_version=2,b=>b.observation.self_id='blue-1',b=>b.observation.self_team=1,b=>b.observation.arena.walls=[],b=>b.observation.self.x=0,b=>b.observation.contacts.push({id:'blue-1',team:1,x:7000,y:3800,yaw:0}),b=>b.observation.projectiles.push({x:7000,y:3800,vx:0,vy:0}),b=>b.observation.projectiles.push({x:1800,y:2900,vx:0,vy:0},{x:1700,y:2900,vx:0,vy:0}),b=>b.observation.self.shots=9,b=>b.observation.contacts.push({id:'blue-1',team:1,x:1000,y:1000,yaw:0,hp:1}),b=>b.observation.contacts.push({id:'red-1',team:0,x:1000,y:1000,yaw:0}),b=>b.observation.projectiles.push({x:1000,y:1000,vx:0,vy:0,owner:'blue-1'}),b=>b.observation.self_heat.video_locked=true,b=>b.observation.self_heat.heat_tenths=1,b=>b.observation.enemy_heat={},b=>b.observation.roster[0].hp=1,b=>b.config.robots[0].profile.official=true,b=>b.config.robots.reverse(),b=>b.config.heat_strategy='guess',b=>b.observation.zones[0].progress_ms=1,b=>b.observation.zones[0].detected=[],b=>b.observation.zones[0].active=[],b=>b.observation.zones[0].grace_deadline_ms=1,b=>b.observation.score=[-1,0],b=>b.observation.rate=[4,4],b=>b.observation.qualified_ms++,b=>b.last_sources[0].id=b.last_sources[1].id,b=>b.last_sources.reverse(),b=>b.last_sources[0].sdk_version=99,b=>b.last_sources[0].action=[],b=>b.result.attack_damage=[1,0],b=>b.result.outcome='red-wins'];
  for(const edit of badResponses){const tick=run('state.tick'),n=stepCalls().length;rewrite={url:'/api/roster/step',edit};await run('advance(1)');assertReady();assert.equal(run('state.tick'),tick+1);assert.equal(stepCalls().length,n+1);assert.equal(run('playing'),false);assert.match(get('operation-status').textContent,/没有自动重试/);assert.equal(run('state.hash'),undefined);}
  // Policy failure is isolated from safe manual usage; raw exception text is never shown.
  choose('red-1','policy');rewrite={url:'/api/roster/meta',edit:b=>{b.policy_error='PRIVATE EXCEPTION';b.policy=null;}};await get('reconnect').onclick();assertReady();assert.equal(run('policyAvailable'),false);assert.equal(get('step').disabled,true);assert.ok(!allText(get('policy-metadata')).includes('PRIVATE EXCEPTION'));allManual();await get('step').onclick();assertReady();choose('blue-1','policy');assert.equal(run('sources.get("blue-1")'),'manual');await get('reconnect').onclick();assertReady();
  for(const edit of [b=>b.unknown='x',b=>b.hz=59,b=>b.default_config.robots[0].profile.id='official',b=>b.observation_version=2]){rewrite={url:'/api/roster/meta',edit};await get('reconnect').onclick();assert.equal(run('ready()'),false);await get('reconnect').onclick();assertReady();}
  const n=calls.length;await selectFile('{}',96*1024*1024+1);assert.equal(calls.length,n);assert.match(get('operation-status').textContent,/96 MiB/);
  input('KeyW');run('setPlaying(true)');windowEvents.blur();assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);run('setPlaying(true)');document.hidden=true;documentEvents.visibilitychange();assert.equal(run('playing'),false);document.hidden=false;
  await reset();run('setPlaying(true);lastTime=1000;loop(99999999)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),600);assert.ok(run('accumulator')<=1200);run('loop(99999999)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),1200);run('pauseAndRelease()');
  await reset();input('KeyW');await get('advance-1s').onclick();assert.equal(run('state.tick'),60);assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.dy,1000);assert.equal(run('keys.size'),0);await get('advance-10s').onclick();assert.equal(run('state.tick'),660);assert.equal((await read('/api/roster/replay')).frames.length,660);
  get('speed').value='60';get('speed').onchange();assert.equal(run('speed'),60);await reset();run('setPlaying(true);lastTime=1000;loop(2000)');await wait(()=>run('!busy'));assert.equal(run('state.tick'),600);assert.ok(run('accumulator')<=1200);run('pauseAndRelease()');get('speed').value='1';get('speed').onchange();
  console.log('PASS real roster handlers: 1v1/2v2/4v4, stable ID sources, selected-only manual, staged JSON, filtered draws, native policy, raw replay, barriers, response validation, uncertain resync and bounded fixed ticks.');
  if(process.env.ROSTER_UI_FAST==='1'){console.log('PASS final-code guard subset. Full420s/heat/replay stage intentionally skipped in ROSTER_UI_FAST mode.');return;}
  // Own synthetic heat, no enemy heat or overheated HP penalty. Held request is recorded during gate closure.
  const heated=structuredClone(defaultConfig);heated.heat_strategy='SYNTHETIC-per-robot-17mm-v1';await reset(heated);allManual();input('Space');await run('advance(51)');assertReady();assert.equal(run('state.observation.self_heat.firing_locked'),true);assert.equal(get('map-veil').hidden,true);assert.match(get('fire').textContent,/热锁/);assert.match(get('heat-note').textContent,/不额外扣 HP/);
  const hot=stateCopy().observation.self;input('KeyW');input('Space');await get('step').onclick();assert.equal(run('state.observation.self.y'),hot.y+55);assert.equal(run('state.observation.self.ammo'),hot.ammo);assert.equal(run('state.observation.self.hp'),hot.hp);assert.equal(stepCalls().at(-1).data.controllers.find(c=>c.id==='red-1').action.fire,1);
  await get('export-replay').onclick();const heatedText=await blobs.at(-1).text();await reset();await selectFile(heatedText);assertReady();assert.equal(run('state.tick'),52);
  // Complete a neutral 420-second run, terminal stop, full action-only replay import, and other-mode isolation.
  await reset(defaultConfig);allManual();while(run('state.terminal')===0){const tick=run('state.tick');await get('advance-10s').onclick();assertReady();assert.ok(run('state.tick')>tick,get('operation-status').textContent);}
  assert.equal(run('state.tick'),25200);assert.equal(run('playing'),false);assert.equal(get('step').disabled,true);assert.equal(get('play').disabled,true);assert.equal(get('export-replay').disabled,false);assert.equal(run('state.result.outcome'),'draw');assert.equal(get('remaining-time').textContent,'00:00');
  await get('export-replay').onclick();const full=await blobs.at(-1).text();assert.equal(JSON.parse(full).frames.length,25200);await reset();await selectFile(full);assertReady();assert.equal(run('state.tick'),25200);assert.match(get('replay-status').textContent,/没有重新执行策略/);
  assert.equal((await read('/api/state')).hash,original.sandbox.hash);assert.equal((await read('/api/objective/state')).hash,original.objective.hash);assert.equal((await read('/api/layered/state')).hash,original.layered.hash);assert.equal((await read('/api/combat/replay')).final_hash,original.combat.final_hash);assert.equal((await read('/api/tactical/replay')).final_hash,original.tactical.final_hash);assert.ok(calls.every(c=>c.url.startsWith('/api/roster/')));assert.ok(stepCalls().every(c=>c.data.count>=1&&c.data.count<=600));
  console.log('PASS native own-heat locks, complete 420-second finish, full replay and old-mode isolation. Browser layout, rendered pixels, real pointer capture, mobile behavior and download persistence remain UNVERIFIED.');
})().catch(error=>{console.error(error);process.exitCode=1;});
