// Programmable policy UI: actual layered.js handlers, mock DOM and live HTTP/native C++.
// Does NOT verify browser layout/rendering, pointer capture or actual download persistence.
'use strict';
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict'), path = require('node:path');
const base = process.argv[2] || 'http://127.0.0.1:8777';
const html = fs.readFileSync(path.join(__dirname, '../web/layered.html'), 'utf8'), defaults = {};
for (const match of html.matchAll(/<(?:input|select|textarea)\b[^>]*id="([^"]+)"[^>]*>/g)) defaults[match[1]] = {value: /\bvalue="([^"]*)"/.exec(match[0])?.[1] || '', checked: /\bchecked\b/.test(match[0])};
const drawing = [];
const ctx = new Proxy({}, {get(target, key) { return key in target ? target[key] : (...args) => drawing.push({method: key, args, stroke: target.strokeStyle}); }});
class Element {
  constructor(id, tag = 'div') { this.id = id; this.tagName = tag.toUpperCase(); this.value = defaults[id]?.value || ''; this.checked = defaults[id]?.checked || false; this.textContent = ''; this.className = ''; this.hidden = false; this.disabled = false; this.children = []; this.files = []; this.width = 1120; this.height = 700; this.style = {}; this.attributes = {}; }
  append(...items) { this.children.push(...items); } replaceChildren(...items) { this.children = items; this.textContent = ''; }
  remove() { this.removed = true; } click() { this.clicked = (this.clicked || 0) + 1; } getContext() { return ctx; }
  setAttribute(key, value) { this.attributes[key] = value; } setPointerCapture() {}
}
const elements = new Map(), created = [], blobs = [], revoked = [], calls = [], windowEvents = {}, documentEvents = {};
const get = id => { assert.ok(html.includes(`id="${id}"`), `HTML contains ${id}`); if (!elements.has(id)) elements.set(id, new Element(id)); return elements.get(id); };
const document = {getElementById: get, body: new Element('body'), hidden: false, addEventListener(type, callback) { documentEvents[type] = callback; }, createElement(tag) { const item = new Element('', tag); created.push(item); return item; }};
const window = {addEventListener(type, callback) { windowEvents[type] = callback; }};
let gate = null, failure = null, failureStatus = 400, drop = null, rewrite = null;
function deferred() { let release; const promise = new Promise(resolve => release = resolve); return {promise, release}; }
function deferNext(url) { const started = deferred(), held = deferred(); gate = {url, started, held}; return {started: started.promise, release: held.release}; }
const context = vm.createContext({document, window, console, TextEncoder, Blob, URL: {createObjectURL(blob) { blobs.push(blob); return `blob:layered-${blobs.length}`; }, revokeObjectURL(url) { revoked.push(url); }}, setTimeout(fn) { fn(); return 0; }, requestAnimationFrame() {}, fetch: async (url, options) => {
  calls.push({url, text: options?.body, data: options?.body ? (() => { try { return JSON.parse(options.body); } catch { return null; } })() : null});
  if (failure === url) { failure = null; const status = failureStatus; failureStatus = 400; return {ok: false, status, json: async () => ({error: status < 500 ? 'Injected native rejection; unchanged.' : 'Injected server failure.', errors: [{path: '$.test', code: 'injected', message: 'Rejected'}]})}; }
  if (drop === url) { drop = null; throw Error('Injected uncertain connection loss'); }
  const hold = gate?.url === url ? gate : null; if (hold) gate = null;
  let response = await fetch(base + url, options);
  if (rewrite?.url === url) { const change = rewrite; rewrite = null; const body = await response.json(); change.edit(body); response = {ok: response.ok, status: response.status, json: async () => body}; }
  if (hold) { hold.started.release(); await hold.held.promise; } return response;
}});
const run = source => vm.runInContext(source, context);
const allText = element => [element.textContent, ...element.children.map(child => typeof child === 'string' ? child : allText(child))].join(' ');
const wait = async predicate => { for (let i = 0; i < 600; i++) { if (predicate()) return; await new Promise(resolve => setTimeout(resolve, 10)); } throw Error(`Timed out: ${get('operation-status').textContent}`); };
const read = async route => { const response = await fetch(base + route); const body = await response.json(); assert.ok(response.ok, JSON.stringify(body)); return body; };
const post = async (route, data) => { const response = await fetch(base + route, {method: 'POST', headers: {'Content-Type': 'application/json', 'X-Sandbox-Client': '1'}, body: JSON.stringify(data)}); const body = await response.json(); assert.ok(response.ok, JSON.stringify(body)); return body; };
const input = (code, down = true, target = null, repeat = false) => windowEvents[down ? 'keydown' : 'keyup']({code, target, repeat, preventDefault() {}});
const edit = value => { get('config-json').value = typeof value === 'string' ? value : JSON.stringify(value); get('config-json').oninput(); };
const selectFile = (kind, text, size = Buffer.byteLength(text)) => get(`${kind}-file`).onchange({target: {files: [{name: `${kind}.json`, size, text: async () => text}], value: 'chosen'}});
const move = async (code, ticks) => { run('releaseInputs()'); input(code); while (ticks) { const n = Math.min(60, ticks); await run(`advance(${n})`); ticks -= n; } input(code, false); };
const assertBusy = () => { for (const id of ['step','reset','load-sample','import-config','apply-config','import-replay','export-replay','config-json','control-source']) assert.equal(get(id).disabled, true, `${id} busy barrier`); };
const choose = value => { get('control-source').value = value; get('control-source').onchange(); };
const policySteps = async count => { while (count > 0) { const n = Math.min(60, count); await run(`advance(${n})`); assert.equal(run('ready()'), true, get('operation-status').textContent); count -= n; } };
(async () => {
  const original = {sandbox: await read('/api/state'), objective: await read('/api/objective/state')};
  const sample = await read('/api/layered/sample'); await post('/api/layered/reset', sample);
  run(fs.readFileSync(path.join(__dirname, '../web/layered.js'), 'utf8'));
  await wait(() => run('ready() && !busy'));
  assert.equal(run('controlSource'), 'manual'); assert.equal(run('state.action_source'), null);
  const metadata = await read('/api/layered/meta');
  assert.equal(metadata.policy.sdk_version, 1); assert.equal(metadata.policy.decision_hz,60);
  assert.match(allText(get('policy-metadata')), /full-authored-geometry/);
  assert.match(allText(get('policy-metadata')), /trusted-local-native/);
  assert.ok(html.includes('layered_mode/example_policy.cpp')); assert.ok(html.includes('没有安全沙箱'));
  // Switching is local next-request selection: pause and release, no reset or HTTP mutation.
  input('KeyD'); get('drive-up').onpointerdown({pointerId:4,preventDefault(){}}); run('setPlaying(true)');
  const startHash=run('state.hash'), switchedCalls=calls.length; choose('policy');
  assert.equal(calls.length,switchedCalls);assert.equal(run('playing'),false);assert.equal(run('state.hash'),startHash);assert.equal(run('state.tick'),0);
  choose('unknown');assert.equal(run('controlSource'),'policy');assert.equal(get('control-source').value,'policy');assert.equal(calls.length,switchedCalls);
  assert.equal(run('keys.size'),0);assert.equal(run('pointers.size'),0);assert.equal(get('drive-up').disabled,true);
  input('KeyD'); get('drive-up').onpointerdown({pointerId:5,preventDefault(){}});
  assert.equal(run('keys.size'),0);assert.equal(run('pointers.size'),0);
  await get('step').onclick();
  assert.equal(calls.at(-1).url,'/api/layered/policy-step');assert.deepEqual(calls.at(-1).data,{count:1,sdk_version:1,controller_id:metadata.policy.controller_id});
  assert.equal(run('state.action_source.kind'),'policy');assert.equal(run('state.robot.x_mm'),sample.config.start.x_mm);assert.ok(run('state.robot.y_mm')>sample.config.start.y_mm);
  assert.match(get('policy-status').textContent, /末批动作来源记录：C\+\+/);
  // Next-source selection and last committed source are intentionally separate.
  const beforeManual=run('state.hash');choose('manual');assert.equal(run('state.hash'),beforeManual);assert.equal(run('state.action_source.kind'),'policy');
  input('KeyD',true,null,true);assert.equal(run('keys.size'),0); // No held-key autorepeat leak after switching.
  await get('step').onclick();assert.deepEqual(calls.at(-1).data.action,[0,0]);assert.equal(run('state.action_source.kind'),'manual');
  input('KeyD');await get('step').onclick();input('KeyD',false);assert.deepEqual(calls.at(-1).data.action,[1000,0]);
  // A native decision per tick reaches the authored upper route; batching cannot change it.
  choose('policy');await get('reset').onclick();assert.equal(run('controlSource'),'policy');assert.equal(run('state.action_source'),null);
  await policySteps(900);
  assert.equal(run('state.robot.surface_id'),'upper_deck');assert.equal(run('state.robot.x_mm'),4000);assert.equal(run('state.robot.y_mm'),1500);assert.equal(run('state.robot.z_mm'),1000);assert.ok(run("state.active_trigger_ids.includes('deck_annotation')"));
  assert.match(allText(get('events')),/ramp/);assert.match(allText(get('events')),/deck_annotation/);
  const policyReplay=await read('/api/layered/replay');assert.equal(policyReplay.schema,'rmuc-layered-replay.v2');assert.equal(policyReplay.frames.length,900);
  assert.ok(policyReplay.action_sources.every(source=>source.kind==='policy'&&source.controller_id===metadata.policy.controller_id));
  await get('reset').onclick();
  for(let i=0;i<60;i++) await get('step').onclick();
  const singles=await read('/api/layered/replay');assert.deepEqual(singles.frames,policyReplay.frames.slice(0,60));
  await policySteps(840);assert.equal(run('state.hash'),policyReplay.final_hash);
  // Source segments preserve policy/manual mixing; replay import executes recorded actions only.
  choose('manual');await get('step').onclick();const mixed=await read('/api/layered/replay');assert.equal(mixed.action_sources.at(-1).kind,'manual');
  await get('export-replay').onclick();assert.deepEqual(JSON.parse(await blobs.at(-1).text()),mixed);assert.match(get('operation-status').textContent,/不重新执行策略/);
  choose('policy');await get('reset').onclick();const beforeImportPolicyCalls=calls.filter(call=>call.url.endsWith('/policy-step')).length;
  await selectFile('replay',JSON.stringify(mixed));assert.equal(run('state.hash'),mixed.final_hash);assert.equal(run('playing'),false);assert.equal(run('controlSource'),'policy');
  assert.equal(run('state.action_source.kind'),'manual');assert.equal(calls.filter(call=>call.url.endsWith('/policy-step')).length,beforeImportPolicyCalls);assert.match(get('replay-status').textContent,/没有重新执行策略/);
  // Historical source IDs are unauthenticated records, not controller identities to execute.
  const historical=structuredClone(policyReplay);historical.action_sources.forEach(source=>{source.controller_id='historical-record-only';});
  const beforeHistoryCalls=calls.filter(call=>call.url.endsWith('/policy-step')).length;await selectFile('replay',JSON.stringify(historical));
  assert.equal(run('ready()'),true);assert.equal(run('state.hash'),historical.final_hash);assert.equal(run('state.action_source.controller_id'),'historical-record-only');assert.equal(run('meta.policy.controller_id'),metadata.policy.controller_id);assert.equal(run('controlSource'),'policy');
  assert.match(get('policy-status').textContent,/historical-record-only.*未经认证/);assert.equal(calls.filter(call=>call.url.endsWith('/policy-step')).length,beforeHistoryCalls);
  await get('reconnect').onclick();assert.equal(run('ready()'),true);assert.equal(run('state.action_source.controller_id'),'historical-record-only');
  await get('export-replay').onclick();assert.deepEqual(JSON.parse(await blobs.at(-1).text()),historical);
  await get('step').onclick();assert.equal(calls.at(-1).data.controller_id,metadata.policy.controller_id);assert.equal(run('state.action_source.controller_id'),metadata.policy.controller_id);assert.equal(run('ready()'),true);
  // Selector is part of every busy barrier, including delayed reads, imports and reset.
  const fileGate=deferred();const reading=get('replay-file').onchange({target:{files:[{size:JSON.stringify(mixed).length,text:()=>fileGate.promise}],value:''}});
  assertBusy();choose('manual');assert.equal(run('controlSource'),'policy');assert.equal(get('control-source').value,'policy');
  const importGate=deferNext('/api/layered/import');fileGate.release(JSON.stringify(mixed));await importGate.started;choose('manual');assert.equal(run('controlSource'),'policy');importGate.release();await reading;
  const configGate=deferNext('/api/layered/reset');const resetting=get('reset').onclick();await configGate.started;assertBusy();choose('manual');assert.equal(run('controlSource'),'policy');configGate.release();await resetting;
  // In-flight policy response cannot be relabeled by a later selector event; pause still works.
  run('setPlaying(true)');const stepGate=deferNext('/api/layered/policy-step');const stepping=run('advance(10)');await stepGate.started;assertBusy();
  const busyCalls=calls.length;choose('manual');await get('step').onclick();await get('reset').onclick();await get('export-replay').onclick();assert.equal(calls.length,busyCalls);assert.equal(run('controlSource'),'policy');
  get('play').onclick();assert.equal(run('playing'),false);stepGate.release();await stepping;assert.equal(run('state.tick'),10);assert.equal(run('state.action_source.kind'),'policy');
  choose('manual');assert.equal(run('keys.size'),0);assert.equal(run('pointers.size'),0);
  // Picker snapshots include control changes, even though the native world did not change.
  get('import-replay').onclick();choose('policy');const importCount=calls.filter(c=>c.url.endsWith('/import')).length;await selectFile('replay',JSON.stringify(mixed));assert.equal(calls.filter(c=>c.url.endsWith('/import')).length,importCount);assert.match(get('operation-status').textContent,/状态已变化/);
  // Invalid policy results pause explicitly as experimental failures, not a game loss.
  const goodHash=run('state.hash');failure='/api/layered/policy-step';run('setPlaying(true)');await run('advance(1)');assert.equal(run('playing'),false);assert.equal(run('state.hash'),goodHash);assert.equal(run('ready()'),true);assert.match(get('policy-status').textContent,/不判负/);
  // A lost policy response requires resync instead of claiming rollback.
  drop='/api/layered/policy-step';await run('advance(1)');assert.equal(run('ready()'),false);assert.equal(get('map-veil').hidden,false);await get('reconnect').onclick();assert.equal(run('ready()'),true);
  // A response with the wrong source/controller is never applied or labeled as this selection.
  for(const editSource of [source=>{source.kind='manual';source.controller_id=null;},source=>{source.controller_id='stale-controller';},source=>{source.sdk_version=999;}]) {
    const tick=run('state.tick');rewrite={url:'/api/layered/policy-step',edit:body=>editSource(body.action_source)};await run('advance(1)');
    assert.equal(run('ready()'),false);assert.equal(run('state.tick'),tick);assert.match(get('operation-status').textContent,/不一致|不兼容/);assert.equal(get('control-source').disabled,true);
    await get('reconnect').onclick();assert.equal(run('state.tick'),tick+1);assert.equal(run('ready()'),true);
  }
  const beforeStale=run('state.tick');rewrite={url:'/api/layered/policy-step',edit:body=>{body.tick=beforeStale;}};await run('advance(1)');assert.equal(run('ready()'),false);assert.equal(run('state.tick'),beforeStale);assert.match(get('operation-status').textContent,/帧号.*不一致/);await get('reconnect').onclick();assert.equal(run('state.tick'),beforeStale+1);
  // SDK/observation/execution metadata mismatch blocks BOTH modes, with no fallback.
  for(const editMeta of [body=>{body.policy.sdk_version=999;},body=>{body.policy.observation='partial-unknown';},body=>{body.policy.execution='browser';},body=>{delete body.policy;}]) {
    rewrite={url:'/api/layered/meta',edit:editMeta};await get('reconnect').onclick();assert.equal(run('ready()'),false);assert.equal(get('play').disabled,true);assert.equal(get('control-source').disabled,true);assert.match(get('operation-status').textContent,/不兼容/);assert.equal(run('controlSource'),'policy');
    await get('reconnect').onclick();assert.equal(run('ready()'),true);
  }
  rewrite={url:'/api/layered/meta',edit:body=>{body.policy=null;body.policy_error='Invalid compiled descriptor';}};await get('reconnect').onclick();assert.equal(run('ready()'),false);assert.match(get('policy-status').textContent,/不判负.*Invalid compiled descriptor/);await get('reconnect').onclick();
  // Export attribution is checked, not taken from the selected next source.
  const countBlobs=blobs.length;rewrite={url:'/api/layered/replay',edit:body=>{body.action_sources.at(-1).controller_id='wrong-controller';}};await get('export-replay').onclick();assert.equal(blobs.length,countBlobs);assert.equal(run('ready()'),false);await get('reconnect').onclick();
  choose('manual');await get('step').onclick();assert.deepEqual(calls.at(-1).data.action,[0,0]);
  // Blur/visibility pause policy just like manual; source choice is retained.
  choose('policy');run('setPlaying(true)');windowEvents.blur();assert.equal(run('playing'),false);assert.equal(run('controlSource'),'policy');run('setPlaying(true)');document.hidden=true;documentEvents.visibilitychange();document.hidden=false;assert.equal(run('playing'),false);
  assert.equal((await read('/api/state')).hash,original.sandbox.hash);assert.equal((await read('/api/objective/state')).hash,original.objective.hash);
  assert.ok(calls.every(call=>call.url.startsWith('/api/layered/')));
  console.log('PASS policy DOM/live HTTP/native: source selection/pause/no reset, keyboard/touch isolation, native ramp-to-deck controller, batch determinism, source-aware action replay, all busy guards, stale picker/source/SDK metadata rejection, explicit experimental failures, uncertain resync and independent original modes. Browser rendering and real downloads remain unverified.');
})().catch(error=>{console.error(error);process.exitCode=1;});
