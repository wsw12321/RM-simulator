"""A single experimental tactical world: combat, capture, public score and timeout.

Reuses unchanged filtered-combat transport/action validation. Public responses
expose one local combat observation plus deliberately public objective broadcasts.
Trusted local code only; not an anti-cheat or secure execution environment.
"""
import copy
from pathlib import Path
from combat_mode.bridge import (_Native, exact, integer, identifier, canonical,
    action_fields, validate_input, validate_frame, frame_tokens,
    validate_observation as validate_combat_observation)
from .build_id import build_id

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'experimental-tactical-v1'
OBSERVATION = 'experimental-tactical-public-control-v2'
OBSERVATION_VERSION = 2
REPLAY_SCHEMA = 'rmuc-experimental-tactical-replay.v1'
ENGINE = 'integer-mm-tactical-v3'
MAX_BYTES = 48 * 1024 * 1024
MAX_TICKS = 25200
MAX_BATCH = 600
BOUNDARIES = ('strict-reject-unadjudicated-v1', 'ENGINEERING-NOT-OFFICIAL-timer-before-input-v1')
CREDITS = ('disabled', 'EXPERIMENTAL-end-of-second-v1')
DAMAGE_STRATEGIES = ('missing-adjudicated-attack-damage', 'EXPERIMENTAL-reconciled-projectile-hp-loss-v1')
HEAT_MODULE = 'rmuc2027-heat-v1.0.0-module.1'
HEAT_SOURCE_VERSION = 'RMUC2027_CN_V1.0.0_20261015'
HEAT_SOURCE_SHA256 = '9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be'
DAMAGE_MODULE = 'rmuc2027-damage-v1.0.0-module.1'
DAMAGE_SOURCE_VERSION = 'RMUC2027_CN_V1.0.0_20261015'
DAMAGE_SOURCE_SHA256 = '9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be'
HEAT_STRATEGIES = ('disabled', 'EXPERIMENTAL-fixed-level1-cooling-priority-17mm-v1')
HEAT_THRESHOLDS = ('strict-reject-unadjudicated', 'ENGINEERING-prose-greater-temporary-at-equality-v1',
                   'ENGINEERING-flowchart-greater-equal-v1')
SELECTED_CONFIG = ('boundary_policy', 'credit_strategy', 'attack_damage_strategy',
                   'heat_strategy', 'heat_threshold_policy')
DEFAULT_CONFIG = {'boundary_policy': BOUNDARIES[1], 'credit_strategy': CREDITS[1],
                  'attack_damage_strategy': DAMAGE_STRATEGIES[1], 'heat_strategy': HEAT_STRATEGIES[1],
                  'heat_threshold_policy': HEAT_THRESHOLDS[0]}
STATIC_CONFIG = {
    'heat_death_policy': 'EXPERIMENTAL-observed-shot-before-death-reset-v1',
    'heat_boundary_policy': 'EXPERIMENTAL-cooling-before-fire-gate-and-shot-v1',
    'geometry_strategy': 'EXPERIMENTAL-inclusive-center-rectangle-v1',
    'eligibility_strategy': 'EXPERIMENTAL-one-alive-eligible-robot-per-team-v1',
    'death_strategy': 'EXPERIMENTAL-immediate-death-deactivation-v1',
    'lifecycle_strategy': 'EXPERIMENTAL-latched-temporary-no-lifecycle-v1',
    'clock_strategy': 'EXPERIMENTAL-ceil-sampled-ms-v1',
    'phase_order': 'EXPERIMENTAL-cooling-gate-combat-shot-heat-death-damage-capture-credit-judge-v3',
    'observation_strategy': OBSERVATION, 'duration_ticks': MAX_TICKS, 'hz': 60,
    'stop_on_disable': False, 'respawn': False, 'base_hp_sentinel': 1,
    'remaining_hp_strategy': 'EXPERIMENTAL-one-robot-per-team-connected-not-dismissed-v1'}
