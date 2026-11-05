'use strict';
// RMUC 2027 rules lab viewer: runs the stateless native lab on the local bridge
// and plays back its recorded 10 Hz frames. All text is inserted via textContent.
(() => {
  const $ = (id) => document.getElementById(id);
  const FRAME_EVERY = 6; // ticks per recorded frame (10 Hz)
  const LABELS = {
    'death': '战亡', 'revived': '读条复活', 'instant-revival': '兑换立即复活', 'level-up': '升级',
    'purchase': '兑换发弹量', 'remote-heal': '远程兑换血量', 'sentry-supply': '哨兵补给区领弹',
    'assembly': '完成装配', 'heavy-stage': '重装属性阶段', '42mm-unlocked': '解锁 42mm', '42mm-installed': '安装 42mm',
    '42mm-confirmed': '42mm 确认射击', 'temporary-control': '临时控制', 'full-control': '完全控制',
    'outpost-destroyed': '前哨站被击毁', 'outpost-rebuilt': '前哨站重建', 'base-destroyed': '基地被击毁',
    'posture': '哨兵姿态', 'chassis-cutoff': '底盘断电', 'match-start': '比赛开始', 'match-end': '比赛结束',
  };
  const OUTCOME = { 'red-wins': '红方胜', 'blue-wins': '蓝方胜', 'draw': '平局', 'ongoing': '未到终局', 'unresolved': '待裁定' };
  const REASON = {
    'base-destroyed': '基地被击毁', 'timeout-control': '420 秒 · 控制度', 'timeout-attack-damage': '420 秒 · 攻击伤害',
    'timeout-surviving-robot-hp': '420 秒 · 剩余血量', 'timeout-equal': '420 秒 · 全部相等', 'not-ended': '时长未满',
  };
  const TEAM = { red: '#ff705e', blue: '#60a8ef' };
  const canvas = $('field');
  const ctx = canvas.getContext('2d');
  let result = null;
  let frame = 0;
  let playing = false;
  let lastTime = 0;
  let carry = 0;

  function value(name) { return $('config').elements[name].value; }
  function readConfig() {
    const team = (side) => ({
      infantry: [0, 1].map((k) => ({ chassis: value(`${side}-c${k}`), launcher: value(`${side}-l${k}`) })),
      sentry: value(`${side}-sentry`),
      max_assembly: Number.parseInt(value(`${side}-asm`), 10),
      gold_reserve: Number.parseInt(value(`${side}-reserve`), 10),
    });
    return {
      duration_s: Number.parseInt(value('duration'), 10),
      credits: $('config').elements.credits.checked,
      red: team('red'),
      blue: team('blue'),
    };
  }

  function setStatus(text, error = false) {
    $('status').textContent = text;
    $('status').classList.toggle('error', error);
  }

  async function run(event) {
    event.preventDefault();
    const config = readConfig();
    if (!Number.isInteger(config.duration_s) || config.duration_s < 1 || config.duration_s > 420) {
      setStatus('时长需为 1–420 秒的整数。', true);
      return;
    }
    $('run').disabled = true;
    setStatus('本机原生引擎计算中…');
    stop();
    try {
      const response = await fetch('/api/lab/run', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Sandbox-Client': '1' },
        body: JSON.stringify({ config, frame_every: FRAME_EVERY }),
      });
      const data = await response.json();
      if (!response.ok || data.error) throw new Error(data.error || `HTTP ${response.status}`);
      if (!Array.isArray(data.frames) || !data.field || data.official !== false) throw new Error('结果格式不符合 rmuc2027-rules-lab-result.v1。');
      result = data;
      frame = 0;
      $('scrub').max = String(Math.max(0, data.frames.length - 1));
      $('scrub').value = '0';
      $('result').hidden = false;
      $('hash').textContent = String(data.hash);
      $('build').textContent = data.build.slice(0, 16);
      $('outcome').textContent = OUTCOME[data.result.outcome] || data.result.outcome;
      $('reason').textContent = REASON[data.result.reason] || data.result.reason;
      setStatus(`完成：${(data.elapsed_ms / 1000).toFixed(1)} 秒，${data.events.length} 个事件，${data.frames.length} 帧。`);
      render();
      play();
    } catch (error) {
      setStatus(`运行失败：${error.message}`, true);
    } finally {
      $('run').disabled = false;
    }
  }

  function timelineAt(ms) {
    const t = result.timeline;
    let lo = 0;
    let hi = t.length - 1;
    if (hi < 0 || ms < t[0].t) return null;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (t[mid].t <= ms) lo = mid; else hi = mid - 1;
    }
    return t[lo];
  }

  const sx = (x) => x / 10;
  const sy = (y) => y / 10;
  function rect(r, stroke, fill, dash) {
    ctx.save();
    if (fill) { ctx.fillStyle = fill; ctx.fillRect(sx(r[0]), sy(r[1]), sx(r[2] - r[0]), sy(r[3] - r[1])); }
    if (stroke) { ctx.strokeStyle = stroke; ctx.setLineDash(dash || []); ctx.lineWidth = 1.5; ctx.strokeRect(sx(r[0]), sy(r[1]), sx(r[2] - r[0]), sy(r[3] - r[1])); }
    ctx.restore();
  }

  function draw(f, line) {
    const field = result.field;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#0d151e';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = '#1a2531';
    for (let x = 0; x <= field.width; x += 1000) { ctx.beginPath(); ctx.moveTo(sx(x), 0); ctx.lineTo(sx(x), canvas.height); ctx.stroke(); }
    for (let y = 0; y <= field.height; y += 1000) { ctx.beginPath(); ctx.moveTo(0, sy(y)); ctx.lineTo(canvas.width, sy(y)); ctx.stroke(); }
    field.zones.forEach((z, i) => {
      const state = line ? line.positions[i] : (i % 2 ? 'blue-full' : 'red-full');
      const owner = state.startsWith('red') ? 'red' : 'blue';
      const full = state.endsWith('full');
      rect(z.rect, TEAM[owner], `${TEAM[owner]}${full ? '26' : '12'}`, full ? [] : [6, 4]);
      ctx.fillStyle = '#9fb0bf';
      ctx.font = '11px sans-serif';
      ctx.fillText(z.id, sx(z.rect[0]) + 4, sy(z.rect[1]) + 13);
    });
    field.supply.forEach((r) => rect(r, '#4cc9c0', '#4cc9c010'));
    field.assembly.forEach((r) => rect(r, '#b38cf0', '#b38cf010'));
    ctx.fillStyle = '#2b3846';
    field.walls.forEach((w) => ctx.fillRect(sx(w[0]), sy(w[1]), sx(w[2] - w[0]), sy(w[3] - w[1])));
    field.structures.forEach((s, i) => {
      const hp = line ? (i < 2 ? line.outpost[i] : line.base[i - 2]) : (i < 2 ? 1000 : 10000);
      const owner = line && i < 2 ? line.outpost_owner[i] : (i % 2 ? 'blue' : 'red');
      ctx.beginPath();
      ctx.arc(sx(s[0]), sy(s[1]), sx(s[2]), 0, Math.PI * 2);
      ctx.fillStyle = hp > 0 ? `${TEAM[owner]}55` : '#33404d';
      ctx.fill();
      ctx.strokeStyle = TEAM[owner];
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.fillStyle = '#e9f0f5';
      ctx.font = 'bold 11px ui-monospace, monospace';
      ctx.textAlign = 'center';
      ctx.fillText(String(hp), sx(s[0]), sy(s[1]) + 4);
      ctx.textAlign = 'left';
    });
    f.p.forEach((p) => {
      ctx.fillStyle = p[3] ? '#a9d4ff' : '#ffb3a8';
      ctx.beginPath();
      ctx.arc(sx(p[0]), sy(p[1]), p[2] ? 3.5 : 1.8, 0, Math.PI * 2);
      ctx.fill();
    });
    f.r.forEach((r, i) => {
      const meta = field.robots[i];
      const alive = (r[3] & 1) === 1;
      ctx.beginPath();
      ctx.arc(sx(r[0]), sy(r[1]), sx(meta.radius), 0, Math.PI * 2);
      ctx.fillStyle = alive ? TEAM[meta.team ? 'blue' : 'red'] : '#4a5562';
      ctx.fill();
      if (r[3] & 4) { ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 2.5; ctx.stroke(); }
      else if (r[3] & 2) { ctx.strokeStyle = '#f0c46a'; ctx.lineWidth = 2.5; ctx.stroke(); }
      ctx.fillStyle = '#0c1016';
      ctx.font = 'bold 13px ui-monospace, monospace';
      ctx.textAlign = 'center';
      ctx.fillText(meta.role === 'heavy' ? 'H' : meta.role === 'sentry' ? 'S' : 'I', sx(r[0]), sy(r[1]) + 4);
      ctx.textAlign = 'left';
    });
  }

  function panels(ms, line) {
    $('clock').textContent = `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}`;
    $('frame-time').textContent = `${(ms / 1000).toFixed(1)} s`;
    if (line) {
      $('red-base').textContent = String(line.base[0]);
      $('blue-base').textContent = String(line.base[1]);
      $('red-line').textContent = `控制度 ${line.control[0]} · 攻击伤害 ${line.attack_damage[0]} · 金币 ${line.gold[0]}`;
      $('blue-line').textContent = `控制度 ${line.control[1]} · 攻击伤害 ${line.attack_damage[1]} · 金币 ${line.gold[1]}`;
    }
    const robots = $('robots');
    robots.replaceChildren();
    result.field.robots.forEach((meta, i) => {
      const stats = line ? line.robots[i] : [0, 1, 1, 1];
      const row = document.createElement('div');
      row.className = `robot ${meta.team ? 'blue' : 'red'}${stats[3] ? '' : ' dead'}`;
      const badge = document.createElement('b');
      badge.textContent = meta.role === 'heavy' ? 'H' : meta.role === 'sentry' ? 'S' : 'I';
      const middle = document.createElement('div');
      middle.textContent = meta.id;
      const bar = document.createElement('div');
      bar.className = 'bar';
      const fill = document.createElement('i');
      fill.style.width = `${Math.max(0, Math.min(100, (100 * stats[0]) / Math.max(1, stats[1])))}%`;
      bar.append(fill);
      middle.append(bar);
      const info = document.createElement('span');
      info.className = 'meta';
      info.textContent = `Lv${stats[2]} ${stats[0]}/${stats[1]}`;
      row.append(badge, middle, info);
      robots.append(row);
    });
    const positions = $('positions');
    positions.replaceChildren();
    result.field.zones.forEach((z, i) => {
      const state = line ? line.positions[i] : '';
      const cell = document.createElement('div');
      cell.className = state.startsWith('red') ? 'red' : 'blue';
      cell.textContent = `${z.id}：${state.startsWith('red') ? '红' : '蓝'}方${state.endsWith('full') ? '完全' : '临时'}控制`;
      positions.append(cell);
    });
    const list = $('events');
    list.replaceChildren();
    const visible = result.events.filter((e) => e.t <= ms && e.kind !== 'posture' && e.kind !== 'purchase');
    $('event-count').textContent = `${visible.length} 条（不含姿态 / 兑换）`;
    visible.slice(-80).reverse().forEach((e) => {
      const item = document.createElement('li');
      const time = document.createElement('time');
      time.textContent = `${(e.t / 1000).toFixed(1)}s`;
      item.append(time, `${LABELS[e.kind] || e.kind} · ${e.subject}${e.detail ? ` · ${e.detail}` : ''}`);
      list.append(item);
    });
  }

  function render() {
    if (!result || !result.frames.length) return;
    const f = result.frames[frame];
    const ms = Math.round((f.tick * 1000) / 60);
    const line = timelineAt(ms);
    draw(f, line);
    panels(ms, line);
    $('scrub').value = String(frame);
  }

  function loop(now) {
    if (!playing) return;
    const speed = Number.parseInt($('speed').value, 10);
    carry += ((now - lastTime) / 1000) * 10 * speed; // 10 recorded frames per match second
    lastTime = now;
    const step = Math.floor(carry);
    if (step > 0) {
      carry -= step;
      frame = Math.min(result.frames.length - 1, frame + step);
      render();
      if (frame >= result.frames.length - 1) { stop(); return; }
    }
    requestAnimationFrame(loop);
  }
  function play() {
    if (!result) return;
    if (frame >= result.frames.length - 1) frame = 0;
    playing = true;
    carry = 0;
    lastTime = performance.now();
    $('play').textContent = '❚❚ 暂停';
    requestAnimationFrame(loop);
  }
  function stop() {
    playing = false;
    $('play').textContent = '▶ 播放';
  }

  $('config').addEventListener('submit', run);
  $('play').addEventListener('click', () => (playing ? stop() : play()));
  $('scrub').addEventListener('input', () => {
    frame = Number.parseInt($('scrub').value, 10) || 0;
    render();
  });
  document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
})();
