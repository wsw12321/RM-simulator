'use strict';
// Rendering/input adapter only: movement, supports, collisions, triggers and replay are native.
const $ = id => document.getElementById(id);
const canvas = $('movement-map'), ctx = canvas.getContext('2d');
const MAX_FILE_BYTES = 12 * 1024 * 1024, MAX_STEP_BATCH = 60;
const POLICY_SDK_VERSION = 1, POLICY_OBSERVATION = 'full-authored-geometry';
const encoder = new TextEncoder(), keys = new Set(), pointers = new Map();
let meta = null, state = null, synchronized = false, busy = '', generation = 0, revision = 0;
let draftRevision = 0, draftDirty = false, playing = false, accumulator = 0, lastTime = null;
let configPicker = null, replayPicker = null, mapTransform = null;
let eventHistory = [], eventCount = 0;
// Local choice for the NEXT request, not a persistent engine controller.
let controlSource = 'manual', policyProblem = '';
const manualControl = () => controlSource === 'manual';
const colorLayers = ['#73c8d7', '#e8cb82', '#a9b1ff', '#b4db94', '#edb7d5'];
function node(tag, text, className) { const item = document.createElement(tag); if (text !== undefined) item.textContent = text; if (className) item.className = className; return item; }
function operation(message, error = false) { $('operation-status').textContent = message; $('operation-status').className = 'operation-status' + (error ? ' error' : ''); }
const numeric = value => value == null ? '—' : Number(value).toLocaleString('zh-CN', {maximumFractionDigits: 3});
const ready = () => synchronized && !!meta && !!state;
const running = () => ready() && state.status === 'active';
function setPlaying(value) { playing = !!value && running() && (!busy || busy === 'step'); accumulator = 0; lastTime = null; updateControls(); }
function releaseInputs() { keys.clear(); pointers.clear(); for (const id of ['drive-up', 'drive-left', 'drive-down', 'drive-right']) $(id).className = ''; }
function pauseAndRelease() { setPlaying(false); releaseInputs(); }
function updateControls() {
  $('play').textContent = playing ? 'Ⅱ 暂停' : '▶ 开始';
  $('play').disabled = !running() || (!!busy && busy !== 'step');
  $('step').disabled = !running() || !!busy || playing;
  for (const id of ['reset', 'load-sample', 'import-config', 'export-replay', 'import-replay']) $(id).disabled = !ready() || !!busy;
  $('apply-config').disabled = !ready() || !!busy || !$('config-json').value.trim();
  $('config-json').disabled = !ready() || !!busy;
  $('view-layer').disabled = !ready();
  $('control-source').disabled = !ready() || !!busy;
  $('control-source').value = controlSource;
  for (const id of ['drive-up', 'drive-left', 'drive-down', 'drive-right']) $(id).disabled = !running() || !manualControl() || (!!busy && busy !== 'step');
  $('reconnect').hidden = synchronized || !!busy;
  $('reconnect').disabled = !!busy;
  $('run-status').textContent = busy ? ({connect: '连接中', step: playing ? '运行中' : '步进中', reset: '校验配置中', sample: '读取样例中', config: '读取配置中', import: '校验回放中', export: '准备回放中'})[busy] : !ready() ? '未同步 · 已暂停' : state.status === 'limit_reached' ? '已到运行上限' : playing ? '运行中' : '已暂停';
  $('run-status').className = 'state-pill' + (playing ? ' active' : '');
  $('config-status').textContent = draftDirty ? `草稿 #${draftRevision} · 尚未应用` : ready() ? '当前运行配置' : '等待连接';
  $('map-veil').hidden = ready();
  renderController();
}
function showErrors(error) {
  const issues = error?.issues?.length ? error.issues : error ? [{path: '$', code: 'request', message: error.message}] : [];
  $('config-errors').replaceChildren(...issues.map(issue => node('li', typeof issue === 'string' ? issue : `${issue.path || '$'} · ${issue.code || 'error'} · ${issue.message || ''}`)));
}
function requireSize(text, size) {
  if (size > MAX_FILE_BYTES || encoder.encode(text).length > MAX_FILE_BYTES) throw new Error('JSON 超过 12 MiB UTF-8 限制。');
}
async function api(path, data, raw = false) {
  let response;
  try { response = await fetch('/api/layered/' + path, data === undefined ? {} : {method: 'POST', headers: {'Content-Type': 'application/json', 'X-Sandbox-Client': '1'}, body: raw ? data : JSON.stringify(data)}); }
  catch (cause) { const error = new Error(`无法连接本机服务：${cause.message}`); error.uncertain = data !== undefined; throw error; }
  let value;
  try { value = await response.json(); }
  catch (_) { const error = new Error(`本机服务返回非 JSON 响应（HTTP ${response.status}）。`); error.uncertain = data !== undefined; throw error; }
  if (!response.ok) { const error = new Error(value.error || `请求失败（HTTP ${response.status}）。`); error.issues = value.errors || []; error.rejected = response.status < 500; error.uncertain = response.status >= 500 && data !== undefined; throw error; }
  return value;
}
function verifyPolicyMetadata(policy, hz) {
  if (!policy || policy.sdk_version !== POLICY_SDK_VERSION || policy.observation !== POLICY_OBSERVATION || policy.execution !== 'trusted-local-native' || policy.state_mode !== 'stateless' || policy.decision_hz !== 60 || hz !== policy.decision_hz || typeof policy.controller_id !== 'string' || !/^[A-Za-z][A-Za-z0-9_.-]{0,63}$/.test(policy.controller_id) || !Number.isFinite(policy.watchdog_seconds) || policy.watchdog_seconds <= 0) throw new Error('C++ 策略元数据 / SDK / 观测协议不兼容；不能回退为人工模式。请使用匹配构建并重新连接。');
}
function verifyActionSource(source, policy, expected = undefined) {
  if (source === null && expected === undefined) return;
  // Recorded replay labels are provenance, not authenticated controller execution.
  const validLabel = source && (source.kind === 'manual' ? source.controller_id === null : source.kind === 'policy' && typeof source.controller_id === 'string' && /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/.test(source.controller_id));
  if (!validLabel || source.sdk_version !== POLICY_SDK_VERSION) throw new Error('原生动作来源记录格式 / SDK 不兼容；请重新连接。');
  // Only a newly requested decision must match this captured compiled controller.
  if (expected !== undefined && (source.kind !== expected || source.sdk_version !== policy.sdk_version || (expected === 'policy' && source.controller_id !== policy.controller_id))) throw new Error('原生动作来源与所选控制源 / 策略身份不一致；结果未显示，请重新连接。');
}
function sourceLabel(source) {
  return source === null ? '尚无已提交动作' : source.kind === 'policy' ? `C++ · ${source.controller_id}（记录标签）` : '人工已解析动作（记录标签）';
}
function verifyPair(nextMeta, nextState) {
  if (nextMeta.profile !== 'layered-native-v1' || nextState.profile !== nextMeta.profile || nextState.schema !== 'rmuc-layered-state.v1' || !nextMeta.map || !nextMeta.config || !nextState.robot) throw new Error('服务响应不属于支持的分层原生模式。');
  for (const key of ['build', 'map_hash', 'config_hash']) if (nextMeta[key] !== nextState[key]) throw new Error(`元数据与状态的 ${key} 不一致，请重新连接。`);
  if (nextMeta.policy_error) throw new Error(`原生策略实验无效（不判负）：${nextMeta.policy_error}`);
  verifyPolicyMetadata(nextMeta.policy, nextMeta.hz);
  verifyActionSource(nextState.action_source, nextMeta.policy);
}
function commitRun(nextMeta, nextState, replaceDraft = true) {
  verifyPair(nextMeta, nextState);
  meta = nextMeta; state = nextState; synchronized = true; policyProblem = ''; revision++;
  eventHistory = []; eventCount = 0; recordEvents(nextState);
  if (replaceDraft) { $('config-json').value = JSON.stringify({map: meta.map, config: meta.config}, null, 2); draftRevision++; draftDirty = false; }
  refreshLayers(); renderMetadata(); renderState(); drawMap(); updateControls();
}
async function reconnect() {
  if (busy) return;
  pauseAndRelease(); const token = ++generation; busy = 'connect'; updateControls();
  try {
    const nextMeta = await api('meta'); const nextState = await api('state');
    if (token !== generation) return;
    commitRun(nextMeta, nextState, !draftDirty);
    operation(`已连接原生分层模式。下一批使用${manualControl() ? '人工方向输入' : '内置 C++ 策略'}；按“开始”推进，显示层不改变承载面。`);
  } catch (error) { if (token !== generation) return; synchronized = false; policyProblem = error.message; operation(`连接失败：${error.message}`, true); }
  finally { if (token === generation) { busy = ''; updateControls(); } }
}
function applyState(nextState) {
  verifyPair(meta, nextState);
  if (nextState.tick < state.tick) throw new Error('收到倒退的状态；已暂停，请重新连接。');
  state = nextState; revision++; recordEvents(nextState);
  if (state.status !== 'active') pauseAndRelease();
  renderState(); drawMap(); updateControls();
}
function currentAction() {
  if (!manualControl()) return [0, 0];
  const held = code => keys.has(code) || [...pointers.values()].includes(code);
  return [(Number(held('KeyD') || held('ArrowRight')) - Number(held('KeyA') || held('ArrowLeft'))) * 1000, (Number(held('KeyW') || held('ArrowUp')) - Number(held('KeyS') || held('ArrowDown'))) * 1000];
}
function selectController() {
  const requested = $('control-source').value;
  if (!ready() || busy) { $('control-source').value = controlSource; return; }
  if (!['manual', 'policy'].includes(requested)) { $('control-source').value = controlSource; operation('未知控制源；当前选择未改变。', true); return; }
  if (requested === controlSource) return;
  pauseAndRelease(); controlSource = requested; revision++; policyProblem = '';
  operation(`已暂停并切换下一批控制源为${manualControl() ? '人工' : '内置 C++ 策略'}。机器人、tick 与已记录动作均未重置。`);
  updateControls();
}
async function advance(count) {
  if (!running() || busy || !Number.isInteger(count) || count < 1) return;
  const token = generation, startRevision = revision, requestedSource = controlSource;
  const action = currentAction(), countBound = Math.min(count, MAX_STEP_BATCH, meta.config.max_ticks - state.tick);
  const expectedTick = state.tick + countBound;
  const requestedPolicy = {sdk_version: meta.policy.sdk_version, controller_id: meta.policy.controller_id};
  const body = requestedSource === 'policy' ? {count: countBound, ...requestedPolicy} : {count: countBound, action};
  busy = 'step'; policyProblem = ''; updateControls();
  try {
    const next = await api(requestedSource === 'policy' ? 'policy-step' : 'step', body);
    if (token !== generation || startRevision !== revision) return;
    if (requestedSource !== controlSource) throw new Error('请求期间控制源发生变化；结果未显示，请重新连接。');
    if (next.tick !== expectedTick) throw new Error('原生响应帧号与本次请求不一致，可能是过期结果或其他页面修改；请重新连接。');
    verifyActionSource(next.action_source, requestedPolicy, requestedSource);
    applyState(next);
    if (!playing) operation(requestedSource === 'policy' ? `已推进至 tick ${state.tick}；每个 1/60 秒动作由 ${requestedPolicy.controller_id} 在原生 C++ 中决定。` : `已推进至 tick ${state.tick}；动作 [${action.join(', ')}]，位置由 C++ 返回。`);
  } catch (error) {
    if (token !== generation) return;
    pauseAndRelease();
    if (error.uncertain || !error.rejected) synchronized = false;
    if (requestedSource === 'policy' || !error.rejected) policyProblem = `${requestedSource === 'policy' ? '策略实验请求无效（不判负）：' : ''}${error.message}`;
    operation(`原生步进失败，已暂停：${error.message}${synchronized ? ' 当前已显示状态保留。' : ' 请重新连接，确认原生运行状态。'}`, true);
  } finally { if (token === generation) { busy = ''; updateControls(); } }
}
async function resetRun(rawText = null) {
  if (!ready() || busy) return;
  pauseAndRelease(); const token = ++generation; busy = 'reset'; showErrors(null); updateControls();
  let accepted = false;
  try {
    const text = rawText === null ? JSON.stringify({map: meta.map, config: meta.config}) : rawText;
    requireSize(text);
    const next = await api('reset', text, true); accepted = true;
    const nextMeta = await api('meta');
    if (token !== generation) return;
    commitRun(nextMeta, next, true);
    $('replay-status').textContent = '新运行已建立；尚未导入回放。';
    operation('配置通过严格验证并已创建新运行。当前地图与配置在这一局内固定。');
  } catch (error) {
    if (token !== generation) return;
    if (accepted || error.uncertain) { synchronized = false; operation(`运行状态需要重新同步：${error.message} 点击“重新连接”确认结果。`, true); }
    else { operation(`配置未应用，原运行保留并已暂停：${error.message}`, true); }
    showErrors(error);
  } finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
function editDraft() {
  if (busy) return;
  pauseAndRelease(); draftRevision++; draftDirty = true; showErrors(null);
  operation('草稿尚未应用，当前运行未改变。点击“验证并新建运行”后才会替换地图与机器人配置。'); updateControls();
}
async function loadSample() {
  if (!ready() || busy) return;
  pauseAndRelease(); const token = ++generation; busy = 'sample'; updateControls();
  try { const value = await api('sample'); if (token !== generation) return; $('config-json').value = JSON.stringify(value, null, 2); draftRevision++; draftDirty = true; showErrors(null); operation('合成样例已载入草稿，当前运行未改变。点击“验证并新建运行”来使用它。'); }
  catch (error) { if (token === generation) operation(`样例读取失败，原草稿与运行保留：${error.message}`, true); }
  finally { if (token === generation) { busy = ''; updateControls(); } }
}
function pickerSnapshot() { return {generation, revision, draftRevision}; }
function pickerCurrent(snapshot) { return !snapshot || (snapshot.generation === generation && snapshot.revision === revision && snapshot.draftRevision === draftRevision); }
function openPicker(kind) {
  if (!ready() || busy) return;
  pauseAndRelease();
  if (kind === 'config') { configPicker = pickerSnapshot(); $('config-file').click(); }
  else { replayPicker = pickerSnapshot(); $('replay-file').click(); }
}
async function importFile(event, kind) {
  const file = event.target.files?.[0], snapshot = kind === 'config' ? configPicker : replayPicker;
  if (kind === 'config') configPicker = null; else replayPicker = null;
  event.target.value = '';
  if (!file) return;
  if (!ready() || busy || !pickerCurrent(snapshot)) { operation('文件选择期间状态已变化，未导入。请重新点击导入并选择文件。', true); return; }
  pauseAndRelease(); const token = ++generation; busy = kind === 'config' ? 'config' : 'import'; showErrors(null); updateControls();
  let accepted = false;
  try {
    if (file.size > MAX_FILE_BYTES) throw new Error('文件超过 12 MiB 限制。');
    if (kind === 'replay') $('replay-status').textContent = '正在读取回放；原运行在完整验证成功前不会替换。';
    const text = await file.text();
    if (token !== generation) return;
    requireSize(text, file.size);
    if (kind === 'config') {
      $('config-json').value = text; draftRevision++; draftDirty = true;
      operation(`已读取 ${file.name || '配置 JSON'} 作为草稿。当前运行未改变，请验证后应用。`);
    } else {
      $('replay-status').textContent = '正在从头逐帧重算并校验回放…';
      const next = await api('import', text, true); accepted = true;
      const nextMeta = await api('meta');
      if (token !== generation) return;
      commitRun(nextMeta, next, true);
      $('replay-status').textContent = `原生动作回放校验通过：${state.tick} 帧，最终哈希 ${state.hash}。停在最后一帧；没有重新执行策略。`;
      operation(`已按记录动作验证并导入 ${state.tick} 帧；保持暂停，没有重新执行策略。下一批仍使用${manualControl() ? '人工输入' : 'C++ 策略'}。`);
    }
  } catch (error) {
    if (token !== generation) return;
    if (accepted || error.uncertain) { synchronized = false; operation(`导入结果需重新同步：${error.message} 点击“重新连接”确认结果。`, true); }
    else operation(`导入失败，原运行${kind === 'config' ? '与草稿' : ''}保留并已暂停：${error.message}`, true);
    if (kind === 'replay') $('replay-status').textContent = accepted || error.uncertain ? '结果未同步，不能声称已验证。请重新连接。' : `校验失败，原运行保留：${error.message}`;
    showErrors(error);
  } finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
async function exportReplay() {
  if (!ready() || busy) return;
  pauseAndRelease(); const token = ++generation; busy = 'export'; updateControls();
  try {
    const replay = await api('replay'); if (token !== generation) return;
    if (replay.profile !== meta.profile || replay.build !== meta.build || replay.map_hash !== meta.map_hash || replay.config_hash !== meta.config_hash || replay.final_hash !== state.hash || !Array.isArray(replay.frames) || replay.frames.length !== state.tick || replay.schema !== 'rmuc-layered-replay.v2' || !Array.isArray(replay.action_sources)) { synchronized = false; throw new Error('服务器回放与当前显示运行不一致，可能由另一页面改变。请重新连接后导出。'); }
    try { verifyReplaySources(replay); } catch (error) { synchronized = false; throw error; }
    const exportedTicks = replay.frames.length;
    const text = JSON.stringify(replay); requireSize(text);
    const blob = new Blob([text], {type: 'application/json'}), url = URL.createObjectURL(blob), anchor = node('a');
    anchor.href = url; anchor.download = `tactics-layered-tick-${exportedTicks}.json`; document.body.append(anchor); anchor.click(); anchor.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    operation(`已请求下载分层动作回放（${exportedTicks} 帧，不重新执行策略）。请在浏览器下载列表确认文件；本页不验证实际落盘。`);
  } catch (error) { if (token === generation) operation(`回放导出失败：${error.message}`, true); }
  finally { if (token === generation) { busy = ''; updateControls(); } }
}
function verifyReplaySources(replay) {
  let covered = 0;
  for (const segment of replay.action_sources) {
    if (!segment) throw new Error('回放控制源分段缺失。');
    verifyActionSource(segment, meta.policy);
    if (!Number.isInteger(segment.start_tick) || segment.start_tick !== covered || !Number.isInteger(segment.count) || segment.count < 1) throw new Error('回放控制源分段与动作帧不一致。');
    covered += segment.count;
  }
  const finalSource = replay.action_sources.length ? replay.action_sources[replay.action_sources.length - 1] : null;
  if (covered !== replay.frames.length || (finalSource === null) !== (state.action_source === null) || (finalSource && ['kind','sdk_version','controller_id'].some(key => finalSource[key] !== state.action_source[key]))) {
    synchronized = false; throw new Error('回放末帧控制源与当前显示状态不一致，请重新连接。');
  }
}
function renderController() {
  const selected = manualControl() ? '人工 · 下次推进' : 'C++ 策略 · 下次推进';
  $('controller-status').textContent = policyProblem ? '实验已暂停 / 需检查' : ready() ? selected : '等待匹配的原生元数据';
  $('controller-status').className = 'mini-tag' + (policyProblem ? ' error' : !manualControl() && ready() ? ' policy' : '');
  $('policy-status').textContent = policyProblem || (ready() ? `末批动作来源记录：${sourceLabel(state.action_source)}。记录标签未经认证；控制选择只影响下一批请求，浏览器不执行策略。` : '没有匹配的策略元数据和状态时，不启用任何控制源。');
  $('policy-status').className = 'panel-note' + (policyProblem ? ' error' : '');
  canvas.setAttribute('aria-label', manualControl() ? '分层地图。WASD 或方向键移动机器人；开始后连续移动，暂停时按住方向再点单步。显示层不会改变物理。' : '分层地图。下一批由原生 C++ 策略控制，键盘方向输入已停用；使用开始、暂停或单步。显示层不会改变物理。');
  $('drive-title').textContent = manualControl() ? 'W A S D / ↑ ← ↓ →' : 'C++ 原生策略接管下一批动作';
  $('drive-note').textContent = manualControl() ? '先开始，再按住移动；暂停时按住方向并点单步。失焦、切后台或导入时自动停下。' : '键盘与触控方向输入已停用。按“开始”或“单步”，由 C++ 按固定 60 Hz 读取观测并决定每帧动作。';
}
function refreshLayers() {
  const previous = $('view-layer').value;
  const option = (value, text) => { const el = node('option', text); el.value = value; return el; };
  $('view-layer').replaceChildren(option('', '全部层'), ...meta.map.layers.map(layer => option(layer.id, `${layer.name} · ${layer.id}`)));
  $('view-layer').value = meta.map.layers.some(layer => layer.id === previous) ? previous : '';
}
function renderMetadata() {
  const items = [];
  const pair = (title, value) => items.push(node('dt', title), node('dd', value));
  pair('模式', meta.profile); pair('引擎构建', meta.build); pair('地图哈希', meta.map_hash); pair('配置哈希', meta.config_hash);
  pair('地图来源', `${meta.map.metadata.name} / ${meta.map.calibration.status} / 非官方`);
  pair('坐标单位', `${meta.map.frame.units}，${meta.map.frame.up_axis} 向上`);
  pair('固定步长', `${meta.hz} Hz；每次最多 ${MAX_STEP_BATCH} 帧`);
  pair('移动约定', 'XY 分量独立；斜向速度更快。无惯性、加速度或真实底盘动力学。');
  for (const limitation of meta.limitations || []) pair('实验限制', typeof limitation === 'string' ? limitation : JSON.stringify(limitation));
  $('mode-metadata').replaceChildren(...items);
  $('map-identity').textContent = meta.map.metadata.id;
  const policy = meta.policy, policyItems = [];
  const policyPair = (title, value) => policyItems.push(node('dt', title), node('dd', value));
  policyPair('策略 ID', policy.controller_id); policyPair('SDK / 决策频率', `v${policy.sdk_version} / ${policy.decision_hz} Hz`);
  policyPair('观测配置', policy.observation); policyPair('执行 / 状态', `${policy.execution} / ${policy.state_mode}`);
  policyPair('进程响应上限', `${policy.watchdog_seconds} s（非安全沙箱）`);
  $('policy-metadata').replaceChildren(...policyItems);
  const manual = node('option', '人工 · 键盘 / 触控方向键'); manual.value = 'manual';
  const native = node('option', `内置 C++ · ${policy.controller_id}`); native.value = 'policy';
  $('control-source').replaceChildren(manual, native); $('control-source').value = controlSource;
  $('control-note').textContent = '切换会暂停并释放按键，不重置机器人位置或回放。合成样例默认起点可演示坡道 → 上层平台；其他地图不保证可达。';
}
function renderHeight(robot, config) {
  const root = $('height-diagram'); root.replaceChildren();
  const bottom = robot.envelope_min_mm, top = robot.envelope_max_mm, ceiling = robot.ceiling_z_mm;
  const low = Math.min(0, bottom), high = Math.max(top, ceiling ?? top), span = Math.max(1, high - low);
  const pct = value => `${8 + (value - low) / span * 78}%`;
  const line = (value, label, className) => { const item = node('div', undefined, `height-line ${className || ''}`); item.style.bottom = pct(value); item.append(node('span', label)); root.append(item); };
  const body = node('div', undefined, 'height-robot'); body.style.bottom = pct(bottom); body.style.height = `${(top - bottom) / span * 78}%`; root.append(body);
  line(bottom, `占位下界 ${numeric(bottom)}`); line(top, `占位上界 ${numeric(top)}`, 'robot-top');
  if (ceiling !== null) line(ceiling, `顶棚 z ${numeric(ceiling)}`, 'ceiling');
  root.setAttribute('aria-label', `原生保守占位高度 ${numeric(bottom)} 至 ${numeric(top)} 毫米；${ceiling === null ? '当前承载面未声明顶棚' : '顶棚 ' + numeric(ceiling) + ' 毫米'}`);
  $('clearance-note').textContent = robot.clearance_mm == null ? '当前承载面未声明顶棚；不代表已核验的无限净空，实体碰撞仍由原生引擎检查。' : `保守可用净高 ${numeric(robot.clearance_mm)} mm；顶部余量 ${numeric(robot.clearance_mm - config.height_mm)} mm。占位上下界直接使用原生结果。`;
}

const contactNames = {boundary: '场地边界', support: '承载边界 / 未连接', slope: '坡度限制', ceiling: '顶棚净空', collision: '实体碰撞'};
function renderState() {
  const robot = state.robot, config = meta.config, items = [];
  const pair = (title, value, className) => items.push(node('dt', title), node('dd', value, className));
  pair('位置 x / y', `${numeric(robot.x_mm)} / ${numeric(robot.y_mm)} mm`);
  pair('中心支承 z', `${numeric(robot.z_mm)} mm`, 'accent'); pair('支承高度范围', `${numeric(robot.support_min_mm)} ~ ${numeric(robot.support_max_mm)} mm`); pair('承载面', robot.surface_id, 'accent'); pair('真实层', robot.layer_id);
  pair('机器人尺寸', `r ${config.radius_mm} × h ${config.height_mm} mm`);
  pair('末帧阻挡', state.contacts.length ? state.contacts.map(contact => `${contactNames[contact.kind] || contact.kind}: ${contact.id}`).join('；') : '无');
  pair('当前触发', state.active_trigger_ids.length ? state.active_trigger_ids.join('、') : '无');
  pair('末批来源记录', sourceLabel(state.action_source)); pair('状态哈希', state.hash); $('robot-state').replaceChildren(...items);
  $('tick-status').textContent = `TICK ${state.tick} · ${(state.tick / meta.hz).toFixed(3)} s`;
  renderHeight(robot, config); renderEvents();
}
function recordEvents(nextState) {
  const incoming = nextState.batch_events || nextState.events || [];
  eventCount += incoming.length; eventHistory.push(...incoming);
  eventHistory = eventHistory.slice(-80);
}
function renderEvents() {
  const visible = eventHistory.slice().reverse();
  $('event-count').textContent = `已接收 ${eventCount} 条${eventCount > 80 ? ' · 最近 80' : ''}`;
  $('events').replaceChildren(...(visible.length ? visible.map(event => {
    const type = event.type;
    const className = type.startsWith('trigger_') ? 'trigger' : type === 'transition' ? 'transition' : 'collision';
    const item = node('li', undefined, className);
    item.append(node('span', `T${event.tick} · ${(event.tick / meta.hz).toFixed(3)}s${event.substep == null ? '' : ' · sub ' + event.substep}`, 'stamp'));
    const descriptions = {trigger_enter: '进入触发区', trigger_exit: '离开触发区', transition: '通过显式连接', collision: '实体阻挡', blocked: '移动受阻'};
    let text = `${descriptions[type] || type} · ${event.id || ''}`;
    if (event.from_surface || event.to_surface) text += ` · ${event.from_surface || '?'} → ${event.to_surface || '?'}`;
    if (event.kind) text += ` · ${contactNames[event.kind] || event.kind}`;
    item.append(node('span', text)); return item;
  }) : [node('li', '尚无原生事件。当前阻挡另见上方状态。', 'empty')]));
}
function screen(point) { const t = mapTransform; return [t.left + (point[0] - t.minX) * t.scale, canvas.height - t.top - (point[1] - t.minY) * t.scale]; }
function polygon(points, fill, stroke, dash = [], width = 1.5) {
  ctx.beginPath(); points.forEach((point, index) => index ? ctx.lineTo(...screen(point)) : ctx.moveTo(...screen(point))); ctx.closePath();
  if (fill) { ctx.fillStyle = fill; ctx.fill(); }
  if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = width; ctx.setLineDash(dash); ctx.stroke(); ctx.setLineDash([]); }
}
function drawMap() {
  ctx.clearRect(0, 0, canvas.width, canvas.height); ctx.fillStyle = '#09131d'; ctx.fillRect(0, 0, canvas.width, canvas.height);
  if (!meta || !state) return;
  const map = meta.map, robot = state.robot, selectedLayer = $('view-layer').value;
  const xs = map.boundary_mm.map(p => p[0]), ys = map.boundary_mm.map(p => p[1]);
  const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
  const scale = Math.min((canvas.width - 120) / (maxX - minX), (canvas.height - 110) / (maxY - minY));
  mapTransform = {minX, maxX, minY, maxY, scale, left: (canvas.width - (maxX - minX) * scale) / 2, top: (canvas.height - (maxY - minY) * scale) / 2};
  const grid = Math.max(100, Math.pow(10, Math.floor(Math.log10(Math.max(maxX - minX, maxY - minY))) - 1));
  ctx.font = '11px ui-monospace, monospace'; ctx.lineWidth = 1;
  for (let x = Math.ceil(minX / grid) * grid; x <= maxX; x += grid) { const a = screen([x,minY]), b = screen([x,maxY]); ctx.beginPath(); ctx.moveTo(...a); ctx.lineTo(...b); ctx.strokeStyle = '#192a35'; ctx.stroke(); ctx.fillStyle = '#6d8799'; ctx.textAlign = 'center'; if (Math.round(x / grid) % 2 === 0) ctx.fillText(String(x), a[0], a[1] + 20); }
  for (let y = Math.ceil(minY / grid) * grid; y <= maxY; y += grid) { const a = screen([minX,y]), b = screen([maxX,y]); ctx.beginPath(); ctx.moveTo(...a); ctx.lineTo(...b); ctx.strokeStyle = '#192a35'; ctx.stroke(); ctx.fillStyle = '#6d8799'; ctx.textAlign = 'right'; if (Math.round(y / grid) % 2 === 0) ctx.fillText(String(y), a[0] - 10, a[1] + 4); }
  polygon(map.boundary_mm, null, '#5a7486', [], 2);
  const surfaces = map.surfaces.filter(surface => !selectedLayer || surface.layer_id === selectedLayer);
  for (const surface of surfaces) {
    const color = colorLayers[Math.max(0, map.layers.findIndex(layer => layer.id === surface.layer_id)) % colorLayers.length];
    polygon(surface.polygon_mm, color + (surface.id === robot.surface_id ? '27' : '12'), surface.id === robot.surface_id ? '#aee17a' : color, [], surface.id === robot.surface_id ? 4 : 1.5);
  }
  const intersectsRobotHeight = volume => robot.envelope_max_mm >= volume.z_min_mm && robot.envelope_min_mm < volume.z_max_mm;
  if ($('show-solids').checked) for (const volume of map.collision_volumes) polygon(volume.polygon_mm, intersectsRobotHeight(volume) ? '#fb89792b' : '#fb897909', intersectsRobotHeight(volume) ? '#fb8979' : '#70534f', [], 2);
  if ($('show-triggers').checked) for (const volume of map.trigger_volumes) {
    if (selectedLayer && !map.surfaces.some(surface => surface.layer_id === selectedLayer && volume.surface_ids.includes(surface.id))) continue;
    polygon(volume.polygon_mm, state.active_trigger_ids.includes(volume.id) ? '#c3a1f445' : '#c3a1f412', '#c3a1f4', [8,5], 2);
  }
  if ($('show-portals').checked) for (const portal of map.transitions) {
    if (selectedLayer && !map.surfaces.some(surface => surface.layer_id === selectedLayer && [portal.from_surface, portal.to_surface].includes(surface.id))) continue;
    ctx.beginPath(); ctx.moveTo(...screen(portal.portal_mm[0])); ctx.lineTo(...screen(portal.portal_mm[1])); ctx.strokeStyle = '#73c8d7'; ctx.lineWidth = 4; ctx.setLineDash([7,5]); ctx.stroke(); ctx.setLineDash([]);
  }
  for (const surface of surfaces) {
    const center = surface.polygon_mm.reduce((p, q) => [p[0] + q[0] / surface.polygon_mm.length, p[1] + q[1] / surface.polygon_mm.length], [0,0]);
    const p = screen(center); const z = surface.height.z_mm + surface.height.dx * center[0] + surface.height.dy * center[1];
    const overlap = surfaces.some(other => other !== surface && JSON.stringify(other.polygon_mm) === JSON.stringify(surface.polygon_mm));
    const offset = overlap ? map.layers.findIndex(layer => layer.id === surface.layer_id) * 29 : 0;
    ctx.textAlign = 'center'; ctx.font = '11px ui-monospace, monospace'; ctx.fillStyle = '#09131ddd'; ctx.fillRect(p[0] - 88, p[1] - 18 + offset, 176, 27);
    ctx.fillStyle = surface.id === robot.surface_id ? '#aee17a' : '#bdd3df'; ctx.fillText(surface.id, p[0], p[1] - 7 + offset); ctx.font = '9px ui-monospace, monospace'; ctx.fillStyle = '#8fa9b9'; ctx.fillText(`z ${numeric(z)} mm`, p[0], p[1] + 5 + offset);
  }
  const position = screen([robot.x_mm, robot.y_mm]); const radius = Math.max(5, meta.config.radius_mm * scale);
  ctx.beginPath(); ctx.arc(...position, radius, 0, Math.PI * 2); ctx.fillStyle = '#aee17a55'; ctx.fill(); ctx.strokeStyle = '#d3f6ac'; ctx.lineWidth = 3; ctx.stroke();
  ctx.beginPath(); ctx.moveTo(position[0]-6,position[1]); ctx.lineTo(position[0]+6,position[1]); ctx.moveTo(position[0],position[1]-6); ctx.lineTo(position[0],position[1]+6); ctx.stroke();
  const hidden = selectedLayer && selectedLayer !== robot.layer_id;
  ctx.fillStyle = '#aee17a'; ctx.font = '11px ui-monospace, monospace'; ctx.textAlign = 'center'; ctx.fillText(`z ${numeric(robot.z_mm)}${hidden ? ' · 真实层未显示' : ''}`, position[0], position[1] - radius - 10);
  ctx.fillStyle = '#8ba6b8'; ctx.textAlign = 'left'; ctx.font = '11px ui-monospace, monospace'; ctx.fillText('↑ +y     → +x    [mm]', 20, 24);
  $('view-status').textContent = `${surfaces.length} 个可见承载面。机器人实际位于 ${robot.surface_id} / ${robot.layer_id}，z = ${numeric(robot.z_mm)} mm。${hidden ? '其真实承载面被显示过滤隐藏，机器人仍保留。' : ''}实体亮色仅表示与机器人高度范围重叠；碰撞以原生状态为准。显示过滤不改变物理。`;
}
function loop(time) {
  if (playing && running()) {
    if (lastTime !== null) accumulator += Math.max(0, (time - lastTime) / 1000) * meta.hz;
    lastTime = time;
    if (!busy && accumulator >= 1) { const count = Math.min(MAX_STEP_BATCH, Math.floor(accumulator), meta.config.max_ticks - state.tick); accumulator -= count; void advance(count); }
  } else lastTime = null;
  requestAnimationFrame(loop);
}
function typingTarget(target) { return target && (['INPUT','TEXTAREA','SELECT'].includes(target.tagName) || target.isContentEditable); }
window.addEventListener('keydown', event => {
  if (!['KeyW','KeyA','KeyS','KeyD','ArrowUp','ArrowLeft','ArrowDown','ArrowRight'].includes(event.code) || typingTarget(event.target) || !running() || !manualControl() || (busy && busy !== 'step')) return;
  // A held key across a pause/source change needs a fresh press, not autorepeat.
  if (event.repeat && !keys.has(event.code)) return;
  event.preventDefault(); keys.add(event.code);
});
window.addEventListener('keyup', event => { keys.delete(event.code); });
window.addEventListener('blur', pauseAndRelease);
document.addEventListener('visibilitychange', () => { if (document.hidden) pauseAndRelease(); });
for (const [id, code] of [['drive-up','KeyW'],['drive-left','KeyA'],['drive-down','KeyS'],['drive-right','KeyD']]) {
  const button = $(id);
  button.onpointerdown = event => { if (!running() || !manualControl() || (busy && busy !== 'step')) return; event.preventDefault(); pointers.set(event.pointerId, code); button.className = 'held'; button.setPointerCapture?.(event.pointerId); };
  const release = event => { pointers.delete(event.pointerId); if (![...pointers.values()].includes(code)) button.className = ''; };
  button.onpointerup = release; button.onpointercancel = release; button.onlostpointercapture = release;
}
$('control-source').onchange = selectController;
$('play').onclick = () => { if (!running() || (busy && busy !== 'step')) return; setPlaying(!playing); };
$('step').onclick = () => { if (!playing) return advance(1); };
$('reset').onclick = () => resetRun();
$('apply-config').onclick = () => resetRun($('config-json').value);
$('config-json').oninput = editDraft;
$('load-sample').onclick = loadSample;
$('import-config').onclick = () => openPicker('config');
$('import-replay').onclick = () => openPicker('replay');
$('config-file').onchange = event => importFile(event, 'config');
$('replay-file').onchange = event => importFile(event, 'replay');
$('config-file').oncancel = () => { configPicker = null; };
$('replay-file').oncancel = () => { replayPicker = null; };
$('export-replay').onclick = exportReplay;
$('reconnect').onclick = reconnect;
for (const id of ['view-layer','show-solids','show-triggers','show-portals']) $(id).onchange = drawMap;
updateControls(); drawMap(); void reconnect(); requestAnimationFrame(loop);
