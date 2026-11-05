'use strict';
// Input/rendering adapter only. The native engine owns filtering, actions and physics.
// The normal state endpoint returns ONE selected observation, never a global view.
const $ = id => document.getElementById(id);
const profileHook = (method,...args) => globalThis.rosterProfileIntegration?.[method]?.(...args);
const canvas = $('roster-map'), ctx = canvas.getContext('2d');
const PROFILE = 'experimental-roster-tactical-v1', OBSERVATION_PROFILE = 'experimental-roster-range-los-public-v1', ENGINE = 'integer-mm-roster-v1';
const SDK_VERSION = 1, MAX_STEP_BATCH = 600, MAX_FILE_BYTES = 96 * 1024 * 1024, MAX_BACKLOG = 1200, MAX_CONFIG_BYTES = 64 * 1024;
const encoder = new TextEncoder(), keys = new Set(), pointers = new Map();
let meta = null, state = null, synchronized = false, busy = '', generation = 0, revision = 0;
let observer = 'red-1', sources = new Map(), sourceControls = new Map(), policyAvailable = false;
let speed = 1, configSelected = false, stagedConfig = ''; 
let playing = false, accumulator = 0, lastTime = null, aim = null, mapTransform = null, replayPicker = null;
const POLICY_ERROR = 'C++ 策略元数据不可用或不兼容。请检查本机源码并重建；仍可明确选择所有机器人人工控制。不会自动回退或判负。';
const ready = () => synchronized && !!meta && !!state;
const running = () => ready() && state.terminal === 0;
const selectedManual = () => sources.get(observer) === 'manual';
const canDrive = () => running() && selectedManual() && state.observation.self.hp > 0;
const canAdvance = () => running() && (policyAvailable || [...sources.values()].every(kind => kind === 'manual'));
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
  profileHook('update');
  $('play').textContent = playing ? 'Ⅱ 暂停' : '▶ 开始';
  $('play').disabled = !canAdvance() || (!!busy && !(busy === 'step' && playing));
  for (const id of ['step','advance-1s','advance-10s']) $(id).disabled = !canAdvance() || !!busy || playing;
  for (const id of ['speed','reset','export-replay','import-replay','observer','config-json','config-default','config-current']) $(id).disabled = !ready() || !!busy;
  $('speed').value = String(speed); $('observer').value = observer;
  for (const [id,select] of sourceControls) { select.disabled = !ready() || !!busy; select.value = sources.get(id); }
  for (const id of ['drive-up','drive-left','drive-down','drive-right','fire']) $(id).disabled = !canDrive() || (!!busy && busy !== 'step');
  $('fire').textContent = ready() && state.observation.self_heat.firing_locked ? '热锁 · 等待' : '发射';
  $('fire').setAttribute('aria-label', '按住请求发射；原生热锁会阻止出弹，临时锁冷却后可恢复');
  $('reconnect').hidden = synchronized || !!busy; $('reconnect').disabled = !!busy; $('map-veil').hidden = ready();
  const busyNames = {connect:'连接中',perspective:'读取所选视角',step:playing?'运行中':'步进中',reset:'重置中',import:'校验回放中',export:'准备开发者导出',profiles:'校验来源参数'};
  $('run-status').textContent = busy ? busyNames[busy] : !ready() ? '未同步 · 已暂停' : state.terminal ? '本局已终止' : playing ? '运行中' : '已暂停';
  $('run-status').className = 'state-pill' + (playing ? ' active' : '');
  $('controller-status').textContent = !ready() ? '等待过滤观测' : !policyAvailable ? '策略不可用 · 可选人工' : `当前视角：${observer}`;
  $('controller-status').className = 'mini-tag' + (ready() && !policyAvailable ? ' error' : '');
  $('drive-title').textContent = selectedManual() ? `${observer} · WASD / 方向键` : `${observer} · C++ 策略`;
  canvas.setAttribute('aria-label', `${observer} 的过滤观测。${selectedManual() ? 'WASD 或方向键移动，鼠标瞄准，空格或左键发射。' : 'C++ 策略控制，人工输入停用。'}不显示隐藏机器人。`);
  $('drive-note').textContent = ready() && state.observation.self.hp === 0 ? '自己的机器人已失效，不能移动或发射。没有复活，比赛仍可推进至 420 秒。' : selectedManual() ? '人工输入只作用于当前视角。其他人工机器人零移动、不发射；暂停单步先捕获输入，再释放按键。' : '当前人工输入停用。原生 C++ 每 tick 根据这台机器人的过滤观测决策。';
}
async function api(path, data, raw = false) {
  const originalPath = path; path = profileHook('route',path) || path;
  [data,raw] = profileHook('request',data,raw,originalPath) || [data,raw];
  let response;
  try { response = await fetch('/api/roster/' + path, data === undefined ? {} : {method:'POST', headers:{'Content-Type':'application/json','X-Sandbox-Client':'1'}, body:raw ? data : JSON.stringify(data)}); }
  catch (cause) { const error = new Error(`无法连接本机服务：${cause.message}`); error.uncertain = data !== undefined; throw error; }
  let value;
  try { value = await response.json(); }
  catch (_) { const error = new Error(`本机服务返回非 JSON 响应（HTTP ${response.status}）。`); error.uncertain = data !== undefined; throw error; }
  if (!response.ok) { const error = new Error(typeof value.error === 'string' ? value.error : `请求失败（HTTP ${response.status}）。`); error.rejected = response.status < 500; error.uncertain = (response.status >= 500 || value.resync_required === true) && data !== undefined; throw error; }
  return profileHook('unwrap',value,originalPath) || value;
}
const ID = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,31}$/, CONTROLLER_ID = /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/;
const BOUNDARIES = ['strict-reject-unadjudicated-v1','ENGINEERING-NOT-OFFICIAL-timer-before-input-v1'];
const CREDITS = ['disabled','EXPERIMENTAL-end-of-second-v1'];
const DAMAGE_STRATEGIES = ['missing-adjudicated-attack-damage','EXPERIMENTAL-reconciled-projectile-hp-loss-v1'];
const HEAT_STRATEGIES = ['disabled','SYNTHETIC-per-robot-17mm-v1'];
const HEAT_THRESHOLDS = ['strict-reject-unadjudicated','ENGINEERING-prose-greater-temporary-at-equality-v1','ENGINEERING-flowchart-greater-equal-v1'];
const ZONES = [
  {id:'red-outpost',label:'红方前哨',rect:[4300,700,5900,2300],owner:0},
  {id:'blue-outpost',label:'蓝方前哨',rect:[6100,5300,7700,6900],owner:1},
  {id:'red-base',label:'红方基地',rect:[600,5600,2200,7200],owner:0},
  {id:'blue-base',label:'蓝方基地',rect:[9800,400,11400,2000],owner:1}
];
const ARENA={width:12000,height:7600,walls:[{x1:5250,y1:2800,x2:6750,y2:4800},{x1:2600,y1:1350,x2:3800,y2:1950},{x1:8200,y1:5650,x2:9400,y2:6250}]};
// Defense-in-depth checks of public geometry, not an authoritative simulation.
function lineBlocked(a,b) {
  return ARENA.walls.some(wall=>{
    let lo=0,hi=65536;
    for(const axis of ['x','y']) {
      const start=a[axis],delta=b[axis]-start,lower=wall[axis+'1'],upper=wall[axis+'2'];
      if(delta===0){if(start<lower||start>upper)return false;continue;}
      const q1=Math.trunc((lower-start)*65536/delta),q2=Math.trunc((upper-start)*65536/delta);
      lo=Math.max(lo,Math.min(q1,q2));hi=Math.min(hi,Math.max(q1,q2));if(lo>hi)return false;
    }
    return true;
  });
}
function clearPose(pose,profile) {
  const radius=profile.radius_mm;
  return integer(pose.x,radius,ARENA.width-radius)&&integer(pose.y,radius,ARENA.height-radius)&&!ARENA.walls.some(w=>{const dx=pose.x-Math.max(w.x1,Math.min(w.x2,pose.x)),dy=pose.y-Math.max(w.y1,Math.min(w.y2,pose.y));return dx*dx+dy*dy<radius*radius;});
}
const PROFILE_BOUNDS = {max_hp:[1,10000],ammo:[0,5000],radius_mm:[100,500],speed_mm_tick:[0,100],projectile_speed_mm_tick:[80,1000],projectile_damage:[1,1000],cooldown_ticks:[1,120],sensor_range_mm:[0,20000],heat_limit_tenths:[0,20000],cooling_per_second:[0,1000]};
const equal = (a,b) => JSON.stringify(stable(a)) === JSON.stringify(stable(b));
function stable(value) { return Array.isArray(value) ? value.map(stable) : value && typeof value === 'object' ? Object.fromEntries(Object.keys(value).sort().map(key => [key,stable(value[key])])) : value; }
const canonical = robots => robots.every((robot,index) => !index || robots[index-1].id < robot.id);
function verifyConfig(config, requireCanonical = true) {
  if (!finiteKeys(config,['boundary_policy','credit_strategy','attack_damage_strategy','heat_strategy','heat_threshold_policy','robots']) || !BOUNDARIES.includes(config.boundary_policy) || !CREDITS.includes(config.credit_strategy) || !DAMAGE_STRATEGIES.includes(config.attack_damage_strategy) || !HEAT_STRATEGIES.includes(config.heat_strategy) || !HEAT_THRESHOLDS.includes(config.heat_threshold_policy) || !Array.isArray(config.robots) || !integer(config.robots.length,2,8)) throw new Error('编队配置字段或实验策略不兼容。');
  const ids = new Set(), teams = [0,0], profiles = new Map();
  for (const robot of config.robots) {
    if (!finiteKeys(robot,['id','team','profile','spawn','eligible']) || typeof robot.id !== 'string' || !ID.test(robot.id) || ids.has(robot.id) || !integer(robot.team,0,1) || typeof robot.eligible !== 'boolean') throw new Error('机器人 ID 必须唯一，队伍和资格必须有效。');
    ids.add(robot.id); teams[robot.team]++;
    const p=robot.profile, spawn=robot.spawn;
    if (!finiteKeys(p,['id',...Object.keys(PROFILE_BOUNDS)]) || typeof p.id !== 'string' || !ID.test(p.id) || !p.id.startsWith('SYNTHETIC-') || Object.entries(PROFILE_BOUNDS).some(([key,[lo,hi]]) => !integer(p[key],lo,hi))) throw new Error('物理、武器和热量参数必须是有界 SYNTHETIC 配置。');
    if (profiles.has(p.id) && !equal(profiles.get(p.id),p)) throw new Error('同名 SYNTHETIC 参数配置不一致。');
    profiles.set(p.id,p);
    if (!finiteKeys(spawn,['x','y','yaw']) || !integer(spawn.x,p.radius_mm,12000-p.radius_mm) || !integer(spawn.y,p.radius_mm,7600-p.radius_mm) || !integer(spawn.yaw,0,255) || !clearPose(spawn,p)) throw new Error('出生位置或朝向无效。');
  }
  for(let i=0;i<config.robots.length;i++)for(let j=0;j<i;j++){const a=config.robots[i],b=config.robots[j],dx=a.spawn.x-b.spawn.x,dy=a.spawn.y-b.spawn.y,radius=a.profile.radius_mm+b.profile.radius_mm;if(dx*dx+dy*dy<radius*radius)throw new Error('机器人出生圆不能重叠。');}
  if (teams.some(count => !integer(count,1,4)) || (requireCanonical && !canonical(config.robots))) throw new Error('每队需要 1–4 台机器人，响应必须按稳定 ID 排序。');
}
function verifyMeta(value) {
  const fields=['schema','profile','observation_profile','observation_version','official','build','engine','hz','duration','max_batch','max_robots','max_team_robots','default_config','boundary_policies','credit_strategies','attack_damage_strategies','heat_strategies','heat_threshold_policies','policy','policy_error','limitations'];
  if (!finiteKeys(value,fields) || value.schema!==1 || value.profile!==PROFILE || value.observation_profile!==OBSERVATION_PROFILE || value.observation_version!==1 || value.official!==false || typeof value.build!=='string' || !/^[a-f0-9]{64}$/.test(value.build) || value.engine!==ENGINE || value.hz!==60 || value.duration!==25200 || value.max_batch!==MAX_STEP_BATCH || value.max_robots!==8 || value.max_team_robots!==4 || !Array.isArray(value.limitations) || value.limitations.some(line => typeof line!=='string') || !(value.policy_error===null || typeof value.policy_error==='string')) throw new Error('本机服务不是匹配的多机实验协议或构建。');
  for (const [name,expected] of [['boundary_policies',BOUNDARIES],['credit_strategies',CREDITS],['attack_damage_strategies',DAMAGE_STRATEGIES],['heat_strategies',HEAT_STRATEGIES],['heat_threshold_policies',HEAT_THRESHOLDS]]) if (!equal(value[name],expected)) throw new Error('服务实验策略集合不匹配。');
  verifyConfig(value.default_config);
}
function validPolicy(value) {
  return finiteKeys(value,['sdk_version','controller_id','execution','observation','state_mode','decision_hz','watchdog_seconds']) && value.sdk_version===SDK_VERSION && typeof value.controller_id==='string' && CONTROLLER_ID.test(value.controller_id) && value.execution==='trusted-local-native' && value.observation===OBSERVATION_PROFILE && value.state_mode==='stateless' && value.decision_hz===60 && Number.isFinite(value.watchdog_seconds) && value.watchdog_seconds>0;
}
function verifySource(source, expected) {
  if (!source || typeof source.id!=='string' || !ID.test(source.id) || source.sdk_version!==SDK_VERSION || !['manual','policy'].includes(source.kind) || (source.kind==='manual' ? source.controller_id!==null : typeof source.controller_id!=='string' || !CONTROLLER_ID.test(source.controller_id))) throw new Error('按 ID 绑定的动作来源标签无效。');
  if (expected && (source.id!==expected.id || source.kind!==expected.kind || (source.kind==='policy' && source.controller_id!==expected.controller_id))) throw new Error('动作来源与本次 ID / 控制源选择不一致。');
}
function verifySources(values,tick,config,expected) {
  if (values===null && tick===0 && expected===undefined) return;
  if (!Array.isArray(values) || values.length!==config.robots.length || tick===0) throw new Error('每台机器人的动作来源必须完整。');
  values.forEach((source,index) => { if (!finiteKeys(source,['id','kind','sdk_version','controller_id']) || source.id!==config.robots[index].id) throw new Error('动作来源 ID 不完整或顺序不规范。'); verifySource(source,expected?.find(item=>item.id===source.id)); });
}
function verifySelfHeat(heat,config,profile) {
  if (!finiteKeys(heat,['enabled','heat_tenths','limit_tenths','cooling_per_second','overheat_locked','permanent_latched','firing_locked']) || typeof heat.enabled!=='boolean' || heat.enabled!==(config.heat_strategy===HEAT_STRATEGIES[1]) || !integer(heat.heat_tenths,0,profile.heat_limit_tenths+1100) || heat.limit_tenths!==profile.heat_limit_tenths || heat.cooling_per_second!==profile.cooling_per_second || ['overheat_locked','permanent_latched','firing_locked'].some(key=>typeof heat[key]!=='boolean')) throw new Error('自己的热量字段与 SYNTHETIC 参数不一致。');
  if (heat.firing_locked!==(heat.overheat_locked||heat.permanent_latched) || (heat.overheat_locked && heat.heat_tenths===0) || (heat.heat_tenths>heat.limit_tenths && !heat.firing_locked) || (!heat.enabled && (heat.heat_tenths!==0 || heat.firing_locked))) throw new Error('热量、热锁状态不一致。');
}
function verifyObservation(value,selected,tick,config) {
  const fields=['sdk_version','observation_version','profile','hz','tick','self_id','self_team','sensor_range_mm','self','arena','roster','contacts','projectiles','self_heat','zones','score','rate','qualified_ms','duration_ms','credits_enabled'];
  const own=config.robots.find(robot=>robot.id===selected);
  if (!own || !finiteKeys(value,fields) || value.sdk_version!==SDK_VERSION || value.observation_version!==1 || value.profile!==OBSERVATION_PROFILE || value.hz!==60 || value.tick!==tick || value.self_id!==selected || value.self_team!==own.team || value.sensor_range_mm!==own.profile.sensor_range_mm || !equal(value.roster,config.robots) || value.qualified_ms!==Math.ceil(tick*1000/60) || value.duration_ms!==420000 || value.credits_enabled!==(config.credit_strategy===CREDITS[1])) throw new Error('过滤观测视角、静态编队、帧号或实验配置不一致。');
  const arena=value.arena, self=value.self, pose=robot=>integer(robot.x,0,12000)&&integer(robot.y,0,7600)&&integer(robot.yaw,0,255);
  if (!equal(arena,ARENA)) throw new Error('公开场地格式不兼容。');
  for (const wall of arena.walls) if (!finiteKeys(wall,['x1','y1','x2','y2']) || !integer(wall.x1,0,12000) || !integer(wall.x2,wall.x1+1,12000) || !integer(wall.y1,0,7600) || !integer(wall.y2,wall.y1+1,7600)) throw new Error('公开墙体格式无效。');
  if (!finiteKeys(self,['x','y','yaw','hp','ammo','cooldown']) || !pose(self) || !clearPose(self,own.profile) || !integer(self.hp,0,own.profile.max_hp) || !integer(self.ammo,0,own.profile.ammo) || !integer(self.cooldown,0,own.profile.cooldown_ticks)) throw new Error('自身动态状态无效。');
  if (!Array.isArray(value.contacts) || value.contacts.length>config.robots.length-1 || !canonical(value.contacts)) throw new Error('可见机器人列表不规范。');
  for (const contact of value.contacts) {
    const known=config.robots.find(robot=>robot.id===contact.id);
    if (!finiteKeys(contact,['id','team','x','y','yaw']) || !known || contact.id===selected || contact.team!==known.team || !pose(contact) || !clearPose(contact,known.profile) || lineBlocked(self,contact) || (contact.x-self.x)**2+(contact.y-self.y)**2>value.sensor_range_mm**2) throw new Error('可见机器人包含未授权字段、身份或无效位置。');
  }
  if (!Array.isArray(value.projectiles) || value.projectiles.length>2048 || value.projectiles.some(p=>!finiteKeys(p,['x','y','vx','vy']) || !integer(p.x,0,12000) || !integer(p.y,0,7600) || !integer(p.vx,-1000,1000) || !integer(p.vy,-1000,1000) || lineBlocked(self,p) || (p.x-self.x)**2+(p.y-self.y)**2>value.sensor_range_mm**2)) throw new Error('可见弹丸仅允许位置与速度。');
  for(let i=1;i<value.projectiles.length;i++){const a=value.projectiles[i-1],b=value.projectiles[i];const first=['x','y','vx','vy'].find(key=>a[key]!==b[key]);if(first&&a[first]>b[first])throw new Error('弹丸列表必须只按公开运动学字段排序。');}
  verifySelfHeat(value.self_heat,config,own.profile);
  if (!Array.isArray(value.zones) || value.zones.length!==ZONES.length) throw new Error('目标区数量不兼容。');
  value.zones.forEach((zone,index)=> { const expected=ZONES[index]; if (!finiteKeys(zone,['id','rect','original_owner','controller','degree']) || zone.id!==expected.id || zone.original_owner!==expected.owner || !integer(zone.controller,0,1) || !['full','temporary'].includes(zone.degree) || (zone.degree==='full'&&zone.controller!==zone.original_owner) || !finiteKeys(zone.rect,['x1','y1','x2','y2']) || ['x1','y1','x2','y2'].some((key,i)=>zone.rect[key]!==expected.rect[i])) throw new Error('公开目标字段或几何不兼容。'); });
  if(tick===0){
    const initialSelf={...own.spawn,hp:own.profile.max_hp,ammo:own.profile.ammo,cooldown:0};
    const initialContacts=config.robots.filter(r=>r.id!==selected&&(r.spawn.x-self.x)**2+(r.spawn.y-self.y)**2<=value.sensor_range_mm**2&&!lineBlocked(self,r.spawn)).map(r=>({id:r.id,team:r.team,...r.spawn}));
    if(!equal(self,initialSelf)||!equal(value.contacts,initialContacts)||value.projectiles.length!==0||value.zones.some(z=>z.controller!==z.original_owner||z.degree!=='full')||value.self_heat.heat_tenths!==0||value.self_heat.overheat_locked||value.self_heat.permanent_latched||value.self_heat.firing_locked)throw new Error('tick 0 必须与配置的精确初始世界一致。');
  }
  const rates=[0,0]; for (const [a,b,points] of [[0,1,1],[2,3,3]]) if (value.zones[a].controller===value.zones[b].controller) rates[value.zones[a].controller]+=points;
  if (!equal(value.rate,rates) || (value.credits_enabled ? !Array.isArray(value.score)||value.score.length!==2||value.score.some(score=>!integer(score,0,4*Math.floor(tick/60))) : value.score!==null)) throw new Error('公开计分率或积分无效。');
}
function verifyResult(result,tick,config,score) {
  if (!finiteKeys(result,['outcome','reason'])) throw new Error('到时结果包含不支持的字段。');
  const is = (outcome,reason) => result.outcome === outcome && result.reason === reason;
  let valid;
  if (tick < 25200) valid = is('ongoing','not-ended');
  else if (config.credit_strategy === 'disabled') valid = is('unscored-timeout','credit-strategy-disabled');
  else if (score[0] !== score[1]) valid = is(score[0] > score[1] ? 'red-wins' : 'blue-wins','timeout-control');
  else if (config.attack_damage_strategy === DAMAGE_STRATEGIES[0]) valid = is('unresolved-timeout-tie','missing-adjudicated-attack-damage');
  else valid = (['red-wins','blue-wins'].includes(result.outcome) && ['timeout-attack-damage','timeout-surviving-robot-hp'].includes(result.reason)) || is('draw','timeout-equal') || is('unresolved-timeout-tie','missing-adjudicated-attack-damage') || is('unresolved-timeout-tie','missing-adjudicated-surviving-robot-hp');
  // Hidden damage and surviving-HP values are verified by the bridge, never sent here.
  if (!valid) throw new Error('到时结果与计时、公开积分或所选攻击裁决策略不一致。');
}
function syncConfigSelection() {
  if (!state) return;
  stagedConfig=JSON.stringify(state.config,null,2); $('config-json').value=stagedConfig; configSelected=true;
}
function selectConfig() {
  if (!ready() || busy) { $('config-json').value=stagedConfig; return; }
  pauseAndRelease(); stagedConfig=$('config-json').value; revision++; profileHook('draftChanged');
  operation('已暂停。JSON 草稿仅在点击“应用草稿并重置”后生效，当前局配置未改变。');
}
function stageConfig(value) {
  if (!ready() || busy) return;
  $('config-json').value=JSON.stringify(value,null,2); selectConfig();
}
function verifyState(next,nextMeta,selected,expectedSources,expectedConfig) {
  verifyMeta(nextMeta);
  const required=['schema','profile','observation_profile','observation_version','official','build','observer','tick','terminal','observation','last_sources','config','result'];
  if (!next || !required.every(key=>Object.prototype.hasOwnProperty.call(next,key)) || Object.keys(next).some(key=>![...required,'advanced','verified_frames'].includes(key)) || next.schema!==1 || next.profile!==PROFILE || next.observation_profile!==OBSERVATION_PROFILE || next.observation_version!==1 || next.official!==false || next.build!==nextMeta.build || typeof next.observer!=='string' || !ID.test(next.observer) || (selected!==undefined&&next.observer!==selected) || !integer(next.tick,0,nextMeta.duration) || next.terminal!==Number(next.tick===nextMeta.duration) || ('advanced' in next&&!integer(next.advanced,1,600)) || ('verified_frames' in next&&next.verified_frames!==next.tick)) throw new Error('状态的视角、构建或过滤字段不一致，该响应不显示。');
  verifyConfig(next.config);
  if (expectedConfig && !equal(next.config,expectedConfig)) throw new Error('响应配置与请求或当前局不一致。');
  verifyObservation(next.observation,next.observer,next.tick,next.config);
  verifyResult(next.result,next.tick,next.config,next.observation.score);
  verifySources(next.last_sources,next.tick,next.config,expectedSources);
}
function commit(next,nextMeta=meta,selected=observer) {
  verifyState(next,nextMeta,selected);
  profileHook('accept',next);
  const first=state===null;
  meta=nextMeta; state=next; observer=next.observer; synchronized=true; revision++;
  sources=new Map(state.config.robots.map(robot=>[robot.id,sources.get(robot.id)||(first&&robot.id!==observer?'policy':'manual')]));
  policyAvailable=!meta.policy_error && !!validPolicy(meta.policy);
  if (!configSelected) syncConfigSelection();
  if (state.terminal) pauseAndRelease();
  renderMetadata(); renderState(); drawMap(); updateControls();
}
function blankView() {
  profileHook('clearActive');
  facts('self-state', [['状态','等待所选视角同步']]); facts('visible-state', [['状态','当前没有同步的观测']]);
  $('objective-list').replaceChildren(node('p','等待同步的公开目标区状态','panel-note'));
  for (const id of ['score-red','score-blue']) $(id).textContent = '—';
  for (const id of ['rate-red','rate-blue']) $(id).textContent = '等待公开积分';
  $('preset-label').textContent = '等待同步'; $('applied-config').textContent = '等待同步的当前配置。';
  $('remaining-time').textContent = '—:—'; $('match-result').textContent = '等待同步'; $('credit-status').textContent = '不显示未同步状态';
  $('heat-status').textContent = '等待同步'; $('heat-value').textContent = '—'; $('heat-note').textContent = '只显示当前机器人的热量，不推测敌方热量。'; $('heat-fill').style.width = '0%';
  $('visibility-status').textContent = '等待观测'; $('tick-status').textContent = 'TICK —';
  drawMap(); updateControls();
}
async function readSelected(selected) {
  try { const next=await api(`state?observer=${encodeURIComponent(selected)}`); return {next,selected}; }
  catch (error) { if (!error.rejected) throw error; const next=await api('state'); return {next,selected:undefined}; }
}
async function reconnect() {
  if (busy) return;
  pauseAndRelease(); const token=++generation; busy='connect'; synchronized=false; blankView();
  try {
    const nextMeta=await api('meta'); verifyMeta(nextMeta);
    const {next,selected}=await readSelected(observer); if(token!==generation)return;
    verifyState(next,nextMeta,selected); commit(next,nextMeta,next.observer); syncConfigSelection();
    operation(policyAvailable?'已连接多机编队实验。只有当前视角的过滤观测可见；点击开始推进。':POLICY_ERROR,!policyAvailable);
  } catch(error) { if(token===generation){synchronized=false;operation(`连接失败：${error.message}`,true);} }
  finally { if(token===generation){busy='';updateControls();drawMap();} }
}
function selectSource(id) {
  const control=sourceControls.get(id); if(!control)return;
  const selected=control.value;
  if(!ready()||busy||!sources.has(id)||!['manual','policy'].includes(selected)||(selected==='policy'&&!policyAvailable)){control.value=sources.get(id);return;}
  if(selected===sources.get(id))return;
  pauseAndRelease();sources.set(id,selected);revision++;
  operation(`已暂停。${id} 的下一批动作改用${selected==='manual'?'人工输入':'C++ 策略'}；身份由 ID 绑定，本局未重置。`);renderState();updateControls();drawMap();
}
async function selectObserver() {
  const selected=$('observer').value;
  if(!ready()||busy||!sources.has(selected)){$('observer').value=observer;return;}
  if(selected===observer)return;
  pauseAndRelease();const token=++generation,old=observer,config=state.config;observer=selected;busy='perspective';synchronized=false;blankView();
  try { const next=await api(`state?observer=${encodeURIComponent(selected)}`);if(token!==generation)return;verifyState(next,meta,selected,undefined,config);commit(next,meta,selected);operation(`已暂停并切换至 ${selected} 的过滤视角，没有重置或推进。`); }
  catch(error){if(token===generation){observer=old;synchronized=false;operation(`视角切换失败：${error.message} 请重新连接。`,true);}}
  finally{if(token===generation){busy='';updateControls();drawMap();}}
}
function currentAction() {
  const held = code => canDrive() && (keys.has(code) || [...pointers.values()].includes(code));
  const self = state.observation.self, arena = state.observation.arena;
  const yaw = self.yaw * Math.PI * 2 / 256;
  const target = aim || {x:Math.round(Math.max(0,Math.min(arena.width,self.x+Math.cos(yaw)*1000))), y:Math.round(Math.max(0,Math.min(arena.height,self.y+Math.sin(yaw)*1000)))};
  return {sdk_version:SDK_VERSION, dx:(Number(held('KeyD') || held('ArrowRight'))-Number(held('KeyA') || held('ArrowLeft')))*1000, dy:(Number(held('KeyW') || held('ArrowUp'))-Number(held('KeyS') || held('ArrowDown')))*1000, aim_x:target.x, aim_y:target.y, fire:Number(held('Space'))};
}
function captureControllers() {
  const action=currentAction();
  return state.config.robots.map(({id})=>sources.get(id)==='policy'?{id,kind:'policy',sdk_version:SDK_VERSION,controller_id:meta.policy.controller_id}:{id,kind:'manual',action:id===observer?action:{sdk_version:SDK_VERSION,dx:0,dy:0,aim_x:0,aim_y:0,fire:0}});
}
async function recoverUncertain(error,token) {
  pauseAndRelease();synchronized=false;blankView();
  try { const {next,selected}=await readSelected(observer);if(token!==generation)return;verifyState(next,meta,selected);commit(next,meta,next.observer);syncConfigSelection();operation(`请求结果无法确认：${error.message} 已只读同步 tick ${state.tick}，保持暂停；原操作没有自动重试。`,true); }
  catch(recovery){if(token===generation){synchronized=false;blankView();operation(`请求结果无法确认：${error.message} 只读同步也失败：${recovery.message} 请重新连接；原操作没有自动重试。`,true);}}
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
    if (selected !== observer || controllers.some(item => item.kind !== sources.get(item.id))) throw new Error('请求期间视角或控制源发生变化。');
    verifyState(next,meta,selected,controllers,state.config);
    if (!integer(next.advanced,1,requestedCount) || next.tick !== start+next.advanced || (next.advanced !== requestedCount && next.terminal !== 1)) throw new Error('原生响应帧号与请求不一致，可能有其他页面修改了本局。');
    commit(next);
    if (!playing) operation(state.terminal ? `本局已在 tick ${state.tick} 终止。保持过滤视角，可导出开发者动作回放。` : `已推进至 tick ${state.tick}。所有机器人按稳定 ID 各自使用所选控制源；浏览器未计算物理或策略。`);
  } catch (error) {
    if (token !== generation) return;
    pauseAndRelease();
    if (error.uncertain || !error.rejected) await recoverUncertain(error,token);
    else operation(`原生步进被拒绝，已暂停；实验错误不判负：${error.message} 当前已显示状态保留。`,true);
  } finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
