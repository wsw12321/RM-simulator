// Actual layered.js handlers, mock DOM and real loopback HTTP/native C++.
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
let gate = null, failure = null, failureStatus = 400, drop = null;
function deferred() { let release; const promise = new Promise(resolve => release = resolve); return {promise, release}; }
function deferNext(url) { const started = deferred(), held = deferred(); gate = {url, started, held}; return {started: started.promise, release: held.release}; }
const context = vm.createContext({document, window, console, TextEncoder, Blob, URL: {createObjectURL(blob) { blobs.push(blob); return `blob:layered-${blobs.length}`; }, revokeObjectURL(url) { revoked.push(url); }}, setTimeout(fn) { fn(); return 0; }, requestAnimationFrame() {}, fetch: async (url, options) => {
  calls.push({url, text: options?.body, data: options?.body ? (() => { try { return JSON.parse(options.body); } catch { return null; } })() : null});
  if (failure === url) { failure = null; const status = failureStatus; failureStatus = 400; return {ok: false, status, json: async () => ({error: status < 500 ? 'Injected native rejection; unchanged.' : 'Injected server failure.', errors: [{path: '$.test', code: 'injected', message: 'Rejected'}]})}; }
  if (drop === url) { drop = null; throw Error('Injected uncertain connection loss'); }
  const hold = gate?.url === url ? gate : null; if (hold) gate = null;
  const response = await fetch(base + url, options); if (hold) { hold.started.release(); await hold.held.promise; } return response;
}});
const run = source => vm.runInContext(source, context);
const allText = element => [element.textContent, ...element.children.map(child => typeof child === 'string' ? child : allText(child))].join(' ');
const wait = async predicate => { for (let i = 0; i < 600; i++) { if (predicate()) return; await new Promise(resolve => setTimeout(resolve, 10)); } throw Error(`Timed out: ${get('operation-status').textContent}`); };
const read = async route => { const response = await fetch(base + route); const body = await response.json(); assert.ok(response.ok, JSON.stringify(body)); return body; };
const post = async (route, data) => { const response = await fetch(base + route, {method: 'POST', headers: {'Content-Type': 'application/json', 'X-Sandbox-Client': '1'}, body: JSON.stringify(data)}); const body = await response.json(); assert.ok(response.ok, JSON.stringify(body)); return body; };
const input = (code, down = true, target = null) => windowEvents[down ? 'keydown' : 'keyup']({code, target, preventDefault() {}});
const edit = value => { get('config-json').value = typeof value === 'string' ? value : JSON.stringify(value); get('config-json').oninput(); };
const selectFile = (kind, text, size = Buffer.byteLength(text)) => get(`${kind}-file`).onchange({target: {files: [{name: `${kind}.json`, size, text: async () => text}], value: 'chosen'}});
const move = async (code, ticks) => { run('releaseInputs()'); input(code); while (ticks) { const n = Math.min(60, ticks); await run(`advance(${n})`); ticks -= n; } input(code, false); };
const assertBusy = () => { for (const id of ['step','reset','load-sample','import-config','apply-config','import-replay','export-replay','config-json','control-source']) assert.equal(get(id).disabled, true, `${id} busy barrier`); };
(async () => {
  const original = {sandbox: await read('/api/state'), objective: await read('/api/objective/state')};
  const sample = await read('/api/layered/sample'); await post('/api/layered/reset', sample);
  run(fs.readFileSync(path.join(__dirname, '../web/layered.js'), 'utf8'));
  await wait(() => run('ready() && !busy'));
  assert.equal(run('state.tick'), 0); assert.equal(run('state.profile'), 'layered-native-v1'); assert.equal(get('map-veil').hidden, true);
  assert.match(allText(get('mode-metadata')), /非官方/); assert.match(allText(get('mode-metadata')), /斜向速度更快/);
  assert.deepEqual(JSON.parse(get('config-json').value), sample);
  // A held movement key affects a paused single-step; focused form inputs do not drive.
  const start = await read('/api/layered/state'); input('KeyD'); await get('step').onclick(); input('KeyD', false);
  assert.equal(run('state.robot.x_mm'), start.robot.x_mm + sample.config.speed_mm_per_tick); assert.deepEqual(calls.at(-1).data.action, [1000,0]);
  input('KeyW', true, {tagName:'TEXTAREA'}); await get('step').onclick(); assert.deepEqual(calls.at(-1).data.action, [0,0]);
  input('KeyD', true, {tagName:'BUTTON'}); input('KeyA'); await get('step').onclick(); assert.deepEqual(calls.at(-1).data.action, [0,0]); run('releaseInputs()');
  // Pointer input uses the same action path and releases on pointer cancellation.
  get('drive-up').onpointerdown({pointerId: 7, preventDefault() {}}); await get('step').onclick(); assert.deepEqual(calls.at(-1).data.action, [0,1000]); get('drive-up').onpointercancel({pointerId: 7}); assert.equal(run('pointers.size'),0);
  // Display layer is local-only and cannot switch support or change hashes.
  const layerHash = run('state.hash'), callCount = calls.length;
  get('view-layer').value = 'upper_layer'; get('view-layer').onchange();
  assert.equal(calls.length, callCount); assert.equal(run('state.hash'), layerHash); assert.equal(run('state.robot.surface_id'), 'west_floor'); assert.match(get('view-status').textContent, /真实承载面被显示过滤隐藏/);
  assert.ok(drawing.some(entry => entry.method === 'stroke' && entry.stroke === '#c3a1f4')); assert.ok(drawing.some(entry => entry.method === 'setLineDash' && entry.args[0].length === 2));
  // Cross an explicit portal and enter the lower annotation. Same XY never chooses upper deck.
  await get('reset').onclick(); await move('KeyD', 120);
  assert.equal(run('state.robot.surface_id'), 'tunnel_floor'); assert.equal(run('state.robot.z_mm'), 0); assert.equal(run('state.robot.clearance_mm'), 700);
  assert.ok(run("state.active_trigger_ids.includes('tunnel_annotation')")); assert.ok(!run("state.active_trigger_ids.includes('deck_annotation')"));
  assert.match(get('clearance-note').textContent, /300 mm/);
  assert.match(allText(get('events')), /west_to_tunnel/); assert.match(allText(get('events')), /tunnel_annotation/);
  assert.equal(run('state.robot.envelope_max_mm'),400); assert.equal(run('state.robot.ceiling_z_mm'),700);
  // Config raw text is sent unchanged, including duplicate keys. Rejection leaves the run and draft.
  const acceptedHash = run('state.hash'), duplicate = JSON.stringify(sample).replace('"config":', '"config":{},"config":');
  edit(duplicate); await get('apply-config').onclick(); assert.equal(calls.at(-1).text, duplicate); assert.equal(run('state.hash'), acceptedHash); assert.equal(get('config-json').value, duplicate); assert.match(allText(get('config-errors')), /duplicate/i);
  const unsupported = structuredClone(sample); unsupported.map.surfaces.find(s => s.id === 'ramp').height.dx = 0.1;
  edit(unsupported); await get('apply-config').onclick(); assert.equal(run('state.hash'), acceptedHash); assert.match(allText(get('config-errors')), /height|1024|portal|decimal/);
  const fractional=structuredClone(sample);fractional.config.speed_mm_per_tick=1.5;edit(fractional);await get('apply-config').onclick();assert.match(allText(get('config-errors')), /speed_mm_per_tick/);assert.equal(run('state.hash'),acceptedHash);
  // Tall robot can stand outside but is blocked by low tunnel clearance; blocked state is native.
  const tall = structuredClone(sample); tall.config.height_mm = 800; edit(tall); await get('apply-config').onclick(); await move('KeyD', 120);
  assert.equal(run('state.robot.surface_id'), 'west_floor'); assert.ok(run('state.contacts.length') > 0); assert.match(allText(get('robot-state')), /顶棚|实体|承载/);
  // Same XY at upper and lower supports retains the explicitly selected height.
  const upper = structuredClone(sample); upper.config.start = {x_mm:3500,y_mm:1400,surface_id:'upper_deck'}; edit(upper); await get('apply-config').onclick();
  assert.equal(run('state.robot.z_mm'), 1000); assert.equal(run('state.robot.surface_id'), 'upper_deck'); assert.ok(run("state.active_trigger_ids.includes('deck_annotation')"));
  const upperHash = run('state.hash'); get('view-layer').value='ground_layer';get('view-layer').onchange();assert.equal(run('state.hash'),upperHash);
  // The complete authored ramp route climbs only through explicit portals.
  edit(sample); await get('apply-config').onclick(); await move('KeyW',150); await move('KeyD',310); await move('KeyS',150); await move('KeyA',175);
  assert.equal(run('state.robot.surface_id'),'upper_deck');assert.equal(run('state.robot.z_mm'),1000);assert.equal(run('state.robot.x_mm'),3500);
  const replay = await read('/api/layered/replay'), replayText=JSON.stringify(replay); assert.equal(replay.frames.length,785);
  // Exact live replay bytes are downloadable and re-imported by native verification.
  await get('export-replay').onclick(); assert.equal(blobs.length,1);assert.deepEqual(JSON.parse(await blobs[0].text()),replay);assert.equal(revoked.length,1);
  assert.ok(created.some(item=>item.download==='tactics-layered-tick-785.json'&&item.clicked));assert.match(get('operation-status').textContent,/已请求下载/);
  await get('reset').onclick(); await selectFile('replay',replayText);assert.equal(run('state.hash'),replay.final_hash);assert.match(get('replay-status').textContent,/785 帧/);assert.equal(run('playing'),false);
  const invalidReplay=structuredClone(replay);invalidReplay.final_hash='0000000000000000';await selectFile('replay',JSON.stringify(invalidReplay));assert.equal(run('state.hash'),replay.final_hash);assert.match(get('replay-status').textContent,/校验失败/);
  // Opening a picker does not lock the page, but changing run or draft invalidates its selection.
  get('import-replay').onclick(); await get('reset').onclick(); const importCalls=calls.filter(c=>c.url.endsWith('/import')).length;
  await selectFile('replay',replayText);assert.equal(calls.filter(c=>c.url.endsWith('/import')).length,importCalls);assert.match(get('operation-status').textContent,/状态已变化/);
  get('import-config').onclick();edit('NEW DRAFT');await selectFile('config',JSON.stringify(sample));assert.equal(get('config-json').value,'NEW DRAFT');get('config-file').oncancel();
  // Slow file reads hold all mutation barriers. Repeated actions cannot issue stray requests.
  const fileGate=deferred(); const reading=get('replay-file').onchange({target:{files:[{size:replayText.length,text:()=>fileGate.promise}],value:''}});
  assertBusy();const duringRead=calls.length;await get('play').onclick();await get('reset').onclick();await get('step').onclick();await get('apply-config').onclick();await get('load-sample').onclick();assert.equal(calls.length,duringRead);assert.equal(run('playing'),false);
  const importGate=deferNext('/api/layered/import');fileGate.release(replayText);await importGate.started;assertBusy();importGate.release();await reading;assert.equal(run('state.hash'),replay.final_hash);
  // An in-flight step cannot race reset, import, or export; pause still takes effect immediately.
  await get('reset').onclick();input('KeyD');const stepGate=deferNext('/api/layered/step');run('setPlaying(true)');const stepping=run('advance(10)');await stepGate.started;assertBusy();
  const duringStep=calls.length;await get('reset').onclick();await get('apply-config').onclick();await get('export-replay').onclick();assert.equal(calls.length,duringStep);get('play').onclick();assert.equal(run('playing'),false);stepGate.release();await stepping;assert.equal(run('state.tick'),10);
  // Rejected native batches pause without changing last good state or leaking held input.
  const rejectedHash=run('state.hash');failure='/api/layered/step';run('setPlaying(true)');await run('advance(1)');assert.equal(run('playing'),false);assert.equal(run('state.hash'),rejectedHash);assert.equal(run('keys.size'),0);
  // A lost mutation response is not called rollback: old view is veiled until resync.
  drop='/api/layered/step';await run('advance(1)');assert.equal(run('ready()'),false);assert.equal(get('map-veil').hidden,false);assert.equal(get('reconnect').hidden,false);await get('reconnect').onclick();assert.equal(run('ready()'),true);
  failure='/api/layered/step';failureStatus=500;await run('advance(1)');assert.equal(run('ready()'),false);assert.equal(get('map-veil').hidden,false);await get('reconnect').onclick();assert.equal(run('ready()'),true);
  // Metadata read failure after accepted reset never displays the new state against old geometry.
  edit(sample);failure='/api/layered/meta';await get('apply-config').onclick();assert.equal(run('ready()'),false);assert.match(get('operation-status').textContent,/重新同步/);await get('reconnect').onclick();assert.equal(run('state.tick'),0);
  // Slow sample reads serialize all operations and change only draft, not current native run.
  const sampleHash=run('state.hash'),sampleGate=deferNext('/api/layered/sample');const sampling=get('load-sample').onclick();await sampleGate.started;assertBusy();await get('reset').onclick();sampleGate.release();await sampling;assert.equal(run('state.hash'),sampleHash);assert.equal(run('draftDirty'),true);
  await selectFile('config','ignored',12*1024*1024+1);assert.match(get('operation-status').textContent,/12 MiB/);assert.equal(run('state.hash'),sampleHash);
  // Long render frames retain backlog but every action request stays bounded.
  run('setPlaying(true);lastTime=1000;loop(3000)');await wait(()=>run("busy===''"));assert.equal(run('state.tick'),60);assert.ok(run('accumulator')>=60);run('loop(3000)');await wait(()=>run("busy===''"));assert.equal(run('state.tick'),120);assert.ok(calls.filter(c=>c.url.endsWith('/step')).every(c=>c.data.count<=60));
  input('KeyW');windowEvents.blur();assert.equal(run('playing'),false);assert.equal(run('keys.size'),0);run('setPlaying(true)');document.hidden=true;documentEvents.visibilitychange();assert.equal(run('playing'),false);document.hidden=false;
  // Terminal native run disables further stepping and remains exportable.
  const short=structuredClone(sample);short.config.max_ticks=2;edit(short);await get('apply-config').onclick();await get('step').onclick();await get('step').onclick();assert.equal(run('state.status'),'limit_reached');assert.equal(get('step').disabled,true);assert.equal(get('play').disabled,true);assert.equal(get('export-replay').disabled,false);
  // Untrusted metadata stays literal text and never becomes HTML or executable source.
  const hostile=structuredClone(sample);hostile.map.metadata.name='<img src=x onerror=alert(1)>';edit(hostile);await get('apply-config').onclick();assert.match(allText(get('mode-metadata')),/<img src=/);assert.ok(created.every(item=>!['IMG','SCRIPT'].includes(item.tagName)));
  // A second tab advancing the native run must not export a mislabeled snapshot.
  const blobCount=blobs.length;await post('/api/layered/step',{count:1,action:[0,0]});await get('export-replay').onclick();assert.equal(blobs.length,blobCount);assert.equal(run('ready()'),false);assert.match(get('operation-status').textContent,/不一致/);await get('reconnect').onclick();assert.equal(run('state.tick'),1);
  assert.equal((await read('/api/state')).hash,original.sandbox.hash);assert.equal((await read('/api/objective/state')).hash,original.objective.hash);assert.ok(calls.every(c=>c.url.startsWith('/api/layered/')));
  console.log('PASS layered DOM/live native HTTP: held-key and pointer movement, display-only layers, tunnel clearance, upper/lower support, full ramp route, raw duplicate-key/unsupported config rejection, native replay export/import, invalid replay rollback, stale picker and delayed read guards, in-flight mutation serialization, rejection/uncertain response pause, metadata-failure veil/resync, bounded backlog, blur/visibility release, terminal run, safe text and independent modes. Browser rendering and real download persistence remain unverified.');
})().catch(error=>{console.error(error);process.exitCode=1;});
