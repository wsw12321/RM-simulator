'use strict';
// Input/rendering adapter only. The native engine owns filtering, actions and physics.
// The normal state endpoint returns ONE selected observation, never both robots.
const $ = id => document.getElementById(id);
const canvas = $('combat-map'), ctx = canvas.getContext('2d');
const PROFILE = 'filtered-combat-v1', OBSERVATION_PROFILE = 'experimental-range-los-v1';
const SDK_VERSION = 1, MAX_STEP_BATCH = 120, MAX_FILE_BYTES = 12 * 1024 * 1024;
const encoder = new TextEncoder(), keys = new Set(), pointers = new Map();
let meta = null, state = null, synchronized = false, busy = '', generation = 0, revision = 0;
let observer = 0, sources = ['manual', 'policy'], policyAvailable = false;
let playing = false, accumulator = 0, lastTime = null, aim = null, mapTransform = null, replayPicker = null;
const POLICY_ERROR = 'C++ 策略元数据不可用或不兼容。请检查本机源码并重建；仍可明确选择两台人工控制。不会自动回退或判负。';
const ready = () => synchronized && !!meta && !!state;
const running = () => ready() && state.terminal === 0;
const selectedManual = () => sources[observer] === 'manual';
const canAdvance = () => running() && (policyAvailable || sources.every(kind => kind === 'manual'));
const integer = (value, low = 0, high = Number.MAX_SAFE_INTEGER) => Number.isSafeInteger(value) && value >= low && value <= high;
const finiteKeys = (value, names) => value && typeof value === 'object' && !Array.isArray(value) && Object.keys(value).length === names.length && names.every(key => Object.prototype.hasOwnProperty.call(value, key));
const number = value => Number(value).toLocaleString('zh-CN', {maximumFractionDigits: 3});
function node(tag, text, className) { const result = document.createElement(tag); if (text !== undefined) result.textContent = text; if (className) result.className = className; return result; }
function operation(text, error = false) { $('operation-status').textContent = text; $('operation-status').className = 'operation-status' + (error ? ' error' : ''); }
function facts(id, pairs) { $(id).replaceChildren(...pairs.flatMap(([title, value]) => [node('dt', title), node('dd', value)])); }
function releaseInputs() { keys.clear(); pointers.clear(); aim = null; for (const id of ['drive-up','drive-left','drive-down','drive-right','fire']) $(id).className = ''; }
function setPlaying(value) { playing = !!value && canAdvance() && (!busy || busy === 'step'); accumulator = 0; lastTime = null; updateControls(); }
function pauseAndRelease() { setPlaying(false); releaseInputs(); }
function updateControls() {
  $('play').textContent = playing ? 'Ⅱ 暂停' : '▶ 开始';
  $('play').disabled = !canAdvance() || (!!busy && !(busy === 'step' && playing));
  $('step').disabled = !canAdvance() || !!busy || playing;
  for (const id of ['reset','export-replay','import-replay','source-0','source-1','observer']) $(id).disabled = !ready() || !!busy;
  for (let i = 0; i < 2; i++) $('source-' + i).value = sources[i];
  $('observer').value = String(observer);
  for (const id of ['drive-up','drive-left','drive-down','drive-right','fire']) $(id).disabled = !running() || !selectedManual() || (!!busy && busy !== 'step');
  $('reconnect').hidden = synchronized || !!busy; $('reconnect').disabled = !!busy;
  $('map-veil').hidden = ready();
  const busyNames = {connect:'连接中', perspective:'读取所选视角', step:playing ? '运行中' : '步进中', reset:'重置中', import:'校验回放中', export:'准备开发者导出'};
  $('run-status').textContent = busy ? busyNames[busy] : !ready() ? '未同步 · 已暂停' : state.terminal ? '本局已终止' : playing ? '运行中' : '已暂停';
  $('run-status').className = 'state-pill' + (playing ? ' active' : '');
  $('controller-status').textContent = !ready() ? '等待过滤观测' : !policyAvailable ? '策略不可用 · 可选人工' : `当前视角：机器人 ${observer}`;
  $('controller-status').className = 'mini-tag' + (ready() && !policyAvailable ? ' error' : '');
  $('drive-title').textContent = selectedManual() ? `机器人 ${observer} · WASD / 方向键` : `机器人 ${observer} · C++ 策略`;
  canvas.setAttribute('aria-label', selectedManual() ? `机器人 ${observer} 的过滤观测。WASD 或方向键移动，鼠标瞄准，空格或左键发射。未观测到的敌方不显示。` : `机器人 ${observer} 的过滤观测。当前由 C++ 策略控制，人工输入停用；使用开始、暂停或单步。`);
  $('drive-note').textContent = selectedManual() ? '鼠标瞄准，空格或左键发射。暂停时按住方向 / 发射键再点单步；单步会先捕获动作，再释放输入。另一台人工机器人保持零移动、不发射。' : '此视角的人工输入已停用。开始或单步后，原生 C++ 从这台机器人的过滤观测生成动作。';
}
async function api(path, data, raw = false) {
  let response;
  try { response = await fetch('/api/combat/' + path, data === undefined ? {} : {method:'POST', headers:{'Content-Type':'application/json','X-Sandbox-Client':'1'}, body:raw ? data : JSON.stringify(data)}); }
  catch (cause) { const error = new Error(`无法连接本机服务：${cause.message}`); error.uncertain = data !== undefined; throw error; }
  let value;
  try { value = await response.json(); }
  catch (_) { const error = new Error(`本机服务返回非 JSON 响应（HTTP ${response.status}）。`); error.uncertain = data !== undefined; throw error; }
  if (!response.ok) { const error = new Error(typeof value.error === 'string' ? value.error : `请求失败（HTTP ${response.status}）。`); error.rejected = response.status < 500; error.uncertain = response.status >= 500 && data !== undefined; throw error; }
  return value;
}
function verifyMeta(value) {
  if (!value || value.schema !== 1 || value.profile !== PROFILE || value.observation_profile !== OBSERVATION_PROFILE || value.official !== false || typeof value.build !== 'string' || !value.build || value.hz !== 60 || value.duration !== 7200 || value.max_batch !== MAX_STEP_BATCH) throw new Error('本机服务不是支持的实验过滤对战协议，请使用匹配构建。');
}
function validPolicy(value) {
  return value && value.sdk_version === SDK_VERSION && typeof value.controller_id === 'string' && /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/.test(value.controller_id) && value.execution === 'trusted-local-native' && value.observation === OBSERVATION_PROFILE && value.state_mode === 'stateless' && value.decision_hz === 60 && Number.isFinite(value.watchdog_seconds) && value.watchdog_seconds > 0;
}
function verifySource(source, expected = undefined) {
  if (!source || source.sdk_version !== SDK_VERSION || !['manual','policy'].includes(source.kind) || (source.kind === 'manual' ? source.controller_id !== null : typeof source.controller_id !== 'string' || !/^[A-Za-z][A-Za-z0-9_.-]{0,63}$/.test(source.controller_id))) throw new Error('动作来源记录或 SDK 不兼容。');
  if (expected && (source.kind !== expected.kind || source.sdk_version !== SDK_VERSION || (source.kind === 'policy' && source.controller_id !== expected.controller_id))) throw new Error('原生动作来源与本次选择不一致；该响应不显示。');
}
function verifySources(values, tick, expected = undefined) {
  if (values === null && tick === 0 && expected === undefined) return;
  if (!Array.isArray(values) || values.length !== 2 || tick === 0) throw new Error('两台机器人的动作来源记录不完整。');
  values.forEach((source, index) => { if (!finiteKeys(source, ['kind','sdk_version','controller_id'])) throw new Error('动作来源标签包含不支持的字段。'); verifySource(source, expected?.[index]); });
}
function verifyObservation(value, selected, tick) {
  if (!finiteKeys(value, ['sdk_version','profile','hz','tick','self_index','sensor_range_mm','self','arena','enemy','projectiles']) || value.sdk_version !== SDK_VERSION || value.profile !== OBSERVATION_PROFILE || value.hz !== 60 || value.tick !== tick || value.self_index !== selected || value.sensor_range_mm !== 6000) throw new Error('过滤观测的视角、帧号或 SDK 不一致。');
  const arena = value.arena, self = value.self;
  if (!finiteKeys(arena, ['width','height','radius','speed_per_tick','projectile_speed_per_tick','walls']) || arena.width !== 12000 || arena.height !== 7600 || arena.radius !== 280 || arena.speed_per_tick !== 55 || arena.projectile_speed_per_tick !== 260 || !Array.isArray(arena.walls)) throw new Error('公开静态场地协议不兼容。');
  for (const wall of arena.walls) if (!finiteKeys(wall, ['x1','y1','x2','y2']) || !integer(wall.x1,0,arena.width) || !integer(wall.x2,wall.x1+1,arena.width) || !integer(wall.y1,0,arena.height) || !integer(wall.y2,wall.y1+1,arena.height)) throw new Error('静态墙体格式无效。');
  const pose = robot => integer(robot.x,0,arena.width) && integer(robot.y,0,arena.height) && integer(robot.yaw,0,255);
  if (!finiteKeys(self, ['x','y','yaw','hp','ammo','cooldown']) || !pose(self) || !integer(self.hp,0,100) || !integer(self.ammo,0,120) || !integer(self.cooldown,0,10)) throw new Error('自己的机器人状态格式无效。');
  if (value.enemy !== null && (!finiteKeys(value.enemy,['x','y','yaw']) || !pose(value.enemy))) throw new Error('敌方观测包含不支持的字段或无效位置。');
  if (!Array.isArray(value.projectiles) || value.projectiles.some(bullet => !finiteKeys(bullet,['x','y','vx','vy']) || !integer(bullet.x,0,arena.width) || !integer(bullet.y,0,arena.height) || !integer(bullet.vx,-260,260) || !integer(bullet.vy,-260,260))) throw new Error('弹丸观测包含不支持的字段或无效位置。');
}
function verifyState(next, nextMeta, selected, expectedSources = undefined) {
  verifyMeta(nextMeta);
  const allowed = ['schema','profile','observation_profile','official','build','observer','tick','terminal','observation','last_sources','advanced','verified_frames'];
  if (!next || Object.keys(next).some(key => !allowed.includes(key)) || next.schema !== 1 || next.profile !== nextMeta.profile || next.observation_profile !== nextMeta.observation_profile || next.official !== false || next.build !== nextMeta.build || next.observer !== selected || !integer(next.tick,0,nextMeta.duration) || ![0,1].includes(next.terminal)) throw new Error('状态的视角、构建或过滤协议不一致；该响应不显示。');
  verifyObservation(next.observation, selected, next.tick);
  verifySources(next.last_sources, next.tick, expectedSources);
}
function commit(next, nextMeta = meta, selected = observer) {
  verifyState(next, nextMeta, selected);
  meta = nextMeta; state = next; observer = selected; synchronized = true; revision++;
  policyAvailable = !meta.policy_error && !!validPolicy(meta.policy);
  if (state.terminal) pauseAndRelease();
  renderMetadata(); renderState(); drawMap(); updateControls();
}
function blankView() {
  facts('self-state', [['状态','等待所选视角同步']]); facts('visible-state', [['状态','当前没有同步的观测']]);
  $('visibility-status').textContent = '等待观测'; $('tick-status').textContent = 'TICK —';
  drawMap(); updateControls();
}
async function reconnect() {
  if (busy) return;
  pauseAndRelease(); const token = ++generation; busy = 'connect'; synchronized = false; blankView();
  try {
    const nextMeta = await api('meta'); verifyMeta(nextMeta);
    const next = await api(`state?observer=${observer}`);
    if (token !== generation) return;
    commit(next, nextMeta);
    operation(policyAvailable ? '已连接原生过滤对战。当前仅显示所选机器人的观测；按“开始”推进。' : POLICY_ERROR, !policyAvailable);
  } catch (error) { if (token === generation) { synchronized = false; operation(`连接失败：${error.message}`, true); } }
  finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
function selectSource(index) {
  const selected = $('source-' + index).value;
  if (!ready() || busy || !['manual','policy'].includes(selected) || (selected === 'policy' && !policyAvailable)) { $('source-' + index).value = sources[index]; return; }
  if (selected === sources[index]) return;
  pauseAndRelease(); sources[index] = selected; revision++;
  operation(`已暂停。机器人 ${index} 的下一批动作改用${selected === 'manual' ? '人工输入' : 'C++ 策略'}，本局未重置。`); renderState(); updateControls(); drawMap();
}
async function selectObserver() {
  const selected = Number($('observer').value);
  if (!ready() || busy || ![0,1].includes(selected)) { $('observer').value = String(observer); return; }
  if (selected === observer) return;
  pauseAndRelease(); const token = ++generation, oldObserver = observer; observer = selected; busy = 'perspective'; synchronized = false; blankView();
  try { const next = await api(`state?observer=${selected}`); if (token !== generation) return; commit(next, meta, selected); operation(`已暂停并切换到机器人 ${selected} 的过滤观测。没有推进或重置本局。`); }
  catch (error) { if (token === generation) { observer = oldObserver; synchronized = false; operation(`切换视角失败，当前画面未同步：${error.message} 请重新连接。`, true); } }
  finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
function currentAction() {
  const held = code => keys.has(code) || [...pointers.values()].includes(code);
  const self = state.observation.self, arena = state.observation.arena;
  const yaw = self.yaw * Math.PI * 2 / 256;
  const target = aim || {x:Math.round(Math.max(0,Math.min(arena.width,self.x+Math.cos(yaw)*1000))), y:Math.round(Math.max(0,Math.min(arena.height,self.y+Math.sin(yaw)*1000)))};
  return {sdk_version:SDK_VERSION, dx:(Number(held('KeyD') || held('ArrowRight'))-Number(held('KeyA') || held('ArrowLeft')))*1000, dy:(Number(held('KeyW') || held('ArrowUp'))-Number(held('KeyS') || held('ArrowDown')))*1000, aim_x:target.x, aim_y:target.y, fire:Number(held('Space'))};
}
function captureControllers() {
  const action = currentAction();
  return sources.map((kind,index) => kind === 'policy' ? {kind, sdk_version:SDK_VERSION, controller_id:meta.policy.controller_id} : {kind, action:index === observer ? action : {sdk_version:SDK_VERSION,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}});
}
async function recoverUncertain(error, token) {
  pauseAndRelease(); synchronized = false; blankView();
  try {
    const next = await api(`state?observer=${observer}`);
    if (token !== generation) return;
    commit(next);
    operation(`请求结果无法确认：${error.message} 已重新读取当前 tick ${state.tick}，保持暂停；原操作没有自动重试。`, true);
  } catch (recoveryError) {
    if (token === generation) { synchronized = false; blankView(); operation(`请求结果无法确认：${error.message} 重新读取状态也失败：${recoveryError.message} 请重新连接；原操作没有自动重试。`, true); }
  }
}
async function advance(count) {
  if (!canAdvance() || busy || !integer(count,1)) return;
  const requestedCount = Math.min(count,MAX_STEP_BATCH,meta.duration-state.tick), token = generation, capturedRevision = revision, selected = observer, start = state.tick;
  const controllers = captureControllers();
  // Capture the held action BEFORE releasing a paused single-step's keys and pointer.
  if (!playing) releaseInputs();
  busy = 'step'; updateControls();
  try {
    const next = await api('step',{count:requestedCount,observer:selected,controllers});
    if (token !== generation || capturedRevision !== revision) return;
    if (selected !== observer || controllers.some((item,i) => item.kind !== sources[i])) throw new Error('请求期间视角或控制源发生变化。');
    verifyState(next,meta,selected,controllers);
    if (!integer(next.advanced,1,requestedCount) || next.tick !== start+next.advanced || (next.advanced !== requestedCount && next.terminal !== 1)) throw new Error('原生响应帧号与请求不一致，可能有其他页面修改了本局。');
    commit(next);
    if (!playing) operation(state.terminal ? `本局已在 tick ${state.tick} 终止。保持过滤视角，可导出开发者动作回放。` : `已推进至 tick ${state.tick}。每台机器人各自使用所选控制源；浏览器未计算物理或策略。`);
  } catch (error) {
    if (token !== generation) return;
    pauseAndRelease();
    if (error.uncertain || !error.rejected) await recoverUncertain(error,token);
    else operation(`原生步进被拒绝，已暂停；实验错误不判负：${error.message} 当前已显示状态保留。`,true);
  } finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
async function resetRun() {
  if (!ready() || busy) return;
  pauseAndRelease(); const token = ++generation, selected = observer; busy = 'reset'; updateControls();
  try {
    const next = await api('reset',{observer:selected});
    if (token !== generation) return;
    verifyState(next,meta,selected);
    if (next.tick !== 0 || next.terminal !== 0 || next.last_sources !== null) throw new Error('重置响应不是新的初始状态。');
    commit(next); $('replay-status').textContent = '已创建新局。尚未导入回放。'; operation('已重置独立过滤对战，保留两台控制源与当前视角。其他实验模式不受影响。');
  } catch (error) { if (token === generation) { if (error.uncertain || !error.rejected) await recoverUncertain(error,token); else operation(`重置被拒绝，当前局保留并已暂停：${error.message}`,true); } }
  finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
function openPicker() {
  if (!ready() || busy) return;
  pauseAndRelease(); replayPicker = {generation,revision,observer}; $('replay-file').click();
}
function requireSize(text, size = 0) { if (size > MAX_FILE_BYTES || encoder.encode(text).length > MAX_FILE_BYTES) throw new Error('回放 JSON 超过 12 MiB UTF-8 限制。'); }
async function importFile(event) {
  const file = event.target.files?.[0], snapshot = replayPicker; replayPicker = null; event.target.value = '';
  if (!file) return;
  if (!ready() || busy || (snapshot && (snapshot.generation !== generation || snapshot.revision !== revision || snapshot.observer !== observer))) { operation('文件选择期间状态、视角或控制源已变化，未导入。请重新选择文件。',true); return; }
  pauseAndRelease(); const token = ++generation, selected = observer; busy = 'import'; updateControls();
  let requested = false;
  try {
    if (file.size > MAX_FILE_BYTES) throw new Error('回放文件超过 12 MiB 限制。');
    $('replay-status').textContent = '正在读取开发者回放，随后由本机服务从零重算验证…';
    const text = await file.text(); if (token !== generation) return; requireSize(text,file.size);
    // Preserve original replay bytes, including duplicate keys, for strict server validation.
    const body = `{"observer":${selected},"replay":${text}}`; requireSize(body);
    requested = true; const next = await api('import',body,true);
    if (token !== generation) return;
    verifyState(next,meta,selected);
    if (next.verified_frames !== next.tick) throw new Error('原生导入校验帧数与状态不一致。');
    commit(next);
    $('replay-status').textContent = `已校验 ${state.tick} 帧并导入 ${file.name || '回放 JSON'}。按记录动作重算，没有重新执行策略；保持暂停。`;
    operation(`回放导入通过，停在 tick ${state.tick}。继续时使用上方当前选择的控制源，来源记录只是未经认证的标签。`);
  } catch (error) {
    if (token !== generation) return;
    if (requested && (error.uncertain || !error.rejected)) { await recoverUncertain(error,token); $('replay-status').textContent = '导入响应无法确认，不能声称此次文件已通过验证；已尝试重新读取当前局。'; }
    else { operation(`导入未应用，当前局保留并已暂停：${error.message}`,true); $('replay-status').textContent = `校验失败，当前局保留：${error.message}`; }
  } finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
function verifyReplay(replay, tick, lastSources) {
  if (!replay || replay.schema !== 'rmuc-filtered-combat-replay.v1' || replay.profile !== PROFILE || replay.engine !== 'integer-mm-combat-v1' || replay.build !== meta.build || replay.official !== false || replay.observation_profile !== OBSERVATION_PROFILE || !integer(replay.initial_hash,0,0xffffffff) || !integer(replay.final_hash,0,0xffffffff) || !Array.isArray(replay.frames) || replay.frames.length !== tick) throw new Error('回放构建或帧数与当前视图不一致，请重新连接后导出。');
  for (let index = 0; index < replay.frames.length; index++) {
    const frame = replay.frames[index];
    if (!frame || frame.tick !== index || !integer(frame.hash,0,0xffffffff) || !Array.isArray(frame.observation_hashes) || frame.observation_hashes.length !== 2 || frame.observation_hashes.some(hash => !integer(hash,0,0xffffffff)) || !Array.isArray(frame.inputs) || frame.inputs.length !== 2) throw new Error('开发者回放帧格式不兼容。');
    frame.inputs.forEach(input => { verifySource(input); if (!Array.isArray(input.action) || input.action.length !== 5 || !integer(input.action[0],-1000,1000) || !integer(input.action[1],-1000,1000) || !integer(input.action[2],0,12000) || !integer(input.action[3],0,7600) || !integer(input.action[4],0,1)) throw new Error('回放中的已解析动作无效。'); });
  }
  const finalFrame = replay.frames.at(-1);
  if ((tick === 0 && replay.final_hash !== replay.initial_hash) || (finalFrame && (replay.final_hash !== finalFrame.hash || finalFrame.inputs.some((input,index) => ['kind','sdk_version','controller_id'].some(key => input[key] !== lastSources?.[index]?.[key]))))) throw new Error('回放末帧或来源记录与当前视图不一致，请重新连接。');
}
async function exportReplay() {
  if (!ready() || busy) return;
  pauseAndRelease(); const token = ++generation, tick = state.tick, lastSources = state.last_sources; busy = 'export'; updateControls();
  try {
    const replay = await api('replay'); if (token !== generation) return;
    try { verifyReplay(replay,tick,lastSources); } catch (error) { synchronized = false; blankView(); throw error; }
    const text = JSON.stringify(replay); requireSize(text);
    const blob = new Blob([text],{type:'application/json'}), url = URL.createObjectURL(blob), anchor = node('a');
    anchor.href = url; anchor.download = `tactics-filtered-combat-developer-tick-${replay.frames.length}.json`; document.body.append(anchor); anchor.click(); anchor.remove(); setTimeout(() => URL.revokeObjectURL(url),1000);
    operation(`已请求下载开发者回放（${replay.frames.length} 帧，包含双方已解析动作）。导入不重新执行策略。请自行确认浏览器下载；本页不验证实际落盘。`);
    $('replay-status').textContent = '已核对导出的构建、帧数和来源格式。普通视图不含全局哈希，不能据此证明它与先前视图是同一个完整状态。';
  } catch (error) { if (token === generation) operation(`开发者回放导出失败：${error.message}`,true); }
  finally { if (token === generation) { busy = ''; updateControls(); } }
}
function sourceLabel(source) { return source.kind === 'manual' ? '人工' : `C++ · ${source.controller_id}`; }
function renderMetadata() {
  for (let i = 0; i < 2; i++) { const manual = node('option','人工 · 当前视角用键盘 / 鼠标'); manual.value = 'manual'; const policy = node('option',policyAvailable ? `C++ · ${meta.policy.controller_id}` : 'C++ 策略暂不可用'); policy.value = 'policy'; policy.disabled = !policyAvailable; $('source-'+i).replaceChildren(manual,policy); $('source-'+i).value = sources[i]; }
  facts('policy-metadata', policyAvailable ? [['策略 ID',meta.policy.controller_id],['SDK / 决策频率',`v${meta.policy.sdk_version} / ${meta.policy.decision_hz} Hz`],['过滤配置',meta.policy.observation],['执行方式',meta.policy.execution],['策略状态',meta.policy.state_mode],['进程响应上限',`${meta.policy.watchdog_seconds} s（不是安全沙箱）`]] : [['策略状态',POLICY_ERROR]]);
  facts('mode-metadata',[['模式',meta.profile],['观测协议',meta.observation_profile],['源码构建',meta.build],['官方状态','非官方工程实验'],['运行上限',`${meta.duration} 帧 / ${meta.duration/meta.hz} 秒`],['步进上限',`每次 ${meta.max_batch} 帧`],['物理来源','src/sim.cpp；非官方物理、实验整数毫米模型'],...(meta.limitations || []).filter(value => typeof value === 'string').map(value => ['实验限制',value])]);
}
function renderState() {
  const observation = state.observation, self = observation.self, enemy = observation.enemy;
  $('map-heading').textContent = `机器人 ${observer} 的过滤视角`; $('self-label').textContent = `机器人 ${observer}`;
  $('tick-status').textContent = `TICK ${state.tick} · ${(state.tick/meta.hz).toFixed(3)} s`;
  facts('self-state',[['位置 x / y',`${number(self.x)} / ${number(self.y)} mm`],['朝向 yaw',`${self.yaw} / 256 圈`],['自己的 HP',String(self.hp)],['自己的弹量',String(self.ammo)],['自己的冷却',`${self.cooldown} 帧`],['下批控制源',sources[observer] === 'manual' ? '人工' : 'C++ 策略']]);
  const visible = enemy ? [['敌方观测','本帧可见'],['敌方 x / y',`${number(enemy.x)} / ${number(enemy.y)} mm`],['敌方 yaw',`${enemy.yaw} / 256 圈`]] : [['敌方观测','本帧未观测到'],['原因范围','超出范围或被墙体遮挡，不推测具体原因']];
  facts('visible-state',[...visible,['可见弹丸',String(observation.projectiles.length)],['传感范围','6000 mm + 原生遮挡判定']]);
  $('visibility-status').textContent = enemy ? '敌方本帧可见' : '敌方本帧不可见';
  $('source-status').textContent = (state.last_sources === null ? '尚无已提交动作。' : `末帧来源记录：0 → ${sourceLabel(state.last_sources[0])}；1 → ${sourceLabel(state.last_sources[1])}。这些是未经认证的记录标签。`) + (!policyAvailable ? ` ${POLICY_ERROR}` : '');
  $('view-status').textContent = `仅绘制机器人 ${observer} 的本帧过滤观测。虚线圆仅表示 6000 mm 最大范围，不表示墙后可见。静态墙体公开；不保留敌方历史位置。${state.terminal ? ' 本局已终止；不公开敌方 HP 或全局统计。' : ''}`;

}
function screen(x,y) { return [mapTransform.left+x*mapTransform.scale,mapTransform.bottom-y*mapTransform.scale]; }
function drawRobot(robot, own) {
  const position = screen(robot.x,robot.y), radius = state.observation.arena.radius*mapTransform.scale, angle = robot.yaw*Math.PI*2/256;
  ctx.beginPath(); ctx.arc(position[0],position[1],radius,0,Math.PI*2); ctx.fillStyle = own ? '#bfe98c28' : '#ff968328'; ctx.fill(); ctx.strokeStyle = own ? '#bfe98c' : '#ff9683'; ctx.lineWidth = 2.5; ctx.stroke();
  ctx.beginPath(); ctx.moveTo(...position); ctx.lineTo(position[0]+Math.cos(angle)*radius*1.6,position[1]-Math.sin(angle)*radius*1.6); ctx.stroke();
  ctx.fillStyle = own ? '#d8f5b5' : '#ffc5b9'; ctx.font = '12px ui-monospace, monospace'; ctx.textAlign = 'center'; ctx.fillText(own ? `SELF ${observer}` : 'VISIBLE',position[0],position[1]-radius-12);
}
function drawMap() {
  ctx.clearRect(0,0,canvas.width,canvas.height); ctx.fillStyle = '#09131d'; ctx.fillRect(0,0,canvas.width,canvas.height); mapTransform = null;
  if (!ready()) return;
  const observation = state.observation, arena = observation.arena;
  const scale = Math.min((canvas.width-90)/arena.width,(canvas.height-90)/arena.height), left = (canvas.width-arena.width*scale)/2, bottom = (canvas.height+arena.height*scale)/2;
  mapTransform = {scale,left,bottom};
  ctx.lineWidth = 1; ctx.strokeStyle = '#1c2d39';
  for (let x = 0; x <= arena.width; x += 1000) { ctx.beginPath(); ctx.moveTo(...screen(x,0)); ctx.lineTo(...screen(x,arena.height)); ctx.stroke(); }
  for (let y = 0; y <= arena.height; y += 1000) { ctx.beginPath(); ctx.moveTo(...screen(0,y)); ctx.lineTo(...screen(arena.width,y)); ctx.stroke(); }
  ctx.strokeStyle = '#607a8c'; ctx.lineWidth = 2; ctx.strokeRect(left,bottom-arena.height*scale,arena.width*scale,arena.height*scale);
  const own = screen(observation.self.x,observation.self.y);
  ctx.save(); ctx.beginPath(); ctx.rect(left,bottom-arena.height*scale,arena.width*scale,arena.height*scale); ctx.clip();
  ctx.beginPath(); ctx.arc(...own,observation.sensor_range_mm*scale,0,Math.PI*2); ctx.strokeStyle = '#78924f66'; ctx.lineWidth = 1.5; ctx.setLineDash([6,8]); ctx.stroke(); ctx.setLineDash([]);
  for (const wall of arena.walls) { const p = screen(wall.x1,wall.y2); ctx.fillStyle = '#293c49'; ctx.fillRect(...p,(wall.x2-wall.x1)*scale,(wall.y2-wall.y1)*scale); ctx.strokeStyle = '#7690a2'; ctx.lineWidth = 2; ctx.strokeRect(...p,(wall.x2-wall.x1)*scale,(wall.y2-wall.y1)*scale); }
  for (const bullet of observation.projectiles) { const p = screen(bullet.x,bullet.y), previous = screen(bullet.x-bullet.vx/2,bullet.y-bullet.vy/2); ctx.beginPath(); ctx.moveTo(...previous); ctx.lineTo(...p); ctx.strokeStyle = '#eddca1'; ctx.lineWidth = 3; ctx.stroke(); }
  // Never use replay data, a prior observation or another observer to draw opponents.
  if (observation.enemy !== null) drawRobot(observation.enemy,false);
  drawRobot(observation.self,true);
  if (selectedManual() && aim) { const p = screen(aim.x,aim.y); ctx.beginPath(); ctx.moveTo(p[0]-7,p[1]); ctx.lineTo(p[0]+7,p[1]); ctx.moveTo(p[0],p[1]-7); ctx.lineTo(p[0],p[1]+7); ctx.strokeStyle = '#bfe98c'; ctx.lineWidth = 1; ctx.stroke(); }
  ctx.restore(); ctx.fillStyle = '#8ca8ba'; ctx.font = '11px ui-monospace, monospace'; ctx.textAlign = 'left'; ctx.fillText(`OBSERVER ${observer} · FILTERED ONLY     ↑ +y     → +x    [mm]`,20,25);
}
function loop(time) {
  if (playing && canAdvance()) { if (lastTime !== null) accumulator += Math.max(0,(time-lastTime)/1000)*meta.hz; lastTime = time; if (!busy && accumulator >= 1) { const count = Math.min(MAX_STEP_BATCH,Math.floor(accumulator),meta.duration-state.tick); accumulator -= count; void advance(count); } }
  else lastTime = null;
  requestAnimationFrame(loop);
}
function typingTarget(target) { return target && (['INPUT','TEXTAREA','SELECT'].includes(target.tagName) || target.isContentEditable); }
const codes = ['KeyW','KeyA','KeyS','KeyD','ArrowUp','ArrowLeft','ArrowDown','ArrowRight','Space'];
window.addEventListener('keydown',event => { if (!codes.includes(event.code) || typingTarget(event.target) || !running() || !selectedManual() || (busy && busy !== 'step') || (event.code === 'Space' && event.target?.tagName === 'BUTTON')) return; if (event.repeat && !keys.has(event.code)) return; event.preventDefault(); keys.add(event.code); });
window.addEventListener('keyup',event => { keys.delete(event.code); });
window.addEventListener('blur',pauseAndRelease);
document.addEventListener('visibilitychange',() => { if (document.hidden) pauseAndRelease(); });
for (const [id,code] of [['drive-up','KeyW'],['drive-left','KeyA'],['drive-down','KeyS'],['drive-right','KeyD'],['fire','Space']]) {
  const button = $(id);
  button.onpointerdown = event => { if (!running() || !selectedManual() || (busy && busy !== 'step')) return; event.preventDefault(); pointers.set(event.pointerId,code); button.className = 'held'; button.setPointerCapture?.(event.pointerId); };
  const release = event => { pointers.delete(event.pointerId); if (![...pointers.values()].includes(code)) button.className = ''; };
  button.onpointerup = release; button.onpointercancel = release; button.onlostpointercapture = release;
}
function setAim(event) {
  if (!running() || !selectedManual() || !mapTransform || (busy && busy !== 'step')) return false;
  const rectangle = canvas.getBoundingClientRect(), x = (event.clientX-rectangle.left)*canvas.width/rectangle.width, y = (event.clientY-rectangle.top)*canvas.height/rectangle.height, arena = state.observation.arena;
  aim = {x:Math.round(Math.max(0,Math.min(arena.width,(x-mapTransform.left)/mapTransform.scale))),y:Math.round(Math.max(0,Math.min(arena.height,(mapTransform.bottom-y)/mapTransform.scale)))};
  drawMap(); return true;
}
canvas.onpointermove = setAim;
canvas.onpointerdown = event => { if (event.button !== 0 || !setAim(event)) return; event.preventDefault(); pointers.set(event.pointerId,'Space'); canvas.setPointerCapture?.(event.pointerId); };
for (const name of ['onpointerup','onpointercancel','onlostpointercapture']) canvas[name] = event => { pointers.delete(event.pointerId); };
$('source-0').onchange = () => selectSource(0); $('source-1').onchange = () => selectSource(1); $('observer').onchange = selectObserver;
$('play').onclick = () => { if (playing) { pauseAndRelease(); drawMap(); } else if (!busy) setPlaying(true); };
$('step').onclick = () => { if (!playing) return advance(1); };
$('reset').onclick = resetRun; $('reconnect').onclick = reconnect;
$('import-replay').onclick = openPicker; $('replay-file').onchange = importFile; $('replay-file').oncancel = () => { replayPicker = null; };
$('export-replay').onclick = exportReplay;
updateControls(); drawMap(); void reconnect(); requestAnimationFrame(loop);
