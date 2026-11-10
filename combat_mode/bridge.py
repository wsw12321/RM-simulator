"""Loopback adapter for the separate trusted-local filtered combat experiment.

All simulation and sensing decisions are native. This adapter validates requests,
keeps failed candidate processes out of the live run, and exposes ONE perspective.
It is not an anti-cheat or security boundary against native user-authored code.
"""
import copy
import json
from pathlib import Path
import re

from native_pipe import NativePipe

from .build_id import build_id

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'filtered-combat-v1'
OBSERVATION = 'experimental-range-los-v1'
REPLAY_SCHEMA = 'rmuc-filtered-combat-replay.v1'
ENGINE = 'integer-mm-combat-v1'
MAX_BYTES = 12 * 1024 * 1024
MAX_TICKS = 7200
MAX_BATCH = 120
LIMITATIONS = [
    'Experimental 2-robot synthetic combat, not official RMUC rules, sensing, or map.',
    'Both robots receive same-tick filtered snapshots; center range <=6000 mm and conservative wall LOS.',
    'Own pose/HP/ammo/cooldown, public geometry, visible enemy pose and visible projectile motion only.',
    'Global terminal notification is run-control metadata outside the policy observation.',
    'C++ is trusted local native code, not a security sandbox or anti-cheat boundary.',
    'Only stateless snapshot-based policies are supported; purity is not enforced.',
    'Replay is a developer audit export revealing both robots\' resolved inputs, not a player observation.',
    'Replay hashes are diagnostic, not cryptographic authenticity or proof that a controller produced an action.',
    'Candidate restoration repeats the resolved-action prefix; latency grows with run length.',
]


def exact(value, fields, where):
    if type(value) is not dict or set(value) != set(fields):
        raise ValueError(where + ' requires exactly ' + ', '.join(fields) + '.')


def integer(value, low, high, where):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{where} must be an integer in [{low},{high}], not boolean or float.')
    return value


def identifier(value, where):
    if type(value) is not str or not re.fullmatch('[A-Za-z][A-Za-z0-9_.-]{0,63}', value):
        raise ValueError(where + ' must be a bounded ASCII controller identifier.')
    return value


def canonical(value):
    try:
        return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()
    except (TypeError, ValueError, RecursionError) as exc:
        raise ValueError('Expected bounded JSON data.') from exc


def action_fields(action, where):
    exact(action, ('sdk_version', 'dx', 'dy', 'aim_x', 'aim_y', 'fire'), where)
    integer(action['sdk_version'], 1, 1, where + '.sdk_version')
    return [integer(action[key], low, high, where + '.' + key) for key, low, high in
            [('dx', -1000, 1000), ('dy', -1000, 1000), ('aim_x', 0, 12000),
             ('aim_y', 0, 7600), ('fire', 0, 1)]]


def validate_input(value, where):
    exact(value, ('kind', 'sdk_version', 'controller_id', 'action'), where)
    integer(value['sdk_version'], 1, 1, where + '.sdk_version')
    if value['kind'] not in ('manual', 'policy'):
        raise ValueError(where + '.kind must be manual or policy.')
    if value['kind'] == 'manual':
        if value['controller_id'] is not None:
            raise ValueError(where + ' manual controller_id must be null.')
    else:
        identifier(value['controller_id'], where + '.controller_id')
    if type(value['action']) is not list or len(value['action']) != 5:
        raise ValueError(where + '.action must have five integers.')
    action_fields(dict(zip(('sdk_version', 'dx', 'dy', 'aim_x', 'aim_y', 'fire'),
                          [value['sdk_version'], *value['action']])), where + '.action')


