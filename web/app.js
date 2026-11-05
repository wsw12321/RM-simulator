'use strict';
const $ = id => document.getElementById(id);
const canvas = $('arena'), ctx = canvas.getContext('2d');
const keys = new Set();
let meta, state, playing = false, inFlight = null, accumulator = 0, lastTime = 0, mouseDown = false;
let operationBusy = false, scenario = 'sandbox', viewVersion = 0, pendingImportScope = null;
const modeValues = {sandbox:'0', objective:'1'};
const busyIds = ['play','step','reset','export','import','mode','scenario','boundary-policy','credit-strategy','objective-apply','simulation-speed','advance-second','advance-ten'];
function setBusy(value) { operationBusy=value; for(const id of busyIds) $(id).disabled=value; }
let aim = {x: 10000, y: 3800}, toastTimer, eventRows = [];
const colors = ['#ff705e','#60a8ef'], names = ['红方','蓝方'];
const isObjective = () => scenario === 'objective';
const zoneLabel = zone => ({'red-outpost':'红方前哨目标区','blue-outpost':'蓝方前哨目标区','red-base':'红方基地目标区','blue-base':'蓝方基地目标区'}[zone.id] || zone.label);
const route = (path, scope = scenario) => scope === 'objective' ? `objective/${path}` : path;
async function api(path, data) {
  const response = await fetch(`/api/${path}`, data === undefined ? {} : {
    method: 'POST', headers: {'Content-Type':'application/json','X-Sandbox-Client':'1'}, body: JSON.stringify(data)
  });
  const value = await response.json();
  if (!response.ok || value.error) throw new Error(value.error || `HTTP ${response.status}`);
  return value;
}
function toast(message, error = false) {
  $('toast').textContent = message; $('toast').className = error ? 'error' : ''; $('toast').style.display = 'block';
  clearTimeout(toastTimer); toastTimer = setTimeout(() => $('toast').style.display = 'none', error ? 7000 : 4000);
}
function replayStatus(message,tone='') { $('replay-status').textContent=message;$('replay-status').className=`replay-status ${tone}`;$('replay-status').hidden=!message; }
function clearInput() { keys.clear(); mouseDown = false; }
function clockText(seconds, tenths = true) {
  const safe=Math.max(0,Math.floor(seconds*(tenths?10:1))/(tenths?10:1));
  return `${String(Math.floor(safe/60)).padStart(2,'0')}:${(safe%60).toFixed(tenths?1:0).padStart(tenths?4:2,'0')}`;
}
function objectiveOutcome() {
  const outcome = state.objective.result.outcome;
  return {'ongoing':'推演进行中','red-wins':'红方胜出 · 实验结算','blue-wins':'蓝方胜出 · 实验结算','draw':'平局 · 实验结算','unscored-timeout':'未计分教学结束'}[outcome] || outcome;
}
function objectiveReason() {
  return {'credit-strategy-disabled':'计分未启用；到达 420 秒，只结束教学推演，不判胜。','timeout-control':'到达 420 秒，按已启用的实验控制分比较结果。','timeout-attack-damage':'到达 420 秒，按攻击伤害比较；本场景不产生攻击伤害。','timeout-surviving-robot-hp':'到达 420 秒，按存活机器人教学 HP 比较。','timeout-equal':'到达 420 秒，各比较项相同；本场景攻击伤害为 0。'}[state.objective.result.reason] || state.objective.result.reason;
}
function setPlaying(value) {
  playing = Boolean(value && state && !state.terminal); accumulator = 0;
  $('play').textContent = playing ? 'Ⅱ 暂停推演' : '▶ 继续推演';
  if (!state || state.tick === 0) $('play').textContent = playing ? 'Ⅱ 暂停推演' : '▶ 开始推演';
  if (!playing) clearInput();
  updateOverlay();
}
function updateOverlay() {
  if (!state) return;
  const overlay = $('overlay');
  overlay.hidden = playing || (state.tick > 0 && !state.terminal);
  if (isObjective()) {
    overlay.querySelector('.eyebrow').textContent = state.terminal ? 'EXPERIMENT ENDED' : 'MOVEMENT → DETECTION → CAPTURE';
    overlay.querySelector('h2').textContent = state.terminal ? objectiveOutcome() : '驶入对方目标区';
    overlay.querySelector('p').textContent = state.terminal ? '仅此无战斗实验场景的结算，不是完整赛事判胜。' : '全自动约 10 秒可看到占领变化；人工驾驶用 WASD。';
    overlay.querySelector('.overlay-tip').textContent = state.terminal ? '导出本模式回放，或重置实验' : '点击开始，或「推进 10 秒」快速观察';
  } else if (state.terminal) {
    overlay.querySelector('.eyebrow').textContent = 'SANDBOX RUN ENDED';
    overlay.querySelector('h2').textContent = state.robots.every(r => r.hp === 0) ? '双方同时停机' : state.robots[0].hp === 0 ? '红方停机' : state.robots[1].hp === 0 ? '蓝方停机' : '试验时限到达';
    overlay.querySelector('p').textContent = '沙盒结束条件，不是正式比赛判胜。';
    overlay.querySelector('.overlay-tip').textContent = '导出回放，或重置后尝试另一种策略';
  } else {
    overlay.querySelector('.eyebrow').textContent = 'READY TO EXPLORE';
    overlay.querySelector('h2').textContent = '让想法进入战场';
    overlay.querySelector('p').textContent = '人工接管，或运行你的 C++ 策略。';
    overlay.querySelector('.overlay-tip').textContent = '点击「开始推演」';
  }
}
function syncScenarioUI() {
  const objective = isObjective();
  $('app').className = objective ? 'app objective-mode' : 'app';
  $('scenario').value = scenario;
  for(const id of ['objective-controls','objective-legend','objective-zones-card']) $(id).hidden=!objective;
  for(const id of ['red-robot-card','blue-robot-card']) $(id).hidden=objective;
  $('notice-title').textContent=objective?'实验目标区 · 非官方教学场景':'沙盒模型 · 非官方规则实现';
  $('notice-text').textContent=objective?'矩形几何、中心检测、工程时钟与可选计分采样均为假设。临时控制锁存，无恢复与前哨生命周期。':'演示场地、血量、速度与伤害均为工程假设。非正式赛地图，不用于评估实战胜率。';
  $('scenario-description').textContent=objective?'独立状态 · 占位检测区 · 真实移动驱动占领':'移动、瞄准与弹道的原始工程沙盒。';
  $('profile-badge').textContent=meta.profile;
  $('arena-title').textContent=objective?'四目标区 · 占位几何试验场':'双机器人交互试验场';
  $('arena-subtitle').textContent=objective?'矩形中心检测 · 无战斗':'全信息观测 · 俯视 2D';
  $('aim-help').textContent=objective?'中心进入矩形 → 合格检测':'鼠标 瞄准';
  $('fire-help').textContent=objective?'武器禁用 · 无攻击伤害':'SPACE / 左键 发射';
  $('mode-manual').textContent=objective?'人工红方 · 蓝方静止':'人工驾驶 + 人工瞄准';
  $('mode-hybrid').textContent=objective?'人工红方 · 蓝方自动':'人工驾驶 + C++ 瞄准';
  $('mode-auto').textContent=objective?'双方 C++ 教学移动策略':'C++ 全自动策略';
  $('mode').value=modeValues[scenario];
  $('duration').textContent=`/ ${clockText(objective?state.objective.duration_ms/1000:meta.duration/meta.hz,false)}`;
  $('replay-help').textContent=objective?'仅导入本实验模式回放；含策略身份与逐帧哈希，不能与战斗沙盒交叉导入。文件仅在本机处理。':'导入将逐帧校验哈希，成功后停在回放末尾。文件仅在本机处理。';
  $('footer-profile').textContent=`Prototype · ${meta.profile}`;
  canvas.setAttribute('aria-label',objective?'实验目标区。WASD移动红方，中心进入矩形区触发检测。武器禁用。':'战术沙盒。WASD移动，鼠标瞄准，空格或鼠标左键发射。');
  updateOwner();
}
function updateOwner() { $('red-owner').textContent=['人工控制','C++ 全自动策略','人工驾驶 · C++ 瞄准'][Number($('mode').value)]; }
function syncConfig() {
  if(!isObjective())return;
  $('boundary-policy').value=state.objective.boundary_policy;
  $('credit-strategy').value=state.objective.credit_strategy;
  $('config-status').className='config-status';
  $('config-status').textContent='配置已应用。严格模式遇到未裁定边界会暂停并拒绝整批步进；加速不会跳过边界。';
}
function element(tag, className, text) {
  const node=document.createElement(tag);if(className)node.className=className;if(text!==undefined)node.textContent=text;return node;
}
function renderObjective() {
  const objective=state.objective;
  for(let team=0;team<2;team++) {
    const prefix=team?'blue':'red';
    $(`${prefix}-score`).textContent=objective.score===null?'未启用':objective.score[team];
    $(`${prefix}-rate`).textContent=`当前控制公式值 ${objective.rate[team]} / 秒`;
  }
  $('objective-phase').textContent=state.tick===0?'等待开始':objectiveOutcome();
  $('objective-result').textContent=state.terminal?objectiveReason():objective.score===null?'计分关闭：420 秒到时仅作未计分教学结束。':'每秒末实验采样已启用，不代表官方计分时机。';
  $('credit-count').textContent=`已采样 ${objective.credits} 个完整秒 · ${clockText(Math.max(0,objective.duration_ms-objective.elapsed_ms)/1000)} 剩余`;
  $('active-boundary').textContent=objective.boundary_policy;
  $('active-credit').textContent=objective.credit_strategy;
  $('active-lifecycle').textContent=objective.lifecycle_strategy;
  $('active-clock').textContent=objective.clock_strategy||'EXPERIMENTAL-ceil-sampled-ms-v1';
  $('objective-zones').replaceChildren(...objective.zones.map(zone=>{
    const card=element('section','zone-state');card.setAttribute('data-owner',zone.controller);
    const title=element('div','zone-title');title.append(element('b','',zoneLabel(zone)),element('span','zone-status',`${names[zone.controller]} · ${zone.degree==='full'?'完整控制':'临时控制'}`));
    const progress=element('div','zone-progress'),bar=element('i');
    const ms=Math.max(0,Math.min(5000,zone.progress_ms));bar.style.width=`${ms/50}%`;progress.append(bar);
    progress.setAttribute('role','progressbar');progress.setAttribute('aria-label',`${zoneLabel(zone)}占领进度`);progress.setAttribute('aria-valuemin','0');progress.setAttribute('aria-valuemax','5000');progress.setAttribute('aria-valuenow',String(ms));
    card.append(title,progress,element('p','zone-progress-caption',zone.completed?'占领已完成 · 临时控制锁存（不恢复）':`${names[1-zone.original_owner]}占领进度 ${(ms/1000).toFixed(2)} / 5.00 秒`));
    for(let team=0;team<2;team++) {
      const row=element('div',`zone-signal ${team?'blue':'red'}`),grace=zone.deactivation_at_ms[team];
      row.append(element('b','',names[team]),element('span',zone.detected[team]?'on':'',`检测${zone.detected[team]?'有':'无'}`),element('span',zone.active[team]?'on':'',`逻辑${zone.active[team]?'激活':'未激活'}`));
      if(grace!==null)row.append(element('span','grace',`宽限 ${Math.max(0,(grace-objective.elapsed_ms)/1000).toFixed(2)} 秒`));
      card.append(row);
    }
    return card;
  }));
}
function clearEvents(message='等待首个事件…') { eventRows=[];$('events').replaceChildren(element('li','muted',message)); }
function renderEvents() {
  eventRows=eventRows.slice(0,24);
  if(eventRows.length)$('events').replaceChildren(...eventRows.map(event=>{
    const li=element('li'),time=element('time','',`${(event.tick/60).toFixed(isObjective()?3:1)}s`);li.append(time,document.createTextNode(event.content));return li;
  }));
}
function display(next, logEvents = true) {
  const previous=state;state=next;
  state.robots.forEach((robot,i)=>{
    const team=i?'blue':'red';$(`${team}-hp`).textContent=robot.hp;$(`${team}-bar`).style.width=`${robot.hp}%`;
    $(`${team}-ammo`).textContent=robot.ammo;$(`${team}-hits`).textContent=`${robot.hits} / ${robot.shots}`;
  });
  const seconds=isObjective()?state.objective.elapsed_ms/1000:state.tick/60;
  $('time').textContent=clockText(seconds);$('tick').textContent=state.tick;$('hash').textContent=state.hash.toString(16).padStart(8,'0');
  if(isObjective()) {
    renderObjective();
    if(logEvents) {
      // These observations are display-only; capture and scoring remain authoritative in C++.
      for(const zone of state.objective.zones) {
        const old=previous?.objective?.zones.find(item=>item.id===zone.id);
        if(zone.completed&&!old?.completed)eventRows.unshift({tick:(zone.completed_at_ms??state.objective.elapsed_ms)*60/1000,content:`${zoneLabel(zone)}：${names[zone.controller]}临时控制已锁存`});
      }
      if(state.terminal&&!previous?.terminal)eventRows.unshift({tick:state.tick,content:objectiveOutcome()});
    }
  } else if(logEvents)for(const [tick,kind,actor,target,amount] of next.events||[]) {
    if(kind===1||kind===4)continue;
    eventRows.unshift({tick,content:kind===2?`${names[actor]}命中${names[target]} · −${amount} HP`:`${names[actor]}机器人停机`});
  }
  renderEvents();if(state.terminal)setPlaying(false);updateOverlay();
}
function action() {
  return [(keys.has('KeyD')||keys.has('ArrowRight')?1000:0)-(keys.has('KeyA')||keys.has('ArrowLeft')?1000:0),
    (keys.has('KeyS')||keys.has('ArrowDown')?1000:0)-(keys.has('KeyW')||keys.has('ArrowUp')?1000:0),
    Math.round(aim.x),Math.round(aim.y),!isObjective()&&(keys.has('Space')||mouseDown)?1:0];
}
async function advance(count,capturedAction=null) {
  if(inFlight||!state||state.terminal)return;
  const scope=scenario,version=viewVersion,request=api(route('step',scope),{count,mode:Number($('mode').value),action:capturedAction||action()});inFlight=request;
  try{const next=await request;if(scope===scenario&&version===viewVersion)display(next);}
  catch(error){if(scope===scenario&&version===viewVersion){setPlaying(false);toast(error.message,true);}}
  finally{if(inFlight===request)inFlight=null;}
}
async function stopAndWait(){setPlaying(false);if(inFlight){try{await inFlight;}catch{}}}
function robot(robot,index) {
  const x=robot.x/10,y=robot.y/10,c=colors[index],angle=robot.yaw*Math.PI/128;
  ctx.save();ctx.translate(x,y);ctx.globalAlpha=robot.hp>0?1:.35;
  ctx.fillStyle=`${c}12`;ctx.beginPath();ctx.arc(0,0,41,0,Math.PI*2);ctx.fill();
  ctx.strokeStyle=`${c}50`;ctx.lineWidth=1;ctx.setLineDash([3,5]);ctx.beginPath();ctx.arc(0,0,39,0,Math.PI*2);ctx.stroke();ctx.setLineDash([]);
  ctx.fillStyle='#111c27';ctx.strokeStyle=c;ctx.lineWidth=2;ctx.beginPath();ctx.roundRect(-25,-24,50,48,11);ctx.fill();ctx.stroke();
  ctx.fillStyle=c;ctx.globalAlpha*=.65;ctx.fillRect(-30,-17,5,12);ctx.fillRect(25,-17,5,12);ctx.fillRect(-30,5,5,12);ctx.fillRect(25,5,5,12);ctx.globalAlpha=robot.hp>0?1:.35;
  ctx.rotate(angle);ctx.fillStyle=c;if(!isObjective()){ctx.beginPath();ctx.roundRect(-3,-5,44,10,3);ctx.fill();}ctx.fillStyle='#cddbe6';ctx.beginPath();ctx.arc(0,0,9,0,Math.PI*2);ctx.fill();ctx.rotate(-angle);
  ctx.fillStyle='#a8bacb';ctx.font='10px ui-monospace, monospace';ctx.textAlign='center';ctx.fillText(index===0?'R-01':'B-02',0,-52);
  if(!isObjective()){ctx.fillStyle='#263544';ctx.fillRect(-25,-44,50,3);ctx.fillStyle=c;ctx.fillRect(-25,-44,robot.hp/2,3);}ctx.restore();
}
function draw() {
  if (!meta || !state) return;
  const width=1200,height=760;
  ctx.clearRect(0,0,width,height);ctx.fillStyle='#101923';ctx.fillRect(0,0,width,height);
  ctx.lineWidth=1;ctx.strokeStyle='#1a2935';
  for(let x=0;x<=width;x+=50){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,height);ctx.stroke();}
  for(let y=0;y<=height;y+=50){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(width,y);ctx.stroke();}
  ctx.strokeStyle='#2a3a47';ctx.lineWidth=2;ctx.strokeRect(22,22,width-44,height-44);
  ctx.strokeStyle='#4b6672';ctx.lineWidth=3;for(const [x,y,sx,sy] of [[22,22,1,1],[1178,22,-1,1],[22,738,1,-1],[1178,738,-1,-1]]){ctx.beginPath();ctx.moveTo(x+sx*25,y);ctx.lineTo(x,y);ctx.lineTo(x,y+sy*25);ctx.stroke();}
  ctx.setLineDash([5,10]);ctx.strokeStyle='#293b49';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(600,23);ctx.lineTo(600,737);ctx.stroke();ctx.setLineDash([]);
  for(const [x,c,label] of [[110,colors[0],'RED SIDE'],[1090,colors[1],'BLUE SIDE']]){ctx.fillStyle=`${c}06`;ctx.fillRect(x-70,100,140,560);ctx.strokeStyle=`${c}20`;ctx.strokeRect(x-70,100,140,560);ctx.fillStyle=`${c}70`;ctx.font='10px ui-monospace, monospace';ctx.textAlign='center';ctx.fillText(label,x,80);}
  for(const [x1,y1,x2,y2] of meta.walls){const x=x1/10,y=y1/10,w=(x2-x1)/10,h=(y2-y1)/10;ctx.fillStyle='#080d13';ctx.fillRect(x+5,y+7,w,h);ctx.fillStyle='#263644';ctx.fillRect(x,y,w,h);ctx.strokeStyle='#516272';ctx.strokeRect(x+.5,y+.5,w-1,h-1);ctx.strokeStyle='#3b4c5a';ctx.lineWidth=1;for(let i=12;i<w;i+=18){ctx.beginPath();ctx.moveTo(x+i,y+5);ctx.lineTo(x+i,y+h-5);ctx.stroke();}ctx.fillStyle='#101923';ctx.fillRect(x+w/2-26,y+h/2-9,52,18);ctx.fillStyle='#8e9fac';ctx.font='9px ui-monospace, monospace';ctx.textAlign='center';ctx.fillText('COVER',x+w/2,y+h/2+3);}
  if(!isObjective()&&Number($('mode').value)===0&&!state.terminal){const r=state.robots[0];ctx.setLineDash([4,8]);ctx.strokeStyle='#ff705e30';ctx.beginPath();ctx.moveTo(r.x/10,r.y/10);ctx.lineTo(aim.x/10,aim.y/10);ctx.stroke();ctx.setLineDash([]);const x=aim.x/10,y=aim.y/10;ctx.strokeStyle='#ff705e90';ctx.beginPath();ctx.arc(x,y,8,0,Math.PI*2);ctx.moveTo(x-13,y);ctx.lineTo(x-5,y);ctx.moveTo(x+5,y);ctx.lineTo(x+13,y);ctx.moveTo(x,y-13);ctx.lineTo(x,y-5);ctx.moveTo(x,y+5);ctx.lineTo(x,y+13);ctx.stroke();}
  if(isObjective())drawObjectiveZones();
  state.robots.forEach(robot);
  for(const [x,y,owner] of state.bullets){ctx.fillStyle=colors[owner];ctx.shadowColor=colors[owner];ctx.shadowBlur=10;ctx.beginPath();ctx.arc(x/10,y/10,3,0,Math.PI*2);ctx.fill();ctx.shadowBlur=0;}
}
function drawObjectiveZones() {
  for(const zone of state.objective.zones) {
    const [x1,y1,x2,y2]=zone.rect.map(value=>value/10),w=x2-x1,h=y2-y1;
    const color=colors[zone.controller],cx=(x1+x2)/2,cy=(y1+y2)/2;
    ctx.save();ctx.fillStyle=`${color}0c`;ctx.fillRect(x1,y1,w,h);
    ctx.setLineDash([7,5]);ctx.strokeStyle=colors[zone.original_owner];ctx.lineWidth=2;ctx.strokeRect(x1,y1,w,h);
    ctx.setLineDash([]);ctx.strokeStyle=`${color}b0`;ctx.lineWidth=3;ctx.strokeRect(x1+5,y1+5,w-10,h-10);
    ctx.fillStyle='#0c151ee8';ctx.fillRect(x1+9,y1+11,w-18,23);
    ctx.font='11px system-ui, sans-serif';ctx.textAlign='center';ctx.fillStyle='#d0dcc5';ctx.fillText(zoneLabel(zone),cx,y1+27);
    ctx.strokeStyle='#334039';ctx.lineWidth=5;ctx.beginPath();ctx.arc(cx,cy+4,27,0,Math.PI*2);ctx.stroke();
    ctx.strokeStyle='#bce679';ctx.beginPath();ctx.arc(cx,cy+4,27,-Math.PI/2,-Math.PI/2+Math.PI*2*Math.min(1,zone.progress_ms/5000));ctx.stroke();
    ctx.font='11px ui-monospace,monospace';ctx.fillStyle='#c4dbad';ctx.fillText(zone.completed?'锁存':`${(zone.progress_ms/1000).toFixed(1)}s`,cx,cy+8);
    ctx.font='10px system-ui,sans-serif';ctx.fillStyle=color;ctx.fillText(`${names[zone.controller]} · ${zone.degree==='full'?'完整':'临时'}`,cx,y2-23);
    for(let team=0;team<2;team++){
      const x=cx+(team?30:-30);ctx.beginPath();ctx.arc(x,y2-11,3,0,Math.PI*2);
      ctx.fillStyle=zone.active[team]?colors[team]:'#40505b';ctx.fill();
      if(zone.detected[team]){ctx.strokeStyle=colors[team];ctx.lineWidth=1;ctx.beginPath();ctx.arc(x,y2-11,6,0,Math.PI*2);ctx.stroke();}
    }
    ctx.restore();
  }
}
function loop(now) {
  const delta=lastTime?Math.min((now-lastTime)/1000,.15):0;lastTime=now;
  if(playing&&!operationBusy){
    const speed=isObjective()?Number($('simulation-speed').value)||1:1;
    // Backpressure retains unprocessed objective time. Each native step is still exactly one tick.
    accumulator=isObjective()?accumulator+delta*speed:Math.min(accumulator+delta,.2);
    if(accumulator>=.05&&!inFlight){const count=Math.min(isObjective()?600:6,Math.floor(accumulator*60));accumulator-=count/60;advance(count);}
  }
  draw();requestAnimationFrame(loop);
}
async function singleAdvance(count){if(operationBusy||!state)return;const capturedAction=action();setBusy(true);try{await stopAndWait();await advance(count,capturedAction);}finally{setBusy(false);}}
async function resetCurrent(){
  if(operationBusy)return;setBusy(true);
  try{await stopAndWait();const config=isObjective()?{boundary_policy:$('boundary-policy').value,credit_strategy:$('credit-strategy').value}:{};
    const next=await api(route('reset'),config);viewVersion++;clearEvents();replayStatus('');display(next,false);syncConfig();setPlaying(false);toast(isObjective()?'已重置实验目标区。':'已重置沙盒。');
  }catch(error){toast(error.message,true);}finally{setBusy(false);}
}
$('play').onclick=()=>{if(operationBusy)return;setPlaying(!playing);canvas.focus();};
$('step').onclick=()=>singleAdvance(1);
$('advance-second').onclick=()=>isObjective()?singleAdvance(60):undefined;
$('advance-ten').onclick=()=>isObjective()?singleAdvance(600):undefined;
$('reset').onclick=resetCurrent;$('objective-apply').onclick=()=>isObjective()?resetCurrent():undefined;
$('scenario').onchange=async()=>{
  const target=$('scenario').value;
  if(operationBusy||!['sandbox','objective'].includes(target)){$('scenario').value=scenario;return;}
  if(target===scenario)return;
  setBusy(true);
  try{await stopAndWait();const [nextMeta,nextState]=await Promise.all([api(route('meta',target)),api(route('state',target))]);
    if(target==='objective'&&(!nextState.objective||nextState.profile!=='objective-experimental-v1'))throw new Error('目标区状态身份不匹配。');
    viewVersion++;scenario=target;meta=nextMeta;replayStatus('');clearEvents('已切换模式；保留各模式独立状态。');state=nextState;syncScenarioUI();syncConfig();display(nextState,false);setPlaying(false);
    toast(isObjective()?'已切换实验目标区。武器禁用，检测几何为工程假设。':'已返回战斗沙盒。');
  }catch(error){$('scenario').value=scenario;toast(`切换失败，原模式已暂停：${error.message}`,true);}finally{setBusy(false);}
};
$('mode').onchange=()=>{if(operationBusy){$('mode').value=modeValues[scenario];return;}clearInput();modeValues[scenario]=$('mode').value;updateOwner();toast('控制方式已切换。下一步采用新输入。');};
for(const id of ['boundary-policy','credit-strategy'])$(id).onchange=()=>{if(operationBusy){syncConfig();return;}setPlaying(false);$('config-status').className='config-status pending';$('config-status').textContent='配置尚未应用。点击「应用配置并重置」后生效；当前权威策略见下方身份。';};
$('simulation-speed').onchange=()=>{if(operationBusy)return;accumulator=0;clearInput();toast('仅改变推进速度，仍逐个计算固定步长。人工驾驶建议 1×。');};
const movementKeys=new Set(['KeyW','KeyA','KeyS','KeyD','ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Space']);
window.addEventListener('keydown',event=>{if(operationBusy||['SELECT','INPUT','TEXTAREA','BUTTON'].includes(event.target.tagName)||$('scope').open)return;if(movementKeys.has(event.code)){event.preventDefault();keys.add(event.code);}});
window.addEventListener('keyup',event=>keys.delete(event.code));
canvas.addEventListener('pointermove',event=>{const rect=canvas.getBoundingClientRect();aim={x:Math.max(0,Math.min(12000,(event.clientX-rect.left)/rect.width*12000)),y:Math.max(0,Math.min(7600,(event.clientY-rect.top)/rect.height*7600))};});
canvas.addEventListener('pointerdown',event=>{if(event.button===0&&!operationBusy){mouseDown=!isObjective();canvas.focus();}});
window.addEventListener('pointerup',()=>mouseDown=false);window.addEventListener('pointercancel',()=>mouseDown=false);
window.addEventListener('blur',()=>setPlaying(false));document.addEventListener('visibilitychange',()=>{if(document.hidden)setPlaying(false);});
$('scope-open').onclick=()=>{setPlaying(false);$('scope').showModal();};$('scope-close').onclick=()=>$('scope').close();
$('export').onclick=async()=>{
  if(operationBusy||!state)return;setBusy(true);
  try{await stopAndWait();const scope=scenario,replay=await api(route('replay',scope));
    if((scope==='objective')!==(replay.profile==='objective-experimental-v1'))throw new Error('回放模式身份不匹配，未导出。');
    const blob=new Blob([JSON.stringify(replay)],{type:'application/json'}),url=URL.createObjectURL(blob),anchor=document.createElement('a');
    anchor.href=url;anchor.download=`tactics-${scope}-tick-${state.tick}.json`;anchor.click();setTimeout(()=>URL.revokeObjectURL(url),1000);toast(`已导出 ${replay.frames.length} 帧${isObjective()?'实验目标区':'沙盒'}回放。`);
  }catch(error){toast(error.message,true);}finally{setBusy(false);}
};
$('import').onclick=async()=>{
  if(operationBusy)return;setBusy(true);
  try{await stopAndWait();pendingImportScope={scenario,version:viewVersion};$('replay-file').value='';$('replay-file').click();}
  finally{setBusy(false);}
};
$('replay-file').onchange=async event=>{
  const file=event.target.files[0],origin=pendingImportScope;event.target.value='';pendingImportScope=null;
  if(!file||operationBusy)return;
  if(origin&&(origin.scenario!==scenario||origin.version!==viewVersion)){toast('模式或状态已变化，请在当前模式重新选择回放。',true);return;}
  setBusy(true);replayStatus('正在读取并校验回放… 控制暂时锁定。','pending');
  try{await stopAndWait();const limit=isObjective()?4000000:2000000;if(file.size>limit)throw new Error(`回放超过 ${isObjective()?4:2} MB 限制。`);
    const replay=JSON.parse(await file.text());
    const expected=isObjective()?'objective-experimental-v1':'sandbox-v1';
    if(replay.profile!==expected)throw new Error('回放属于其他模式，不能交叉导入。请先切换到对应模式。');
    replayStatus('正在逐帧校验回放… 完整回放可能需要数十秒，控制暂时锁定。','pending');
    const result=await api(route('import'),replay);viewVersion++;clearEvents('回放已校验；停在最后一帧。');display(result,false);syncConfig();setPlaying(false);replayStatus(`已校验 ${result.verified_frames} 帧；停在最后一帧。`);toast(`逐帧校验通过：${result.verified_frames} 帧。`);
  }catch(error){replayStatus(`校验失败：${error.message}`,'error');toast(error.message,true);}finally{setBusy(false);}
};
async function init(){
  setBusy(true);
  try{const [initialMeta,initialState]=await Promise.all([api('meta'),api('state')]);meta=initialMeta;state=initialState;syncScenarioUI();display(initialState);$('connection').textContent='C++ 引擎在线';$('connection-dot').classList.add('connected');requestAnimationFrame(loop);}
  catch(error){$('connection').textContent='引擎连接失败';toast(`请通过本地服务打开页面：${error.message}`,true);}finally{setBusy(false);}
}
init();
