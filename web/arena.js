'use strict';
// This page is a stateless geometry workbench. It never touches simulation APIs.
const $ = id => document.getElementById(id);
const MAX_ARENA_BYTES = 256 * 1024;
const editor = $('arena-json');
const canvas = $('arena-map');
const ctx = canvas.getContext('2d');
const encoder = new TextEncoder();
let draftRevision = 0;
let validatedRevision = -1;
let lastVerifiedRevision = -1;
let validatedDocument = null;
let validatedText = '';
let sourceGeneration = 0;
let sourcePending = false;
let pickerSnapshot = null;
let validationGeneration = 0;
let validationPending = false;
let queryGeneration = 0;
let queryPending = false;
let queryResult = null;
let mapTransform = null;

const isCurrent = () => !!validatedDocument && draftRevision === validatedRevision;
const canUseDocument = () => isCurrent() && !sourcePending && !validationPending;
const numberText = value => value == null ? '无上限记录' : Number(value).toLocaleString('zh-CN', {maximumFractionDigits: 3});
function node(tag, text, className) {
  const element = document.createElement(tag);
  if (text !== undefined) element.textContent = text;
  if (className) element.className = className;
  return element;
}
function operation(message, error = false) {
  $('operation-status').textContent = message;
  $('operation-status').className = 'operation-status' + (error ? ' error' : '');
}
function issues(id, list) {
  $(id).replaceChildren(...list.map(item => node('li', typeof item === 'string' ? item : `${item.path} · ${item.code} · ${item.message}`)));
}
function resetQuery(message = '探针或场地已变化，请重新检查。') {
  queryGeneration++;
  queryPending = false;
  queryResult = null;
  $('query-status').textContent = message;
  $('query-status').className = 'query-status';
  $('query-summary').hidden = true;
  $('query-summary').replaceChildren();
  $('query-evidence').replaceChildren();
  $('query-details').hidden = true;
  $('query-raw').textContent = '';
}
function updateControls() {
  const usable = canUseDocument();
  $('validate-arena').disabled = !editor.value.trim() || validationPending || sourcePending;
  $('export-arena').disabled = !usable;
  $('format-json').disabled = !usable;
  $('query-arena').disabled = !usable || queryPending;
  for (const id of ['layer-select', 'surface-select', 'slice-height', 'probe-x', 'probe-y', 'probe-radius', 'probe-height', 'probe-slope']) $(id).disabled = !usable;
  $('draft-version').textContent = `草稿 #${draftRevision}`;
  $('calibration-version').textContent = validatedDocument ? `已验证快照 #${lastVerifiedRevision}${isCurrent() ? '' : '（旧）'}` : '未验证';
  const status = $('document-status');
  status.className = 'state-pill';
  if (sourcePending) status.textContent = '正在读取 · 可继续编辑取消';
  else if (validationPending) status.textContent = `验证中 · 草稿 #${draftRevision}`;
  else if (isCurrent()) { status.textContent = `已验证 · 草稿 #${draftRevision}`; status.className += ' valid'; }
  else status.textContent = validatedDocument ? '草稿已变更 · 预览停用' : '尚无已验证场地';
  const veil = $('preview-veil');
  veil.hidden = usable;
  if (!usable) veil.replaceChildren(node('strong', sourcePending ? '正在读取新的草稿' : validationPending ? '正在验证当前草稿' : validatedDocument ? '草稿尚未通过验证，预览已停用' : '先验证场地 JSON'), node('span', validatedDocument ? `背景为上一次验证的草稿 #${lastVerifiedRevision}，不能用于当前草稿。查询、格式化和导出已停用。` : '预览、查询和导出始终对应同一份已验证数据。'));
}
function changeDraft(text, message) {
  sourceGeneration++;
  sourcePending = false;
  validationGeneration++;
  validationPending = false;
  draftRevision++;
  if (text !== undefined) editor.value = text;
  resetQuery('当前草稿尚未验证，查询结果已清除。');
  $('validation-status').textContent = `草稿 #${draftRevision} 尚未验证。`;
  issues('validation-errors', []);
  issues('validation-warnings', []);
  if (message) operation(message);
  else operation('草稿已修改。重新验证后才能预览、查询或导出；旧的导出状态已清除。');
  updateControls();
  drawMap();
}
function requireSize(text) {
  if (encoder.encode(text).length > MAX_ARENA_BYTES) throw new Error('场地 JSON 超过 256 KiB UTF-8 限制。请缩小文件后重试。');
}
async function api(path, data) {
  const response = await fetch(path, data === undefined ? {} : {method: 'POST', headers: {'Content-Type': 'application/json', 'X-Sandbox-Client': '1'}, body: JSON.stringify(data)});
  let result;
  try { result = await response.json(); } catch (_) { throw new Error(`本机服务返回了非 JSON 响应（HTTP ${response.status}）。`); }
  if (!response.ok) {
    const error = new Error(result.error || `请求失败（HTTP ${response.status}）。`);
    error.issues = result.errors || [];
    throw error;
  }
  return result;
}
function refreshSelects() {
  const layerValue = $('layer-select').value;
  const surfaceValue = $('surface-select').value;
  const option = (value, label) => { const item = node('option', label); item.value = value; return item; };
  $('layer-select').replaceChildren(option('', '全部层'), ...validatedDocument.layers.map(layer => option(layer.id, `${layer.name} · ${layer.id}`)));
  $('layer-select').value = validatedDocument.layers.some(layer => layer.id === layerValue) ? layerValue : '';
  $('surface-select').replaceChildren(option('', '比较全部候选面（不自动选层）'), ...validatedDocument.surfaces.map(surface => option(surface.id, `${surface.id} · ${surface.layer_id}`)));
  $('surface-select').value = validatedDocument.surfaces.some(surface => surface.id === surfaceValue) ? surfaceValue : '';
}
function renderMetadata() {
  const data = validatedDocument;
  const dl = node('dl');
  const pair = (key, value, className) => dl.append(node('dt', key), node('dd', value, className));
  pair('场地', `${data.metadata.name} (${data.metadata.id})`);
  pair('说明', data.metadata.description);
  pair('正式赛', '否 · 实验数据，不是官方几何', 'blocked');
  pair('标定状态', ({synthetic: '合成样例', unverified: '未经核验', partial: '部分标定', calibrated: '自声明已标定，尚非官方认证'})[data.calibration.status]);
  pair('标定记录', data.calibration.notes.join('；'));
  pair('坐标', `${data.frame.units} · ${data.frame.handedness} · 向上 ${data.frame.up_axis}`);
  pair('轴向 / 原点', `x: ${data.frame.x_axis}；y: ${data.frame.y_axis}；${data.frame.origin}`);
  pair('对象数量', `${data.layers.length} 层 / ${data.surfaces.length} 承载面 / ${data.collision_volumes.length} 碰撞体 / ${data.trigger_volumes.length} 触发体 / ${data.transitions.length} 转换口`);
  for (const source of data.sources) pair(`来源 ${source.id}`, `${source.kind} · ${source.reference}${source.sha256 ? `；SHA-256 ${source.sha256}` : '；未提供 SHA-256'}；${source.notes}`);
  $('calibration-info').replaceChildren(dl);
  $('map-identity').textContent = data.metadata.id;
}
async function validateDraft() {
  if (sourcePending || validationPending || !editor.value.trim()) return;
  const revision = draftRevision;
  const text = editor.value;
  const token = ++validationGeneration;
  validationPending = true;
  resetQuery('正在验证场地；旧查询结果已清除。');
  issues('validation-errors', []);
  issues('validation-warnings', []);
  $('validation-status').textContent = `正在验证草稿 #${revision}…`;
  operation('正在执行严格 JSON 与分层几何校验…');
  updateControls();
  try {
    requireSize(text);
    // Send the original string: parsing in the browser would erase duplicate keys.
    const result = await api('/api/arena/validate', {text});
    if (token !== validationGeneration || revision !== draftRevision) return;
    if (result.valid !== true || !result.document) throw new Error('服务未返回已验证场地。');
    validatedDocument = result.document;
    validatedText = text;
    validatedRevision = revision;
    lastVerifiedRevision = revision;
    refreshSelects();
    renderMetadata();
    $('validation-status').textContent = `草稿 #${revision} 验证通过。格式与几何约束有效；来源和实际尺寸仍需人工核验。`;
    issues('validation-warnings', result.warnings || ['合成 / 自声明几何不能作为官方场地。此工具未接入战斗沙盒或目标区模式。']);
    resetQuery('可点击图面设置位置，再检查圆柱探针。');
    operation(`草稿 #${revision} 验证通过。现在可查询或导出；尚未保存到磁盘。`);
  } catch (error) {
    if (token !== validationGeneration || revision !== draftRevision) return;
    validatedRevision = -1;
    $('validation-status').textContent = `草稿 #${revision} 验证失败。修改以下字段后重试。`;
    issues('validation-errors', error.issues?.length ? error.issues : [{path: '$', code: 'request', message: error.message}]);
    operation(`验证失败：${error.message}`, true);
    resetQuery('场地验证失败，查询已停用。');
  } finally {
    if (token === validationGeneration && revision === draftRevision) { validationPending = false; updateControls(); drawMap(); }
  }
}
function beginSource() {
  const token = ++sourceGeneration;
  sourcePending = true;
  validationGeneration++;
  validationPending = false;
  resetQuery('正在读取场地；旧查询结果已清除。');
  updateControls();
  return {token, revision: draftRevision};
}
function sourceIsCurrent(snapshot) { return snapshot.token === sourceGeneration && snapshot.revision === draftRevision; }
async function loadSample() {
  const snapshot = beginSource();
  operation('正在读取合成桥面 / 桥下样例。编辑草稿会取消这次载入。');
  try {
    const sample = await api('/api/arena/sample');
    if (!sourceIsCurrent(snapshot)) return;
    const text = JSON.stringify(sample, null, 2);
    requireSize(text);
    changeDraft(text, '已载入合成样例，正在验证。它不是官方场地。');
    await validateDraft();
  } catch (error) {
    if (!sourceIsCurrent(snapshot)) return;
    sourcePending = false;
    operation(`样例载入失败，原草稿保留：${error.message}`, true);
    updateControls();
  }
}
async function importFile(event) {
  const file = event.target.files?.[0];
  const openedFor = pickerSnapshot;
  pickerSnapshot = null;
  event.target.value = '';
  if (!file) return;
  if (openedFor && !sourceIsCurrent(openedFor)) {
    operation('文件选择期间草稿已变化，未覆盖当前内容。请重新点击“导入 JSON”选择文件。', true);
    return;
  }
  const snapshot = beginSource();
  operation(`正在读取 ${file.name || 'JSON 文件'}；编辑草稿会取消这次导入。`);
  try {
    if (file.size > MAX_ARENA_BYTES) throw new Error('文件超过 256 KiB 限制。');
    const text = await file.text();
    if (!sourceIsCurrent(snapshot)) return;
    requireSize(text);
    changeDraft(text, `已导入 ${file.name || 'JSON 文件'}，正在验证。`);
    await validateDraft();
  } catch (error) {
    if (!sourceIsCurrent(snapshot)) return;
    sourcePending = false;
    operation(`导入失败，原草稿保留：${error.message}`, true);
    updateControls();
  }
}
function exportDocument() {
  if (!canUseDocument()) return;
  let text = JSON.stringify(validatedDocument, null, 2) + '\n';
  if (encoder.encode(text).length > MAX_ARENA_BYTES) text = JSON.stringify(validatedDocument);
  try {
    requireSize(text);
    const url = URL.createObjectURL(new Blob([text], {type: 'application/json;charset=utf-8'}));
    try {
      const anchor = node('a');
      anchor.href = url;
      anchor.download = `${validatedDocument.metadata.id}.json`;
      document.body.append(anchor);
      anchor.click();
      anchor.remove();
    } finally { setTimeout(() => URL.revokeObjectURL(url), 1000); }
    operation(`已请求下载草稿 #${validatedRevision} 的已验证 JSON。请在浏览器下载记录确认文件已保存。`);
  } catch (error) { operation(`导出失败：${error.message}`, true); }
}
function formatDocument() {
  if (!canUseDocument()) return;
  const text = JSON.stringify(validatedDocument, null, 2);
  try { requireSize(text); } catch (error) { operation(`格式化会超过文件限制，原文保留：${error.message}`, true); return; }
  changeDraft(text, '已格式化已验证数据，正在重新验证。');
  return validateDraft();
}
function readProbe() {
  const result = {};
  for (const [id, field] of [['probe-x', 'x_mm'], ['probe-y', 'y_mm'], ['probe-radius', 'radius_mm'], ['probe-height', 'height_mm'], ['probe-slope', 'max_slope_deg']]) {
    const raw = $(id).value.trim();
    const value = Number(raw);
    if (!raw || !Number.isFinite(value)) throw new Error(`${field} 必须是有限数字，不能留空。`);
    result[field] = value;
  }
  result.surface_id = $('surface-select').value || null;
  return result;
}
function evidenceGroup(title, lines) {
  const group = node('section', undefined, 'evidence-group');
  group.append(node('h3', title));
  for (const text of lines) group.append(node('p', text, 'evidence-value'));
  return group;
}
function renderQuery(result) {
  const candidates = result.candidates;
  const selected = result.selected_surface_id;
  const badges = [];
  if (!selected) badges.push(node('span', '未选层 · 逐面比较', 'result-badge trigger'));
  else badges.push(node('span', result.feasible ? '所选承载面：几何可容纳' : '所选承载面：不可容纳', 'result-badge' + (result.feasible ? '' : ' warn')));
  badges.push(node('span', `${candidates.length} 个候选面`, 'result-badge'));
  $('query-summary').replaceChildren(...badges);
  $('query-summary').hidden = false;
  const groups = [];
  for (const candidate of candidates) {
    const group = evidenceGroup(`${candidate.surface_id} · ${candidate.layer_id}${candidate.surface_id === selected ? ' · 已选' : ''}`, [
      `几何可容纳：${candidate.feasible ? '是' : '否'}`,
      `地面 z：${numberText(candidate.support_z_mm)} mm`,
      `足迹高度范围：${candidate.support_range_mm.map(numberText).join(' ~ ')} mm`,
      `坡度：${numberText(candidate.slope_deg)}°`,
      `可用净高：${candidate.clearance_mm == null ? '未设置上限（非已核验无限空间）' : numberText(candidate.clearance_mm) + ' mm'}`,
      `机器人顶部余量：${candidate.clearance_mm == null ? '无法核验' : numberText(candidate.clearance_mm - result.robot.height_mm) + ' mm（恰好接触也阻挡）'}`,
      `实体碰撞：${candidate.collision_ids.length ? candidate.collision_ids.join(', ') : '无'}`,
      `逻辑触发：${candidate.trigger_ids.length ? candidate.trigger_ids.join(', ') : '无'}（不作实体阻挡）`,
      `转换口：${candidate.transition_ids.length ? candidate.transition_ids.join(', ') : '无'}（不表示路径可通行）`
    ]);
    if (candidate.reasons.length) {
      const list = node('ul');
      for (const reason of candidate.reasons) list.append(node('li', `${reason.code}: ${reason.message}`));
      group.append(list);
    }
    groups.push(group);
  }
  if (!candidates.length) groups.push(evidenceGroup('无候选承载面', ['当前中心不在可查询的承载面中，或不在所选承载面中。'])) ;
  if (result.warnings?.length) groups.push(evidenceGroup('查询边界', result.warnings));
  $('query-evidence').replaceChildren(...groups);
  $('query-details').hidden = false;
  $('query-raw').textContent = JSON.stringify(result, null, 2);
}
async function queryArena() {
  if (!canUseDocument() || queryPending) return;
  const token = ++queryGeneration;
  const revision = draftRevision;
  queryPending = true;
  queryResult = null;
  $('query-summary').hidden = true;
  $('query-evidence').replaceChildren();
  $('query-details').hidden = true;
  $('query-status').textContent = '正在检查当前位置…';
  $('query-status').className = 'query-status';
  updateControls();
  try {
    const probe = readProbe();
    const result = await api('/api/arena/query', {text: validatedText, probe});
    if (token !== queryGeneration || revision !== draftRevision || !isCurrent()) return;
    queryResult = result;
    renderQuery(result);
    $('query-status').textContent = `草稿 #${revision} · (${numberText(probe.x_mm)}, ${numberText(probe.y_mm)}) mm · 静态几何检查完成。`;
  } catch (error) {
    if (token !== queryGeneration || revision !== draftRevision) return;
    $('query-status').textContent = `查询失败：${error.message}${error.issues?.length ? ' ' + error.issues.map(item => `${item.path} · ${item.message}`).join('；') : ''}`;
    $('query-status').className = 'query-status error';
  } finally {
    if (token === queryGeneration && revision === draftRevision) { queryPending = false; updateControls(); drawMap(); }
  }
}
function viewBounds(data) {
  const xs = data.boundary_mm.map(point => point[0]);
  const ys = data.boundary_mm.map(point => point[1]);
  const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
  const scale = Math.min((canvas.width - 120) / (maxX - minX), (canvas.height - 100) / (maxY - minY));
  return {minX, maxX, minY, maxY, scale, left: (canvas.width - (maxX - minX) * scale) / 2, top: (canvas.height - (maxY - minY) * scale) / 2};
}
function screen(point) { return [mapTransform.left + (point[0] - mapTransform.minX) * mapTransform.scale, canvas.height - mapTransform.top - (point[1] - mapTransform.minY) * mapTransform.scale]; }
function polygon(points, fill, stroke, dash = [], width = 1.5) {
  ctx.beginPath();
  points.forEach((point, index) => { const p = screen(point); index ? ctx.lineTo(...p) : ctx.moveTo(...p); });
  ctx.closePath();
  if (fill) { ctx.fillStyle = fill; ctx.fill(); }
  if (stroke) { ctx.setLineDash(dash); ctx.lineWidth = width; ctx.strokeStyle = stroke; ctx.stroke(); ctx.setLineDash([]); }
}
function drawMap() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#09131d'; ctx.fillRect(0, 0, canvas.width, canvas.height);
  if (!validatedDocument) { mapTransform = null; return; }
  const data = validatedDocument;
  mapTransform = viewBounds(data);
  const t = mapTransform;
  const selectedLayer = $('layer-select').value;
  const selectedSurface = $('surface-select').value;
  const rawSlice = $('slice-height').value.trim();
  const slice = rawSlice ? Number(rawSlice) : NaN;
  const gridStep = Math.max(1000, Math.pow(10, Math.floor(Math.log10(Math.max(t.maxX - t.minX, t.maxY - t.minY))) - 1));
  ctx.font = '11px ui-monospace, monospace'; ctx.lineWidth = 1;
  for (let x = Math.ceil(t.minX / gridStep) * gridStep; x <= t.maxX; x += gridStep) {
    const a = screen([x, t.minY]), b = screen([x, t.maxY]);
    ctx.beginPath(); ctx.moveTo(...a); ctx.lineTo(...b); ctx.strokeStyle = '#1a2a36'; ctx.stroke();
    ctx.fillStyle = '#6d8799'; ctx.textAlign = 'center'; ctx.fillText(String(x), a[0], a[1] + 21);
  }
  for (let y = Math.ceil(t.minY / gridStep) * gridStep; y <= t.maxY; y += gridStep) {
    const a = screen([t.minX, y]), b = screen([t.maxX, y]);
    ctx.beginPath(); ctx.moveTo(...a); ctx.lineTo(...b); ctx.strokeStyle = '#1a2a36'; ctx.stroke();
    ctx.fillStyle = '#6d8799'; ctx.textAlign = 'right'; ctx.fillText(String(y), a[0] - 10, a[1] + 4);
  }
  polygon(data.boundary_mm, null, '#5a7486', [], 2);
  const colors = ['#73c8d7', '#e8cb82', '#a9b1ff', '#b4db94', '#edb7d5'];
  const surfaces = data.surfaces.filter(surface => !selectedLayer || surface.layer_id === selectedLayer);
  if ($('show-surfaces').checked) for (const surface of surfaces) {
    const index = data.layers.findIndex(layer => layer.id === surface.layer_id);
    const color = colors[index % colors.length];
    polygon(surface.polygon_mm, color + (surface.id === selectedSurface ? '40' : '1b'), color, [], surface.id === selectedSurface ? 4 : 1.5);
    const center = surface.polygon_mm.reduce((p, v) => [p[0] + v[0] / surface.polygon_mm.length, p[1] + v[1] / surface.polygon_mm.length], [0, 0]);
    const p = screen(center);
    const height = surface.height.z_mm + surface.height.dx * center[0] + surface.height.dy * center[1];
    ctx.textAlign = 'center'; ctx.font = '12px ui-monospace, monospace';
    ctx.fillStyle = '#09131de0'; ctx.fillRect(p[0] - 105, p[1] - 23, 210, 36);
    ctx.fillStyle = color; ctx.fillText(surface.id, p[0], p[1] - 9);
    ctx.font = '10px ui-monospace, monospace'; ctx.fillStyle = '#b8cbd6'; ctx.fillText(`z ${numberText(height)} mm${surface.traversable ? '' : ' · 禁止通行'}`, p[0], p[1] + 6);
  }
  const inSlice = volume => Number.isFinite(slice) && slice >= volume.z_min_mm && slice <= volume.z_max_mm;
  const collisions = data.collision_volumes.filter(inSlice);
  const triggers = data.trigger_volumes.filter(inSlice);
  if ($('show-collisions').checked) for (const volume of collisions) polygon(volume.polygon_mm, '#fb897946', '#fb8979', [], 2.2);
  if ($('show-triggers').checked) for (const volume of triggers) polygon(volume.polygon_mm, '#c3a1f418', '#c3a1f4', [8, 5], 2.5);
  let probe;
  try { probe = readProbe(); } catch (_) { probe = null; }
  if (probe && probe.radius_mm >= 0) {
    const p = screen([probe.x_mm, probe.y_mm]);
    const radius = Math.max(3, probe.radius_mm * t.scale);
    ctx.beginPath(); ctx.arc(...p, radius, 0, Math.PI * 2); ctx.fillStyle = '#aee17a22'; ctx.fill();
    ctx.strokeStyle = '#c5f295'; ctx.lineWidth = 2; ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p[0] - 9, p[1]); ctx.lineTo(p[0] + 9, p[1]); ctx.moveTo(p[0], p[1] - 9); ctx.lineTo(p[0], p[1] + 9); ctx.stroke();
  }
  ctx.fillStyle = '#8ba6b8'; ctx.textAlign = 'left'; ctx.font = '11px ui-monospace, monospace';
  ctx.fillText('↑ +y     → +x    [mm]', 20, 23);
  $('view-status').textContent = Number.isFinite(slice) ? `切片 z = ${numberText(slice)} mm：${collisions.length} 个实体、${triggers.length} 个触发体；承载面 ${surfaces.length} 个。显示过滤不改变查询。` : '请输入有限的切片高度。切片无效时不显示实体 / 触发体；查询仍由承载面决定。';
}
function moveProbe(event) {
  if (!canUseDocument() || !mapTransform) return;
  const rect = canvas.getBoundingClientRect();
  const px = (event.clientX - rect.left) * canvas.width / rect.width;
  const py = (event.clientY - rect.top) * canvas.height / rect.height;
  const t = mapTransform;
  $('probe-x').value = String(Math.round(t.minX + (px - t.left) / t.scale));
  $('probe-y').value = String(Math.round(t.minY + (canvas.height - t.top - py) / t.scale));
  resetQuery('已更新探针位置，请点击“检查当前位置”。');
  updateControls(); drawMap();
}
editor.oninput = () => changeDraft();
$('load-sample').onclick = loadSample;
$('import-arena').onclick = () => { pickerSnapshot = {token: sourceGeneration, revision: draftRevision}; $('arena-file').click(); };
$('arena-file').onchange = importFile;
$('arena-file').oncancel = () => { pickerSnapshot = null; };
$('validate-arena').onclick = validateDraft;
$('export-arena').onclick = exportDocument;
$('format-json').onclick = formatDocument;
$('query-arena').onclick = queryArena;
$('surface-select').onchange = () => { resetQuery(); updateControls(); drawMap(); };
for (const id of ['probe-x', 'probe-y', 'probe-radius', 'probe-height', 'probe-slope']) $(id).oninput = () => { resetQuery(); updateControls(); drawMap(); };
for (const id of ['layer-select', 'slice-height', 'show-surfaces', 'show-collisions', 'show-triggers']) $(id).onchange = drawMap;
canvas.onclick = moveProbe;
updateControls(); drawMap();
loadSample();