def validate_frame(frame, tick):
    where = f'frames[{tick}]'
    exact(frame, ('tick', 'inputs', 'observation_hashes', 'hash'), where)
    integer(frame['tick'], tick, tick, where + '.tick')
    if type(frame['inputs']) is not list or len(frame['inputs']) != 2:
        raise ValueError(where + '.inputs must contain exactly two robot inputs.')
    for i, value in enumerate(frame['inputs']):
        validate_input(value, where + f'.inputs[{i}]')
    if type(frame['observation_hashes']) is not list or len(frame['observation_hashes']) != 2:
        raise ValueError(where + '.observation_hashes requires two diagnostics.')
    for value in [*frame['observation_hashes'], frame['hash']]:
        integer(value, 0, 4294967295, where + '.hash')


def validate_observation(obs, observer, tick):
    exact(obs, ('sdk_version', 'profile', 'hz', 'tick', 'self_index', 'sensor_range_mm',
                'self', 'arena', 'enemy', 'projectiles'), 'observation')
    for key, expected in [('sdk_version', 1), ('hz', 60), ('tick', tick),
                          ('self_index', observer), ('sensor_range_mm', 6000)]:
        integer(obs[key], expected, expected, 'observation.' + key)
    if obs['profile'] != OBSERVATION:
        raise ValueError('Unsupported combat observation profile.')
    exact(obs['self'], ('x', 'y', 'yaw', 'hp', 'ammo', 'cooldown'), 'observation.self')
    for key, low, high in [('x', 0, 12000), ('y', 0, 7600), ('yaw', 0, 255),
                           ('hp', 0, 100), ('ammo', 0, 120), ('cooldown', 0, 10)]:
        integer(obs['self'][key], low, high, 'observation.self.' + key)
    expected_arena = {'width': 12000, 'height': 7600, 'radius': 280, 'speed_per_tick': 55,
        'projectile_speed_per_tick': 260, 'walls': [{'x1': 5250, 'y1': 2800, 'x2': 6750, 'y2': 4800},
        {'x1': 2600, 'y1': 1350, 'x2': 3800, 'y2': 1950},
        {'x1': 8200, 'y1': 5650, 'x2': 9400, 'y2': 6250}]}
    if canonical(obs['arena']) != canonical(expected_arena):
        raise ValueError('Unexpected public combat arena geometry.')
    if obs['enemy'] is not None:
        exact(obs['enemy'], ('x', 'y', 'yaw'), 'observation.enemy')
        for key, high in [('x', 12000), ('y', 7600), ('yaw', 255)]:
            integer(obs['enemy'][key], 0, high, 'observation.enemy.' + key)
    projectiles = obs['projectiles']
    if type(projectiles) is not list or len(projectiles) > 128:
        raise ValueError('Expected at most 128 visible projectiles.')
    for projectile in projectiles:
        exact(projectile, ('x', 'y', 'vx', 'vy'), 'observation.projectile')
        for key, low, high in [('x', 0, 12000), ('y', 0, 7600), ('vx', -260, 260), ('vy', -260, 260)]:
            integer(projectile[key], low, high, 'observation.projectile.' + key)
    if projectiles != sorted(projectiles, key=lambda p: (p['x'], p['y'], p['vx'], p['vy'])):
        raise ValueError('Visible projectile ordering must use only visible fields.')



def frame_tokens(frame):
    result = [frame['tick']]
    for value in frame['inputs']:
        result += [value['kind'], value['controller_id'] or '-', value['sdk_version'], *value['action']]
    return result + [frame['hash'], *frame['observation_hashes']]


class _Native(NativePipe):
    def __init__(self, binary):
        super().__init__(binary, MAX_BYTES)

    def command(self, command):
        try:
            line = self.exchange((command + '\n').encode(), 20,
                                 write_timeout='Combat native command timed out; candidate run invalid.',
                                 read_timeout='Combat native command timed out; candidate run invalid.',
                                 stopped='Combat native process stopped without a response.',
                                 oversize='Combat native response exceeds size bound.')
            result = json.loads(line)
            if type(result) is not dict:
                raise RuntimeError('Malformed native combat response.')
        except Exception:
            self.close()
            raise
        if 'error' in result:
            raise ValueError(result['error'])
        return result


