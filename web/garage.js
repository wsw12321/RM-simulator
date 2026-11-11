'use strict';
// Garage: edit rmuc-vehicle.v1 documents and keep them on this machine.
// The form is generated from the engine's parameter table; the engine validates.
// All text is inserted via textContent.
(() => {
  const $ = (id) => document.getElementById(id);
  const el = (tag, props = {}, ...children) => {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(props)) {
      if (k === 'class') node.className = v; else if (k === 'text') node.textContent = v; else node[k] = v;
    }
    node.append(...children);
    return node;
  };
  const SECTION_ORDER = ['', 'rules', 'chassis', 'gimbal', 'launcher', 'sensors.camera', 'sensors.lidar'];
  let schema = null, labels = null, list = [], current = null;   // current: {id, readonly, vehicle}
  let checkTimer = null, checkSeq = 0, busy = false, defaultId = null;
  const inputs = new Map();     // "section.key" -> input element
  const armorRows = new Map();  // face -> {on, size, height, offset}

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
  const pathOf = (f) => (f.section ? `${f.section}.${f.key}` : f.key);
  const choiceLabel = (c) => labels.choices[c] || c;

  function buildForm() {
    const form = $('form');
    form.replaceChildren();
    for (const section of SECTION_ORDER) {
      const fields = schema.fields.filter((f) => f.section === section);
      const group = el('section', { class: 'group' }, el('h3', { text: labels.sections[section] || section }));
      if (labels.section_help[section]) group.append(el('p', { class: 'help', text: labels.section_help[section] }));
      const grid = el('div', { class: 'fields' });
      for (const f of fields) {
        const [label, help] = labels.fields[pathOf(f)] || [f.key, ''];
        let input;
        if (f.type === 'boolean') {
          input = el('input', { type: 'checkbox' });
          grid.append(el('label', { class: 'field check' }, input, el('span', { text: label }), el('small', { text: help })));
        } else if (f.type === 'enum') {
          input = el('select', {}, ...f.choices.map((c) => el('option', { value: c, text: choiceLabel(c) })));
          grid.append(el('label', { class: 'field' }, el('span', { text: label }), input, el('small', { text: help })));
        } else {
          input = el('input', { type: 'number', min: String(f.min), max: String(f.max), step: f.type === 'integer' ? '1' : 'any' });
          const range = `${f.min} ~ ${f.max}${help ? ' · ' + help : ''}`;
          grid.append(el('label', { class: 'field' }, el('span', { text: label }),
            el('span', { class: 'row' }, input, el('span', { class: 'unit', text: f.unit })), el('small', { text: range })));
        }
        input.addEventListener('input', changed);
        inputs.set(pathOf(f), input);
      }
      group.append(grid);
      form.append(group);
    }
    // armour plates: one optional plate per face
    const body = el('tbody');
    for (const face of schema.armor.faces) {
      const row = {
        on: el('input', { type: 'checkbox' }),
        size: el('select', {}, ...Object.keys(schema.armor.sizes_mm).map((s) => el('option', { value: s, text: `${choiceLabel(s)} ${schema.armor.sizes_mm[s].join(' × ')}` }))),
        height: el('input', { type: 'number', step: 'any' }),
        offset: el('input', { type: 'number', step: 'any' }),
      };
      for (const input of Object.values(row)) input.addEventListener('input', changed);
      armorRows.set(face, row);
      body.append(el('tr', {}, el('td', {}, el('label', { class: 'field check' }, row.on, el('span', { text: choiceLabel(face) + '侧' }))),
        el('td', {}, row.size), el('td', {}, row.height), el('td', {}, row.offset)));
    }
    const table = el('table', { class: 'armor' }, el('thead', {}, el('tr', {}, el('th', { text: '安装' }), el('th', { text: '规格 (mm)' }),
      el('th', { text: '中心离地高度 (mm)' }), el('th', { text: '沿该侧面的横向偏移 (mm)' }))), body);
    form.append(el('section', { class: 'group' }, el('h3', { text: labels.sections.armor }), el('p', { class: 'help', text: labels.section_help.armor }), table));
  }

  function getPath(doc, path) { return path.split('.').reduce((o, k) => (o == null ? undefined : o[k]), doc); }
  function setPath(doc, path, value) {
    const keys = path.split('.');
    let o = doc;
    for (const k of keys.slice(0, -1)) o = o[k] = o[k] || {};
    o[keys[keys.length - 1]] = value;
  }
  function fill(vehicle) {
    $('name').value = vehicle.name;
    $('notes').value = vehicle.notes;
    for (const f of schema.fields) {
      const input = inputs.get(pathOf(f)), value = getPath(vehicle, pathOf(f));
      if (f.type === 'boolean') input.checked = Boolean(value); else input.value = value;
      input.classList.remove('invalid');
    }
    for (const [face, row] of armorRows) {
      const plate = vehicle.armor.find((p) => p.face === face);
      row.on.checked = Boolean(plate);
      row.size.value = plate ? plate.size : 'small';
      row.height.value = plate ? plate.center_height_mm : 150;
      row.offset.value = plate ? plate.offset_mm : 0;
    }
  }
  function collect() {
    const doc = { format: schema.format, name: $('name').value, notes: $('notes').value };
    for (const f of schema.fields) {
      const input = inputs.get(pathOf(f));
      let value;
      if (f.type === 'boolean') value = input.checked;
      else if (f.type === 'enum') value = input.value;
      else value = input.value.trim() === '' ? null : Number(input.value);
      setPath(doc, pathOf(f), value);
    }
    doc.armor = [];
    for (const [face, row] of armorRows) {
      if (row.on.checked) doc.armor.push({ face, size: row.size.value, center_height_mm: Number(row.height.value), offset_mm: Number(row.offset.value) });
    }
    return doc;
  }

  function showAttributes(rows) {
    const body = $('attributes').tBodies[0];
    body.replaceChildren();
    for (const r of rows) {
      body.append(el('tr', {}, ...[`${r.level}`, `${r.max_hp}`, `${r.chassis_power_limit_w} W`, r.heat_limit ? `${r.heat_limit}` : '—',
        r.heat_limit ? `${r.cooling_per_s}` : '—', r.muzzle_speed_limit_mps ? `${r.muzzle_speed_limit_mps} m/s` : '—', `${r.initial_allowance}`]
        .map((text) => el('td', { text }))));
    }
    const cls = inputs.get('class').value;
    $('derived-note').textContent = cls === 'infantry' ? '步兵随等级变化（表 5-17 / 5-18），这里列出 1、5、10 级'
      : cls === 'heavy' ? '重装不使用经验体系（表 5-16）；装上 42mm 发射机构后的属性' : '哨兵不使用经验体系（表 3-6）';
  }
  function updateButtons(valid) {
    const builtin = !current || current.readonly;
    $('save').disabled = busy || builtin || !valid;
    $('save-as').disabled = busy || !valid;
    $('export').disabled = busy || !valid;
    $('delete').disabled = busy || builtin;
    $('save').title = builtin ? '内置车辆只读，请另存为新车辆' : '';
  }
  function changed() {
    clearTimeout(checkTimer);
    checkTimer = setTimeout(validate, 250);
  }
  async function validate() {
    const seq = ++checkSeq;
    try {
      const out = await api('/api/field/vehicles/check', { vehicle: collect() });
      if (seq !== checkSeq) return null;
      showAttributes(out.attributes);
      const dirty = current && out.hash !== current.hash;
      setStatus(dirty ? '参数有效 · 有未保存的修改' : '参数有效');
      updateButtons(true);
      return out;
    } catch (error) {
      if (seq !== checkSeq) return null;
      setStatus(`参数无效：${error.message}`, true);
      updateButtons(false);
      return null;
    }
  }

  function renderList() {
    const ul = $('vehicles');
    ul.replaceChildren();
    for (const v of list) {
      const button = el('button', { type: 'button' });
      if (v.invalid) {
        button.append(el('span', { text: v.id }), el('span', { class: 'tag', text: '失效' }), el('small', { text: v.invalid }));
        button.classList.add('broken');
        button.disabled = true;
      } else {
        button.append(el('span', { text: v.name }), el('span', { class: v.readonly ? 'tag' : 'tag saved', text: v.readonly ? '内置' : '已保存' }),
          el('small', { text: `${choiceLabel(v.class)} · ${v.attributes.max_hp} 血 · ${v.attributes.chassis_power_limit_w} W` }));
        button.addEventListener('click', () => select(v.id));
      }
      if (current && v.id === current.id) button.classList.add('selected');
      ul.append(el('li', {}, button));
    }
  }
  async function refreshList() {
    const out = await api('/api/field/vehicles');
    list = out.vehicles;
    defaultId = out.default;
    renderList();
  }
  async function select(id) {
    try {
      current = await api('/api/field/vehicles/get', { id });
      fill(current.vehicle);
      showAttributes(current.attributes);
      renderList();
      setStatus(current.readonly ? '内置车辆（只读）：修改后请“另存为新车辆”。' : `已保存的车辆 · ${current.saved_at || ''}`);
      updateButtons(true);
    } catch (error) { setStatus(error.message, true); }
  }
  async function save(asNew) {
    if (busy) return;
    const vehicle = collect();
    if (asNew && current && vehicle.name === current.vehicle.name) {
      const name = window.prompt('新车辆的名称：', `${vehicle.name} 副本`.slice(0, 40));
      if (name === null) return;
      vehicle.name = name;
    }
    busy = true; updateButtons(false);
    try {
      const saved = await api('/api/field/vehicles/save', { vehicle, id: asNew ? null : current.id });
      current = saved;
      fill(saved.vehicle);
      await refreshList();
      setStatus(`已保存：${saved.vehicle.name}（${saved.hash.slice(0, 10)}）`);
    } catch (error) { setStatus(`保存失败：${error.message}`, true); } finally { busy = false; updateButtons(true); }
  }
  async function remove() {
    if (busy || !current || current.readonly) return;
    if (!window.confirm(`删除“${current.vehicle.name}”？此操作不能撤销。`)) return;
    busy = true; updateButtons(false);
    try {
      await api('/api/field/vehicles/delete', { id: current.id });
      await refreshList();
      await select(defaultId);
      setStatus('已删除。');
    } catch (error) { setStatus(`删除失败：${error.message}`, true); } finally { busy = false; updateButtons(true); }
  }
  async function exportJson() {
    const out = await validate();
    if (!out) return;
    const blob = new Blob([JSON.stringify(out.vehicle, null, 1) + '\n'], { type: 'application/json' });
    const link = el('a', { href: URL.createObjectURL(blob), download: `${out.vehicle.name.replace(/[^\p{L}\p{N}_-]+/gu, '_') || 'vehicle'}.json` });
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  }
  async function importJson(file) {
    try {
      if (file.size > 65536) throw new Error('文件过大');
      const out = await api('/api/field/vehicles/check', { vehicle: JSON.parse(await file.text()) });
      current = { id: null, readonly: true, vehicle: out.vehicle, hash: null, attributes: out.attributes };
      fill(out.vehicle);
      showAttributes(out.attributes);
      renderList();
      setStatus('已读入文件，尚未保存：请“另存为新车辆”。');
      updateButtons(true);
    } catch (error) { setStatus(`导入失败：${error.message}`, true); }
  }

  $('name').addEventListener('input', changed);
  $('notes').addEventListener('input', changed);
  $('save').addEventListener('click', () => save(false));
  $('save-as').addEventListener('click', () => save(true));
  $('delete').addEventListener('click', remove);
  $('export').addEventListener('click', exportJson);
  $('import').addEventListener('click', () => $('import-file').click());
  $('import-file').addEventListener('change', (event) => { const f = event.target.files[0]; event.target.value = ''; if (f) importJson(f); });

  (async () => {
    try {
      const meta = await api('/api/field/vehicles/schema');
      schema = meta.schema; labels = meta.labels;
      buildForm();
      await refreshList();
      const wanted = new URLSearchParams(location.search).get('id');
      await select(list.some((v) => v.id === wanted && !v.invalid) ? wanted : defaultId);
    } catch (error) { setStatus(error.message, true); }
  })();
})();
