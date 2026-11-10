"""Strict disposable-process adapter for the SYNTHETIC multi-robot experiment.

The native engine owns physics, sensing and adjudication. Public state is one
filtered observer plus explicitly public roster/config/objective data. Replays
are privileged developer artifacts, never inputs to the browser's player view.
"""
import copy
import json
from pathlib import Path
import re

from combat_mode.bridge import exact, integer, identifier, canonical, action_fields
from native_pipe import NativePipe
from .build_id import build_id

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'experimental-roster-tactical-v1'
ENGINE = 'integer-mm-roster-v1'
OBSERVATION = 'experimental-roster-range-los-public-v1'
OBSERVATION_VERSION = 1
REPLAY_SCHEMA = 'rmuc-experimental-roster-replay.v1'
MAX_BYTES = 96 * 1024 * 1024
MAX_LINE_BYTES = 1024 * 1024
MAX_TICKS = 25200
MAX_BATCH = 600
MAX_ROBOTS = 8
MAX_TEAM_ROBOTS = 4
BOUNDARIES = ('strict-reject-unadjudicated-v1', 'ENGINEERING-NOT-OFFICIAL-timer-before-input-v1')
CREDITS = ('disabled', 'EXPERIMENTAL-end-of-second-v1')
DAMAGE_STRATEGIES = ('missing-adjudicated-attack-damage', 'EXPERIMENTAL-reconciled-projectile-hp-loss-v1')
HEAT_STRATEGIES = ('disabled', 'SYNTHETIC-per-robot-17mm-v1')
HEAT_THRESHOLDS = ('strict-reject-unadjudicated', 'ENGINEERING-prose-greater-temporary-at-equality-v1', 'ENGINEERING-flowchart-greater-equal-v1')
SELECTED_CONFIG = ('boundary_policy', 'credit_strategy', 'attack_damage_strategy', 'heat_strategy', 'heat_threshold_policy')
HEAT_MODULE = 'rmuc2027-heat-v1.0.0-module.1'
DAMAGE_MODULE = 'rmuc2027-damage-v1.0.0-module.1'
HEAT_BOUNDARY = 'EXPERIMENTAL-cooling-before-fire-gate-and-shot-v1'
PROFILE_BOUNDS = {'max_hp': (1,10000), 'ammo': (0,5000), 'radius_mm': (100,500),
    'speed_mm_tick': (0,100), 'projectile_speed_mm_tick': (80,1000),
    'projectile_damage': (1,1000), 'cooldown_ticks': (1,120), 'sensor_range_mm': (0,20000),
    'heat_limit_tenths': (0,20000), 'cooling_per_second': (0,1000)}
ARENA = {'width':12000, 'height':7600, 'walls':[
    {'x1':5250,'y1':2800,'x2':6750,'y2':4800},
    {'x1':2600,'y1':1350,'x2':3800,'y2':1950},
    {'x1':8200,'y1':5650,'x2':9400,'y2':6250}]}
ZONES = [
    {'id':'red-outpost','rect':{'x1':4300,'y1':700,'x2':5900,'y2':2300},'original_owner':0},
    {'id':'blue-outpost','rect':{'x1':6100,'y1':5300,'x2':7700,'y2':6900},'original_owner':1},
    {'id':'red-base','rect':{'x1':600,'y1':5600,'x2':2200,'y2':7200},'original_owner':0},
    {'id':'blue-base','rect':{'x1':9800,'y1':400,'x2':11400,'y2':2000},'original_owner':1}]
DEFAULT_CONFIG = json.loads((ROOT / 'roster_mode/default_config.json').read_text(encoding='utf-8'))
LIMITATIONS = [
    'SYNTHETIC 1–4 robots per team, default 2v2. These profiles, physics and arena are engineering assumptions, not an official RMUC roster or robot specification.',
    'Every configured stable ID supplies one source each tick. All per-robot observations are frozen before any policy callback; IDs, not array positions, bind actions.',
    'Public static config includes initial spawn, profile and eligibility. It does not reveal current hidden HP, heat, ammunition or occupancy.',
    'Only own private state, range/LOS-visible other poses and visible projectile kinematics are observed. Same-team robots receive no extra visibility privileges.',
    'Objective ownership, control degree/rates and scores are deliberately public experimental broadcasts. Detection, progress, individual grace and hidden occupancy remain private.',
    'Individual eligible living robot activation is aggregated by team OR; individual leave receives 2 s grace and death clears only that robot. This is a named synthetic aggregation, not an official multi-robot qualification claim.',
    'Each profile explicitly configures synthetic HP, ammo, size, movement, weapon and heat values. A detected 17mm shot adds 10 heat; cooling is on 100 ms boundaries before firing, then shot and death reset.',
    'Strict unresolved heat threshold equality is the default. Named engineering interpretations, disabled heat, disabled credits and missing-ledger strategies remain selectable.',
    'Same-tick movement proposals and emissions are frozen; collision-blocked movement causes no HP loss. Friendly shots pass through teammates; enemy impacts accumulate before HP clamp.',
    '420 s clock, latched temporary capture, no respawn, no destructible bases and no penalties. Disable does not end the run.',
    'Trusted same-process stateless C++ policies only. No security, CPU or memory isolation and no enforced purity. Invalid/timeout responses invalidate a candidate, not a robot.',
    'Developer replay exposes every robot’s resolved inputs and diagnostic hashes. Labels are unauthenticated; import reruns actions, never policy callbacks.',
    'Candidate restoration is O(history). Fixed-step playback preserves every tick but makes no real-time performance promise.',
]


