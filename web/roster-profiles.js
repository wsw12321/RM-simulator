'use strict';
// Optional, additive source-parameter UI. It never replaces native physics,
// observations, raw formats or the original roster response validator.
(() => {
  const SCOPE='partial-parameter-projection-into-synthetic-roster-physics';
  const PIN='11ea10bb3434847fa9793ac856f7d01a63043ddd81782345aa0d519d081e6af5';
  const PREFIX='rmuc-sourced2027-', packets=new WeakMap();
  let catalog=null, catalogLoading=false, catalogEpoch=0, prepared=null, active=null, activeStatus='unknown', activeRevision=-1;
  let choices=new Map(), chooserKey='', picker=null, selectionText='';
  const editable=['profiles-prepare','profiles-resolve','profiles-selection','profiles-import-config','profiles-import-replay'];
  const text=(id,value)=>{$(id).textContent=value;};
  const clone=value=>JSON.parse(JSON.stringify(value));
  const canonicalJSON=value=>JSON.stringify(stable(value)).replace(/[\u007f-\uffff]/g,ch=>'\\u'+ch.charCodeAt(0).toString(16).padStart(4,'0'));
  async function digest(value){const bytes=await globalThis.crypto.subtle.digest('SHA-256',encoder.encode(canonicalJSON(value)));return [...new Uint8Array(bytes)].map(x=>x.toString(16).padStart(2,'0')).join('');}
  function validateCatalog(c){
    if(!c||c.schema!==PREFIX+'catalog.v1'||c.id!=='rmuc2027-robot-parameter-catalog'||c.version!=='1.0.0'||c.official!==false||c.complete!==false||c.scope!==SCOPE||c.native_source_pin!==PIN||!finiteKeys(c.identity,['id','version','sha256','source_audit_sha256'])||c.identity.id!==c.id||c.identity.version!==c.version||!Object.values(c.identity).slice(2).every(x=>typeof x==='string'&&/^[0-9a-f]{64}$/.test(x))||c.source_audit_sha256!==c.identity.source_audit_sha256||!equal(c.profile_bounds,PROFILE_BOUNDS)||!Array.isArray(c.documents)||!Array.isArray(c.limitations)||c.limitations.some(x=>typeof x!=='string')||!Array.isArray(c.presets)||!c.presets.length)throw Error('来源目录身份、边界或完整性声明无效。');
    const ids=new Set();
    for(const p of c.presets){
      if(!p||typeof p.id!=='string'||ids.has(p.id)||!['infantry','heavy','sentry'].includes(p.role)||typeof p.label!=='string'||typeof p.projection_kind!=='string'||!p.conditions||typeof p.conditions!=='object'||Array.isArray(p.conditions)||!p.overrides||!Object.keys(p.overrides).length||Object.keys(p.overrides).some(k=>!['max_hp','heat_limit_tenths','cooling_per_second'].includes(k)||!integer(p.overrides[k],...PROFILE_BOUNDS[k]))||!equal(Object.keys(p.field_provenance||{}).sort(),Object.keys(p.overrides).sort())||!p.excluded_fields||typeof p.excluded_fields!=='object'||Array.isArray(p.excluded_fields)||p.official!==false||p.complete!==false)throw Error('目录包含不支持的角色或参数投影。');
      ids.add(p.id);
    }
  }
  async function validateEnvelope(e){
    if(!catalog)throw Error('来源目录尚未校验，不能确认来源标记。');
    if(!finiteKeys(e,['schema','official','complete','scope','selection','resolved_config','provenance','configuration_sha256'])||e.schema!==PREFIX+'roster-config.v1'||e.official!==false||e.complete!==false||e.scope!==SCOPE)throw Error('仅接受非官方、部分参数配置。');
    const s=e.selection, p=e.provenance;
    if(!finiteKeys(s,['schema','catalog','strategies','robots'])||s.schema!==PREFIX+'selection.v1'||!equal(s.catalog,catalog.identity)||!finiteKeys(s.strategies,['boundary_policy','credit_strategy','attack_damage_strategy','heat_strategy','heat_threshold_policy'])||!Array.isArray(s.robots)||!canonical(s.robots))throw Error('配置选择或目录版本不一致。');
    const robots=[],records=[];
    for(const r of s.robots){
      const preset=catalog.presets.find(v=>v.id===r.preset_id);
      if(!finiteKeys(r,['id','team','spawn','eligible','preset_id','conditions','synthetic'])||!preset||!equal(r.conditions,preset.conditions)||!finiteKeys(r.synthetic,['id',...Object.keys(PROFILE_BOUNDS).filter(k=>!(k in preset.overrides))]))throw Error('角色条件或显式合成余项无效。');
      const profile={...r.synthetic,...preset.overrides}; robots.push({id:r.id,team:r.team,spawn:r.spawn,eligible:r.eligible,profile});
      const fields={};for(const key of Object.keys(PROFILE_BOUNDS))fields[key]={classification:key in preset.overrides?'sourced-parameter':'user-supplied-synthetic-assumption',value:profile[key],unit:catalog.units[key],source:key in preset.overrides?preset.field_provenance[key]:null};
      records.push({id:r.id,profile_id:profile.id,preset_id:preset.id,role:preset.role,label:preset.label,projection_kind:preset.projection_kind,conditions:preset.conditions,initialization_note:preset.initialization_note,unsupported_behaviors:preset.unsupported_behaviors,complete:false,official:false,fields,excluded_fields:preset.excluded_fields});
    }
    const config={...s.strategies,robots};verifyConfig(config);
    const expected={catalog:catalog.identity,native_source_pin:PIN,documents:catalog.documents,robots:records,synthetic_context:{strategies:s.strategies,robots:s.robots.map(({id,team,spawn,eligible})=>({id,team,spawn,eligible}))},limitations:catalog.limitations,completeness:'partial-parameters-only; all unlisted behavior remains synthetic'};
    if(!equal(e.resolved_config,config)||!equal(p,expected))throw Error('参数、出处或合成余项被修改。');
    const unsigned={...e};delete unsigned.configuration_sha256;
    if(e.configuration_sha256!==await digest(unsigned))throw Error('来源配置摘要不匹配。');
    return e;
  }
  function route(path){return /^(state(?:\?|$)|step$|reset$|import$)/.test(path)?'profiles/session/'+path:path;}
  function request(data,raw,path){
    if(!/^(step|reset|import|profiles\/(reset|import))$/.test(path))return [data,raw];
    return [`{"expected_revision":${activeRevision},"expected_tick":${state.tick},"request":${raw?data:JSON.stringify(data)}}`,true];
  }
  async function unwrap(value,path){
    if(!/^(state(?:\?|$)|step$|reset$|import$|profiles\/(reset|import)$)/.test(path))return value;
    if(!finiteKeys(value,['schema','run_revision','state','active_config','provenance_status'])||value.schema!=='rmuc-roster-profile-session.v1'||!integer(value.run_revision,0)||value.run_revision<activeRevision||!['synthetic','sourced-partial','unverified'].includes(value.provenance_status)||(value.provenance_status==='sourced-partial')!==(value.active_config!==null))throw Error('来源与过滤状态的原子响应不一致。');
    let envelope=null,status=value.provenance_status;
    if(value.active_config){
      if(catalog){envelope=await validateEnvelope(value.active_config);if(value.state.build!==PIN||!equal(envelope.resolved_config,value.state.config))throw Error('来源配置未绑定当前原生状态。');}
      else status='unverified';
    }
    packets.set(value.state,{envelope,status,revision:value.run_revision});return value.state;
  }
  function accept(next){
    const packet=packets.get(next);if(!packet)throw Error('缺少与当前过滤状态绑定的来源上下文。');
    active=packet.envelope;activeStatus=packet.status;activeRevision=packet.revision;packets.delete(next);
  }
  function clearActive(){active=null;activeStatus='unknown';renderActive();}
  function sourceSummary(envelope){return envelope.provenance.robots.map(r=>`${r.id}：${r.label}；${Object.entries(r.fields).filter(([,f])=>f.classification==='sourced-parameter').map(([k,f])=>`${k}=${f.value} ${f.unit}`).join(' / ')}`).join('\n');}
  function renderActive(){
    text('profiles-active',active&&ready()?'当前局 · 已核对来源的部分参数':'当前局 · '+(activeStatus==='synthetic'&&ready()?'普通 SYNTHETIC，无来源标记':'来源未同步 / 未验证'));
    text('profiles-active-detail',active&&ready()?sourceSummary(active)+'\n完整官方类：否。速度、弹量、尺寸、伤害、感知、资格、地图、生命周期和裁决仍为显式实验选择。':'只有服务将来源配置与同一次过滤状态绑定后才显示标记。数值恰好一致的普通重置 / 回放不会获得来源标记。');
  }
  function invalidatePrepared(message='来源草稿已变化。请重新解析，再明确应用。'){
    prepared=null;text('profiles-draft-status',message);text('profiles-draft-detail','尚无已解析的来源参数草稿。当前局未改变。');
  }
  function draftChanged(){chooserKey='';invalidatePrepared();}
  function currentDraft(){if(encoder.encode(stagedConfig).length>MAX_CONFIG_BYTES)throw Error('编队 JSON 草稿超过 64 KiB。');const config=JSON.parse(stagedConfig);verifyConfig(config,false);return config;}
  function refreshChoices(){
    if(!catalog||!ready())return;
    let config;try{config=currentDraft();}catch(_){text('profiles-choice-note','先修正上方普通编队 JSON 草稿。');return;}
    const key=JSON.stringify(config.robots.map(r=>r.id))+':'+catalog.identity.sha256;if(key===chooserKey)return;chooserKey=key;
    choices=new Map(config.robots.map(r=>[r.id,choices.get(r.id)||catalog.presets[0].id]));
    const cards=config.robots.map(r=>{
      const box=node('div',undefined,'profile-choice'),title=node('strong',r.id),role=node('select'),presetSelect=node('select'),conditions=node('small');
      role.setAttribute('aria-label',r.id+' 的来源角色');presetSelect.setAttribute('aria-label',r.id+' 的来源条件快照');
      role.replaceChildren(...[...new Set(catalog.presets.map(p=>p.role))].map(v=>{const o=node('option',{infantry:'步兵 · HP / 热量',heavy:'重装 · 仅 HP',sentry:'哨兵 · 仅 HP'}[v]);o.value=v;return o;}));
      const chosen=catalog.presets.find(p=>p.id===choices.get(r.id));role.value=chosen.role;
      const fill=()=>{presetSelect.replaceChildren(...catalog.presets.filter(p=>p.role===role.value).map(p=>{const o=node('option',p.label);o.value=p.id;return o;}));presetSelect.value=choices.get(r.id);conditions.textContent=JSON.stringify(catalog.presets.find(p=>p.id===choices.get(r.id)).conditions);};fill();
      role.onchange=()=>{if(!ready()||busy){role.value=catalog.presets.find(p=>p.id===choices.get(r.id)).role;return;}pauseAndRelease();choices.set(r.id,catalog.presets.find(p=>p.role===role.value).id);revision++;invalidatePrepared();fill();updateControls();};
      presetSelect.onchange=()=>{if(!ready()||busy){presetSelect.value=choices.get(r.id);return;}const p=catalog.presets.find(p=>p.id===presetSelect.value&&p.role===role.value);if(!p)return;pauseAndRelease();choices.set(r.id,p.id);revision++;invalidatePrepared();fill();updateControls();};
      box.append(title,role,presetSelect,conditions);box.profileControls=[role,presetSelect];return box;
    });$('profiles-choices').replaceChildren(...cards);text('profiles-choice-note','每个选择只投影目录明确支持的字段。等级 / 阶段为固定快照，不执行升级、XP、复活、姿态或官方开局流程。');
  }
  function update(){
    refreshChoices();renderActive();const blocked=!ready()||!!busy||!catalog;
    for(const id of editable)$(id).disabled=blocked;
    $('profiles-apply').disabled=blocked||!prepared;$('profiles-export-config').disabled=blocked||!prepared;
    $('profiles-export-replay').disabled=!ready()||!!busy||!active;
    $('profiles-reload').disabled=!!busy||catalogLoading;
    for(const child of $('profiles-choices').children)for(const control of child.profileControls||[])control.disabled=blocked;
  }
  async function loadCatalog(){
    if(catalogLoading||busy)return;const epoch=++catalogEpoch;catalogLoading=true;catalog=null;prepared=null;active=null;activeStatus='unverified';update();
    try{const value=await api('profiles/catalog');validateCatalog(value);if(epoch!==catalogEpoch)return;catalog=value;chooserKey='';text('profiles-catalog-status',`已校验本机目录 ${value.version} · ${value.presets.length} 个部分参数快照`);text('profiles-catalog-pin',`目录 SHA-256：${value.identity.sha256}\n审计 SHA-256：${value.identity.source_audit_sha256}`);text('profiles-documents',JSON.stringify(value.documents,null,2));text('profiles-limitations',value.limitations.join('\n'));}
    catch(error){text('profiles-catalog-status',`来源目录不可用：${error.message} 普通 SYNTHETIC 配置与回放仍可使用。`);}
    finally{catalogLoading=false;update();}
  }
  function showPrepared(e){prepared=e;selectionText=JSON.stringify(e.selection,null,2);$('profiles-selection').value=selectionText;text('profiles-draft-status','来源草稿已解析 · 尚未应用');text('profiles-draft-detail',sourceSummary(e));text('profiles-draft-provenance',JSON.stringify(e.provenance,null,2));}
  async function prepare(resolveEdited=false){
    if(!ready()||busy||!catalog)return;pauseAndRelease();const token=++generation;busy='profiles';updateControls();
    try{let next;if(resolveEdited){if(encoder.encode(selectionText).length>1024*1024)throw Error('来源选择超过 1 MiB。');next=await api('profiles/resolve',selectionText,true);}else{currentDraft();next=await api('profiles/prepare',`{"config":${stagedConfig},"presets":${JSON.stringify(Object.fromEntries(choices))}}`,true);}if(token!==generation)return;await validateEnvelope(next);showPrepared(next);operation('已解析来源草稿。请检查来源字段、条件与显式合成余项，再点击“应用来源草稿并重置”。');}
    catch(error){if(token===generation){invalidatePrepared('来源草稿解析失败：'+error.message);operation('来源草稿解析失败，当前局未改变：'+error.message,true);}}
    finally{if(token===generation){busy='';updateControls();}}
  }
  async function apply(){
    if(!ready()||busy||!prepared)return;pauseAndRelease();const envelope=prepared,token=++generation,selected=envelope.resolved_config.robots.some(r=>r.id===observer)?observer:envelope.resolved_config.robots[0].id;busy='profiles';updateControls();
    try{const next=await api('profiles/reset',{observer:selected,configuration:envelope});if(token!==generation)return;verifyState(next,meta,selected,undefined,envelope.resolved_config);if(next.tick!==0||next.last_sources!==null)throw Error('来源重置没有返回精确初始状态。');commit(next,meta,selected);syncConfigSelection();operation('已应用来源部分参数并重置。C++ 仍使用原 SYNTHETIC 身体、武器和实验规则；不是官方角色实现。');}
    catch(error){if(token===generation){if(error.uncertain||!error.rejected)await recoverUncertain(error,token);else operation('来源重置被拒绝，当前局与来源上下文保留：'+error.message,true);}}
    finally{if(token===generation){busy='';updateControls();drawMap();}}
  }
  function download(value,name){const blob=new Blob([JSON.stringify(value)],{type:'application/json'}),url=URL.createObjectURL(blob),a=node('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  function open(kind){if(!ready()||busy||!catalog)return;pauseAndRelease();picker={kind,generation,revision,observer,catalogEpoch};$('profiles-file').click();}
  async function importFile(event){
    const file=event.target.files?.[0],snapshot=picker;picker=null;event.target.value='';if(!file)return;
    if(!snapshot||!ready()||busy||snapshot.generation!==generation||snapshot.revision!==revision||snapshot.observer!==observer||snapshot.catalogEpoch!==catalogEpoch){operation('来源文件选择期间状态、目录、视角或草稿已变化，未导入。',true);return;}
    pauseAndRelease();const token=++generation;busy='profiles';updateControls();let requested=false;
    try{const limit=snapshot.kind==='config'?1024*1024:MAX_FILE_BYTES;if(file.size>limit)throw Error('来源文件超过大小限制。');const raw=await file.text();if(token!==generation)return;if(encoder.encode(raw).length>limit)throw Error('来源文件超过大小限制。');
      if(snapshot.kind==='config'){const e=await api('profiles/validate',`{"configuration":${raw}}`,true);if(token!==generation)return;await validateEnvelope(e);showPrepared(e);operation('已校验并载入来源配置草稿，当前局未改变。请明确应用。');}
      else{const parsed=JSON.parse(raw);await validateEnvelope(parsed.configuration);const conf=parsed.configuration.resolved_config,selected=conf.robots.some(r=>r.id===observer)?observer:conf.robots[0].id;const body=`{"observer":${JSON.stringify(selected)},"replay":${raw}}`;requireSize(body);requested=true;const next=await api('profiles/import',body,true);if(token!==generation)return;verifyState(next,meta,selected,undefined,conf);if(!Array.isArray(parsed.native_replay?.frames)||next.verified_frames!==parsed.native_replay.frames.length||next.tick!==next.verified_frames)throw Error('来源回放帧数不一致。');commit(next,meta,selected);syncConfigSelection();operation(`已校验来源回放 ${state.tick} 帧。来源是部分参数，动作标签未经认证；保持暂停。`);}
    }catch(error){if(token===generation){if(requested&&(error.uncertain||!error.rejected))await recoverUncertain(error,token);else operation('来源文件未应用，当前局保留：'+error.message,true);}}
    finally{if(token===generation){busy='';updateControls();drawMap();}}
  }
  async function exportReplay(){
    if(!ready()||busy||!active)return;pauseAndRelease();const token=++generation,envelope=active,tick=state.tick,last=state.last_sources;busy='profiles';updateControls();
    try{const value=await api('profiles/replay');if(token!==generation)return;if(!finiteKeys(value,['schema','official','complete','configuration','native_replay','native_replay_sha256'])||value.schema!==PREFIX+'roster-replay.v1'||value.official!==false||value.complete!==false)throw Error('来源回放包装无效。');await validateEnvelope(value.configuration);if(!equal(value.configuration,envelope))throw Error('其他客户端已改变当前来源配置。');verifyReplay(value.native_replay,tick,last);if(value.native_replay_sha256!==await digest(value.native_replay))throw Error('原生回放摘要不一致。');download(value,`tactics-roster-sourced2027-tick-${tick}.json`);operation('已请求下载来源包装回放，包含全部机器人已解析动作。不是过滤观测文件；请确认浏览器下载。');}
    catch(error){if(token===generation){clearActive();await recoverUncertain(error,token);}}
    finally{if(token===generation){busy='';updateControls();}}
  }
  function initialize(){
    $('profiles-reload').onclick=loadCatalog;$('profiles-prepare').onclick=()=>prepare(false);$('profiles-resolve').onclick=()=>prepare(true);$('profiles-apply').onclick=apply;
    $('profiles-selection').oninput=()=>{if(!ready()||busy){$('profiles-selection').value=selectionText;return;}pauseAndRelease();selectionText=$('profiles-selection').value;revision++;invalidatePrepared();updateControls();};
    $('profiles-export-config').onclick=()=>{if(!ready()||busy||!prepared)return;download(prepared,'tactics-roster-sourced2027-config.json');operation('已请求下载来源配置草稿，包含条件、出处和显式合成余项。当前局未改变。');};
    $('profiles-import-config').onclick=()=>open('config');$('profiles-import-replay').onclick=()=>open('replay');$('profiles-export-replay').onclick=exportReplay;$('profiles-file').onchange=importFile;$('profiles-file').oncancel=()=>{picker=null;};
    void loadCatalog();
  }
  globalThis.rosterProfileIntegration={route,request,unwrap,accept,clearActive,draftChanged,update,initialize};
})();
