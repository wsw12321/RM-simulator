// Actual roster.js + roster-profiles.js handlers with a mock DOM and live loopback HTTP/native C++.
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
const context=vm.createContext({document,window,console,TextEncoder,Uint8Array,crypto:require('node:crypto').webcrypto,Blob,URL:{createObjectURL(blob) { blobs.push(blob); return `blob:roster-${blobs.length}`; },revokeObjectURL(url) { revoked.push(url); }},setTimeout(fn) { fn(); return 0; },requestAnimationFrame() {},fetch:async(url,options)=>{
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
const sourceFile=(text,size=Buffer.byteLength(text),name='source.json')=>get('profiles-file').onchange({target:{files:[{name,size,text:async()=>text}],value:'chosen'}});
const sourceState=()=>get('profiles-active').textContent;
const sessionPath='/api/roster/profiles/session/';
(async()=>{
  const nativeMeta=await read('/api/roster/meta');await post('/api/roster/reset',{observer:'red-1',config:nativeMeta.default_config});
  run(fs.readFileSync(path.join(__dirname,'../web/roster-profiles.js'),'utf8'));
  run(fs.readFileSync(path.join(__dirname,'../web/roster.js'),'utf8'));await wait(()=>run('ready()&&!busy')&&!get('profiles-prepare').disabled);
  assert.match(get('profiles-catalog-status').textContent,/46/);assert.match(sourceState(),/普通 SYNTHETIC/);assert.equal(get('profiles-choices').children.length,4);
  assert.ok(get('profiles-choices').children[0].profileControls[0].children.some(o=>o.value==='heavy'&&o.textContent==='重装 · 仅 HP'));assert.ok(!allText(get('profiles-choices')).includes('英雄'));
  assert.ok(html.includes('42mm'));assert.ok(html.includes('升级'));assert.ok(html.includes('普通回放导入永远不会自动恢复来源标记'));
  assert.equal(get('profiles-export-replay').disabled,true);
  await get('profiles-prepare').onclick();assert.match(get('profiles-draft-status').textContent,/尚未应用/);assert.match(get('profiles-draft-detail').textContent,/max_hp=150/);assert.equal(run('state.observation.self.hp'),100);
  get('profiles-export-config').onclick();const envelope=JSON.parse(await blobs.at(-1).text());assert.equal(envelope.official,false);assert.equal(envelope.complete,false);assert.equal(envelope.resolved_config.robots[0].profile.max_hp,150);
  const beforeApply=calls.length;await get('profiles-apply').onclick();assertReady();assert.equal(run('state.observation.self.hp'),150);assert.equal(run('state.observation.self_heat.limit_tenths'),1700);assert.match(sourceState(),/已核对来源/);assert.ok(calls.slice(beforeApply).some(c=>c.data?.expected_revision>=0&&c.data.expected_tick===0));
  const allIds=stateCopy().config.robots.map(r=>r.id);allManual();input('Space');await get('advance-1s').onclick();assertReady();assert.equal(run('state.tick'),60);assert.ok(run('state.observation.self_heat.heat_tenths')>0);assert.match(sourceState(),/已核对来源/);
  await get('profiles-export-replay').onclick();const replayText=await blobs.at(-1).text(),wrapper=JSON.parse(replayText);assert.equal(wrapper.native_replay.frames.length,60);assert.deepEqual(wrapper.native_replay,await read('/api/roster/replay'));
  await get('reset').onclick();assertReady();assert.match(sourceState(),/普通 SYNTHETIC/);assert.equal(get('profiles-export-replay').disabled,true);
  get('profiles-import-replay').onclick();await sourceFile(replayText);assertReady();assert.equal(run('state.tick'),60);assert.match(sourceState(),/已核对来源/);
  // Wrapper fields and raw duplicate bytes reach the strict server parser unchanged.
  const before=await read('/api/roster/replay');get('profiles-import-replay').onclick();await sourceFile(replayText.replace('"official":false','"official":true,"official":false'));
  assert.match(get('operation-status').textContent,/Duplicate/);assert.match(sourceState(),/已核对来源/);assert.equal((await read('/api/roster/replay')).final_hash,before.final_hash);
  const bad=structuredClone(wrapper);bad.native_replay.final_hash^=1;get('profiles-import-replay').onclick();await sourceFile(JSON.stringify(bad));assert.match(sourceState(),/已核对来源/);assert.equal((await read('/api/roster/replay')).final_hash,before.final_hash);
  get('import-replay').onclick();await selectFile(JSON.stringify(wrapper.native_replay));assertReady();assert.match(sourceState(),/普通 SYNTHETIC/);
  // A validated config is a draft, never an automatic reset or a badge.
  const count=calls.length;get('profiles-import-config').onclick();await sourceFile(JSON.stringify(envelope));assert.equal(run('state.tick'),60);assert.match(sourceState(),/普通 SYNTHETIC/);assert.ok(!calls.slice(count).some(c=>c.url.endsWith('/reset')));
  await get('profiles-apply').onclick();assert.match(sourceState(),/已核对来源/);
  const tampered=structuredClone(envelope);tampered.official=true;get('profiles-import-config').onclick();await sourceFile(JSON.stringify(tampered));assert.match(get('operation-status').textContent,/未应用/);assert.match(sourceState(),/已核对来源/);
  // Role choices preserve honest HP-only residuals and unsupported lifecycle notes.
  for(const role of ['heavy','sentry','infantry']){
    const card=get('profiles-choices').children.find(c=>c.children[0].textContent==='red-1');card.profileControls[0].value=role;card.profileControls[0].onchange();assert.equal(get('profiles-apply').disabled,true);await get('profiles-prepare').onclick();get('profiles-export-config').onclick();const cfg=JSON.parse(await blobs.at(-1).text()),record=cfg.provenance.robots.find(r=>r.id==='red-1');
    assert.equal(record.role,role);assert.equal(record.fields.heat_limit_tenths.classification,role==='infantry'?'sourced-parameter':'user-supplied-synthetic-assumption');assert.ok(record.unsupported_behaviors.length);assert.ok(record.initialization_note);await get('profiles-apply').onclick();assertReady();assert.equal(run('state.observation.self.hp'),record.fields.max_hp.value);
  }
  // Selection editing invalidates prepared provenance; unsupported condition is not normalized away.
  const selection=JSON.parse(get('profiles-selection').value);selection.robots[0].conditions.respawn='official';get('profiles-selection').value=JSON.stringify(selection);get('profiles-selection').oninput();assert.equal(get('profiles-apply').disabled,true);await get('profiles-resolve').onclick();assert.match(get('profiles-draft-status').textContent,/失败/);assert.match(sourceState(),/已核对来源/);
  await get('profiles-prepare').onclick();
  // Catalog and staged raw config changes invalidate pickers; duplicates are sent as raw JSON.
  get('profiles-import-config').onclick();configure(stateCopy().config);const n=calls.length;await sourceFile(JSON.stringify(envelope));assert.equal(calls.length,n);assert.match(get('operation-status').textContent,/已变化/);
  get('config-json').value=JSON.stringify(stateCopy().config).replace('"max_hp":','"max_hp":1,"max_hp":');get('config-json').oninput();await get('profiles-prepare').onclick();assert.match(get('profiles-draft-status').textContent,/Duplicate/);get('config-current').onclick();await get('profiles-prepare').onclick();
  // Busy source apply bars source choices, native controls, draft edits and stale file reads.
  const gateApply=deferNext('/api/roster/profiles/reset'),applying=get('profiles-apply').onclick();await gateApply.started;assertBusy();assert.equal(get('profiles-prepare').disabled,true);assert.ok(get('profiles-choices').children.every(c=>c.profileControls.every(v=>v.disabled)));const bn=calls.length;get('profiles-import-config').onclick();await get('profiles-prepare').onclick();await get('step').onclick();assert.equal(calls.length,bn);gateApply.release();await applying;assertReady();
  // Another client resets to identical numeric config: stale wrapper must reject without advancing.
  await post('/api/roster/reset',{observer:'red-1',config:stateCopy().config});const sn=calls.filter(c=>c.url===sessionPath+'step').length;await get('step').onclick();assertReady();assert.equal(run('state.tick'),0);assert.equal(calls.filter(c=>c.url===sessionPath+'step').length,sn+1);assert.match(sourceState(),/普通 SYNTHETIC/);assert.match(get('operation-status').textContent,/没有自动重试/);
  // A lost sourced mutation response is never retried; its source badge is cleared until read-only sync.
  await get('profiles-prepare').onclick();dropAfter='/api/roster/profiles/reset';const syncGate=deferNext(sessionPath+'state?observer=red-1'),lost=get('profiles-apply').onclick();await syncGate.started;assert.match(sourceState(),/未同步|未验证/);assert.equal(get('map-veil').hidden,false);syncGate.release();await lost;assertReady();assert.match(sourceState(),/已核对来源/);assert.match(get('operation-status').textContent,/没有自动重试/);
  // Forged source or native response fields cannot render a trusted badge from the bad response.
  for(const edit of [p=>p.active_config.official=true,p=>p.active_config.complete=true,p=>p.active_config.provenance.robots[0].fields.max_hp.source.printed_page=1,p=>p.active_config.resolved_config.robots[0].profile.max_hp++,p=>p.state.hash=1,p=>p.run_revision=-1]){
    rewrite={url:sessionPath+'step',edit};await get('step').onclick();assertReady();assert.match(get('operation-status').textContent,/没有自动重试/);assert.equal(run('state.hash'),undefined);
  }
  // Catalog reload failure leaves native play operational but never retains a stale source badge.
  get('profiles-import-config').onclick();failure='/api/roster/profiles/catalog';await get('profiles-reload').onclick();assert.match(get('profiles-catalog-status').textContent,/不可用/);assert.match(sourceState(),/未验证/);const cn=calls.length;await sourceFile(JSON.stringify(envelope));assert.equal(calls.length,cn);assert.equal(get('profiles-apply').disabled,true);
  await get('reset').onclick();assertReady();await get('step').onclick();assertReady();assert.match(sourceState(),/普通 SYNTHETIC/);await get('profiles-reload').onclick();assert.match(get('profiles-catalog-status').textContent,/46/);
  assert.ok(calls.filter(c=>c.url===sessionPath+'step').every(c=>Number.isSafeInteger(c.data.expected_revision)&&Number.isSafeInteger(c.data.expected_tick)&&c.data.request.count>=1&&c.data.request.count<=600));
  console.log('PASS actual source UI handlers + native HTTP: chooser/explicit residuals, draft/apply, HP+heat, raw/wrapped replay, role limits, raw duplicate keys, stale pickers/config/catalog, busy barriers, run revision conflict, forged metadata, catalog failure and uncertain no-retry recovery. Browser layout/pixels and download persistence UNVERIFIED.');
})().catch(error=>{console.error(error);process.exitCode=1;});
