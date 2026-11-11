"""Local bridge for the field_sim native engine.

The C++ process is the only authority. This module starts it, checks that the
binary matches the checked-in sources and field data, validates the session
shape (the engine validates everything else again), and swaps in a new run only
after a candidate process accepted it.
"""
import json
import re
import threading
from pathlib import Path

from native_pipe import NativePipe, executable

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/rmuc_field'
FIELD_ID = 'rmuc2027_v1'
FIELD_DIR = ROOT / 'field_sim/field' / FIELD_ID
PROFILE = 'rmuc2027-field-sim-v1'
TICK_HZ = 100
MAX_ROBOTS = 8
MAX_STEP_TICKS = 200
ID_PATTERN = re.compile(r'^[A-Za-z0-9_-]{1,16}$')
TIMEOUT = 'Field engine timed out; the run was discarded.'
DEFAULT_SESSION = {'robots': [{'id': 'R1', 'team': 'red'}, {'id': 'R2', 'team': 'red'},
                              {'id': 'B1', 'team': 'blue'}, {'id': 'B2', 'team': 'blue'}]}


class FieldError(ValueError):
    """A request the bridge or the engine rejected; the current run is unchanged."""


def build_id():
    import runpy
    return runpy.run_path(str(ROOT / 'field_sim/build_id.py'))['build_id']()


def field_document():
    return json.loads((FIELD_DIR / 'field.json').read_text(encoding='utf-8'))


def terrain_bytes():
    return (FIELD_DIR / 'terrain.bin').read_bytes()


def validate_session(data):
    if not isinstance(data, dict) or set(data) != {'robots'}:
        raise FieldError('A session needs exactly the key "robots".')
    robots = data['robots']
    if not isinstance(robots, list) or not 1 <= len(robots) <= MAX_ROBOTS:
        raise FieldError(f'A session has 1..{MAX_ROBOTS} robots.')
    seen, out = set(), []
    per_team = {'red': 0, 'blue': 0}
    for robot in robots:
        if not isinstance(robot, dict) or set(robot) != {'id', 'team'}:
            raise FieldError('A robot needs exactly "id" and "team".')
        if not isinstance(robot['id'], str) or not ID_PATTERN.match(robot['id']):
            raise FieldError('A robot id is 1..16 letters, digits, "_" or "-".')
        if robot['team'] not in per_team:
            raise FieldError('A robot team is "red" or "blue".')
        if robot['id'] in seen:
            raise FieldError(f'Duplicate robot id {robot["id"]!r}.')
        seen.add(robot['id'])
        per_team[robot['team']] += 1
        if per_team[robot['team']] > 4:
            raise FieldError('At most four robots per team (four start poses).')
        out.append({'id': robot['id'], 'team': robot['team']})
    return {'robots': out}


class _Engine(NativePipe):
    def __init__(self):
        if not executable(BINARY).is_file():
            raise RuntimeError('Field engine is not built. Run bash tools/build.sh (Windows: py tools/native_build.py).')
        super().__init__(BINARY, 4_000_000)

    def call(self, request, seconds=10):
        if self.proc.poll() is not None:
            raise RuntimeError('Field engine stopped; reset the field session.')
        line = self.exchange((json.dumps(request, separators=(',', ':')) + '\n').encode(), seconds,
                             write_timeout=TIMEOUT, read_timeout=TIMEOUT,
                             stopped='Field engine returned no response.', oversize='Field engine response exceeded its size bound.')
        value = json.loads(line)
        if 'error' in value:
            raise FieldError(value['error'])
        return value


class FieldSim:
    """One field session. All public methods are serialised by an internal lock."""

    def __init__(self):
        self._lock = threading.RLock()
        self._engine = None
        self._session = None
        self._build = build_id()
        self.reset(DEFAULT_SESSION)

    def close(self):
        with self._lock:
            if self._engine is not None:
                self._engine.close()
                self._engine = None

    def meta(self):
        return {'profile': PROFILE, 'build': self._build, 'tick_hz': TICK_HZ, 'max_robots': MAX_ROBOTS,
                'max_step_ticks': MAX_STEP_TICKS, 'field': field_document(), 'session': self._session,
                'default_session': DEFAULT_SESSION}

    def reset(self, session):
        session = validate_session(session)
        candidate = _Engine()
        try:
            if candidate.call({'cmd': 'meta'})['build'] != self._build:
                raise RuntimeError('Field engine binary is stale. Run bash tools/build.sh and restart the server.')
            state = candidate.call({'cmd': 'init', 'field': str(FIELD_DIR), 'robots': session['robots']})['state']
        except Exception:
            candidate.close()
            raise
        with self._lock:
            previous, self._engine, self._session = self._engine, candidate, session
        if previous is not None:
            previous.close()
        return state

    def _call(self, request):
        with self._lock:
            if self._engine is None:
                raise RuntimeError('Field engine stopped; reset the field session.')
            try:
                return self._engine.call(request)
            except FieldError:
                raise
            except Exception:
                # timeout, crash or protocol damage: the run cannot be trusted any more
                self._engine.close()
                self._engine = None
                raise

    def state(self):
        return self._call({'cmd': 'state'})['state']

    def step(self, ticks, commands):
        if isinstance(ticks, bool) or not isinstance(ticks, int) or not 1 <= ticks <= MAX_STEP_TICKS:
            raise FieldError(f'ticks must be an integer 1..{MAX_STEP_TICKS}.')
        if not isinstance(commands, dict):
            raise FieldError('commands must be an object keyed by robot id.')
        return self._call({'cmd': 'step', 'ticks': ticks, 'commands': commands})['state']

    def probe(self, x, y):
        for value in (x, y):
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not -1000 < value < 1000:
                raise FieldError('probe needs finite x and y in metres.')
        out = self._call({'cmd': 'probe', 'x': x, 'y': y})
        out.pop('ok', None)
        return out