def robot_id(value, where='id'):
    if type(value) is not str or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,31}', value):
        raise ValueError(where + ' must be a stable ASCII ID of 1–32 characters.')
    return value


def validate_config(config):
    """Return a canonical copy. Validation happens before any native launch."""
    exact(config, (*SELECTED_CONFIG, 'robots'), 'config')
    choices = (BOUNDARIES, CREDITS, DAMAGE_STRATEGIES, HEAT_STRATEGIES, HEAT_THRESHOLDS)
    for key, values in zip(SELECTED_CONFIG, choices):
        if type(config[key]) is not str or config[key] not in values:
            raise ValueError('Unsupported roster configuration choice: ' + key + '.')
    robots = config['robots']
    if type(robots) is not list or not 2 <= len(robots) <= MAX_ROBOTS:
        raise ValueError('config.robots requires 2–8 robot records.')
    ids, profiles, teams = set(), {}, [0,0]
    for item in robots:
        exact(item, ('id','team','profile','spawn','eligible'), 'config.robot')
        rid = robot_id(item['id'], 'config.robot.id')
        if rid in ids:
            raise ValueError('Duplicate robot ID; identities must be unique.')
        ids.add(rid)
        team = integer(item['team'],0,1,'config.robot.team')
        teams[team] += 1
        if type(item['eligible']) is not bool:
            raise ValueError('Robot eligibility must be boolean.')
        profile = item['profile']
        exact(profile, ('id', *PROFILE_BOUNDS), 'config.robot.profile')
        pid = robot_id(profile['id'], 'profile.id')
        if not pid.startswith('SYNTHETIC-'):
            raise ValueError('Profile IDs must explicitly start with SYNTHETIC-.')
        for key, (low,high) in PROFILE_BOUNDS.items():
            integer(profile[key],low,high,'profile.' + key)
        if pid in profiles and profiles[pid] != profile:
            raise ValueError('Reused profile IDs require identical synthetic values.')
        profiles[pid] = profile
        spawn, radius = item['spawn'], profile['radius_mm']
        exact(spawn, ('x','y','yaw'), 'config.robot.spawn')
        integer(spawn['x'],radius,ARENA['width']-radius,'spawn.x')
        integer(spawn['y'],radius,ARENA['height']-radius,'spawn.y')
        integer(spawn['yaw'],0,255,'spawn.yaw')
        for wall in ARENA['walls']:
            dx = spawn['x'] - max(wall['x1'],min(wall['x2'],spawn['x']))
            dy = spawn['y'] - max(wall['y1'],min(wall['y2'],spawn['y']))
            if dx*dx + dy*dy < radius*radius:
                raise ValueError('Robot spawn overlaps a static wall.')
    if any(not 1 <= count <= MAX_TEAM_ROBOTS for count in teams):
        raise ValueError('Each team requires 1–4 configured robots.')
    for index, a in enumerate(robots):
        for b in robots[index+1:]:
            dx,dy = a['spawn']['x']-b['spawn']['x'],a['spawn']['y']-b['spawn']['y']
            if dx*dx+dy*dy < (a['profile']['radius_mm']+b['profile']['radius_mm'])**2:
                raise ValueError('Robot spawn circles overlap.')
    result = copy.deepcopy(config)
    result['robots'].sort(key=lambda item:item['id'])
    return result


def ids_of(config):
    return [item['id'] for item in config['robots']]


def selected_observer(observer, config):
    robot_id(observer, 'observer')
    if observer not in ids_of(config):
        raise ValueError('Observer ID is not part of the configured roster.')
    return observer


def reset_tokens(observer, config):
    values = ['RESET', observer]
    values += [choices.index(config[key]) for key, choices in zip(SELECTED_CONFIG,
        (BOUNDARIES,CREDITS,DAMAGE_STRATEGIES,HEAT_STRATEGIES,HEAT_THRESHOLDS))]
    values.append(len(config['robots']))
    for robot in config['robots']:
        profile, spawn = robot['profile'], robot['spawn']
        values += [robot['id'],robot['team'],profile['id'],*[profile[k] for k in PROFILE_BOUNDS],
                   spawn['x'],spawn['y'],spawn['yaw'],int(robot['eligible'])]
    return ' '.join(map(str, values))


def validate_source(source, where, with_action=False):
    keys = ('id','kind','sdk_version','controller_id')
    exact(source, (*keys,'action','observation_hash') if with_action else keys, where)
    robot_id(source['id'], where + '.id')
    integer(source['sdk_version'],1,1,where + '.sdk_version')
    if source['kind'] == 'manual':
        if source['controller_id'] is not None:
            raise ValueError('Manual input cannot claim a controller identity.')
    elif source['kind'] == 'policy':
        identifier(source['controller_id'],where + '.controller_id')
    else:
        raise ValueError('Input source must be manual or policy.')
    if with_action:
        action = source['action']
        if type(action) is not list or len(action) != 5:
            raise ValueError('Resolved action requires five integers.')
        action_fields(dict(zip(('sdk_version','dx','dy','aim_x','aim_y','fire'),[1,*action])),where + '.action')
        integer(source['observation_hash'],0,0xffffffff,where + '.observation_hash')


def validate_sources(sources, config, where, with_action=False):
    if type(sources) is not list or len(sources) != len(config['robots']):
        raise ValueError(where + ' requires exactly one record for every robot ID.')
    for item in sources:
        validate_source(item, where, with_action)
    if [item['id'] for item in sources] != ids_of(config):
        raise ValueError(where + ' must contain every configured ID exactly once in canonical order.')