async function resetRun() {
  if(!ready()||busy)return;
  pauseAndRelease();let config;
  try{if(encoder.encode(stagedConfig).length>MAX_CONFIG_BYTES)throw Error('编队 JSON 草稿超过 64 KiB UTF-8 限制。');config=JSON.parse(stagedConfig);verifyConfig(config,false);config.robots.sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0);}catch(error){operation(`配置草稿无效，未发送重置：${error.message}`,true);return;}
  const token=++generation,selected=config.robots.some(r=>r.id===observer)?observer:config.robots[0].id;busy='reset';updateControls();
  try {const raw=`{"observer":${JSON.stringify(selected)},"config":${stagedConfig}}`;const next=await api('reset',raw,true);if(token!==generation)return;verifyState(next,meta,selected,undefined,config);if(next.tick!==0||next.last_sources!==null)throw Error('重置没有返回新的初始状态。');commit(next,meta,selected);syncConfigSelection();$('replay-status').textContent='已创建新局，尚未导入回放。';operation('已应用 JSON 草稿并重置。保留仍在编队中的 ID 控制源；新增 ID 默认为人工。其他模式不受影响。');}
  catch(error){if(token===generation){if(error.uncertain||!error.rejected)await recoverUncertain(error,token);else operation(`重置被拒绝，当前局保留：${error.message}`,true);}}
  finally{if(token===generation){busy='';updateControls();drawMap();}}
}
function openPicker() {
  if (!ready() || busy) return;
  pauseAndRelease(); replayPicker = {generation,revision,observer}; $('replay-file').click();
}
function requireSize(text, size = 0) { if (size > MAX_FILE_BYTES || encoder.encode(text).length > MAX_FILE_BYTES) throw new Error('回放 JSON 超过 96 MiB UTF-8 限制。'); }
async function importFile(event) {
  const file = event.target.files?.[0], snapshot = replayPicker; replayPicker = null; event.target.value = '';
  if (!file) return;
  if (!ready() || busy || (snapshot && (snapshot.generation !== generation || snapshot.revision !== revision || snapshot.observer !== observer))) { operation('文件选择期间状态、视角或控制源已变化，未导入。请重新选择文件。',true); return; }
  pauseAndRelease(); const token = ++generation; let selected = observer; busy = 'import'; updateControls();
  let requested = false;
  try {
    if (file.size > MAX_FILE_BYTES) throw new Error('回放文件超过 96 MiB 限制。');
    $('replay-status').textContent = '正在读取开发者回放，随后由本机服务从零重算验证…';
    const text = await file.text(); if (token !== generation) return; requireSize(text,file.size);
    // Preserve original replay bytes, including duplicate keys, for strict server validation.
    const parsed=JSON.parse(text); verifyConfig(parsed.config,false); const importedConfig=JSON.parse(JSON.stringify(parsed.config)); importedConfig.robots.sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0);
    selected=parsed.config.robots.some(robot=>robot.id===observer)?observer:[...parsed.config.robots].sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0)[0].id;
    const body = `{"observer":${JSON.stringify(selected)},"replay":${text}}`; requireSize(body);
    requested = true; const next = await api('import',body,true);
    if (token !== generation) return;
    verifyState(next,meta,selected,undefined,importedConfig);
    if (!Array.isArray(parsed.frames) || next.verified_frames !== next.tick || next.tick !== parsed.frames.length) throw new Error('原生导入校验帧数与状态不一致。');
    commit(next,meta,selected); syncConfigSelection();
    $('replay-status').textContent = `已校验 ${state.tick} 帧并导入 ${file.name || '回放 JSON'}。按记录动作重算，没有重新执行策略；保持暂停。`;
    operation(`回放导入通过，停在 tick ${state.tick}。继续时使用上方当前选择的控制源，来源记录只是未经认证的标签。`);
  } catch (error) {
    if (token !== generation) return;
    if (requested && (error.uncertain || !error.rejected)) { await recoverUncertain(error,token); $('replay-status').textContent = '导入响应无法确认，不能声称此次文件已通过验证；已尝试重新读取当前局。'; }
    else { operation(`导入未应用，当前局保留并已暂停：${error.message}`,true); $('replay-status').textContent = `校验失败，当前局保留：${error.message}`; }
  } finally { if (token === generation) { busy = ''; updateControls(); drawMap(); } }
}
function verifyReplay(replay,tick,lastSources) {
  if(!finiteKeys(replay,['schema','profile','engine','build','official','observation_profile','observation_version','initial_hash','final_hash','frames','config'])||replay.schema!=='rmuc-experimental-roster-replay.v1'||replay.profile!==PROFILE||replay.engine!==ENGINE||replay.build!==meta.build||replay.official!==false||replay.observation_profile!==OBSERVATION_PROFILE||replay.observation_version!==1||!integer(replay.initial_hash,0,0xffffffff)||!integer(replay.final_hash,0,0xffffffff)||!Array.isArray(replay.frames)||replay.frames.length!==tick)throw Error('回放身份或帧数与当前视图不一致，请重新连接。');
  verifyConfig(replay.config);if(!equal(replay.config,state.config))throw Error('回放配置与当前局不一致。');
  for(let i=0;i<replay.frames.length;i++){
    const frame=replay.frames[i];if(!finiteKeys(frame,['tick','inputs','hash'])||frame.tick!==i||!integer(frame.hash,0,0xffffffff)||!Array.isArray(frame.inputs)||frame.inputs.length!==state.config.robots.length)throw Error('开发者回放帧格式无效。');
    frame.inputs.forEach((input,j)=>{if(!finiteKeys(input,['id','kind','sdk_version','controller_id','action','observation_hash'])||input.id!==state.config.robots[j].id||!integer(input.observation_hash,0,0xffffffff))throw Error('回放动作 ID 或过滤摘要无效。');verifySource(input);const a=input.action;if(!Array.isArray(a)||a.length!==5||!integer(a[0],-1000,1000)||!integer(a[1],-1000,1000)||!integer(a[2],0,12000)||!integer(a[3],0,7600)||!integer(a[4],0,1))throw Error('回放动作无效。');});
  }
  const final=replay.frames.at(-1);if((tick===0&&replay.final_hash!==replay.initial_hash)||(final&&(replay.final_hash!==final.hash||final.inputs.some((input,i)=>['id','kind','sdk_version','controller_id'].some(key=>input[key]!==lastSources?.[i]?.[key])))))throw Error('回放末帧或来源与当前视图不一致。');
}
async function exportReplay() {
  if (!ready() || busy) return;
  pauseAndRelease(); const token = ++generation, tick = state.tick, lastSources = state.last_sources; busy = 'export'; updateControls();
  try {
    const replay = await api('replay'); if (token !== generation) return;
    try { verifyReplay(replay,tick,lastSources); } catch (error) { synchronized = false; blankView(); throw error; }
    const text = JSON.stringify(replay); requireSize(text);
    const blob = new Blob([text],{type:'application/json'}), url = URL.createObjectURL(blob), anchor = node('a');
    anchor.href = url; anchor.download = `tactics-roster-developer-tick-${replay.frames.length}.json`; document.body.append(anchor); anchor.click(); anchor.remove(); setTimeout(() => URL.revokeObjectURL(url),1000);
    operation(`已请求下载开发者回放（${replay.frames.length} 帧，包含全部机器人已解析动作）。导入不重新执行策略。请自行确认浏览器下载；本页不验证实际落盘。`);
    $('replay-status').textContent = '已核对导出的构建、帧数和来源格式。普通视图不含全局哈希，不能据此证明它与先前视图是同一个完整状态。';
  } catch (error) { if (token === generation) operation(`开发者回放导出失败：${error.message}`,true); }
  finally { if (token === generation) { busy = ''; updateControls(); } }
}
function sourceLabel(source) { return source.kind === 'manual' ? '人工' : `C++ · ${source.controller_id}`; }
function renderMetadata() {
  const choices=[],options=[];sourceControls=new Map();
  for(const robot of state.config.robots){
    const label=node('label',undefined,'roster-choice team-'+robot.team),name=node('strong',robot.id),small=node('small',`${robot.team===0?'红':'蓝'}方 · ${robot.profile.id} · ${robot.eligible?'可参与目标检测':'不参与目标检测'}`),select=node('select');
    select.setAttribute('aria-label',`${robot.id} 的控制源`);
    const manual=node('option','人工 · 仅选中视角响应输入');manual.value='manual';const policy=node('option',policyAvailable?`C++ · ${meta.policy.controller_id}`:'C++ 暂不可用');policy.value='policy';policy.disabled=!policyAvailable;
    select.replaceChildren(manual,policy);select.value=sources.get(robot.id);select.onchange=()=>selectSource(robot.id);sourceControls.set(robot.id,select);label.append(name,small,select);choices.push(label);
    const option=node('option',`${robot.id} · ${robot.team===0?'红':'蓝'}方过滤视角`);option.value=robot.id;options.push(option);
  }
  $('roster-controls').replaceChildren(...choices);$('observer').replaceChildren(...options);$('observer').value=observer;
  facts('policy-metadata',policyAvailable?[['策略 ID',meta.policy.controller_id],['SDK / 决策频率',`v${meta.policy.sdk_version} / ${meta.policy.decision_hz} Hz`],['过滤协议',meta.policy.observation],['执行方式',meta.policy.execution],['策略状态',meta.policy.state_mode],['响应保护',`${meta.policy.watchdog_seconds} s；不是安全沙箱`]]:[['策略状态',POLICY_ERROR]]);
  facts('mode-metadata',[['模式',meta.profile],['观测协议',meta.observation_profile],['源码构建',meta.build],['官方状态','非官方 SYNTHETIC 引擎；部分来源参数的状态见独立来源面板'],['运行上限',`${meta.duration} 帧 / 420 秒`],['步进上限',`每批 ${meta.max_batch} 帧`],...meta.limitations.map(value=>['实验限制',value])]);
}
function renderState(){
  const observation=state.observation,self=observation.self,profile=observation.roster.find(r=>r.id===observer).profile;
  renderMatch();renderHeat();$('map-heading').textContent=`${observer} · 当前过滤视角`;$('self-label').textContent=observer;
  $('tick-status').textContent=`TICK ${state.tick} · ${(state.tick/meta.hz).toFixed(3)} s`;
  facts('self-state',[['身份 / 队伍',`${observer} / ${observation.self_team===0?'红':'蓝'}方`],['SYNTHETIC 参数',profile.id],['位置 x / y',`${number(self.x)} / ${number(self.y)} mm`],['朝向',`${self.yaw} / 256 圈`],['自己的 HP',`${self.hp} / ${profile.max_hp}${self.hp===0?' · 已失效，无复活':''}`],['自己的弹量',String(self.ammo)],['机械冷却',`${self.cooldown} 帧`],['下批控制源',sources.get(observer)==='manual'?'人工':'C++ 策略']]);
  facts('visible-state',[['可见其他机器人',String(observation.contacts.length)],...observation.contacts.map(c=>[`${c.id} · ${c.team===0?'红':'蓝'}`,`${c.x}, ${c.y} mm / yaw ${c.yaw}`]),['可见弹丸',String(observation.projectiles.length)],['自己传感范围',`${observation.sensor_range_mm} mm + 原生 LOS`]]);
  $('visibility-status').textContent=`本帧可见 ${observation.contacts.length} 台其他机器人`;
  $('source-status').textContent=(state.last_sources===null?'尚无已提交动作。':`末帧来源：${state.last_sources.map(s=>`${s.id} → ${sourceLabel(s)}`).join('；')}。来源标签未经认证。`)+(!policyAvailable?` ${POLICY_ERROR}`:'');
  $('view-status').textContent=`只绘制 ${observer} 的当前观测。虚线圈是 ${observation.sensor_range_mm} mm 最大范围，不表示墙后可见。队友也经过范围 / 遮挡过滤；隐藏机器人不留残影，不从公开出生点、目标归属推测当前位置或存活。`;
}
function renderHeat() {
  const heat = state.observation.self_heat;
  $('heat-value').textContent = heat.enabled ? `${(heat.heat_tenths/10).toFixed(1)} / ${(heat.limit_tenths/10).toFixed(1)}` : '关闭';
  $('heat-fill').style.width = heat.enabled ? `${Math.min(100,100*heat.heat_tenths/Math.max(1,heat.limit_tenths))}%` : '0%';
  $('heat-fill').className = heat.firing_locked ? 'locked' : '';
  $('heat-status').className = 'mini-tag' + (heat.firing_locked ? ' error' : '');
  $('heat-status').textContent = !heat.enabled ? '无热量对照' : heat.permanent_latched ? '本局发射永久热锁' : heat.overheat_locked ? '冷却至 0 后解锁' : '热锁未触发';
  $('heat-note').textContent = !heat.enabled ? '已明确关闭热量限制，保留原有射击节拍与弹量。热量配置仅在重置后生效。' : `${heat.firing_locked ? '发射停用；移动仍可用。按住发射仍记录请求，但锁定时不出弹、不耗弹；临时锁解除后可恢复。' : '每次原生确认发射 +10；单纯按键不增加热量。'}按 100 ms 固定边界冷却，${heat.cooling_per_second} 热量 / s；临时锁需降至 0。超热量不额外扣 HP，也不计入攻击伤害。第一人称图传未模拟；本页战术地图保持显示。`;

}
function renderMatch() {
  const publicState = state.observation, result = state.result;
  const remaining = Math.ceil((meta.duration-state.tick)/meta.hz);
  $('remaining-time').textContent = `${String(Math.floor(remaining/60)).padStart(2,'0')}:${String(remaining%60).padStart(2,'0')}`;
  for (const [i,name] of ['red','blue'].entries()) {
    $('score-'+name).textContent = publicState.score === null ? '—' : String(publicState.score[i]);
    $('rate-'+name).textContent = `当前 ${publicState.rate[i]} 分 / s${publicState.credits_enabled ? '' : ' · 仅公式，未计分'}`;
  }
  const resultText = {
    ongoing:'争夺目标 · 战斗不会提前结束比赛',
    'red-wins':`420 s 到时 · 红方${result.reason === 'timeout-control' ? '控制积分' : result.reason === 'timeout-attack-damage' ? '实验攻击伤害' : '存活机器人 HP'}胜出`,
    'blue-wins':`420 s 到时 · 蓝方${result.reason === 'timeout-control' ? '控制积分' : result.reason === 'timeout-attack-damage' ? '实验攻击伤害' : '存活机器人 HP'}胜出`,
    draw:'420 s 到时 · 各级比较相同，平局',
    'unresolved-timeout-tie':'420 s 控制同分 · 未能裁决',
    'unscored-timeout':'420 s 已到时 · 未启用积分，不裁决'
  };
  $('match-result').textContent = resultText[result.outcome];
  $('match-result').className = result.outcome === 'unresolved-timeout-tie' || result.outcome === 'unscored-timeout' ? 'unresolved' : state.terminal ? 'terminal' : '';
  $('credit-status').textContent = result.outcome === 'unresolved-timeout-tie' ? (result.reason === 'missing-adjudicated-surviving-robot-hp' ? '存活机器人 HP 无法完整裁定：同分保持未裁决' : state.config.attack_damage_strategy === DAMAGE_STRATEGIES[0] ? '已选缺失攻击账本策略：控制同分保持未裁决' : '原生攻击账本无法完整重核：控制同分保持未裁决') : state.terminal && publicState.credits_enabled && state.config.attack_damage_strategy === DAMAGE_STRATEGIES[1] && result.reason !== 'timeout-control' ? '实验依据：控制积分 → 实际扣血攻击账本 → 存活机器人 HP；数值不公开' : publicState.credits_enabled ? '实验：每秒末采样控制并结算，包括第 420 秒' : '积分关闭：公开当前计分率，仅演示控制归属';
  $('preset-label').textContent = `${state.config.robots.filter(r=>r.team===0).length}v${state.config.robots.filter(r=>r.team===1).length} · SYNTHETIC`;
  $('applied-config').textContent = `当前局：${state.config.boundary_policy === BOUNDARIES[1] ? '工程计时先于输入' : '严格拒绝未裁定边界'} / ${publicState.credits_enabled ? '实验每秒末计分' : '不累计积分'} / ${state.config.attack_damage_strategy === DAMAGE_STRATEGIES[1] ? '实验实际扣血攻击裁决' : '缺失攻击账本保持未裁决'} / ${state.config.heat_strategy === HEAT_STRATEGIES[1] ? 'SYNTHETIC 独立热量开启' : '热量关闭对照'} / ${state.config.heat_threshold_policy === HEAT_THRESHOLDS[0] ? 'Q2 等值严格拒绝' : state.config.heat_threshold_policy === HEAT_THRESHOLDS[1] ? 'Q2 等值工程临时锁' : 'Q2 等值工程按流程图'}。JSON 草稿仅在显式重置后生效。`;
  const cards = publicState.zones.map((zone,index) => {
    const card = node('article',undefined,'objective-card');
    const label = node('strong',ZONES[index].label);
    const owner = node('span',zone.controller === 0 ? '红方控制' : '蓝方控制','owner '+(zone.controller === 0 ? 'red' : 'blue'));
    const degree = node('span',zone.degree === 'full' ? '完全控制' : '临时控制');
    const rect = zone.rect;
    card.append(label,owner,degree,node('small',`${rect.x1},${rect.y1} → ${rect.x2},${rect.y2} mm`));
    return card;
  });
  $('objective-list').replaceChildren(...cards);
}
function drawZones() {
  for (const [index,zone] of state.observation.zones.entries()) {
    const rect = zone.rect, p = screen(rect.x1,rect.y2), width = (rect.x2-rect.x1)*mapTransform.scale, height = (rect.y2-rect.y1)*mapTransform.scale;
    const color = zone.controller === 0 ? '#f18d92' : '#82bcf2';
    ctx.fillStyle = color+'16'; ctx.fillRect(...p,width,height);
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.setLineDash(zone.degree === 'temporary' ? [6,4] : []); ctx.strokeRect(...p,width,height); ctx.setLineDash([]);
    ctx.fillStyle = color; ctx.font = '11px ui-monospace, monospace'; ctx.textAlign = 'center';
    ctx.fillText(ZONES[index].label,p[0]+width/2,p[1]+19);
    ctx.font = '10px ui-monospace, monospace'; ctx.fillText(`${zone.controller === 0 ? '红' : '蓝'} · ${zone.degree === 'full' ? '完全' : '临时'}`,p[0]+width/2,p[1]+35);
  }
}
function screen(x,y) { return [mapTransform.left+x*mapTransform.scale,mapTransform.bottom-y*mapTransform.scale]; }
function drawRobot(robot,own,id,team) {
  const profile=state.config.robots.find(r=>r.id===id).profile,position=screen(robot.x,robot.y),radius=profile.radius_mm*mapTransform.scale,angle=robot.yaw*Math.PI*2/256;
  const color=own?'#bfe98c':team===0?'#f18d92':'#82bcf2';
  ctx.beginPath();ctx.arc(...position,radius,0,Math.PI*2);ctx.fillStyle=color+'28';ctx.fill();ctx.strokeStyle=color;ctx.lineWidth=own?3:2;ctx.stroke();
  ctx.beginPath();ctx.moveTo(...position);ctx.lineTo(position[0]+Math.cos(angle)*radius*1.6,position[1]-Math.sin(angle)*radius*1.6);ctx.stroke();
  const disabled=own&&robot.hp===0;
  if(disabled){ctx.beginPath();ctx.moveTo(position[0]-radius*.6,position[1]-radius*.6);ctx.lineTo(position[0]+radius*.6,position[1]+radius*.6);ctx.moveTo(position[0]+radius*.6,position[1]-radius*.6);ctx.lineTo(position[0]-radius*.6,position[1]+radius*.6);ctx.strokeStyle='#b2bdc6';ctx.stroke();}
  ctx.fillStyle=color;ctx.font='12px ui-monospace, monospace';ctx.textAlign='center';ctx.fillText(`${own?'SELF':'VISIBLE'} ${id}${disabled?' · DISABLED':''}`,position[0],position[1]-radius-12);
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
  drawZones();
  for (const bullet of observation.projectiles) { const p = screen(bullet.x,bullet.y), previous = screen(bullet.x-bullet.vx/2,bullet.y-bullet.vy/2); ctx.beginPath(); ctx.moveTo(...previous); ctx.lineTo(...p); ctx.strokeStyle = '#eddca1'; ctx.lineWidth = 3; ctx.stroke(); }
  // Never use replay data, a prior observation or another observer to draw opponents.
  for(const contact of observation.contacts) drawRobot(contact,false,contact.id,contact.team);
  drawRobot(observation.self,true,observer,observation.self_team);
  if (selectedManual() && aim) { const p = screen(aim.x,aim.y); ctx.beginPath(); ctx.moveTo(p[0]-7,p[1]); ctx.lineTo(p[0]+7,p[1]); ctx.moveTo(p[0],p[1]-7); ctx.lineTo(p[0],p[1]+7); ctx.strokeStyle = '#bfe98c'; ctx.lineWidth = 1; ctx.stroke(); }
  ctx.restore(); ctx.fillStyle = '#8ca8ba'; ctx.font = '11px ui-monospace, monospace'; ctx.textAlign = 'left'; ctx.fillText(`OBSERVER ${observer} · LOCAL COMBAT + PUBLIC OBJECTIVES     ↑ +y     → +x    [mm]`,20,25);
}
function loop(time) {
  if (playing && canAdvance()) { if (lastTime !== null) accumulator = Math.min(MAX_BACKLOG,accumulator+Math.max(0,(time-lastTime)/1000)*meta.hz*speed); lastTime = time; if (!busy && accumulator >= 1) { const count = Math.min(MAX_STEP_BATCH,Math.floor(accumulator),meta.duration-state.tick); accumulator -= count; void advance(count); } }
  else lastTime = null;
  requestAnimationFrame(loop);
}
function typingTarget(target) { return target && (['INPUT','TEXTAREA','SELECT'].includes(target.tagName) || target.isContentEditable); }
const codes = ['KeyW','KeyA','KeyS','KeyD','ArrowUp','ArrowLeft','ArrowDown','ArrowRight','Space'];
window.addEventListener('keydown',event => { if (!codes.includes(event.code) || typingTarget(event.target) || !canDrive() || (busy && busy !== 'step') || (event.code === 'Space' && event.target?.tagName === 'BUTTON')) return; if (event.repeat && !keys.has(event.code)) return; event.preventDefault(); keys.add(event.code); });
window.addEventListener('keyup',event => { keys.delete(event.code); });
window.addEventListener('blur',pauseAndRelease);
document.addEventListener('visibilitychange',() => { if (document.hidden) pauseAndRelease(); });
for (const [id,code] of [['drive-up','KeyW'],['drive-left','KeyA'],['drive-down','KeyS'],['drive-right','KeyD'],['fire','Space']]) {
  const button = $(id);
  button.onpointerdown = event => { if (!canDrive() || (busy && busy !== 'step')) return; event.preventDefault(); pointers.set(event.pointerId,code); button.className = 'held'; button.setPointerCapture?.(event.pointerId); };
  const release = event => { pointers.delete(event.pointerId); if (![...pointers.values()].includes(code)) button.className = ''; };
  button.onpointerup = release; button.onpointercancel = release; button.onlostpointercapture = release;
}
function setAim(event) {
  if (!canDrive() || !mapTransform || (busy && busy !== 'step')) return false;
  const rectangle = canvas.getBoundingClientRect(), x = (event.clientX-rectangle.left)*canvas.width/rectangle.width, y = (event.clientY-rectangle.top)*canvas.height/rectangle.height, arena = state.observation.arena;
  aim = {x:Math.round(Math.max(0,Math.min(arena.width,(x-mapTransform.left)/mapTransform.scale))),y:Math.round(Math.max(0,Math.min(arena.height,(mapTransform.bottom-y)/mapTransform.scale)))};
  drawMap(); return true;
}
canvas.onpointermove = setAim;
canvas.onpointerdown = event => { if (event.button !== 0 || !setAim(event)) return; event.preventDefault(); pointers.set(event.pointerId,'Space'); canvas.setPointerCapture?.(event.pointerId); };
for (const name of ['onpointerup','onpointercancel','onlostpointercapture']) canvas[name] = event => { pointers.delete(event.pointerId); };
$('observer').onchange = selectObserver;
$('play').onclick = () => { if (playing) { pauseAndRelease(); drawMap(); } else if (!busy) setPlaying(true); };
$('step').onclick = () => { if (!playing) return advance(1); };
$('advance-1s').onclick = () => { if (!playing) return advance(60); };
$('advance-10s').onclick = () => { if (!playing) return advance(600); };
$('speed').onchange = () => { const chosen = Number($('speed').value); if (!ready() || busy || ![1,4,16,60].includes(chosen)) { $('speed').value = String(speed); return; } pauseAndRelease(); speed = chosen; revision++; updateControls(); operation(`已暂停，播放速度改为 ${speed}×。仍按 60 Hz 固定 tick 推进与记录，每批最多 600 帧，最多积压 1200 帧；过长现实时间间隔不会形成无限补帧。`); };
$('config-json').oninput=selectConfig; $('config-default').onclick=()=>stageConfig(meta.default_config); $('config-current').onclick=()=>stageConfig(state.config);
$('reset').onclick = resetRun; $('reconnect').onclick = reconnect;
$('import-replay').onclick = openPicker; $('replay-file').onchange = importFile; $('replay-file').oncancel = () => { replayPicker = null; };
$('export-replay').onclick = exportReplay;
profileHook('initialize');
updateControls(); drawMap(); void reconnect(); requestAnimationFrame(loop);
