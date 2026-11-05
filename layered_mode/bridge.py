"""Strict data/replay adapter for the isolated experimental native mover.

Python validates and transports data; movement and state hashes are native.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import time

from arena_format import ArenaValidationError, validate_document
from .build_id import build_id

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'layered-native-v1'
POLICY_SDK_VERSION = 1
POLICY_OBSERVATION = 'full-authored-geometry'
REPLAY_SCHEMA = 'rmuc-layered-replay.v2'
MAX_REPLAY_BYTES = 12 * 1024 * 1024
LIMITS = {'coordinate_mm': 100000, 'surfaces': 32, 'solids': 64, 'triggers': 32,
          'portals': 64, 'polygon_vertices': 16, 'ticks': 36000, 'step_batch': 600}
DEFAULT_CONFIG = {'radius_mm': 120, 'height_mm': 400, 'speed_mm_per_tick': 20,
                  'max_slope_q1024': 512, 'max_ticks': 36000,
                  'start': {'x_mm': 800, 'y_mm': 1400, 'surface_id': 'west_floor'}}
LIMITATIONS = ['Experimental synthetic geometry; not an official RMUC map or physics engine.',
               'One upright cylinder, affine support surfaces and explicit continuous-height portals.',
               'No jumping, gravity, combat, scoring, referee adjudication or automatic layer switching.',
               'Quantized conservative motion may reject narrow passages; triggers are annotations only.',
               'C++ controllers are trusted local native code, not sandboxed or contained.',
               'Full authored geometry is exposed to the controller, not official sensing or competition fairness.',
               'Action replay verifies resolved motion, not policy determinism or authenticated attribution.']


def fail(path, message):
    raise ArenaValidationError(path, 'layered_profile', message)


def exact(value, fields, path):
    if type(value) is not dict or set(value) != set(fields):
        fail(path, 'Expected exactly: ' + ', '.join(fields) + '.')


def integer(value, low, high, path):
    if type(value) is not int or not low <= value <= high:
        fail(path, f'Expected integer in [{low}, {high}], not boolean or float.')
    return value


def mm(value, path):
    # Integral JSON floats are exact and accepted, but fractional values never round.
    if type(value) not in (int, float) or not -100000 <= value <= 100000 or int(value) != value:
        fail(path, 'Expected an exactly integral millimeter value in [-100000, 100000].')
    return int(value)


def canonical(value):
    try:
        return json.dumps(value, sort_keys=True, ensure_ascii=True, separators=(',', ':'), allow_nan=False).encode()
    except (TypeError, ValueError, RecursionError) as exc:
        raise ValueError('Expected bounded JSON-compatible data.') from exc


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def validate_map(document):
    validate_document(document)
    def polygon(poly, path):
        if len(poly) > 16:
            fail(path, 'Native profile permits at most 16 vertices.')
        for i, point in enumerate(poly):
            for j, value in enumerate(point):
                mm(value, f'{path}[{i}][{j}]')
    polygon(document['boundary_mm'], '$.map.boundary_mm')
    for field, maximum in (('surfaces', 32), ('collision_volumes', 64), ('trigger_volumes', 32), ('transitions', 64)):
        if len(document[field]) > maximum:
            fail('$.map.' + field, f'Native profile permits at most {maximum} items.')
        for i, item in enumerate(document[field]):
            p = f'$.map.{field}[{i}]'
            if field == 'transitions':
                for j, point in enumerate(item['portal_mm']):
                    for k, value in enumerate(point):
                        mm(value, f'{p}.portal_mm[{j}][{k}]')
                if item['max_step_mm'] != 0:
                    fail(p + '.max_step_mm', 'Only continuous-height portals (max_step_mm=0) are supported.')
            else:
                polygon(item['polygon_mm'], p + '.polygon_mm')
                if field == 'surfaces':
                    h = item['height']
                    mm(h['z_mm'], p + '.height.z_mm')
                    for axis in ('dx', 'dy'):
                        if h[axis] * 1024 != int(h[axis] * 1024):
                            fail(p + '.height.' + axis, 'Slope must be an exact multiple of 1/1024; no rounding is performed.')
                    if abs(h['dx']) + abs(h['dy']) > 1:
                        fail(p + '.height', 'Absolute slope components must sum to at most 1.')
                    for x, y in item['polygon_mm']:
                        if not -100000 <= h['z_mm'] + h['dx'] * x + h['dy'] * y <= 100000:
                            fail(p + '.height', 'Vertex support height exceeds native coordinate bounds.')
                    if item['ceiling_z_mm'] is not None:
                        mm(item['ceiling_z_mm'], p + '.ceiling_z_mm')
                else:
                    for z in ('z_min_mm', 'z_max_mm'):
                        mm(item[z], p + '.' + z)
    surfaces = {s['id']: s for s in document['surfaces']}
    for i, portal in enumerate(document['transitions']):
        a, b = portal['portal_mm']
        def side(surface):
            values = [(b[0]-a[0])*(v[1]-a[1])-(b[1]-a[1])*(v[0]-a[0]) for v in surface['polygon_mm']]
            return 1 if max(values) > 0 else -1
        if side(surfaces[portal['from_surface']]) == side(surfaces[portal['to_surface']]):
            fail(f'$.map.transitions[{i}].portal_mm', 'Joined surfaces must lie on opposite sides of the portal.')
        for x, y in (a, b):
            hs = [surfaces[portal[key]]['height'] for key in ('from_surface', 'to_surface')]
            if hs[0]['z_mm'] + hs[0]['dx']*x + hs[0]['dy']*y != hs[1]['z_mm'] + hs[1]['dx']*x + hs[1]['dy']*y:
                fail(f'$.map.transitions[{i}]', 'Portal support heights must match exactly.')
    return document


def validate_config(config, document):
    exact(config, DEFAULT_CONFIG, '$.config')
    for name, lo, hi in (('radius_mm', 1, 1000), ('height_mm', 1, 3000), ('speed_mm_per_tick', 1, 50), ('max_slope_q1024', 0, 1024), ('max_ticks', 1, 36000)):
        integer(config[name], lo, hi, '$.config.' + name)
    start = config['start']
    exact(start, ('x_mm', 'y_mm', 'surface_id'), '$.config.start')
    for name in ('x_mm', 'y_mm'):
        integer(start[name], -100000, 100000, '$.config.start.' + name)
    if type(start['surface_id']) is not str or start['surface_id'] not in {s['id'] for s in document['surfaces']}:
        fail('$.config.start.surface_id', 'Unknown support surface.')
    return config


def sample():
    return {'map': json.loads((ROOT / 'layered_mode/examples/synthetic_ramp_tunnel.json').read_text()), 'config': copy.deepcopy(DEFAULT_CONFIG)}


def load_command(document, config):
    ids = {s['id']: i for i, s in enumerate(document['surfaces'])}
    start = config['start']
    tokens = ['LOAD', digest(document), digest(config), config['radius_mm'], config['height_mm'], config['speed_mm_per_tick'], config['max_slope_q1024'], config['max_ticks'], start['x_mm'], start['y_mm'], ids[start['surface_id']]]
    def poly(points):
        tokens.append(len(points))
        for point in points:
            tokens.extend(int(v) for v in point)
    poly(document['boundary_mm'])
    tokens.append(len(document['surfaces']))
    for s in document['surfaces']:
        h = s['height']; ceiling = s['ceiling_z_mm']
        tokens.extend([s['id'], s['layer_id'], int(h['z_mm']), int(h['dx']*1024), int(h['dy']*1024), int(ceiling is not None), int(ceiling or 0), int(s['traversable'])])
        poly(s['polygon_mm'])
    for field in ('collision_volumes', 'trigger_volumes'):
        tokens.append(len(document[field]))
        for v in document[field]:
            tokens.extend([v['id'], int(v['z_min_mm']), int(v['z_max_mm'])]); poly(v['polygon_mm'])
            if field == 'trigger_volumes':
                tokens.append(len(v['surface_ids'])); tokens.extend(ids[sid] for sid in v['surface_ids'])
    tokens.append(len(document['transitions']))
    for p in document['transitions']:
        tokens.extend([p['id'], ids[p['from_surface']], ids[p['to_surface']], *[int(v) for point in p['portal_mm'] for v in point], int(p['bidirectional']), 0])
    return ' '.join(map(str, tokens))


class _Native:
    def __init__(self, binary):
        self.proc = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        os.set_blocking(self.proc.stdin.fileno(), False)
        self.buffer = b''

    def command(self, command):
        deadline = time.monotonic() + 20
        pending = memoryview((command + '\n').encode())
        try:
            while pending:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([], [self.proc.stdin], [], remaining)[1]:
                    raise RuntimeError('Native layered engine timed out; run invalid.')
                try:
                    pending = pending[os.write(self.proc.stdin.fileno(), pending):]
                except BlockingIOError:
                    continue
            while b'\n' not in self.buffer:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([self.proc.stdout], [], [], remaining)[0]:
                    raise RuntimeError('Native layered engine timed out; run invalid.')
                chunk = os.read(self.proc.stdout.fileno(), 65536)
                if not chunk:
                    raise RuntimeError('Native layered engine stopped without a response.')
                self.buffer += chunk
                if len(self.buffer) > MAX_REPLAY_BYTES:
                    raise RuntimeError('Native layered response exceeds size bound.')
            line, self.buffer = self.buffer.split(b'\n', 1)
            result = json.loads(line)
            if type(result) is not dict:
                raise RuntimeError('Invalid native layered response.')
        except Exception:
            self.close()
            raise
        if 'error' in result:
            raise ValueError(result['error'])
        return result

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill(); self.proc.wait()
        self.proc.stdin.close(); self.proc.stdout.close()


class LayeredProcess:
    def __init__(self, binary=None):
        self.binary = Path(binary) if binary else ROOT / 'build/rmuc_layered'
        self._native = None
        self._build = build_id()
        self.reset(sample())

    def _candidate(self, data):
        exact(data, ('map', 'config'), '$')
        document = copy.deepcopy(validate_map(data['map']))
        config = copy.deepcopy(validate_config(data['config'], document))
        candidate = _Native(self.binary)
        try:
            state = candidate.command(load_command(document, config))
            if state.get('build') != self._build or self._build != build_id():
                raise ValueError('Layered binary is stale. Run bash tools/build.sh and restart.')
            if state.get('map_hash') != digest(document) or state.get('config_hash') != digest(config):
                raise ValueError('Native map/config identity mismatch.')
        except Exception:
            candidate.close(); raise
        return candidate, document, config, state

    def _swap(self, candidate, document, config, frames, sources=None):
        old = self._native
        self._native, self._map, self._config, self._frames = candidate, document, config, frames
        self._sources = copy.deepcopy(sources or [])
        if old is not None:
            old.close()

    def reset(self, data):
        candidate, document, config, state = self._candidate(data)
        self._swap(candidate, document, config, [])
        return self._with_source(state)

    def state(self):
        return self._with_source(self._native.command('STATE'))

    def _with_source(self, state):
        state['action_source'] = ({k: self._sources[-1][k] for k in ('kind', 'sdk_version', 'controller_id')}
                                  if self._sources else None)
        return state

    def _guard_source(self):
        if self._build != build_id():
            fail('$.build', 'Layered source changed. Rebuild and restart before advancing; run unchanged.')

    def _policy_metadata(self):
        # Read user-authored descriptor in a disposable process, never the live world.
        self._guard_source()
        process = _Native(self.binary)
        try:
            result = process.command('POLICY_META')
            policy = result['policy']
            exact(policy, ('sdk_version', 'controller_id', 'execution', 'observation', 'state_mode',
                           'decision_hz', 'watchdog_seconds'), '$.policy')
            integer(policy['sdk_version'], 1, 1, '$.policy.sdk_version')
            if type(policy['controller_id']) is not str or not re.fullmatch('[A-Za-z][A-Za-z0-9_.-]{0,63}', policy['controller_id']):
                fail('$.policy.controller_id', 'Invalid bounded controller identity.')
            if (policy['execution'] != 'trusted-local-native' or policy['observation'] != POLICY_OBSERVATION or
                    policy['state_mode'] != 'stateless' or type(policy['decision_hz']) is not int or policy['decision_hz'] != 60 or
                    type(policy['watchdog_seconds']) is not int or policy['watchdog_seconds'] != 20):
                fail('$.policy', 'Unsupported policy execution contract.')
            return policy
        finally:
            process.close()

    def meta(self):
        state = self.state()
        try:
            policy, policy_error = self._policy_metadata(), None
        except (ValueError, RuntimeError, KeyError, AttributeError) as exc:
            policy, policy_error = None, str(exc)
        return {'profile': PROFILE, 'official': False, 'hz': 60, 'build': state['build'],
                'map_hash': digest(self._map), 'config_hash': digest(self._config),
                'map': copy.deepcopy(self._map), 'config': copy.deepcopy(self._config),
                'limits': dict(LIMITS), 'limitations': list(LIMITATIONS),
                'policy': policy, 'policy_error': policy_error}

    def _record_source(self, count, kind, controller_id=None):
        source = {'kind': kind, 'sdk_version': POLICY_SDK_VERSION, 'controller_id': controller_id}
        start = len(self._frames) - count
        if self._sources and all(self._sources[-1][key] == value for key, value in source.items()):
            self._sources[-1]['count'] += count
        else:
            self._sources.append({'start_tick': start, 'count': count, **source})

    def step(self, data):
        self._guard_source()
        exact(data, ('count', 'action'), '$.step')
        if type(data['action']) is not list or len(data['action']) != 2:
            fail('$.step.action', 'Expected [dx, dy].')
        count = integer(data['count'], 1, 600, '$.step.count')
        dx = integer(data['action'][0], -1000, 1000, '$.step.action[0]')
        dy = integer(data['action'][1], -1000, 1000, '$.step.action[1]')
        if len(self._frames) + count > self._config['max_ticks']:
            fail('$.step.count', 'Batch exceeds configured run limit; run unchanged.')
        result = self._native.command(f'ADVANCE {count} {dx} {dy}')
        self._frames.extend(result['frames'])
        self._record_source(count, 'manual')
        state = result['state']
        state['batch_events'] = result['batch_events']
        return self._with_source(state)

    def _restore_prefix(self, candidate, frames):
        # Still O(prefix) physics work, but group equal actions to avoid one pipe
        # roundtrip per historical tick. Compare every resolved action and hash.
        index = 0
        state = None
        while index < len(frames):
            end = index + 1
            while end < min(len(frames), index + 600) and frames[end][1:3] == frames[index][1:3]:
                end += 1
            while True:
                try:
                    result = candidate.command(f'ADVANCE {end-index} {frames[index][1]} {frames[index][2]}')
                    break
                except ValueError as exc:
                    # The native event cap rejects atomically. A smaller batch
                    # preserves the same frames and can return bounded evidence.
                    if 'batch event bound' not in str(exc) or end - index == 1:
                        raise
                    end = index + max(1, (end-index)//2)
            if result['frames'] != frames[index:end]:
                fail('$.replay.frames', f'Divergence at prefix tick {index}; existing run unchanged.')
            state = result['state']
            index = end
        return state

    def policy_step(self, data):
        self._guard_source()
        exact(data, ('count', 'sdk_version', 'controller_id'), '$.policy_step')
        count = integer(data['count'], 1, 600, '$.policy_step.count')
        integer(data['sdk_version'], POLICY_SDK_VERSION, POLICY_SDK_VERSION, '$.policy_step.sdk_version')
        if type(data['controller_id']) is not str or not re.fullmatch('[A-Za-z][A-Za-z0-9_.-]{0,63}', data['controller_id']):
            fail('$.policy_step.controller_id', 'Expected a bounded compiled controller identifier.')
        if len(self._frames) + count > self._config['max_ticks']:
            fail('$.policy_step.count', 'Batch exceeds configured run limit; run unchanged.')
        policy = self._policy_metadata()
        if data['controller_id'] != policy['controller_id']:
            fail('$.policy_step.controller_id', 'Compiled controller identity differs; run unchanged.')
        candidate, document, config, _ = self._candidate({'map': self._map, 'config': self._config})
        try:
            self._restore_prefix(candidate, self._frames)
            result = candidate.command(f"POLICY {count} {data['sdk_version']} {data['controller_id']}")
            frames = result['frames']
            if (type(frames) is not list or len(frames) != count or
                    any(type(frame) is not list or len(frame) != 4 or frame[0] != len(self._frames)+i for i, frame in enumerate(frames))):
                raise RuntimeError('Invalid policy frame output; experiment invalid, existing run unchanged.')
            for i, frame in enumerate(frames):
                integer(frame[0], len(self._frames)+i, len(self._frames)+i, '$.policy.frames.tick')
                integer(frame[1], -1000, 1000, '$.policy.frames.dx')
                integer(frame[2], -1000, 1000, '$.policy.frames.dy')
                if type(frame[3]) is not str or not re.fullmatch('[0-9a-f]{16}', frame[3]):
                    raise RuntimeError('Invalid policy frame hash; experiment invalid, existing run unchanged.')
            state = result['state']
            if (type(state) is not dict or state.get('build') != self._build or
                    state.get('tick') != len(self._frames)+count or state.get('hash') != frames[-1][3] or
                    state.get('map_hash') != digest(document) or state.get('config_hash') != digest(config) or
                    type(result.get('batch_events')) is not list or len(result['batch_events']) > 8192):
                raise RuntimeError('Invalid policy result; experiment invalid, existing run unchanged.')
            self._guard_source()
        except Exception:
            candidate.close()
            raise
        old_sources = copy.deepcopy(self._sources)
        self._swap(candidate, document, config, self._frames + frames, old_sources)
        self._record_source(count, 'policy', data['controller_id'])
        state = result['state']
        state['batch_events'] = result['batch_events']
        return self._with_source(state)

    def replay(self):
        state = self.state()
        return {'schema': REPLAY_SCHEMA, 'profile': PROFILE, 'official': False,
                'build': state['build'], 'map_hash': digest(self._map), 'config_hash': digest(self._config),
                'map': copy.deepcopy(self._map), 'config': copy.deepcopy(self._config),
                'frames': copy.deepcopy(self._frames), 'action_sources': copy.deepcopy(self._sources), 'final_hash': state['hash']}

    def import_replay(self, data):
        if type(data) is dict and data.get('schema') == 'rmuc-layered-replay.v1':
            fail('$.replay.schema', 'Replay v1 requires its original source/binary build. This policy SDK build accepts v2 only; no migration is fabricated.')
        exact(data, ('schema', 'profile', 'official', 'build', 'map_hash', 'config_hash', 'map', 'config', 'frames', 'action_sources', 'final_hash'), '$.replay')
        if len(canonical(data)) > MAX_REPLAY_BYTES:
            fail('$.replay', 'Replay exceeds 12 MiB.')
        if data['schema'] != REPLAY_SCHEMA or data['profile'] != PROFILE or data['official'] is not False:
            fail('$.replay', 'Unsupported replay identity.')
        if data['build'] != self._build or self._build != build_id() or self.state()['build'] != self._build:
            fail('$.replay.build', 'Replay source build differs.')
        validate_map(data['map']); validate_config(data['config'], data['map'])
        for key, value in (('map_hash', data['map']), ('config_hash', data['config'])):
            if data[key] != digest(value):
                fail('$.replay.' + key, 'Canonical SHA-256 mismatch.')
        frames = data['frames']
        if type(frames) is not list or len(frames) > data['config']['max_ticks']:
            fail('$.replay.frames', 'Expected bounded frame array.')
        def state_hash(value, path):
            if type(value) is not str or not re.fullmatch('[0-9a-f]{16}', value):
                fail(path, 'Expected a 16-character lowercase native state hash.')
        state_hash(data['final_hash'], '$.replay.final_hash')
        for tick, frame in enumerate(frames):
            path = f'$.replay.frames[{tick}]'
            if type(frame) is not list or len(frame) != 4:
                fail(path, 'Expected [tick, dx, dy, hash].')
            integer(frame[0], tick, tick, path + '[0]')
            integer(frame[1], -1000, 1000, path + '[1]')
            integer(frame[2], -1000, 1000, path + '[2]')
            state_hash(frame[3], path + '[3]')
        sources = data['action_sources']
        if type(sources) is not list or len(sources) > len(frames):
            fail('$.replay.action_sources', 'Expected bounded source segments covering all resolved frames.')
        cursor = 0
        for i, source in enumerate(sources):
            path = f'$.replay.action_sources[{i}]'
            exact(source, ('start_tick', 'count', 'kind', 'sdk_version', 'controller_id'), path)
            integer(source['start_tick'], cursor, cursor, path+'.start_tick')
            cursor += integer(source['count'], 1, len(frames)-cursor, path+'.count')
            integer(source['sdk_version'], 1, 1, path+'.sdk_version')
            if source['kind'] == 'manual':
                if source['controller_id'] is not None:
                    fail(path, 'Manual source must have null controller_id.')
            elif source['kind'] == 'policy':
                if type(source['controller_id']) is not str or not re.fullmatch('[A-Za-z][A-Za-z0-9_.-]{0,63}', source['controller_id']):
                    fail(path, 'Policy source must name its bounded controller identifier.')
            else:
                fail(path, 'Source kind must be manual or policy.')
        if cursor != len(frames):
            fail('$.replay.action_sources', 'Source segments must cover every resolved frame exactly once.')
        candidate, document, config, state = self._candidate({'map': data['map'], 'config': data['config']})
        try:
            restored = self._restore_prefix(candidate, frames)
            if restored is not None:
                state = restored
            if state['hash'] != data['final_hash']:
                fail('$.replay.final_hash', 'Final state mismatch; existing run unchanged.')
        except Exception:
            candidate.close(); raise
        self._swap(candidate, document, config, copy.deepcopy(frames), sources)
        state['verified_frames'] = len(frames)
        return self._with_source(state)

    def close(self):
        if self._native is not None:
            self._native.close()
