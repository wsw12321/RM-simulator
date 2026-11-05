// Actual arena.js handlers + mock DOM + real loopback HTTP API.
// This intentionally does not claim browser rendering, layout, or file-download QA.
'use strict';
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict'), path = require('node:path');
const base = process.argv[2] || 'http://127.0.0.1:8766';
const html = fs.readFileSync(path.join(__dirname, '../web/arena.html'), 'utf8');
const defaults = {};
for (const match of html.matchAll(/<(?:input|select|textarea)\b[^>]*id="([^"]+)"[^>]*>/g)) {
  defaults[match[1]] = {value: /\bvalue="([^"]*)"/.exec(match[0])?.[1] || '', checked: /\bchecked\b/.test(match[0])};
}
const drawing = [];
const ctx = new Proxy({}, {get(target, key) { if (key in target) return target[key]; return (...args) => drawing.push({method: key, args, stroke: target.strokeStyle}); }});
class Element {
  constructor(id, tag = 'div') { this.id = id; this.tagName = tag.toUpperCase(); this.value = defaults[id]?.value || ''; this.checked = defaults[id]?.checked || false; this.textContent = ''; this.className = ''; this.hidden = false; this.disabled = false; this.children = []; this.files = []; this.width = 1120; this.height = 700; }
  append(...items) { this.children.push(...items); }
  replaceChildren(...items) { this.children = items; this.textContent = ''; }
  remove() { this.removed = true; }
  click() { this.clicked = (this.clicked || 0) + 1; }
  getContext() { return ctx; }
  getBoundingClientRect() { return {left: 0, top: 0, width: 1120, height: 700}; }
}
const elements = new Map(), created = [];
const get = id => { if (!elements.has(id)) elements.set(id, new Element(id)); return elements.get(id); };
const document = {getElementById: get, body: new Element('body'), createElement(tag) { const item = new Element('', tag); created.push(item); return item; }};
const calls = [], blobs = [], revoked = [];
let gate = null, failure = null;
function deferred() { let release; const promise = new Promise(resolve => release = resolve); return {promise, release}; }
function deferNext(url) { const started = deferred(), held = deferred(); gate = {url, started, held}; return {started: started.promise, release: held.release}; }
function failNext(url) { failure = url; }
const context = vm.createContext({document, console, TextEncoder, Blob, URL: {createObjectURL(blob) { blobs.push(blob); return `blob:arena-${blobs.length}`; }, revokeObjectURL(url) { revoked.push(url); }}, setTimeout(fn) { fn(); return 0; }, fetch: async (url, options) => {
  calls.push({url, data: options?.body ? JSON.parse(options.body) : null});
  if (failure === url) { failure = null; return {ok: false, status: 500, json: async () => ({error: 'Injected network failure'})}; }
  const hold = gate?.url === url ? gate : null; if (hold) gate = null;
  const response = await fetch(base + url, options);
  if (hold) { hold.started.release(); await hold.held.promise; }
  return response;
}});
const run = source => vm.runInContext(source, context);
const allText = element => [element.textContent, ...element.children.map(child => typeof child === 'string' ? child : allText(child))].join(' ');
const wait = async predicate => { for (let i = 0; i < 300; i++) { if (predicate()) return; await new Promise(resolve => setTimeout(resolve, 10)); } throw Error(`Timed out: ${get('operation-status').textContent}`); };
const edit = text => { get('arena-json').value = text; get('arena-json').oninput(); };
const number = (id, value) => { get(id).value = String(value); get(id).oninput(); };
const selectSurface = value => { get('surface-select').value = value; get('surface-select').onchange(); };
const importText = (text, name = 'test.json', size = Buffer.byteLength(text)) => get('arena-file').onchange({target: {files: [{name, size, text: async () => text}], value: 'test.json'}});
const read = async route => (await fetch(base + route)).json();
(async () => {
  const sandboxBefore = await read('/api/state');
  const objectiveBefore = await read('/api/objective/state');
  run(fs.readFileSync(path.join(__dirname, '../web/arena.js'), 'utf8'));
  await wait(() => run('isCurrent() && !validationPending'));
  const sampleText = get('arena-json').value, sample = JSON.parse(sampleText);
  assert.equal(sample.schema, 'rmuc-layered-arena.v1');
  assert.equal(get('preview-veil').hidden, true);
  assert.equal(get('export-arena').disabled, false);
  assert.match(allText(get('calibration-info')), /合成样例/);
  assert.match(allText(get('calibration-info')), /不是官方几何/);
  // Real geometry evidence distinguishes the two surfaces at identical XY.
  await get('query-arena').onclick();
  let result = run('queryResult');
  assert.equal(result.candidates.length, 2);
  assert.equal(result.selected_surface_id, null);
  assert.ok(result.candidates.every(candidate => candidate.feasible));
  assert.match(allText(get('query-summary')), /未选层/);
  assert.match(allText(get('query-evidence')), /tunnel_test_trigger/);
  assert.match(allText(get('query-evidence')), /deck_test_trigger/);
  assert.match(allText(get('query-evidence')), /可用净高：700 mm/);
  assert.match(allText(get('query-evidence')), /机器人顶部余量：100 mm/);
  assert.match(allText(get('query-evidence')), /非已核验无限空间/);
  // Display-only filters never change query facts; collisions and triggers have distinct drawing styles.
  const queryCalls = calls.filter(call => call.url === '/api/arena/query').length;
  get('layer-select').value = 'deck_layer'; get('layer-select').onchange();
  get('slice-height').value = '750'; get('slice-height').onchange();
  assert.equal(run('queryResult.candidates.length'), 2);
  assert.equal(calls.filter(call => call.url === '/api/arena/query').length, queryCalls);
  assert.ok(drawing.some(call => call.method === 'setLineDash' && call.args[0].length === 2));
  assert.ok(drawing.some(call => call.method === 'stroke' && call.stroke === '#fb8979'));
  assert.ok(drawing.some(call => call.method === 'stroke' && call.stroke === '#c3a1f4'));
  selectSurface('tunnel_floor'); number('probe-height', 700);
  await get('query-arena').onclick(); result = run('queryResult');
  assert.equal(result.feasible, false); assert.ok(result.candidates[0].collision_ids.includes('bridge_slab'));
  assert.match(allText(get('query-summary')), /不可容纳/);
  selectSurface('bridge_deck'); await get('query-arena').onclick();
  assert.equal(run('queryResult.feasible'), true); assert.equal(run('queryResult.candidates[0].support_z_mm'), 800);
  // A late query cannot restore evidence after the robot dimensions or surface change.
  const queryGate = deferNext('/api/arena/query');
  const queryOperation = get('query-arena').onclick(); await queryGate.started;
  const count = calls.length; await get('query-arena').onclick(); assert.equal(calls.length, count, 'repeated query guarded');
  number('probe-height', 600); selectSurface('tunnel_floor');
  queryGate.release(); await queryOperation;
  assert.equal(run('queryResult'), null); assert.equal(get('query-details').hidden, true);
  await get('query-arena').onclick(); assert.equal(run('queryResult.feasible'), true);
  // Blank or nonfinite fields are never silently coerced to zero.
  number('probe-radius', ''); const beforeInvalid = calls.length; await get('query-arena').onclick();
  assert.equal(calls.length, beforeInvalid); assert.match(get('query-status').textContent, /不能留空/);
  number('probe-radius', 200); number('probe-slope', -1); await get('query-arena').onclick();
  assert.match(get('query-status').textContent, /max_slope_deg/); number('probe-slope', 30);
  // A click actually updates coordinate fields and invalidates previous evidence.
  await get('query-arena').onclick(); get('arena-map').onclick({clientX: 560, clientY: 350});
  assert.equal(get('probe-x').value, '3000'); assert.equal(get('probe-y').value, '2000');
  assert.equal(run('queryResult'), null);
  // Export is a bounded, parseable current validated document; no save-success claim.
  get('export-arena').onclick(); assert.equal(blobs.length, 1);
  assert.deepEqual(JSON.parse(await blobs[0].text()), sample);
  assert.ok(blobs[0].size <= 256 * 1024);
  assert.ok(created.some(item => item.download === `${sample.metadata.id}.json` && item.clicked));
  assert.match(get('operation-status').textContent, /已请求下载/);
  edit(sampleText + '\n'); assert.equal(get('export-arena').disabled, true); assert.equal(get('query-arena').disabled, true); assert.equal(get('format-json').disabled, true); assert.equal(get('preview-veil').hidden, false);
  assert.match(get('calibration-version').textContent, /旧/); assert.doesNotMatch(get('operation-status').textContent, /已请求下载/);
  get('export-arena').onclick(); assert.equal(blobs.length, 1, 'programmatic stale export blocked');
  // Strict raw text preserves duplicate-key evidence instead of JSON.parse hiding it.
  const duplicate = sampleText.replace('"schema":', '"schema":"duplicate-value","schema":');
  edit(duplicate); await get('validate-arena').onclick();
  assert.match(allText(get('validation-errors')), /\$\.schema.*duplicate_key/);
  assert.equal(calls.at(-1).data.text, duplicate);
  const beforeFormat = get('arena-json').value; await get('format-json').onclick(); assert.equal(get('arena-json').value, beforeFormat);
  edit(sampleText); await get('validate-arena').onclick(); assert.equal(run('isCurrent()'), true);
  // Async validation of a previous draft never overrides a newer draft or newer validation.
  const validationGate = deferNext('/api/arena/validate');
  edit(sampleText + ' '); const validating = get('validate-arena').onclick(); await validationGate.started;
  const next = structuredClone(sample); next.metadata.name = 'NEWER CURRENT DOCUMENT';
  edit(JSON.stringify(next)); await get('validate-arena').onclick();
  validationGate.release(); await validating;
  assert.equal(run('validatedDocument.metadata.name'), next.metadata.name); assert.equal(run('isCurrent()'), true);
  // A picker opened against an old draft must not overwrite edits made before selection returns.
  get('import-arena').onclick(); edit('EDITED WHILE PICKER OPEN'); await importText(sampleText);
  assert.equal(get('arena-json').value, 'EDITED WHILE PICKER OPEN'); assert.match(get('operation-status').textContent, /选择期间草稿已变化/);
  get('import-arena').onclick(); get('arena-file').oncancel(); await importText(JSON.stringify(next)); assert.equal(run('isCurrent()'), true);
  // Edit during file.text cancels that import, and cannot erase the user's typing.
  const fileGate = deferred();
  const importing = get('arena-file').onchange({target: {files: [{name: 'slow.json', size: 100, text: () => fileGate.promise}], value: ''}});
  edit('USER IS TYPING'); fileGate.release(sampleText); await importing;
  assert.equal(get('arena-json').value, 'USER IS TYPING'); assert.equal(run('isCurrent()'), false);
  // Among two simultaneous reads, only the latest chosen file can commit.
  const firstFileGate = deferred();
  const oldImport = get('arena-file').onchange({target: {files: [{name: 'old.json', size: 100, text: () => firstFileGate.promise}], value: ''}});
  await importText(JSON.stringify(next), 'new.json');
  firstFileGate.release(sampleText); await oldImport;
  assert.equal(run('validatedDocument.metadata.name'), next.metadata.name);
  // Failed read and oversize import retain the current draft; UTF-8 bytes, not code units, are bounded.
  const beforeImportFailure = get('arena-json').value;
  await get('arena-file').onchange({target: {files: [{name: 'broken.json', size: 10, text: async () => { throw Error('disk read failed'); }}], value: ''}});
  assert.equal(get('arena-json').value, beforeImportFailure); assert.match(get('operation-status').textContent, /原草稿保留/);
  await importText('ignored', 'huge.json', 256 * 1024 + 1); assert.equal(get('arena-json').value, beforeImportFailure);
  await importText('测'.repeat(100000), 'utf8.json', 1); assert.equal(get('arena-json').value, beforeImportFailure); assert.match(get('operation-status').textContent, /UTF-8/);
  edit('测'.repeat(100000)); const beforeOversize = calls.length; await get('validate-arena').onclick();
  assert.equal(calls.length, beforeOversize); assert.match(allText(get('validation-errors')), /UTF-8/);
  // Server-rejected imports remain visible as editable drafts with precise paths, but are never exported.
  const invalid = structuredClone(sample); invalid.surfaces[0].source_ids = ['missing-source'];
  await importText(JSON.stringify(invalid));
  assert.equal(get('export-arena').disabled, true); assert.match(allText(get('validation-errors')), /source_ids/);
  // Late sample fetch also cannot overwrite newer user edits.
  const sampleGate = deferNext('/api/arena/sample'); const sampling = get('load-sample').onclick(); await sampleGate.started;
  edit(JSON.stringify(next)); sampleGate.release(); await sampling;
  assert.equal(JSON.parse(get('arena-json').value).metadata.name, next.metadata.name);
  await get('validate-arena').onclick();
  // Network failure is recoverable, leaves the draft intact, and never silently marks it valid.
  const preserved = get('arena-json').value; failNext('/api/arena/validate'); await get('validate-arena').onclick();
  assert.equal(get('arena-json').value, preserved); assert.equal(get('export-arena').disabled, true);
  assert.match(allText(get('validation-errors')), /Injected network failure/);
  await get('validate-arena').onclick(); assert.equal(run('isCurrent()'), true);
  await get('format-json').onclick(); assert.equal(run('isCurrent()'), true); assert.deepEqual(JSON.parse(get('arena-json').value), next);
  // Inclusive maximum: compact export must not add a newline that exceeds the accepted byte limit.
  const exactLimit = structuredClone(sample), maxBytes = 256 * 1024;
  while (Buffer.byteLength(JSON.stringify(exactLimit)) < maxBytes) {
    exactLimit.sources.push({id: `padding_${exactLimit.sources.length}`, kind: 'synthetic', reference: 'max-byte regression fixture', sha256: null, notes: 'x'.repeat(2048)});
  }
  let excess = Buffer.byteLength(JSON.stringify(exactLimit)) - maxBytes;
  for (let index = exactLimit.sources.length - 1; excess > 0; index--) {
    const source = exactLimit.sources[index], cut = Math.min(excess, source.notes.length - 1);
    source.notes = source.notes.slice(0, source.notes.length - cut); excess -= cut;
  }
  const exactText = JSON.stringify(exactLimit); assert.equal(Buffer.byteLength(exactText), maxBytes);
  await importText(exactText, 'exact-limit.json'); assert.equal(run('isCurrent()'), true, allText(get('validation-errors')));
  get('export-arena').onclick(); assert.equal(blobs.at(-1).size, maxBytes);
  await importText(await blobs.at(-1).text(), 'exact-roundtrip.json'); assert.equal(run('isCurrent()'), true);
  // Literal hostile strings remain text. No remote image/HTML interpolation occurs.
  const hostile = structuredClone(sample); hostile.metadata.description = '<img src="https://invalid.example/x" onerror="alert(1)">';
  await importText(JSON.stringify(hostile)); assert.match(allText(get('calibration-info')), /<img src=/);
  assert.ok(created.every(item => item.tagName !== 'IMG' && item.tagName !== 'SCRIPT'));
  assert.equal((await read('/api/state')).hash, sandboxBefore.hash);
  assert.equal((await read('/api/objective/state')).hash, objectiveBefore.hash);
  assert.ok(calls.every(call => call.url.startsWith('/api/arena/')), 'UI never accesses simulation APIs');
  assert.equal(revoked.length, blobs.length);
  console.log('PASS arena DOM/real API: stacked floor/clearance evidence, ceiling collision vs trigger, display-only layers, explicit floor, numeric validation, click probe, current export, duplicate-key paths, dirty gates, late validation/query/sample/file guards, repeated import, UTF-8 limits, recoverable failure, safe text rendering, format roundtrip, and sandbox/objective isolation. Browser rendering and real downloads are not verified by this suite.');
})().catch(error => { console.error(error); process.exitCode = 1; });