def validate_frame(frame, tick, config):
    exact(frame, ('tick','inputs','hash'), 'frame')
    integer(frame['tick'],tick,tick,'frame.tick')
    integer(frame['hash'],0,0xffffffff,'frame.hash')
    validate_sources(frame['inputs'],config,'frame.inputs',True)


def frame_tokens(frame):
    values = [frame['tick']]
    for item in frame['inputs']:
        values += [item['id'],item['kind'],item['controller_id'] or '-',item['sdk_version'],
                   *item['action'],item['observation_hash']]
    return values + [frame['hash']]


def validate_heat_flags(value, profile, where):
    integer(value['heat_tenths'],0,profile['ammo']*100,where + '.heat_tenths')
    for key in ('overheat_locked','permanent_latched','firing_locked'):
        if type(value[key]) is not bool:
            raise ValueError('Heat lock flags must be boolean.')
    if value['firing_locked'] != (value['overheat_locked'] or value['permanent_latched']):
        raise ValueError('Heat firing lock contradicts thermal state.')
    if (value['overheat_locked'] and not value['heat_tenths']) or (value['heat_tenths'] > profile['heat_limit_tenths'] and not value['firing_locked']):
        raise ValueError('Heat value contradicts its lock state.')


def validate_self_heat(heat, config, profile):
    exact(heat, ('enabled','heat_tenths','limit_tenths','cooling_per_second','overheat_locked','permanent_latched','firing_locked'), 'self_heat')
    enabled = config['heat_strategy'] == HEAT_STRATEGIES[1]
    if type(heat['enabled']) is not bool or heat['enabled'] != enabled:
        raise ValueError('Heat enabled flag differs from configuration.')
    for key, expected in [('limit_tenths',profile['heat_limit_tenths']),('cooling_per_second',profile['cooling_per_second'])]:
        integer(heat[key],expected,expected,'self_heat.'+key)
    validate_heat_flags(heat,profile,'self_heat')
    if not enabled and any(heat[key] for key in ('heat_tenths','overheat_locked','permanent_latched','firing_locked')):
        raise ValueError('Disabled heat cannot publish thermal state.')


