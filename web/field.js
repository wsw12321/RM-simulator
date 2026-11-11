'use strict';
// Official-field viewer: draws the extracted height field and lets one robot be
// driven through the local bridge. The C++ engine is the only authority; this
// page only renders its state. All text is inserted via textContent.
(() => {
  const $ = (id) => document.getElementById(id);
  const canvas = $('field');
  const ctx = canvas.getContext('2d');
  const TEAM = { red: '#ff705e', blue: '#60a8ef' };
  const STEP_MS = 50, STEP_TICKS = 5;           // 20 requests a second, 100 Hz engine
  const TILT = Math.tan(1.5 * Math.PI / 180);
  const ZONE_NAMES = {
    supply: '补给区', high_point: '制高点', base: '基地', fortress: '堡垒', outpost: '前哨站', assembly: '装配区',
    central_highland: '中央高地', crossing_highland_low: '地形跨越·高地(低)', crossing_road_low: '地形跨越·公路(低)',
    crossing_road_high: '地形跨越·公路(高)', tunnel_end: '隧道口', tunnel_inside: '隧道内',
  };
  let meta = null, terrain = null, background = null, state = null;
  let running = false, busy = false, selected = null, timer = null;
  const keys = new Set();
  let aim = null;                                 // field coordinates of the pointer

  async function api(path, body) {
    const options = body === undefined ? {} : {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Sandbox-Client': '1' }, body: JSON.stringify(body),
    };
    const response = await fetch(path, options);
    const value = await response.json();
    if (!response.ok) throw new Error(value.error || `HTTP ${response.status}`);
    return value;
  }
  function setStatus(text, error) {
    $('status').textContent = text;
    $('status').classList.toggle('error', Boolean(error));
  }

  function parseTerrain(buffer) {
    const view = new DataView(buffer);
    const magic = String.fromCharCode(...new Uint8Array(buffer, 0, 8));
    if (magic !== 'RMTERRN1') throw new Error('地形文件格式不符');
    const nx = view.getInt32(8, true), ny = view.getInt32(12, true), cell = view.getInt32(16, true), count = view.getInt32(20, true);
    const cells = nx * ny, base = 28;
    if (buffer.byteLength !== base + cells * 2 + count * 8) throw new Error('地形文件长度不符');
    const ground = new Int16Array(cells), overBottom = new Int16Array(cells).fill(-32768), overTop = new Int16Array(cells).fill(-32768);
    for (let k = 0; k < cells; k++) ground[k] = view.getInt16(base + 2 * k, true);
    const idx = base + 2 * cells, ob = idx + 4 * count, ot = ob + 2 * count;
    for (let k = 0; k < count; k++) {
      const c = view.getUint32(idx + 4 * k, true);
      overBottom[c] = view.getInt16(ob + 2 * k, true);
      overTop[c] = view.getInt16(ot + 2 * k, true);
    }
    return { nx, ny, cell, ground, overBottom, overTop };
  }
  function floorMm(yMm) { return -Math.abs(yMm - terrain.ny * terrain.cell / 2) * TILT; }
  const STOPS = [[0, [28, 33, 44]], [200, [44, 86, 150]], [300, [38, 128, 122]], [400, [70, 160, 84]], [800, [214, 196, 72]], [1400, [214, 96, 56]], [2600, [240, 240, 240]]];
  function colour(h) {
    if (h <= STOPS[0][0]) return STOPS[0][1];
    for (let i = 1; i < STOPS.length; i++) {
      if (h <= STOPS[i][0]) {
        const f = (h - STOPS[i - 1][0]) / (STOPS[i][0] - STOPS[i - 1][0]), a = STOPS[i - 1][1], b = STOPS[i][1];
        return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
      }
    }
    return STOPS[STOPS.length - 1][1];
  }
  function renderBackground() {
    const { nx, ny, cell, ground, overBottom } = terrain;
    const off = document.createElement('canvas');
    off.width = nx; off.height = ny;
    const octx = off.getContext('2d');
    const image = octx.createImageData(nx, ny);
    for (let j = 0; j < ny; j++) {
      const floor = floorMm((j + 0.5) * cell), row = (ny - 1 - j) * nx;   // field y points up
      for (let i = 0; i < nx; i++) {
        const k = j * nx + i, h = ground[k];
        const east = ground[j * nx + Math.min(nx - 1, i + 1)], north = ground[Math.min(ny - 1, j + 1) * nx + i];
        const shade = Math.max(0.55, Math.min(1.45, 1 - ((east - h) + (north - h)) / 70));
        const rgb = colour(h - floor);
        let r = rgb[0] * shade, g = rgb[1] * shade, b = rgb[2] * shade;
        if (overBottom[k] !== -32768 && (i + j) % 4 === 0) { r = r * 0.4 + 150; g = g * 0.4 + 150; b = b * 0.4 + 160; }   // hatch: roofed
        const p = 4 * (row + i);
        image.data[p] = r; image.data[p + 1] = g; image.data[p + 2] = b; image.data[p + 3] = 255;
      }
    }
    octx.putImageData(image, 0, 0);
    background = off;
  }

  const toCanvas = (x, y) => [x / 28 * canvas.width, (1 - y / 15) * canvas.height];
  const scale = () => canvas.width / 28;
  function draw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (background) ctx.drawImage(background, 0, 0, canvas.width, canvas.height);
    if (meta && $('show-zones').checked) {
      ctx.lineWidth = 1.5;
      ctx.font = '11px system-ui, sans-serif';
      ctx.textAlign = 'center';
      for (const zone of meta.field.zones) {
        ctx.beginPath();
        let cx = 0, cy = 0;
        zone.polygon_mm.forEach(([x, y], n) => {
          const [px, py] = toCanvas(x / 1000, y / 1000);
          cx += px; cy += py;
          if (n) ctx.lineTo(px, py); else ctx.moveTo(px, py);
        });
        ctx.closePath();
        ctx.strokeStyle = zone.team === 'red' ? 'rgba(255,150,130,.9)' : zone.team === 'blue' ? 'rgba(130,180,255,.9)' : 'rgba(240,220,90,.85)';
        ctx.fillStyle = zone.team === 'red' ? 'rgba(255,112,94,.10)' : zone.team === 'blue' ? 'rgba(96,168,239,.10)' : 'rgba(240,220,90,.08)';
        ctx.fill(); ctx.stroke();
        if ($('show-labels').checked) {
          ctx.fillStyle = 'rgba(255,255,255,.85)';
          ctx.fillText(ZONE_NAMES[zone.kind] || zone.kind, cx / zone.polygon_mm.length, cy / zone.polygon_mm.length + 4);
        }
      }
    }
    if (!state) return;
    const s = scale();
    for (const robot of state.robots) {
      const [px, py] = toCanvas(robot.x, robot.y);
      ctx.save();
      ctx.translate(px, py);
      ctx.beginPath();
      ctx.arc(0, 0, robot.radius * s, 0, 2 * Math.PI);
      ctx.strokeStyle = robot.id === selected ? '#ffffff' : 'rgba(255,255,255,.35)';
      ctx.lineWidth = robot.id === selected ? 2 : 1;
      ctx.setLineDash(robot.airborne ? [4, 3] : []);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.rotate(-robot.yaw);
      ctx.fillStyle = TEAM[robot.team];
      ctx.globalAlpha = robot.power.chassis_enabled ? 0.9 : 0.4;
      ctx.fillRect(-robot.length * s / 2, -robot.width * s / 2, robot.length * s, robot.width * s);
      ctx.globalAlpha = 1;
      ctx.fillStyle = '#10161d';
      ctx.fillRect(robot.length * s / 2 - 3, -robot.width * s / 4, 3, robot.width * s / 2);   // front mark
      ctx.rotate(robot.yaw - robot.gimbal.yaw);
      ctx.strokeStyle = '#f4f7fa';
      ctx.lineWidth = 2.5;
      ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(0.42 * s, 0); ctx.stroke();
      ctx.restore();
      ctx.fillStyle = '#ffffff';
      ctx.font = '600 11px ui-monospace, monospace';
      ctx.textAlign = 'center';
      ctx.fillText(robot.id, px, py - robot.radius * s - 5);
    }
  }

  const deg = (r) => (r * 180 / Math.PI).toFixed(1) + '°';
  function showTelemetry() {
    if (!state) return;
    $('f-time').textContent = (state.time_ms / 1000).toFixed(2) + ' s · tick ' + state.tick;
    const robot = state.robots.find((r) => r.id === selected);
    $('sel-id').textContent = robot ? `${robot.id} · ${robot.team === 'red' ? '红方' : '蓝方'} · ${robot.vehicle}` : '—';
    if (!robot) return;
    const floor = floorMm(robot.y * 1000) / 1000;
    $('t-pos').textContent = `${robot.x.toFixed(3)} / ${robot.y.toFixed(3)} m`;
    $('t-height').textContent = `${((robot.z - floor) * 1000).toFixed(0)} mm`;
    $('t-speed').textContent = `${Math.hypot(robot.vx, robot.vy).toFixed(2)} m/s · ${(robot.wz * 180 / Math.PI).toFixed(0)} °/s`;
    $('t-att').textContent = `${deg(robot.yaw)} / ${deg(robot.pitch)} / ${deg(robot.roll)}`;
    $('t-gimbal').textContent = `${deg(robot.gimbal.yaw)} / ${deg(robot.gimbal.pitch)}`;
    $('t-power').textContent = `${robot.power.measured_w.toFixed(0)} / ${robot.power.limit_w.toFixed(0)} W`;
    $('t-buffer').textContent = `${robot.power.buffer_j.toFixed(1)} J`;
    const flags = [];
    if (robot.airborne) flags.push('腾空');
    if (robot.blocked) flags.push('受阻');
    if (!robot.power.chassis_enabled) flags.push('底盘断电');
    $('t-state').textContent = flags.length ? flags.join(' · ') : '正常';
    $('t-zones').textContent = robot.zones.length ? robot.zones.join(', ') : '无';
  }

  function command() {
    let vx = 0, vy = 0, wz = 0;
    if (keys.has('KeyW') || keys.has('ArrowUp')) vx += 1;
    if (keys.has('KeyS') || keys.has('ArrowDown')) vx -= 1;
    if (keys.has('KeyA') || keys.has('ArrowLeft')) vy += 1;
    if (keys.has('KeyD') || keys.has('ArrowRight')) vy -= 1;
    if (keys.has('KeyQ')) wz += 1;
    if (keys.has('KeyE')) wz -= 1;
    const out = { chassis: { vx: vx * 3.5, vy: vy * 3.0, wz: wz * 6.0, frame: $('gimbal-frame').checked ? 'gimbal' : 'chassis' } };
    const robot = state && state.robots.find((r) => r.id === selected);
    if (aim && robot) out.gimbal = { mode: 'angle', yaw: Math.atan2(aim[1] - robot.y, aim[0] - robot.x), pitch: 0 };
    return out;
  }
  async function tick() {
    if (!running || busy || !selected) return;
    busy = true;
    try {
      state = (await api('/api/field/step', { ticks: STEP_TICKS, commands: { [selected]: command() } })).state;
      draw(); showTelemetry();
    } catch (error) {
      pause();
      setStatus(error.message, true);
    } finally { busy = false; }
  }
  function pause() {
    running = false;
    keys.clear();
    $('play').textContent = '▶ 开始';
  }
  function play() {
    running = true;
    $('play').textContent = '❚❚ 暂停';
    setStatus('运行中：100 Hz 引擎，每 50 ms 推进 5 步。');
    canvas.focus();
  }
  async function release(id) {
    // a robot that is no longer driven gets a neutral command
    if (!id) return;
    try { state = (await api('/api/field/step', { ticks: 1, commands: { [id]: { chassis: { vx: 0, vy: 0, wz: 0 }, gimbal: { mode: 'hold' } } } })).state; } catch (error) { setStatus(error.message, true); }
  }
  function fillRobots() {
    const select = $('robot');
    select.replaceChildren();
    for (const robot of state.robots) {
      const option = document.createElement('option');
      option.value = robot.id;
      option.textContent = `${robot.id}（${robot.team === 'red' ? '红' : '蓝'}）`;
      select.append(option);
    }
    if (!state.robots.some((r) => r.id === selected)) selected = state.robots[0].id;
    select.value = selected;
  }
  async function select(id) {
    if (id === selected) return;
    const previous = selected;
    selected = id;
    $('robot').value = id;
    await release(previous);
    draw(); showTelemetry();
  }
  async function reset() {
    pause();
    $('reset').disabled = true;
    try {
      state = (await api('/api/field/reset', meta.default_session)).state;
      fillRobots(); draw(); showTelemetry();
      setStatus('已重置。点击“开始”后用 WASD 行驶。');
    } catch (error) { setStatus(error.message, true); } finally { $('reset').disabled = false; }
  }

  function pointerField(event) {
    const box = canvas.getBoundingClientRect();
    return [(event.clientX - box.left) / box.width * 28, (1 - (event.clientY - box.top) / box.height) * 15];
  }
  function showProbe(x, y) {
    if (!terrain || x < 0 || y < 0 || x >= 28 || y >= 15) return;
    const i = Math.floor(x * 1000 / terrain.cell), j = Math.floor(y * 1000 / terrain.cell), k = j * terrain.nx + i;
    const floor = floorMm(y * 1000);
    $('p-pos').textContent = `${x.toFixed(2)} / ${y.toFixed(2)} m`;
    $('p-ground').textContent = `${(terrain.ground[k] - floor).toFixed(0)} mm`;
    $('p-over').textContent = terrain.overBottom[k] === -32768 ? '无'
      : `净空 ${(terrain.overBottom[k] - terrain.ground[k])} mm，顶面 ${(terrain.overTop[k] - floor).toFixed(0)} mm`;
    const inside = meta.field.zones.filter((zone) => {
      let hit = false;
      const p = zone.polygon_mm, px = x * 1000, py = y * 1000;
      for (let a = 0, b = p.length - 1; a < p.length; b = a++) {
        if ((p[a][1] > py) !== (p[b][1] > py) && px < (p[b][0] - p[a][0]) * (py - p[a][1]) / (p[b][1] - p[a][1]) + p[a][0]) hit = !hit;
      }
      return hit;
    });
    $('p-zones').textContent = inside.length ? inside.map((zone) => zone.id).join(', ') : '无';
  }

  canvas.addEventListener('pointermove', (event) => { aim = pointerField(event); showProbe(aim[0], aim[1]); });
  canvas.addEventListener('pointerleave', () => { aim = null; });
  canvas.addEventListener('pointerdown', (event) => {
    canvas.focus();
    if (!state) return;
    const [x, y] = pointerField(event);
    const hit = state.robots.find((r) => Math.hypot(r.x - x, r.y - y) <= r.radius);
    if (hit) select(hit.id);
  });
  const DRIVE_KEYS = new Set(['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight']);
  canvas.addEventListener('keydown', (event) => { if (DRIVE_KEYS.has(event.code)) { keys.add(event.code); event.preventDefault(); } });
  canvas.addEventListener('keyup', (event) => { keys.delete(event.code); });
  canvas.addEventListener('blur', () => keys.clear());
  document.addEventListener('visibilitychange', () => { if (document.hidden && running) { pause(); setStatus('页面在后台，已暂停。'); } });
  $('play').addEventListener('click', () => (running ? (pause(), setStatus('已暂停。')) : play()));
  $('reset').addEventListener('click', reset);
  $('robot').addEventListener('change', (event) => select(event.target.value));
  $('show-zones').addEventListener('change', draw);
  $('show-labels').addEventListener('change', draw);

  (async () => {
    try {
      meta = await api('/api/field/meta');
      const response = await fetch('/api/field/terrain');
      if (!response.ok) throw new Error('无法读取地形数据');
      terrain = parseTerrain(await response.arrayBuffer());
      renderBackground();
      state = await api('/api/field/state');
      $('f-id').textContent = meta.field.id;
      $('f-size').textContent = `${meta.field.size_mm[0] / 1000} × ${meta.field.size_mm[1] / 1000} m / ${meta.field.cell_mm} mm`;
      $('f-build').textContent = meta.build.slice(0, 12);
      fillRobots(); draw(); showTelemetry();
      setStatus('已连接。点击“开始”后用 WASD 行驶，鼠标控制云台指向。');
      timer = setInterval(tick, STEP_MS);
    } catch (error) { setStatus(error.message, true); }
  })();
  window.addEventListener('pagehide', () => { if (timer) clearInterval(timer); });
})();