class CombatProcess:
    def __init__(self, binary=None):
        self.binary = Path(binary) if binary else ROOT / 'build/rmuc_combat'
        self._native = None
        self._build = build_id()
        self._frames = []
        self.reset({'observer': 0})

    def _guard_source(self):
        if self._build != build_id():
            raise ValueError('Combat source changed. Rebuild and restart before advancing; run unchanged.')

    def _candidate(self, observer):
        self._guard_source()
        candidate = _Native(self.binary)
        try:
            state = candidate.command(f'RESET {observer}')
            if state.get('build') != self._build:
                raise ValueError('Combat binary is stale. Run bash tools/build.sh and restart.')
            self._check_state(state, observer, 0)
        except Exception:
            candidate.close()
            raise
        return candidate, state

    def _swap(self, candidate, frames):
        previous = self._native
        self._native = candidate
        self._frames = copy.deepcopy(frames)
        if previous is not None:
            previous.close()

    def _check_state(self, state, observer, tick=None):
        exact(state, ('build', 'schema', 'profile', 'observation_profile', 'official', 'observer',
                      'tick', 'terminal', 'hash', 'observation', 'last_sources', 'frames'), 'native.state')
        if (type(state) is not dict or state.get('build') != self._build or
                state.get('schema') != 1 or type(state.get('schema')) is not int or
                state.get('profile') != PROFILE or state.get('observation_profile') != OBSERVATION or
                state.get('official') is not False or state.get('observer') != observer):
            raise RuntimeError('Combat native state identity mismatch; run invalid.')
        integer(state.get('tick'), 0, MAX_TICKS, 'native.tick')
        integer(state.get('terminal'), 0, 1, 'native.terminal')
        integer(state.get('hash'), 0, 4294967295, 'native.hash')
        if tick is not None and state['tick'] != tick:
            raise RuntimeError('Combat native tick mismatch; run invalid.')
        integer(state.get('observer'), observer, observer, 'native.observer')
        validate_observation(state.get('observation'), observer, state['tick'])
        sources = state.get('last_sources')
        if state['tick'] == 0:
            if sources is not None:
                raise RuntimeError('Initial combat state cannot have input source labels.')
        else:
            if type(sources) is not list or len(sources) != 2:
                raise RuntimeError('Combat state needs two last-input source labels.')
            for source in sources:
                exact(source, ('kind', 'sdk_version', 'controller_id'), 'native.last_sources')
                validate_input({**source, 'action': [0, 0, 0, 0, 0]}, 'native.last_sources')

    def _public(self, state):
        # Explicit allowlist. Never return native internal frames or global hash.
        return copy.deepcopy({key: state[key] for key in ('build', 'schema', 'profile',
            'observation_profile', 'official', 'observer', 'tick', 'terminal', 'observation', 'last_sources')})

    def reset(self, data):
        exact(data, ('observer',), 'reset')
        observer = integer(data['observer'], 0, 1, 'observer')
        candidate, state = self._candidate(observer)
        self._initial_hash = state['hash']
        self._swap(candidate, [])
        return self._public(state)

    def state(self, observer=0):
        integer(observer, 0, 1, 'observer')
        state = self._native.command(f'STATE {observer}')
        self._check_state(state, observer, len(self._frames))
        return self._public(state)

    def _policy_metadata(self):
        candidate, _ = self._candidate(0)
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
                raise ValueError('Unsupported compiled combat policy contract.')
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
        return {'schema': 1, 'profile': PROFILE, 'observation_profile': OBSERVATION, 'official': False,
                'build': self._build, 'hz': 60, 'duration': MAX_TICKS, 'max_batch': MAX_BATCH,
                'policy': policy, 'policy_error': policy_error, 'limitations': list(LIMITATIONS)}

    def _restore(self, candidate, frames, observer):
        for begin in range(0, len(frames), MAX_BATCH):
            batch = frames[begin:begin + MAX_BATCH]
            tokens = ['FRAMES', len(batch), observer]
            for frame in batch:
                tokens.extend(frame_tokens(frame))
            state = candidate.command(' '.join(map(str, tokens)))
            self._check_state(state, observer, begin + len(batch))
            if state.get('frames') != batch or state['hash'] != batch[-1]['hash']:
                raise ValueError(f'Combat replay divergence at batch beginning tick {begin}; run unchanged.')
        return candidate.command(f'STATE {observer}')

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
                raise ValueError('Requested combat controller differs from compiled identity; run unchanged.')
        candidate, _ = self._candidate(observer)
        try:
            self._restore(candidate, self._frames, observer)
            result = candidate.command(' '.join(map(str, tokens)))
            frames = result.get('frames')
            if type(frames) is not list or not 1 <= len(frames) <= count:
                raise RuntimeError('Invalid combat native batch size; run unchanged.')
            for i, frame in enumerate(frames):
                validate_frame(frame, len(self._frames) + i)
                actual_sources = [{k: v[k] for k in ('kind', 'sdk_version', 'controller_id')} for v in frame['inputs']]
                if actual_sources != expected_sources:
                    raise RuntimeError('Combat input attribution differs from requested sources; run unchanged.')
                for robot, request in enumerate(requests):
                    if request['kind'] == 'manual' and frame['inputs'][robot]['action'] != action_fields(request['action'], 'manual'):
                        raise RuntimeError('Native manual input differs; run unchanged.')
            self._check_state(result, observer, len(self._frames) + len(frames))
            if result['hash'] != frames[-1]['hash'] or result.get('last_sources') != expected_sources:
                raise RuntimeError('Combat state does not match its resolved frames; run unchanged.')
            if len(frames) < count and result['terminal'] != 1:
                raise RuntimeError('Combat returned an incomplete nonterminal batch; run unchanged.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        self._swap(candidate, self._frames + frames)
        return {**self._public(result), 'advanced': len(frames)}

    def replay(self):
        state = self._native.command('STATE 0')
        self._check_state(state, 0, len(self._frames))
        return {'schema': REPLAY_SCHEMA, 'profile': PROFILE, 'engine': ENGINE,
                'observation_profile': OBSERVATION, 'official': False, 'build': self._build,
                'initial_hash': self._initial_hash, 'frames': copy.deepcopy(self._frames), 'final_hash': state['hash']}

    def import_replay(self, data):
        self._guard_source()
        exact(data, ('observer', 'replay'), 'import')
        observer = integer(data['observer'], 0, 1, 'observer')
        replay = data['replay']
        exact(replay, ('schema', 'profile', 'engine', 'observation_profile', 'official',
                       'build', 'initial_hash', 'frames', 'final_hash'), 'replay')
        if len(canonical(replay)) > MAX_BYTES:
            raise ValueError('Combat replay exceeds 12 MiB.')
        if (replay['schema'] != REPLAY_SCHEMA or replay['profile'] != PROFILE or replay['engine'] != ENGINE or
                replay['observation_profile'] != OBSERVATION or replay['official'] is not False or replay['build'] != self._build):
            raise ValueError('Combat replay identity differs. Use the original source build.')
        for key in ('initial_hash', 'final_hash'):
            integer(replay[key], 0, 4294967295, key)
        frames = replay['frames']
        if type(frames) is not list or len(frames) > MAX_TICKS:
            raise ValueError('Combat replay requires at most 7200 frames.')
        for tick, frame in enumerate(frames):
            validate_frame(frame, tick)
        candidate, initial = self._candidate(observer)
        try:
            if initial['hash'] != replay['initial_hash']:
                raise ValueError('Combat initial hash differs; run unchanged.')
            result = self._restore(candidate, frames, observer)
            self._check_state(result, observer, len(frames))
            if result['hash'] != replay['final_hash']:
                raise ValueError('Combat final hash mismatch; run unchanged.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        self._initial_hash = initial['hash']
        self._swap(candidate, frames)
        return {**self._public(result), 'verified_frames': len(frames)}

    def close(self):
        if self._native is not None:
            self._native.close()