def line_blocked(a, b):
    """Same conservative integer Q16 slab test as native public arena LOS."""
    def trunc_div(n, d):
        return (abs(n)//abs(d)) * (-1 if (n<0)!=(d<0) else 1)
    for wall in ARENA['walls']:
        lo,hi = 0,65536
        hit = True
        for axis in ('x','y'):
            start,delta = a[axis],b[axis]-a[axis]
            lower,upper = wall[axis+'1'],wall[axis+'2']
            if delta == 0:
                if not lower<=start<=upper:
                    hit = False
                    break
                continue
            q1,q2 = trunc_div((lower-start)*65536,delta),trunc_div((upper-start)*65536,delta)
            lo,hi = max(lo,min(q1,q2)),min(hi,max(q1,q2))
            if lo>hi:
                hit = False
                break
        if hit:
            return True
    return False


def validate_pose_clearance(pose, profile):
    radius = profile['radius_mm']
    integer(pose['x'],radius,ARENA['width']-radius,'pose.x')
    integer(pose['y'],radius,ARENA['height']-radius,'pose.y')
    for wall in ARENA['walls']:
        dx = pose['x']-max(wall['x1'],min(wall['x2'],pose['x']))
        dy = pose['y']-max(wall['y1'],min(wall['y2'],pose['y']))
        if dx*dx+dy*dy<radius*radius:
            raise ValueError('Native robot pose overlaps static cover.')


def validate_observation(obs, observer, tick, config):
    exact(obs, ('sdk_version','observation_version','profile','hz','tick','self_id','self_team',
        'sensor_range_mm','self','arena','roster','contacts','projectiles','self_heat','zones','score',
        'rate','qualified_ms','duration_ms','credits_enabled'), 'observation')
    own = next(item for item in config['robots'] if item['id'] == observer)
    profile = own['profile']
    for key, expected in [('sdk_version',1),('observation_version',1),('hz',60),('tick',tick),
        ('self_team',own['team']),('sensor_range_mm',profile['sensor_range_mm']),
        ('qualified_ms',(tick*1000+59)//60),('duration_ms',420000)]:
        integer(obs[key],expected,expected,'observation.'+key)
    if obs['profile'] != OBSERVATION or obs['self_id'] != observer:
        raise ValueError('Filtered observation profile or observer identity mismatch.')
    if canonical(obs['arena']) != canonical(ARENA) or canonical(obs['roster']) != canonical(config['robots']):
        raise ValueError('Public static arena/roster differs from committed configuration.')
    exact(obs['self'],('x','y','yaw','hp','ammo','cooldown'),'observation.self')
    for key, high in [('x',12000),('y',7600),('yaw',255),('hp',profile['max_hp']),('ammo',profile['ammo']),('cooldown',profile['cooldown_ticks'])]:
        integer(obs['self'][key],0,high,'observation.self.'+key)
    validate_pose_clearance(obs['self'],profile)
    by_id = {item['id']:item for item in config['robots']}
    contacts = obs['contacts']
    if type(contacts) is not list or len(contacts) >= len(config['robots']):
        raise ValueError('Invalid filtered contact count.')
    seen = []
    for contact in contacts:
        exact(contact,('id','team','x','y','yaw'),'observation.contact')
        rid = robot_id(contact['id'],'contact.id')
        if rid == observer or rid not in by_id or rid in seen:
            raise ValueError('Contact must identify a distinct configured other robot.')
        seen.append(rid)
        integer(contact['team'],by_id[rid]['team'],by_id[rid]['team'],'contact.team')
        for key,high in [('x',12000),('y',7600),('yaw',255)]:
            integer(contact[key],0,high,'contact.'+key)
        validate_pose_clearance(contact,by_id[rid]['profile'])
        if line_blocked(obs['self'],contact):
            raise ValueError('Returned contact is occluded by public static cover.')
        if (contact['x']-obs['self']['x'])**2+(contact['y']-obs['self']['y'])**2 > profile['sensor_range_mm']**2:
            raise ValueError('Returned contact lies outside own configured sensor range.')
    if seen != sorted(seen):
        raise ValueError('Contacts must be ordered by public stable ID.')
    bullets = obs['projectiles']
    if type(bullets) is not list or len(bullets) > 2048:
        raise ValueError('Expected at most 2048 filtered projectile motions.')
    for bullet in bullets:
        exact(bullet,('x','y','vx','vy'),'observation.projectile')
        for key,low,high in [('x',0,12000),('y',0,7600),('vx',-1000,1000),('vy',-1000,1000)]:
            integer(bullet[key],low,high,'projectile.'+key)
        if line_blocked(obs['self'],bullet):
            raise ValueError('Returned projectile is occluded by public static cover.')
        if (bullet['x']-obs['self']['x'])**2+(bullet['y']-obs['self']['y'])**2 > profile['sensor_range_mm']**2:
            raise ValueError('Returned projectile lies outside own configured sensor range.')
    if bullets != sorted(bullets,key=lambda b:(b['x'],b['y'],b['vx'],b['vy'])):
        raise ValueError('Projectile order may depend only on visible kinematics.')
    validate_self_heat(obs['self_heat'],config,profile)
    enabled = config['credit_strategy'] == CREDITS[1]
    if type(obs['credits_enabled']) is not bool or obs['credits_enabled'] != enabled:
        raise ValueError('Public scoring flag differs from configuration.')
    if type(obs['zones']) is not list or len(obs['zones']) != 4:
        raise ValueError('Expected four public objective records.')
    for zone, expected in zip(obs['zones'],ZONES):
        exact(zone,('id','rect','original_owner','controller','degree'),'observation.zone')
        if canonical({k:zone[k] for k in expected}) != canonical(expected):
            raise ValueError('Objective static identity differs from the synthetic arena.')
        integer(zone['controller'],0,1,'zone.controller')
        if zone['degree'] not in ('full','temporary') or (zone['degree']=='full' and zone['controller']!=zone['original_owner']):
            raise ValueError('Invalid public objective control degree.')
    rates = [0,0]
    for a,b,points in ((0,1,1),(2,3,3)):
        if obs['zones'][a]['controller']==obs['zones'][b]['controller']:
            rates[obs['zones'][a]['controller']] += points
    if type(obs['rate']) is not list or len(obs['rate']) != 2:
        raise ValueError('Expected two public control rates.')
    for i,value in enumerate(obs['rate']):
        integer(value,rates[i],rates[i],'observation.rate')
    if enabled:
        if type(obs['score']) is not list or len(obs['score']) != 2:
            raise ValueError('Expected two public scores.')
        for value in obs['score']:
            integer(value,0,4*(tick//60),'observation.score')
    elif obs['score'] is not None:
        raise ValueError('Disabled scoring cannot publish scores.')


def validate_heat_admin(value, config, observer, observation, tick):
    exact(value,('schema','module','strategy','threshold_policy','boundary_policy','robots'),'native.heat_admin')
    integer(value['schema'],1,1,'heat_admin.schema')
    if (value['module'] != HEAT_MODULE or value['strategy'] != config['heat_strategy'] or value['threshold_policy'] != config['heat_threshold_policy'] or value['boundary_policy'] != HEAT_BOUNDARY):
        raise ValueError('Native heat accounting identity mismatch.')
    records = value['robots']
    if type(records) is not list or len(records) != len(config['robots']):
        raise ValueError('Heat accounting needs every configured robot exactly once.')
    fields = ('heat_tenths','overheat_locked','permanent_latched','firing_locked')
    enabled = config['heat_strategy'] == HEAT_STRATEGIES[1]
    for record, robot in zip(records,config['robots']):
        exact(record,('id',*fields,'detected_shots','blocked_fire_ticks'),'native.heat_admin.robot')
        if record['id'] != robot['id']:
            raise ValueError('Native heat records have duplicate, unknown or reordered identities.')
        profile = robot['profile']
        validate_heat_flags(record,profile,'heat_admin')
        integer(record['detected_shots'],0,min(profile['ammo'],tick),'heat_admin.detected_shots')
        integer(record['blocked_fire_ticks'],0,tick,'heat_admin.blocked_fire_ticks')
        if not enabled and any(record[key] for key in (*fields,'detected_shots','blocked_fire_ticks')):
            raise ValueError('Disabled native heat accounting must remain zero.')
        if enabled and (record['heat_tenths'] > 100*record['detected_shots'] or (record['permanent_latched'] and record['detected_shots']*100 < profile['heat_limit_tenths']+1000)):
            raise ValueError('Native heat cannot precede sufficient detected shots.')
        if robot['id'] == observer:
            if any(record[key] != observation['self_heat'][key] for key in fields):
                raise ValueError('Own filtered heat differs from native accounting.')
            if enabled and record['detected_shots'] != profile['ammo']-observation['self']['ammo']:
                raise ValueError('Own actual shots differ from ammunition consumption.')


def validate_damage_admin(value, config, observer, observation):
    exact(value,('schema','module','strategy','complete','reason','attack_damage','remaining_robot_hp',
        'actual_hp_loss','excluded_hp_loss','penalty_attack_damage','nominal_projectile_damage','last_sequence'),'native.damage_admin')
    integer(value['schema'],1,1,'damage.schema')
    if value['module'] != DAMAGE_MODULE or value['strategy'] != config['attack_damage_strategy'] or type(value['complete']) is not bool:
        raise ValueError('Native damage accounting identity mismatch.')
    maxima = [sum(r['profile']['max_hp'] for r in config['robots'] if r['team']==team) for team in (0,1)]
    nominal_max = [sum(r['profile']['ammo']*r['profile']['projectile_damage'] for r in config['robots'] if r['team']==team) for team in (0,1)]
    def totals(key, bounds, nullable=False):
        numbers = value[key]
        if numbers is None and nullable:
            return
        if type(numbers) is not list or len(numbers) != 2:
            raise ValueError('Damage accounting requires two team totals.')
        for i,number in enumerate(numbers):
            integer(number,0,bounds[i],'damage.'+key)
    for key in ('actual_hp_loss','excluded_hp_loss','penalty_attack_damage'):
        totals(key,maxima if key!='penalty_attack_damage' else list(reversed(maxima)))
    totals('nominal_projectile_damage',nominal_max)
    totals('remaining_robot_hp',maxima,True)
    integer(value['last_sequence'],0,sum(value['actual_hp_loss']),'damage.last_sequence')
    if bool(value['last_sequence']) != bool(sum(value['actual_hp_loss'])):
        raise ValueError('Damage sequence contradicts positive loss entries.')
    hp = value['remaining_robot_hp']
    own = next(robot for robot in config['robots'] if robot['id']==observer)
    if hp is not None:
        others_max = maxima[own['team']]-own['profile']['max_hp']
        if not observation['self']['hp'] <= hp[own['team']] <= others_max+observation['self']['hp']:
            raise ValueError('Own HP contradicts remaining team total.')
    if value['excluded_hp_loss'] != [0,0] or value['penalty_attack_damage'] != [0,0]:
        raise ValueError('Projectile-only roster cannot invent penalty or excluded HP sources.')
    if value['complete']:
        totals('attack_damage',list(reversed(maxima)))
        if value['reason'] is not None or config['attack_damage_strategy'] != DAMAGE_STRATEGIES[1]:
            raise ValueError('Unresolved strategy cannot claim complete damage accounting.')
        if hp is not None and value['actual_hp_loss'] != [maxima[i]-hp[i] for i in (0,1)]:
            raise ValueError('Damage loss does not reconcile with configured initial HP.')
        if value['attack_damage'] != list(reversed(value['actual_hp_loss'])) or any(nominal < actual or bool(nominal)!=bool(actual) for nominal,actual in zip(value['nominal_projectile_damage'],value['attack_damage'])):
            raise ValueError('Projectile damage accounting does not reconcile with actual loss.')
    else:
        reason = DAMAGE_STRATEGIES[0] if config['attack_damage_strategy']==DAMAGE_STRATEGIES[0] else 'unadjudicated-native-damage'
        if value['reason'] != reason or value['attack_damage'] is not None:
            raise ValueError('Incomplete damage accounting cannot fabricate totals.')
        if config['attack_damage_strategy']==DAMAGE_STRATEGIES[0] and (value['actual_hp_loss'] != [0,0] or value['nominal_projectile_damage'] != [0,0] or value['last_sequence']):
            raise ValueError('Missing-ledger strategy cannot claim settled loss entries.')


def validate_result(result, terminal, config, score, damage):
    exact(result,('outcome','reason'),'result')
    if not terminal:
        expected = {'outcome':'ongoing','reason':'not-ended'}
    elif config['credit_strategy']=='disabled':
        expected = {'outcome':'unscored-timeout','reason':'credit-strategy-disabled'}
    elif score[0]==score[1] and not damage['complete']:
        expected = {'outcome':'unresolved-timeout-tie','reason':DAMAGE_STRATEGIES[0]}
    else:
        stages = [('timeout-control',score)]
        if damage['complete']:
            stages += [('timeout-attack-damage',damage['attack_damage']),('timeout-surviving-robot-hp',damage['remaining_robot_hp'])]
        expected = {'outcome':'draw','reason':'timeout-equal'}
        for reason,values in stages:
            if values is None:
                expected = {'outcome':'unresolved-timeout-tie','reason':'missing-adjudicated-surviving-robot-hp'}
                break
            if values[0] != values[1]:
                expected = {'outcome':'red-wins' if values[0]>values[1] else 'blue-wins','reason':reason}
                break
    if result != expected:
        raise ValueError('Native result contradicts selected adjudication and accounting.')


def validate_initial_state(state, observer, config):
    """Tick zero is fully determined by public config, not bounded telemetry."""
    obs = state['observation']
    own = next(robot for robot in config['robots'] if robot['id']==observer)
    profile = own['profile']
    expected_self = {**own['spawn'],'hp':profile['max_hp'],'ammo':profile['ammo'],'cooldown':0}
    contacts = []
    for robot in config['robots']:
        pose = robot['spawn']
        if (robot['id']!=observer and
                (pose['x']-own['spawn']['x'])**2+(pose['y']-own['spawn']['y'])**2<=profile['sensor_range_mm']**2 and
                not line_blocked(own['spawn'],pose)):
            contacts.append({'id':robot['id'],'team':robot['team'],**pose})
    expected_zones = [{**zone,'controller':zone['original_owner'],'degree':'full'} for zone in ZONES]
    expected_heat = {'enabled':config['heat_strategy']==HEAT_STRATEGIES[1],
        'heat_tenths':0,'limit_tenths':profile['heat_limit_tenths'],
        'cooling_per_second':profile['cooling_per_second'],'overheat_locked':False,
        'permanent_latched':False,'firing_locked':False}
    if (obs['self']!=expected_self or obs['contacts']!=contacts or obs['projectiles']!=[] or
            obs['zones']!=expected_zones or obs['rate']!=[0,0] or
            obs['score']!=([0,0] if config['credit_strategy']==CREDITS[1] else None) or
            obs['self_heat']!=expected_heat):
        raise ValueError('Native reset observation differs from the exact configured initial state.')
    for record in state['heat_admin']['robots']:
        if any(record[key] for key in ('heat_tenths','overheat_locked','permanent_latched',
                'firing_locked','detected_shots','blocked_fire_ticks')):
            raise ValueError('Native reset cannot contain pre-existing heat or firing accounting.')
    damage = state['damage_admin']
    maxima = [sum(robot['profile']['max_hp'] for robot in config['robots'] if robot['team']==team) for team in (0,1)]
    enabled = config['attack_damage_strategy']==DAMAGE_STRATEGIES[1]
    if (damage['remaining_robot_hp']!=maxima or damage['complete']!=enabled or
            damage['attack_damage']!=([0,0] if enabled else None) or damage['last_sequence']!=0 or
            any(damage[key]!=[0,0] for key in ('actual_hp_loss','excluded_hp_loss',
                'penalty_attack_damage','nominal_projectile_damage'))):
        raise ValueError('Native reset damage accounting differs from the configured initial totals.')


def _unique_object(pairs):
    obj = {}
    for key,value in pairs:
        if key in obj:
            raise ValueError('Duplicate JSON keys are not permitted.')
        obj[key] = value
    return obj


class _Native(NativePipe):
    """Bounded strict line transport, separate from legacy modes."""
    def __init__(self, binary):
        super().__init__(binary, MAX_BYTES)

    def command(self, command):
        wire = (command+'\n').encode('ascii')
        if len(wire) > MAX_LINE_BYTES:
            raise ValueError('Roster native command exceeds the 1 MiB bound.')
        try:
            line = self.exchange(wire, 20,
                                 write_timeout='Roster native command timed out; candidate invalid.',
                                 read_timeout='Roster native response timed out; candidate invalid.',
                                 stopped='Roster native stopped without a response.',
                                 oversize='Roster native response exceeds bounded size.')
            result = json.loads(line,object_pairs_hook=_unique_object,parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Non-finite native JSON.')))
            if type(result) is not dict:
                raise ValueError('Native response must be an object.')
            if 'error' in result:
                exact(result,('error',),'native.error')
                if type(result['error']) is not str or len(result['error'])>2048:
                    raise ValueError('Malformed native error response.')
                raise ValueError(result['error'])
            return result
        except Exception:
            self.close()
            raise


class RosterProcess:
    def __init__(self,binary=None):
        self.binary = Path(binary) if binary else ROOT/'build/rmuc_roster'
        self._native = None
        self._build = build_id()
        self._frames = []
        self.reset({'observer':ids_of(DEFAULT_CONFIG)[0],'config':DEFAULT_CONFIG})

    def _guard_source(self):
        if self._build != build_id():
            raise ValueError('Roster source changed. Rebuild and restart before continuing; run unchanged.')

    def _candidate(self,observer,config):
        config = validate_config(config)
        selected_observer(observer,config)
        self._guard_source()
        candidate = _Native(self.binary)
        try:
            state = candidate.command(reset_tokens(observer,config))
            self._check_state(state,observer,0,config)
            if state['frames']:
                raise ValueError('New roster state cannot contain resolved frames.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        return candidate,state

    def _check_state(self,state,observer,tick,config=None):
        config = self._config if config is None else config
        exact(state,('build','schema','profile','observation_profile','observation_version','official','observer',
            'tick','terminal','hash','config','result','damage_admin','heat_admin','observation','last_sources','frames'),'native.state')
        if state['build'] != self._build:
            raise ValueError('Roster binary is stale; rebuild and restart before continuing.')
        for key,expected in [('schema',1),('observation_version',1),('tick',tick),('terminal',int(tick==MAX_TICKS))]:
            integer(state[key],expected,expected,'native.'+key)
        if not 0<=tick<=MAX_TICKS or state['official'] is not False or state['profile'] != PROFILE or state['observation_profile'] != OBSERVATION or state['observer'] != observer:
            raise ValueError('Native roster identity, tick or observer differs from requested state.')
        integer(state['hash'],0,0xffffffff,'native.hash')
        if canonical(state['config']) != canonical(config):
            raise ValueError('Native roster configuration differs from committed configuration.')
        validate_observation(state['observation'],observer,tick,config)
        validate_heat_admin(state['heat_admin'],config,observer,state['observation'],tick)
        validate_damage_admin(state['damage_admin'],config,observer,state['observation'])
        validate_result(state['result'],state['terminal'],config,state['observation']['score'],state['damage_admin'])
        if tick==0:
            if state['last_sources'] is not None:
                raise ValueError('Initial state cannot claim input sources.')
        else:
            validate_sources(state['last_sources'],config,'native.last_sources')
        if type(state['frames']) is not list or len(state['frames'])>MAX_BATCH or len(state['frames'])>tick:
            raise ValueError('Native frame response has invalid size.')
        for index,frame in enumerate(state['frames']):
            validate_frame(frame,tick-len(state['frames'])+index,config)
        if state['frames'] and (state['hash'] != state['frames'][-1]['hash'] or state['last_sources'] != [{k:item[k] for k in ('id','kind','sdk_version','controller_id')} for item in state['frames'][-1]['inputs']]):
            raise ValueError('Native state differs from its resolved last frame.')
        if tick==0:
            validate_initial_state(state,observer,config)

    def _public(self,state):
        return copy.deepcopy({key:state[key] for key in ('build','schema','profile','observation_profile',
            'observation_version','official','observer','tick','terminal','config','result','observation','last_sources')})

    def _swap(self,candidate,frames,config):
        previous = self._native
        self._native = candidate
        self._frames = frames
        self._config = copy.deepcopy(config)
        if previous is not None:
            previous.close()

    def reset(self,data):
        exact(data,('observer','config'),'reset')
        config = validate_config(data['config'])
        observer = selected_observer(data['observer'],config)
        candidate,state = self._candidate(observer,config)
        self._initial_hash = state['hash']
        self._swap(candidate,[],config)
        return self._public(state)

    def _check_history_tail(self,state,frames,initial_hash):
        expected_hash = frames[-1]['hash'] if frames else initial_hash
        expected_sources = ([{k:item[k] for k in ('id','kind','sdk_version','controller_id')}
            for item in frames[-1]['inputs']] if frames else None)
        if state['hash'] != expected_hash or state['last_sources'] != expected_sources:
            raise ValueError('Native state differs from the committed action history.')

    def state(self,observer=None):
        self._guard_source()
        observer = ids_of(self._config)[0] if observer is None else selected_observer(observer,self._config)
        state = self._native.command('STATE '+observer)
        self._check_state(state,observer,len(self._frames))
        self._check_history_tail(state,self._frames,self._initial_hash)
        if state['frames']:
            raise ValueError('State read cannot return authoritative action frames.')
        return self._public(state)

    def _policy_metadata(self):
        candidate,_ = self._candidate(ids_of(self._config)[0],self._config)
        try:
            result = candidate.command('POLICY_META')
            exact(result,('policy',),'policy_response')
            policy = result['policy']
            exact(policy,('sdk_version','controller_id','execution','observation','state_mode','decision_hz'),'policy')
            integer(policy['sdk_version'],1,1,'policy.sdk_version')
            identifier(policy['controller_id'],'policy.controller_id')
            integer(policy['decision_hz'],60,60,'policy.decision_hz')
            if policy['execution']!='trusted-local-native' or policy['observation']!=OBSERVATION or policy['state_mode']!='stateless':
                raise ValueError('Unsupported compiled roster controller contract.')
            return {**policy,'watchdog_seconds':20}
        finally:
            candidate.close()

    def meta(self):
        self.state()
        try:
            policy,policy_error = self._policy_metadata(),None
        except (ValueError,RuntimeError,KeyError,TypeError,OSError):
            policy,policy_error = None,'Compiled policy metadata unavailable; rebuild/restart or explicitly select manual sources.'
        return {'schema':1,'profile':PROFILE,'observation_profile':OBSERVATION,'observation_version':1,
            'official':False,'build':self._build,'engine':ENGINE,'hz':60,'duration':MAX_TICKS,
            'max_batch':MAX_BATCH,'max_robots':MAX_ROBOTS,'max_team_robots':MAX_TEAM_ROBOTS,
            'default_config':copy.deepcopy(DEFAULT_CONFIG),'boundary_policies':list(BOUNDARIES),
            'credit_strategies':list(CREDITS),'attack_damage_strategies':list(DAMAGE_STRATEGIES),
            'heat_strategies':list(HEAT_STRATEGIES),'heat_threshold_policies':list(HEAT_THRESHOLDS),
            'policy':policy,'policy_error':policy_error,'limitations':list(LIMITATIONS)}

    def _restore(self,candidate,frames,observer,config,initial_hash=None):
        for begin in range(0,len(frames),MAX_BATCH):
            batch = frames[begin:begin+MAX_BATCH]
            tokens = ['FRAMES',len(batch),observer,len(config['robots'])]
            for frame in batch:
                tokens.extend(frame_tokens(frame))
            result = candidate.command(' '.join(map(str,tokens)))
            self._check_state(result,observer,begin+len(batch),config)
            if result['frames'] != batch or result['hash'] != batch[-1]['hash']:
                raise ValueError('Roster replay diverged; committed run unchanged.')
        result = candidate.command('STATE '+observer)
        self._check_state(result,observer,len(frames),config)
        if result['frames']:
            raise ValueError('Restored state read cannot contain resolved frames.')
        self._check_history_tail(result,frames,self._initial_hash if initial_hash is None else initial_hash)
        return result

    def step(self,data):
        self._guard_source()
        exact(data,('count','observer','controllers'),'step')
        count = integer(data['count'],1,MAX_BATCH,'count')
        observer = selected_observer(data['observer'],self._config)
        requests = data['controllers']
        if type(requests) is not list or len(requests)!=len(self._config['robots']):
            raise ValueError('controllers requires exactly one source for every configured robot ID.')
        sources,actions = {},{}
        for item in requests:
            if type(item) is not dict:
                raise ValueError('Controller must be an object.')
            rid = robot_id(item.get('id'),'controller.id')
            if rid not in ids_of(self._config) or rid in sources:
                raise ValueError('Controller IDs must cover each configured robot exactly once.')
            if item.get('kind')=='manual':
                exact(item,('id','kind','action'),'controller')
                actions[rid] = action_fields(item['action'],'controller.action')
                sources[rid] = {'id':rid,'kind':'manual','sdk_version':1,'controller_id':None}
            elif item.get('kind')=='policy':
                exact(item,('id','kind','sdk_version','controller_id'),'controller')
                validate_source(item,'controller')
                actions[rid] = [0,0,0,0,0]
                sources[rid] = dict(item)
            else:
                raise ValueError('Each controller source must explicitly be manual or policy.')
        expected_sources = [sources[rid] for rid in ids_of(self._config)]
        if any(item['kind']=='policy' for item in expected_sources):
            policy = self._policy_metadata()
            if any(item['kind']=='policy' and item['controller_id']!=policy['controller_id'] for item in expected_sources):
                raise ValueError('Requested controller identity differs from compiled policy; run unchanged.')
        tokens = ['STEP',count,observer,len(expected_sources)]
        for item in expected_sources:
            tokens += [item['id'],item['kind'],1,*actions[item['id']]]
        candidate,_ = self._candidate(observer,self._config)
        try:
            self._restore(candidate,self._frames,observer,self._config)
            result = candidate.command(' '.join(map(str,tokens)))
            frames = result.get('frames')
            if type(frames) is not list or not 1<=len(frames)<=count:
                raise ValueError('Native step batch size differs from request; run unchanged.')
            self._check_state(result,observer,len(self._frames)+len(frames))
            if len(frames)<count and not result['terminal']:
                raise ValueError('Incomplete nonterminal native step batch; run unchanged.')
            for frame in frames:
                frame_sources = [{k:item[k] for k in ('id','kind','sdk_version','controller_id')} for item in frame['inputs']]
                if frame_sources != expected_sources:
                    raise ValueError('Native sources differ from per-ID requested sources; run unchanged.')
                for item in frame['inputs']:
                    if item['kind']=='manual' and item['action']!=actions[item['id']]:
                        raise ValueError('Native resolved manual action differs from request; run unchanged.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        self._swap(candidate,self._frames+frames,self._config)
        return {**self._public(result),'advanced':len(frames)}

    def replay(self):
        self._guard_source()
        observer = ids_of(self._config)[0]
        state = self._native.command('STATE '+observer)
        self._check_state(state,observer,len(self._frames))
        self._check_history_tail(state,self._frames,self._initial_hash)
        if state['frames']:
            raise ValueError('Replay state read cannot contain unexpected frames.')
        return {'schema':REPLAY_SCHEMA,'profile':PROFILE,'engine':ENGINE,'build':self._build,
            'observation_profile':OBSERVATION,'observation_version':1,'official':False,
            'config':copy.deepcopy(self._config),'initial_hash':self._initial_hash,
            'frames':copy.deepcopy(self._frames),'final_hash':state['hash']}

    def import_replay(self,data):
        self._guard_source()
        exact(data,('observer','replay'),'import')
        replay = data['replay']
        exact(replay,('schema','profile','engine','build','observation_profile','observation_version','official','config','initial_hash','frames','final_hash'),'replay')
        if len(canonical(replay))>MAX_BYTES:
            raise ValueError('Roster replay exceeds 96 MiB.')
        if replay['schema']!=REPLAY_SCHEMA or replay['profile']!=PROFILE or replay['engine']!=ENGINE or replay['build']!=self._build or replay['observation_profile']!=OBSERVATION or type(replay['observation_version']) is not int or replay['observation_version']!=1 or replay['official'] is not False:
            raise ValueError('Roster replay identity differs; use the original source build.')
        config = validate_config(replay['config'])
        observer = selected_observer(data['observer'],config)
        for key in ('initial_hash','final_hash'):
            integer(replay[key],0,0xffffffff,'replay.'+key)
        if type(replay['frames']) is not list or len(replay['frames'])>MAX_TICKS:
            raise ValueError('Replay must contain at most 25200 frames.')
        frames = []
        for tick,original in enumerate(replay['frames']):
            if type(original) is not dict or type(original.get('inputs')) is not list or any(type(item) is not dict or type(item.get('id')) is not str for item in original['inputs']):
                raise ValueError('Malformed replay input records.')
            frame = {**original,'inputs':sorted(original['inputs'],key=lambda item:item['id'])}
            validate_frame(frame,tick,config)
            frames.append(frame)
        if (frames and frames[-1]['hash']!=replay['final_hash']) or (not frames and replay['initial_hash']!=replay['final_hash']):
            raise ValueError('Replay final hash does not match its recorded tail.')
        candidate,initial = self._candidate(observer,config)
        try:
            if initial['hash']!=replay['initial_hash']:
                raise ValueError('Replay initial configuration hash differs; run unchanged.')
            result = self._restore(candidate,frames,observer,config,initial['hash'])
            if result['hash']!=replay['final_hash']:
                raise ValueError('Replay final hash mismatch; run unchanged.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        self._initial_hash = initial['hash']
        self._swap(candidate,copy.deepcopy(frames),config)
        return {**self._public(result),'verified_frames':len(frames)}

    def close(self):
        if self._native is not None:
            self._native.close()


RosterSession = RosterProcess