ZONES = [
    {'id': 'red-outpost', 'rect': {'x1':4300,'y1':700,'x2':5900,'y2':2300}, 'original_owner':0},
    {'id': 'blue-outpost', 'rect': {'x1':6100,'y1':5300,'x2':7700,'y2':6900}, 'original_owner':1},
    {'id': 'red-base', 'rect': {'x1':600,'y1':5600,'x2':2200,'y2':7200}, 'original_owner':0},
    {'id': 'blue-base', 'rect': {'x1':9800,'y1':400,'x2':11400,'y2':2000}, 'original_owner':1},
]
LIMITATIONS = [
    'One experimental scene; synthetic arena/robot physics, not a full official RMUC match.',
    '17mm heat is an explicit experimental borrowed level-1 cooling-priority profile: Q0=40, cooling=12/s, +10 per detected shot; synthetic HP100 and geometry are unchanged.',
    'Cooling uses exact tenths at each 100ms boundary before the firing gate and detected shot; this phase is an engineering choice.',
    'Threshold equality at Q0+100 is rejected by default because prose and figure conflict; named engineering interpretations are selectable.',
    'First-person video interruption is not simulated. Heat reset after an observed same-tick shot and death is an explicit engineering order; permanent firing locks survive death.',
    'Heat locks consume no ammunition, deduct no HP and earn no attack damage. Only own heat is public; the tactical map is not first-person video.',
    'One 60 Hz authority orders combat, reconciled damage, qualified detection, activation, capture, credit and timeout.',
    'Default preset explicitly enables engineering timer-before-input order, end-of-second credits and experimental reconciled projectile HP loss.',
    'Only live center-in-rectangle occupancy is qualified; death immediately clears activation as an experimental assumption.',
    'Temporary control is latched; no restoration, outpost lifecycle, respawn or destructible bases.',
    'Objective ownership, control rates and score are deliberate GLOBAL PUBLIC experimental broadcasts.',
    'Capture progress, detection/activation, grace deadlines and hidden occupancy are not exposed.',
    'Local combat sensing retains experimental6000mm center-range and wall LOS filtering.',
    'Combat does not end on robot disable; the world continues to420s.',
    'Control-tied timeout compares a private actual-deducted-HP ledger, then surviving robot HP; equal values draw. Missing-ledger strategy remains unresolved.',
    'Attack damage counts opposing penalties and excludes collision/offline loss; friendly-fire attribution is unresolved. This scene only produces projectile damage.',
    'Live attack totals, damage events and opponent HP remain private. Remaining HP assumes one connected, non-dismissed robot per team.',
    'Trusted shared-process C++ only; stateless policy purity and security isolation are not enforced.',
    'Developer replay exposes both robot inputs and diagnostic hashes, not a filtered player view or authenticated record.',
    'Candidate action-prefix reconstruction is O(history); no real-time performance guarantee.',
]


def validate_config_selection(config):
    exact(config, SELECTED_CONFIG, 'config')
    choices = (BOUNDARIES, CREDITS, DAMAGE_STRATEGIES, HEAT_STRATEGIES, HEAT_THRESHOLDS)
    if any(type(config[key]) is not str or config[key] not in values
           for key, values in zip(SELECTED_CONFIG, choices)):
        raise ValueError('Unsupported tactical boundary, credit, attack-damage or heat strategy; no silent fallback.')
    return tuple(values.index(config[key]) for key, values in zip(SELECTED_CONFIG, choices))


def expanded_config(config):
    boundary, credits, damage, heat, threshold = validate_config_selection(config)
    return {**STATIC_CONFIG, **config, 'preset': 'EXPERIMENTAL-playable-engineering-v3'
            if boundary == 1 and credits == 1 and damage == 1 and heat == 1 and threshold == 0 else 'EXPERIMENTAL-custom-v3'}


def validate_full_config(config):
    exact(config, (*STATIC_CONFIG, *SELECTED_CONFIG, 'preset'), 'config')
    selected = {k: config[k] for k in SELECTED_CONFIG}
    if canonical(config) != canonical(expanded_config(selected)):
        raise ValueError('Tactical configuration differs from the versioned experimental contract.')
    return selected


def validate_observation(obs, observer, tick, config):
    exact(obs, ('sdk_version','observation_version','profile','combat','self_heat','zones','score','rate','qualified_ms','duration_ms','credits_enabled'), 'observation')
    integer(obs['sdk_version'],1,1,'observation.sdk_version')
    integer(obs['observation_version'],2,2,'observation.observation_version')
    if obs['profile'] != OBSERVATION:
        raise ValueError('Unsupported tactical observation profile.')
    validate_combat_observation(obs['combat'], observer, tick)
    validate_self_heat(obs['self_heat'], config)
    integer(obs['qualified_ms'],(tick*1000+59)//60,(tick*1000+59)//60,'observation.qualified_ms')
    integer(obs['duration_ms'],420000,420000,'observation.duration_ms')
    enabled = config['credit_strategy'] != 'disabled'
    if type(obs['credits_enabled']) is not bool or obs['credits_enabled'] != enabled:
        raise ValueError('Tactical observation credits status differs from configuration.')
    if type(obs['zones']) is not list or len(obs['zones']) != 4:
        raise ValueError('Expected exactly four public objective records.')
    for index, zone in enumerate(obs['zones']):
        exact(zone, ('id','rect','original_owner','controller','degree'), 'observation.zone')
        if canonical({k:zone[k] for k in ('id','rect','original_owner')}) != canonical(ZONES[index]):
            raise ValueError('Unexpected synthetic objective geometry or identity.')
        integer(zone['controller'],0,1,'observation.zone.controller')
        if zone['degree'] not in ('full','temporary'):
            raise ValueError('Unsupported objective control degree.')
        if zone['degree'] == 'full' and zone['controller'] != zone['original_owner']:
            raise ValueError('This latched scenario cannot invent enemy full control.')
    if type(obs['rate']) is not list or len(obs['rate']) != 2:
        raise ValueError('Expected two public control rates.')
    for value in obs['rate']:
        integer(value,0,4,'observation.rate')
    # Check the public formula, not hidden detector state.
    rate = [0,0]
    for a,b,points in ((0,1,1),(2,3,3)):
        if obs['zones'][a]['controller'] == obs['zones'][b]['controller']:
            rate[obs['zones'][a]['controller']] += points
    if obs['rate'] != rate:
        raise ValueError('Public control rates do not match public ownership.')
    if enabled:
        if type(obs['score']) is not list or len(obs['score']) != 2:
            raise ValueError('Expected two public experimental scores.')
        for value in obs['score']:
            integer(value,0,4*(tick//60),'observation.score')
    elif obs['score'] is not None:
        raise ValueError('Disabled scoring must not publish fabricated totals.')


def validate_heat_flags(value, name):
    integer(value['heat_tenths'], 0, 12000, name + '.heat_tenths')
    for key in ('overheat_locked', 'permanent_latched', 'firing_locked'):
        if type(value[key]) is not bool:
            raise ValueError('Native heat lock flags must be boolean.')
    if value['overheat_locked'] and value['heat_tenths'] == 0:
        raise ValueError('Temporary heat lock must clear when heat reaches zero.')
    locked = value['overheat_locked'] or value['permanent_latched']
    if value['firing_locked'] != locked:
        raise ValueError('Native firing heat locks contradict their thermal state.')
    if value['heat_tenths'] > 400 and not locked:
        raise ValueError('Over-limit heat must not silently permit firing.')


def validate_self_heat(value, config):
    exact(value, ('enabled', 'heat_tenths', 'limit_tenths', 'cooling_per_second',
                  'overheat_locked', 'permanent_latched', 'firing_locked'), 'observation.self_heat')
    enabled = config['heat_strategy'] == HEAT_STRATEGIES[1]
    if type(value['enabled']) is not bool or value['enabled'] != enabled:
        raise ValueError('Own heat enabled flag differs from configuration.')
    integer(value['limit_tenths'], 400, 400, 'heat.limit_tenths')
    integer(value['cooling_per_second'], 12, 12, 'heat.cooling_per_second')
    validate_heat_flags(value, 'self_heat')
    if not enabled and (value['heat_tenths'] != 0 or any(value[k] for k in
            ('overheat_locked', 'permanent_latched', 'firing_locked'))):
        raise ValueError('Disabled heat strategy must not invent heat or locks.')


def validate_heat_admin(value, config, observer, observation, tick):
    """Validate native thermal diagnostics without exposing the opponent's heat."""
    exact(value, ('schema', 'module', 'strategy', 'threshold_policy', 'boundary_policy', 'robots'), 'native.heat_admin')
    integer(value['schema'], 1, 1, 'heat.schema')
    if (value['module'] != HEAT_MODULE or value['strategy'] != config['heat_strategy'] or
            value['threshold_policy'] != config['heat_threshold_policy'] or
            value['boundary_policy'] != config['heat_boundary_policy']):
        raise ValueError('Native heat module or selected configuration mismatch.')
    if type(value['robots']) is not list or len(value['robots']) != 2:
        raise ValueError('Native heat accounting needs exactly two private robot records.')
    thermal_keys = ('heat_tenths', 'overheat_locked', 'permanent_latched', 'firing_locked')
    enabled = config['heat_strategy'] == HEAT_STRATEGIES[1]
    for robot in value['robots']:
        exact(robot, (*thermal_keys, 'detected_shots', 'blocked_fire_ticks'), 'native.heat_admin.robot')
        validate_heat_flags(robot, 'heat_admin.robot')
        integer(robot['detected_shots'], 0, min(120, tick), 'heat.detected_shots')
        integer(robot['blocked_fire_ticks'], 0, tick, 'heat.blocked_fire_ticks')
        if not enabled and any(robot[key] for key in robot):
            raise ValueError('Disabled heat accounting must remain zero.')
        if enabled and (robot['heat_tenths'] > 100 * robot['detected_shots'] or
                        (robot['permanent_latched'] and robot['detected_shots'] < 14)):
            raise ValueError('Heat or permanent lock cannot precede sufficient detected shots.')
    own = value['robots'][observer]
    if any(own[key] != observation['self_heat'][key] for key in thermal_keys):
        raise ValueError('Own heat observation contradicts native thermal accounting.')
    if enabled and own['detected_shots'] != 120 - observation['combat']['self']['ammo']:
        raise ValueError('Detected shots contradict actual ammunition consumption.')


def validate_damage_admin(value, config, observer, self_hp):
    """Validate privileged native accounting, then keep it outside public output.

    These are administrative integrity checks, never extra controller observations.
    Current scene has no regeneration, penalty, collision or offline HP sources.
    """
    exact(value, ('schema', 'module', 'strategy', 'complete', 'reason', 'attack_damage',
                 'remaining_robot_hp', 'actual_hp_loss', 'excluded_hp_loss',
                 'penalty_attack_damage', 'nominal_projectile_damage', 'last_sequence'), 'native.damage_admin')
    integer(value['schema'], 1, 1, 'damage.schema')
    if value['module'] != DAMAGE_MODULE or value['strategy'] != config['attack_damage_strategy']:
        raise ValueError('Native damage module or selected strategy mismatch.')
    if type(value['complete']) is not bool:
        raise ValueError('Native damage completeness must be boolean.')
    for key in ('actual_hp_loss', 'excluded_hp_loss', 'penalty_attack_damage', 'nominal_projectile_damage'):
        if type(value[key]) is not list or len(value[key]) != 2:
            raise ValueError('Native damage accounting requires exactly two team values.')
        for number in value[key]:
            upper = 1200 if key == 'nominal_projectile_damage' else 100
            integer(number, 0, upper, 'damage.' + key)
    integer(value['last_sequence'], 0, sum(value['actual_hp_loss']), 'damage.last_sequence')
    if bool(value['last_sequence']) != bool(sum(value['actual_hp_loss'])):
        raise ValueError('Native damage sequence contradicts its positive deduction entries.')
    hp = value['remaining_robot_hp']
    if hp is not None:
        if type(hp) is not list or len(hp) != 2:
            raise ValueError('Native remaining HP must be null or exactly two verified totals.')
        for number in hp:
            integer(number, 0, 100, 'damage.remaining_robot_hp')
        if hp[observer] != self_hp:
            raise ValueError('Native remaining HP contradicts the selected combat observation.')
    if value['complete']:
        attack = value['attack_damage']
        if value['reason'] is not None or type(attack) is not list or len(attack) != 2:
            raise ValueError('Completed native damage accounting requires two totals and no unresolved reason.')
        for number in attack:
            integer(number, 0, 9223372036854775807, 'damage.attack_damage')
        if config['attack_damage_strategy'] != DAMAGE_STRATEGIES[1]:
            raise ValueError('Missing-ledger strategy cannot publish adjudicated damage.')
        if ((hp is not None and value['actual_hp_loss'] != [100 - number for number in hp]) or
                value['excluded_hp_loss'] != [0, 0] or value['penalty_attack_damage'] != [0, 0] or
                attack != list(reversed(value['actual_hp_loss'])) or
                any(nominal < actual or nominal % 10 or bool(nominal) != bool(actual)
                    for nominal, actual in zip(value['nominal_projectile_damage'], attack))):
            raise ValueError('Native projectile damage accounting contradicts actual HP loss.')
    else:
        expected_reason = (DAMAGE_STRATEGIES[0] if config['attack_damage_strategy'] == DAMAGE_STRATEGIES[0]
                           else 'unadjudicated-native-damage')
        if value['attack_damage'] is not None or value['reason'] != expected_reason:
            raise ValueError('Incomplete native damage accounting has an unsupported reason or fabricated totals.')
        if value['excluded_hp_loss'] != [0, 0] or value['penalty_attack_damage'] != [0, 0]:
            raise ValueError('This projectile-only scenario cannot invent penalty, collision or offline sources.')
        if config['attack_damage_strategy'] == DAMAGE_STRATEGIES[0] and (
                any(value[key] != [0, 0] for key in ('actual_hp_loss', 'nominal_projectile_damage'))
                or value['last_sequence'] != 0):
            raise ValueError('Disabled damage accounting must not claim settled entries.')


def validate_result(result, terminal, config, score, damage):
    exact(result, ('outcome', 'reason'), 'result')
    if not terminal:
        expected = {'outcome': 'ongoing', 'reason': 'not-ended'}
    elif config['credit_strategy'] == 'disabled':
        expected = {'outcome': 'unscored-timeout', 'reason': 'credit-strategy-disabled'}
    else:
        stages = [('timeout-control', score)]
        if score[0] == score[1] and not damage['complete']:
            expected = {'outcome': 'unresolved-timeout-tie', 'reason': DAMAGE_STRATEGIES[0]}
        else:
            if damage['complete']:
                stages.extend([('timeout-attack-damage', damage['attack_damage']),
                               ('timeout-surviving-robot-hp', damage['remaining_robot_hp'])])
            expected = {'outcome': 'draw', 'reason': 'timeout-equal'}
            for reason, values in stages:
                if values is None:
                    expected = {'outcome': 'unresolved-timeout-tie', 'reason': 'missing-adjudicated-surviving-robot-hp'}
                    break
                if values[0] != values[1]:
                    expected = {'outcome': 'red-wins' if values[0] > values[1] else 'blue-wins', 'reason': reason}
                    break
    if result != expected:
        raise ValueError('Tactical result contradicts the configured control, attack-damage and surviving-HP comparison order.')


class TacticalProcess:
    def __init__(self, binary=None):
        self.binary = Path(binary) if binary else ROOT / 'build/rmuc_tactical'
        self._native = None
        self._build = build_id()
        self._frames = []
        self.reset({'observer': 0, 'config': dict(DEFAULT_CONFIG)})

    def _guard_source(self):
        if self._build != build_id():
            raise ValueError('Tactical source changed. Rebuild and restart before advancing; run unchanged.')

    def _candidate(self, observer, config):
        self._guard_source()
        candidate = _Native(self.binary)
        try:
            boundary, credits, damage, heat, threshold = validate_config_selection(config)
            state = candidate.command(f'RESET {observer} {boundary} {credits} {damage} {heat} {threshold}')
            if state.get('build') != self._build:
                raise ValueError('Tactical binary is stale. Run bash tools/build.sh and restart.')
            self._check_state(state, observer, 0)
            if state['config'] != expanded_config(config):
                raise RuntimeError('Tactical native reset configuration mismatch.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        return candidate, state

    def _swap(self, candidate, frames, config):
        previous = self._native
        self._native = candidate
        self._frames = copy.deepcopy(frames)
        self._config = copy.deepcopy(config)
        if previous is not None:
            previous.close()

    def _check_state(self, state, observer, tick=None):
        exact(state, ('build', 'schema', 'profile', 'observation_profile', 'observation_version', 'official', 'observer',
                      'tick', 'terminal', 'hash', 'observation', 'last_sources', 'frames', 'config', 'result', 'damage_admin', 'heat_admin'), 'native.state')
        if (type(state) is not dict or state.get('build') != self._build or
                state.get('schema') != 1 or type(state.get('schema')) is not int or
                state.get('profile') != PROFILE or state.get('observation_profile') != OBSERVATION or
                type(state.get('observation_version')) is not int or state.get('observation_version') != OBSERVATION_VERSION or
                state.get('official') is not False or state.get('observer') != observer):
            raise RuntimeError('Tactical native state identity mismatch; run invalid.')
        integer(state.get('tick'), 0, MAX_TICKS, 'native.tick')
        integer(state.get('terminal'), 0, 1, 'native.terminal')
        integer(state.get('hash'), 0, 4294967295, 'native.hash')
        if tick is not None and state['tick'] != tick:
            raise RuntimeError('Tactical native tick mismatch; run invalid.')
        integer(state.get('observer'), observer, observer, 'native.observer')
        validate_full_config(state['config'])
        validate_observation(state.get('observation'), observer, state['tick'], state['config'])
        validate_heat_admin(state['heat_admin'], state['config'], observer, state['observation'], state['tick'])
        validate_damage_admin(state['damage_admin'], state['config'], observer,
                              state['observation']['combat']['self']['hp'])
        validate_result(state['result'], state['terminal'], state['config'],
                        state['observation']['score'], state['damage_admin'])
        if state['terminal'] != int(state['tick'] == MAX_TICKS):
            raise RuntimeError('Tactical terminal status must follow the420s clock, not a robot disable.')
        sources = state.get('last_sources')
        if state['tick'] == 0:
            if sources is not None:
                raise RuntimeError('Initial tactical state cannot have input source labels.')
        else:
            if type(sources) is not list or len(sources) != 2:
                raise RuntimeError('Tactical state needs two last-input source labels.')
            for source in sources:
                exact(source, ('kind', 'sdk_version', 'controller_id'), 'native.last_sources')
                validate_input({**source, 'action': [0, 0, 0, 0, 0]}, 'native.last_sources')

    def _public(self, state):
        # Explicit allowlist. Never return native internal frames or global hash.
        return copy.deepcopy({key: state[key] for key in ('build', 'schema', 'profile',
            'observation_profile', 'observation_version', 'official', 'observer', 'tick', 'terminal', 'observation', 'last_sources', 'config', 'result')})

    def reset(self, data):
        exact(data, ('observer', 'config'), 'reset')
        observer = integer(data['observer'], 0, 1, 'observer')
        candidate, state = self._candidate(observer, data['config'])
        self._initial_hash = state['hash']
        self._swap(candidate, [], data['config'])
        return self._public(state)

    def state(self, observer=0):
        integer(observer, 0, 1, 'observer')
        state = self._native.command(f'STATE {observer}')
        self._check_state(state, observer, len(self._frames))
        if state['config'] != expanded_config(self._config):
            raise RuntimeError('Tactical live configuration differs from the committed run.')
        return self._public(state)

    def _policy_metadata(self):
        candidate, _ = self._candidate(0, self._config)
        try:
            result = candidate.command('POLICY_META')
            policy = result.get('policy')
            exact(policy, ('sdk_version', 'controller_id', 'execution', 'observation',
                           'state_mode', 'decision_hz'), 'policy')
            integer(policy['sdk_version'], 1, 1, 'policy.sdk_version')
            identifier(policy['controller_id'], 'policy.controller_id')
            if (policy['execution'] != 'trusted-local-native' or policy['observation'] != OBSERVATION or
                    policy['state_mode'] != 'stateless' or type(policy['decision_hz']) is not int or
                    policy['decision_hz'] != 60):
                raise ValueError('Unsupported compiled tactical policy contract.')
            return {**policy, 'watchdog_seconds': 20}
        finally:
            candidate.close()

    def meta(self):
        self.state(0)
        try:
            policy = self._policy_metadata()
            policy_error = None
        except (ValueError, RuntimeError, KeyError, TypeError, OSError):
            policy = None
            policy_error = 'Compiled policy metadata is unavailable; rebuild/restart or use manual control.'
        return {'schema': 1, 'profile': PROFILE, 'observation_profile': OBSERVATION,
                'observation_version': OBSERVATION_VERSION, 'official': False,
                'build': self._build, 'hz': 60, 'duration': MAX_TICKS, 'max_batch': MAX_BATCH,
                'policy': policy, 'policy_error': policy_error, 'limitations': list(LIMITATIONS),
                'default_config': expanded_config(DEFAULT_CONFIG), 'boundary_policies': list(BOUNDARIES),
                'credit_strategies': list(CREDITS), 'attack_damage_strategies': list(DAMAGE_STRATEGIES),
                'heat_module': HEAT_MODULE, 'heat_source_version': HEAT_SOURCE_VERSION,
                'heat_source_sha256': HEAT_SOURCE_SHA256, 'heat_strategies': list(HEAT_STRATEGIES), 'heat_threshold_policies': list(HEAT_THRESHOLDS),
                'damage_module': DAMAGE_MODULE, 'damage_source_version': DAMAGE_SOURCE_VERSION,
                'damage_source_sha256': DAMAGE_SOURCE_SHA256}

    def _restore(self, candidate, frames, observer, config):
        for begin in range(0, len(frames), MAX_BATCH):
            batch = frames[begin:begin + MAX_BATCH]
            tokens = ['FRAMES', len(batch), observer]
            for frame in batch:
                tokens.extend(frame_tokens(frame))
            state = candidate.command(' '.join(map(str, tokens)))
            self._check_state(state, observer, begin + len(batch))
            if (state.get('frames') != batch or state['hash'] != batch[-1]['hash'] or
                    state['config'] != expanded_config(config)):
                raise ValueError(f'Tactical replay divergence at batch beginning tick {begin}; run unchanged.')
        state = candidate.command(f'STATE {observer}')
        self._check_state(state, observer, len(frames))
        if state['config'] != expanded_config(config):
            raise RuntimeError('Restored tactical configuration differs from the replay.')
        return state

    def step(self, data):
        self._guard_source()
        exact(data, ('count', 'observer', 'controllers'), 'step')
        count = integer(data['count'], 1, MAX_BATCH, 'count')
        observer = integer(data['observer'], 0, 1, 'observer')
        if type(data['controllers']) is not list or len(data['controllers']) != 2:
            raise ValueError('controllers must contain exactly two robot sources.')
        requests = data['controllers']
        tokens = ['STEP', count, observer]
        expected_sources = []
        policy = None
        for i, item in enumerate(requests):
            if type(item) is not dict:
                raise ValueError('Controller request must be an object.')
            if item.get('kind') == 'manual':
                exact(item, ('kind', 'action'), f'controllers[{i}]')
                action = action_fields(item['action'], f'controllers[{i}].action')
                source = {'kind': 'manual', 'sdk_version': 1, 'controller_id': None}
            elif item.get('kind') == 'policy':
                exact(item, ('kind', 'sdk_version', 'controller_id'), f'controllers[{i}]')
                integer(item['sdk_version'], 1, 1, 'policy.sdk_version')
                identifier(item['controller_id'], 'policy.controller_id')
                source = dict(item)
                action = [0, 0, 0, 0, 0]
            else:
                raise ValueError('Controller kind must be manual or policy.')
            expected_sources.append(source)
            tokens += [source['kind'], 1, *action]
        if any(s['kind'] == 'policy' for s in expected_sources):
            policy = self._policy_metadata()
            if any(s['kind'] == 'policy' and s['controller_id'] != policy['controller_id'] for s in expected_sources):
                raise ValueError('Requested tactical controller differs from compiled identity; run unchanged.')
        candidate, _ = self._candidate(observer, self._config)
        try:
            self._restore(candidate, self._frames, observer, self._config)
            result = candidate.command(' '.join(map(str, tokens)))
            frames = result.get('frames')
            if type(frames) is not list or not 1 <= len(frames) <= count:
                raise RuntimeError('Invalid tactical native batch size; run unchanged.')
            for i, frame in enumerate(frames):
                validate_frame(frame, len(self._frames) + i)
                actual_sources = [{k: v[k] for k in ('kind', 'sdk_version', 'controller_id')} for v in frame['inputs']]
                if actual_sources != expected_sources:
                    raise RuntimeError('Tactical input attribution differs from requested sources; run unchanged.')
                for robot, request in enumerate(requests):
                    if request['kind'] == 'manual' and frame['inputs'][robot]['action'] != action_fields(request['action'], 'manual'):
                        raise RuntimeError('Native manual input differs; run unchanged.')
            self._check_state(result, observer, len(self._frames) + len(frames))
            if (result['hash'] != frames[-1]['hash'] or result.get('last_sources') != expected_sources or
                    result['config'] != expanded_config(self._config)):
                raise RuntimeError('Tactical state does not match its resolved frames; run unchanged.')
            if len(frames) < count and result['terminal'] != 1:
                raise RuntimeError('Tactical returned an incomplete nonterminal batch; run unchanged.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        self._swap(candidate, self._frames + frames, self._config)
        return {**self._public(result), 'advanced': len(frames)}

    def replay(self):
        state = self._native.command('STATE 0')
        self._check_state(state, 0, len(self._frames))
        if state['config'] != expanded_config(self._config):
            raise RuntimeError('Tactical replay source configuration differs from the live run.')
        return {'schema': REPLAY_SCHEMA, 'profile': PROFILE, 'engine': ENGINE,
                'observation_profile': OBSERVATION, 'observation_version': OBSERVATION_VERSION,
                'official': False, 'build': self._build,
                'config': expanded_config(self._config), 'initial_hash': self._initial_hash, 'frames': copy.deepcopy(self._frames), 'final_hash': state['hash']}

    def import_replay(self, data):
        self._guard_source()
        exact(data, ('observer', 'replay'), 'import')
        observer = integer(data['observer'], 0, 1, 'observer')
        replay = data['replay']
        exact(replay, ('schema', 'profile', 'engine', 'observation_profile', 'observation_version', 'official',
                       'build', 'config', 'initial_hash', 'frames', 'final_hash'), 'replay')
        if len(canonical(replay)) > MAX_BYTES:
            raise ValueError('Tactical replay exceeds48MiB.')
        if (replay['schema'] != REPLAY_SCHEMA or replay['profile'] != PROFILE or replay['engine'] != ENGINE or
                replay['observation_profile'] != OBSERVATION or type(replay['observation_version']) is not int or
                replay['observation_version'] != OBSERVATION_VERSION or replay['official'] is not False or replay['build'] != self._build):
            raise ValueError('Tactical replay identity differs. Use the original source build.')
        for key in ('initial_hash', 'final_hash'):
            integer(replay[key], 0, 4294967295, key)
        frames = replay['frames']
        if type(frames) is not list or len(frames) > MAX_TICKS:
            raise ValueError('Tactical replay requires at most25200 frames.')
        for tick, frame in enumerate(frames):
            validate_frame(frame, tick)
        config = validate_full_config(replay['config'])
        candidate, initial = self._candidate(observer, config)
        try:
            if initial['hash'] != replay['initial_hash']:
                raise ValueError('Tactical initial hash differs; run unchanged.')
            result = self._restore(candidate, frames, observer, config)
            self._check_state(result, observer, len(frames))
            if result['hash'] != replay['final_hash']:
                raise ValueError('Tactical final hash mismatch; run unchanged.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        self._initial_hash = initial['hash']
        self._swap(candidate, frames, config)
        return {**self._public(result), 'verified_frames': len(frames)}

    def close(self):
        if self._native is not None:
            self._native.close()
