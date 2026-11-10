#!/usr/bin/env python3
"""Loopback-only browser bridge. Simulation authority stays in C++.
No dependencies, cloud compiler, analytics, upload, or network service calls.
"""
import argparse
from decimal import Decimal
import json
import math
import re
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, parse_qs

ROOT = Path(__file__).resolve().parent
# Keep file-based test/tool imports self-contained, just like `python server.py`.
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from arena_format import ArenaValidationError, parse_document, query as arena_query
from native_pipe import NativePipe, executable
BINARY = ROOT / 'build/rmuc_sandbox'
OBJECTIVE_BINARY = ROOT / 'build/rmuc_objective'
MAX_BODY = 2_000_000
ARENA_MAX_BODY = 2 * 1024 * 1024
LAYERED_MAX_BODY = 12 * 1024 * 1024
COMBAT_MAX_BODY = 12 * 1024 * 1024
TACTICAL_MAX_BODY = 48 * 1024 * 1024
ROSTER_MAX_BODY = 96 * 1024 * 1024
LOCK = threading.RLock()

ENGINE_TIMEOUT = 'C++ policy timed out; restart the server. This run is invalid, not a game loss.'
ENGINE_OVERSIZE = 'Engine response exceeded size bound.'

class EngineProcess(NativePipe):
    def __init__(self, binary=None):
        super().__init__(binary or BINARY, 4_000_000)
    def command(self, text):
        if self.proc.poll() is not None:
            raise RuntimeError('C++ engine stopped; restart the server.')
        try:
            line = self.exchange((text + '\n').encode(), 5, write_timeout=ENGINE_TIMEOUT, read_timeout=ENGINE_TIMEOUT,
                                 stopped='C++ engine returned no response.', oversize=ENGINE_OVERSIZE)
        except RuntimeError as exc:
            if str(exc) in (ENGINE_TIMEOUT, ENGINE_OVERSIZE):  # as before: only these discard the process
                self.close()
            raise
        value = json.loads(line)
        if 'error' in value:
            raise ValueError(value['error'])
        return value

def objective_build_id():
    import runpy
    return runpy.run_path(str(ROOT / 'objective_mode/build_id.py'))['build_id']()

class ObjectiveProcess(EngineProcess):
    def __init__(self):
        super().__init__(OBJECTIVE_BINARY)
        try:
            if self.command('STATE')['build'] != objective_build_id():
                raise ValueError('Objective binary is stale. Run bash tools/build.sh and restart the server.')
        except Exception:
            self.close()
            raise

BOUNDARIES = ('strict-reject-unadjudicated-v1', 'ENGINEERING-NOT-OFFICIAL-timer-before-input-v1')
CREDITS = ('disabled', 'EXPERIMENTAL-end-of-second-v1')
LIFECYCLE = 'EXPERIMENTAL-latched-temporary-no-lifecycle-v1'
CLOCK = 'EXPERIMENTAL-ceil-sampled-ms-v1'

def objective_config(data, replay=False):
    expected = {'boundary_policy', 'credit_strategy'}
    if replay: expected |= {'lifecycle_strategy', 'clock_strategy'}
    if not isinstance(data, dict) or set(data) != expected:
        raise ValueError('Objective configuration requires exactly the named boundary and credit strategies.')
    if data['boundary_policy'] not in BOUNDARIES or data['credit_strategy'] not in CREDITS:
        raise ValueError('Unsupported objective policy. No silent fallback is available.')
    if replay and (data['lifecycle_strategy'] != LIFECYCLE or data['clock_strategy'] != CLOCK):
        raise ValueError('Unsupported objective lifecycle or clock strategy.')
    return BOUNDARIES.index(data['boundary_policy']), CREDITS.index(data['credit_strategy'])

def validate_objective_replay(data):
    fields = {'schema','profile','engine','build','official','config','frames','final_hash'}
    if not isinstance(data, dict) or set(data) != fields or type(data['schema']) is not int or data['schema'] != 1 or data['profile'] != 'objective-experimental-v1' or data['engine'] != 'objective-native-v1' or data['official'] is not False:
        raise ValueError('Unsupported objective replay schema or mode identity.')
    objective_config(data['config'], replay=True)
    frames = data['frames']
    if not isinstance(frames, list) or len(frames) > 25200:
        raise ValueError('Objective replay requires at most 25200 frames.')
    for tick, frame in enumerate(frames):
        if not isinstance(frame,list) or len(frame) != 12 or type(frame[0]) is not int or frame[0] != tick:
            raise ValueError(f'Malformed objective frame {tick}.')
        for offset in (1,6):
            for j, limits in enumerate(((-1000,1000),(-1000,1000),(0,12000),(0,7600),(0,0))):
                integer(frame[offset+j], *limits)
        integer(frame[11], 0, 4294967295)
    integer(data['final_hash'], 0, 4294967295)
    return frames

def objective_engine(server):
    if not hasattr(server, 'objective_engine'):
        server.objective_engine = ObjectiveProcess()
    return server.objective_engine

def objective_post(server, path, data):
    engine = objective_engine(server)
    if path == '/api/objective/reset':
        boundary, credits = objective_config(data)
        return engine.command(f'RESET {boundary} {credits}')
    if path == '/api/objective/step':
        if set(data) != {'count','mode','action'}:
            raise ValueError('Objective step requires exactly count, mode and action.')
        count = integer(data['count'],1,600); mode = integer(data['mode'],0,2)
        action = data['action']
        if not isinstance(action,list) or len(action) != 5:
            raise ValueError('Expected five objective action fields.')
        for j,limits in enumerate(((-1000,1000),(-1000,1000),(0,12000),(0,7600),(0,0))):
            integer(action[j],*limits)
        return engine.command('ADVANCE '+' '.join(map(str,[count,mode,*action])))
    if path == '/api/objective/import':
        frames = validate_objective_replay(data)
        if data['build'] != engine.command('STATE')['build']:
            raise ValueError('Objective replay build differs. Use the original source build.')
        boundary, credits = objective_config(data['config'], replay=True)
        candidate = ObjectiveProcess()
        try:
            result = candidate.command(f'RESET {boundary} {credits}')
            if result['build'] != data['build']:
                raise ValueError('New objective engine binary differs. Restart the server.')
            for frame in frames:
                result = candidate.command('FRAME '+' '.join(map(str,frame[:11])))
                if result['hash'] != frame[11]:
                    raise ValueError(f'Objective replay divergence at tick {frame[0]}. Existing run unchanged.')
            if result['hash'] != data['final_hash']:
                raise ValueError('Objective final hash mismatch. Existing run unchanged.')
        except Exception:
            candidate.close(); raise
        server.objective_engine = candidate; engine.close()
        result['verified_frames'] = len(frames)
        return result
    raise ValueError('Unknown objective endpoint.')

def unique_object(pairs):
    result = {}
    for key,value in pairs:
        if key in result: raise ValueError('Duplicate JSON key: '+ascii(key))
        result[key] = value
    return result

def layered_json_float(token):
    """Reject textual precision loss before validating the fixed-point profile."""
    value=float(token)
    if not math.isfinite(value) or Decimal(token) != Decimal.from_float(value):
        raise ValueError('Layered JSON decimal is not exactly representable; no numeric rounding is permitted.')
    return value

def layered_json_constant(token):
    raise ValueError('Layered JSON does not allow ' + token + '.')

def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'Expected integer in [{low}, {high}].')
    return value

def validate_replay(data):
    if not isinstance(data, dict) or data.get('schema') != 1 or data.get('profile') != 'sandbox-v1' or data.get('engine') != 'integer-mm-v1' or data.get('official') is not False:
        raise ValueError('Unsupported replay schema, engine, or sandbox profile.')
    frames = data.get('frames')
    if not isinstance(frames, list) or len(frames) > 7200:
        raise ValueError('Replay requires at most 7200 frames.')
    for tick, frame in enumerate(frames):
        if not isinstance(frame, list) or len(frame) != 12 or frame[0] != tick:
            raise ValueError(f'Malformed or non-sequential frame {tick}.')
        integer(frame[0], 0, 7199)
        for offset in (1, 6):
            for j, limits in enumerate(((-1000,1000),(-1000,1000),(0,12000),(0,7600),(0,1))):
                integer(frame[offset+j], *limits)
        integer(frame[11], 0, 4294967295)
    integer(data.get('final_hash'), 0, 4294967295)
    return frames

META = {'profile':'sandbox-v1', 'official':False, 'width':12000, 'height':7600, 'hz':60, 'radius':280, 'duration':7200,
        'walls':[[5250,2800,6750,4800],[2600,1350,3800,1950],[8200,5650,9400,6250]],
        'transport':'C++ native engine / loopback Python bridge', 'observation':'omniscient'}

def arena_sample():
    """The workbench sample is synthetic and never applied to either C++ engine."""
    return parse_document((ROOT / 'arena_format/examples/synthetic_bridge_tunnel.json').read_text(encoding='utf-8'))

def arena_post(path, data):
    """Stateless, bounded calibration requests. No files or simulation state are written."""
    expected = {'text'} if path == '/api/arena/validate' else {'text', 'probe'}
    if path not in ('/api/arena/validate', '/api/arena/query'):
        raise ValueError('Unknown arena endpoint.')
    if set(data) != expected:
        raise ValueError('Arena request fields must be exactly ' + ', '.join(sorted(expected)) + '.')
    document = parse_document(data['text'])
    if path == '/api/arena/validate':
        return {'valid': True, 'document': document}
    probe = data['probe']
    allowed = {'x_mm', 'y_mm', 'radius_mm', 'height_mm', 'max_slope_deg', 'surface_id'}
    if not isinstance(probe, dict) or not {'x_mm', 'y_mm'} <= set(probe) or not set(probe) <= allowed:
        raise ValueError('Arena probe requires x_mm and y_mm with only supported cylinder options.')
    return arena_query(document, **probe)

def layered_engine(server):
    """A separate process and immutable-per-run map; never replace other modes."""
    if not hasattr(server, 'layered_engine'):
        from layered_mode.bridge import LayeredProcess
        server.layered_engine = LayeredProcess()
    return server.layered_engine

def layered_sample():
    from layered_mode.bridge import sample
    return sample()

def layered_post(server, path, data):
    engine = layered_engine(server)
    if path == '/api/layered/reset':
        return engine.reset(data)
    if path == '/api/layered/step':
        return engine.step(data)
    if path == '/api/layered/policy-step':
        return engine.policy_step(data)
    if path == '/api/layered/import':
        return engine.import_replay(data)
    raise ValueError('Unknown layered endpoint.')

def combat_engine(server):
    """Separate filtered combat process; never mutates the original sandbox."""
    if not hasattr(server, 'combat_engine'):
        from combat_mode.bridge import CombatProcess
        server.combat_engine = CombatProcess()
    return server.combat_engine


def combat_observer(query):
    values = parse_qs(query, keep_blank_values=True)
    if not values:
        return 0
    if set(values) != {'observer'} or values['observer'] not in (['0'], ['1']):
        raise ValueError('Expected exactly observer=0 or observer=1.')
    return int(values['observer'][0])


def combat_post(server, path, data):
    engine = combat_engine(server)
    if path == '/api/combat/reset':
        return engine.reset(data)
    if path == '/api/combat/step':
        return engine.step(data)
    if path == '/api/combat/import':
        return engine.import_replay(data)
    raise ValueError('Unknown combat endpoint.')


def tactical_engine(server):
    """One coherent combat/objective world, separate from older profiles."""
    if not hasattr(server, 'tactical_engine'):
        from tactical_mode.bridge import TacticalProcess
        server.tactical_engine = TacticalProcess()
    return server.tactical_engine


def tactical_post(server, path, data):
    engine = tactical_engine(server)
    if path == '/api/tactical/reset':
        return engine.reset(data)
    if path == '/api/tactical/step':
        return engine.step(data)
    if path == '/api/tactical/import':
        return engine.import_replay(data)
    raise ValueError('Unknown tactical endpoint.')


def roster_engine(server):
    """Isolated roster-capable experimental tactical authority."""
    if not hasattr(server, 'roster_engine'):
        from roster_mode.bridge import RosterProcess
        server.roster_engine = RosterProcess()
    return server.roster_engine


def roster_observer(query):
    values = parse_qs(query, keep_blank_values=True)
    if not values:
        return None
    if (set(values) != {'observer'} or len(values['observer']) != 1 or
            re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,31}', values['observer'][0]) is None):
        raise ValueError('Expected one stable robot identifier in observer.')
    return values['observer'][0]


def roster_changed(server):
    server.roster_run_revision = getattr(server, 'roster_run_revision', 0) + 1


def roster_context(server, state):
    """Only return trusted public metadata bound to this committed native world.

    A matching numeric config alone never promotes a raw run. Catalog problems
    hide the badge, but do not prevent the original synthetic API from working.
    """
    context = getattr(server, 'roster_profile_context', None)
    if context is None:
        return None, 'synthetic'
    engine = roster_engine(server)
    if context['engine'] is not engine or context['native'] is not engine._native:
        server.roster_profile_context = None
        return None, 'unverified'
    try:
        from robot_profiles_v1.api import validate_config_envelope, NATIVE_SOURCE_PIN
        from combat_mode.bridge import canonical
        envelope = validate_config_envelope(context['configuration'])
        if state['build'] != NATIVE_SOURCE_PIN or canonical(envelope['resolved_config']) != canonical(state['config']):
            raise ValueError('Sourced context differs from the native world.')
        return envelope, 'sourced-partial'
    except (ValueError, OSError):
        return None, 'unverified'


def roster_packet(server, state):
    envelope, status = roster_context(server, state)
    return {'schema':'rmuc-roster-profile-session.v1',
            'run_revision':getattr(server, 'roster_run_revision', 0),
            'state':state, 'active_config':envelope, 'provenance_status':status}


def roster_post(server, path, data):
    engine = roster_engine(server)
    if path == '/api/roster/reset':
        result = engine.reset(data)
        server.roster_profile_context = None
        roster_changed(server)
        return result
    if path == '/api/roster/step':
        # RosterProcess swaps its disposable native process on every successful
        # batch. Carry metadata only from the exact prior committed process.
        context = getattr(server, 'roster_profile_context', None)
        carry = context is not None and context['engine'] is engine and context['native'] is engine._native
        result = engine.step(data)
        if carry:
            context['native'] = engine._native
        elif context is not None:
            server.roster_profile_context = None
        roster_changed(server)
        return result
    if path == '/api/roster/import':
        result = engine.import_replay(data)
        server.roster_profile_context = None
        roster_changed(server)
        return result
    raise ValueError('Unknown roster endpoint.')


class RosterStaleRequest(ValueError):
    resync_required = True


def roster_profiles_post(server, path, data):
    from combat_mode.bridge import exact
    prefix = '/api/roster/profiles/'
    action = path[len(prefix):]
    if action in ('session/reset','session/step','session/import','reset','import'):
        exact(data, ('expected_revision','expected_tick','request'), 'session mutation')
        revision = integer(data['expected_revision'],0,9007199254740991)
        tick = integer(data['expected_tick'],0,25200)
        engine = roster_engine(server)
        if revision != getattr(server, 'roster_run_revision', 0) or tick != len(engine._frames):
            raise RosterStaleRequest('Another client changed this run. Read-only resync required; no action was applied.')
        if type(data['request']) is not dict:
            raise ValueError('session request must be an object.')
        data = data['request']
    if action in ('session/reset','session/step','session/import'):
        state = roster_post(server, '/api/roster/' + action.split('/')[1], data)
        return roster_packet(server, state)
    from robot_profiles_v1 import api as profiles
    if action == 'prepare':
        # Choice preparation is explicit, read-only and preserves raw parser
        # rejection of duplicate keys before constructing a new selection.
        from roster_mode.bridge import validate_config, SELECTED_CONFIG
        exact(data, ('config','presets'), 'prepare')
        config = validate_config(data['config'])
        exact(data['presets'], tuple(r['id'] for r in config['robots']), 'prepare.presets')
        catalog = profiles.catalog()
        presets = {p['id']:p for p in catalog['presets']}
        robots = []
        for robot in config['robots']:
            chosen = data['presets'][robot['id']]
            if type(chosen) is not str or chosen not in presets:
                raise ValueError('Unknown sourced preset.')
            preset = presets[chosen]
            residual = {k:v for k,v in robot['profile'].items() if k not in preset['overrides']}
            residual['id'] = 'SYNTHETIC-src-' + str(len(robots) + 1)
            robots.append({**{k:robot[k] for k in ('id','team','spawn','eligible')},
                           'preset_id':chosen, 'conditions':preset['conditions'], 'synthetic':residual})
        return profiles.resolve_config({'schema':profiles.SELECTION_SCHEMA, 'catalog':catalog['identity'],
            'strategies':{k:config[k] for k in SELECTED_CONFIG}, 'robots':robots})
    if action == 'resolve':
        return profiles.resolve_config(data)
    if action == 'validate':
        exact(data, ('configuration',), 'validate')
        return profiles.validate_config_envelope(data['configuration'])
    if action == 'reset':
        exact(data, ('observer','configuration'), 'sourced.reset')
        envelope = profiles.validate_config_envelope(data['configuration'])
        engine = roster_engine(server)
        state = profiles.reset_process(engine, data['observer'], envelope)
    elif action == 'import':
        exact(data, ('observer','replay'), 'sourced.import')
        wrapper = profiles.validate_replay_wrapper(data['replay'])
        envelope = wrapper['configuration']
        engine = roster_engine(server)
        state = profiles.import_process(engine, data['observer'], wrapper)
    else:
        raise ValueError('Unknown sourced profile endpoint.')
    # Native reset/import is transactional; do not replace context until success.
    server.roster_profile_context = {'engine':engine, 'native':engine._native,
                                     'configuration':envelope}
    roster_changed(server)
    return roster_packet(server, state)


LAB_MAX_BODY = 20_000
LAB_LOCK = threading.Lock()


LAB_SWEEP_MAX = 50  # matches per request; each is a full native run


def lab_post(path, data):
    """Stateless integrated rules lab: validated config in, native result out.

    /api/lab/run plays one match (optionally with frames); /api/lab/sweep plays
    the same setup with consecutive seeds and returns only the tallies.
    """
    from match_lab import cli as lab
    try:
        if path == '/api/lab/run':
            if set(data) != {'config', 'frame_every'}:
                raise ValueError('Rules lab request needs exactly config and frame_every.')
            frames = integer(data['frame_every'], 0, 600)
            with LAB_LOCK:  # one native lab run at a time
                return lab.run(lab.validate(data['config']), frames)
        if path == '/api/lab/sweep':
            if set(data) != {'config', 'seeds'}:
                raise ValueError('Rules lab sweep needs exactly config and seeds.')
            seeds = integer(data['seeds'], 2, LAB_SWEEP_MAX)
            with LAB_LOCK:
                return lab.sweep(lab.validate(data['config']), seeds)
    except lab.LabError as exc:
        raise ValueError(str(exc)) from exc
    raise ValueError('Unknown rules lab endpoint.')


def roster_profiles_get(server, path, query):
    if path == '/api/roster/profiles/session/state':
        return roster_packet(server, roster_engine(server).state(roster_observer(query)))
    if query:
        raise ValueError('This profile endpoint does not accept query parameters.')
    from robot_profiles_v1 import api as profiles
    if path == '/api/roster/profiles/catalog':
        return profiles.catalog()
    if path == '/api/roster/profiles/replay':
        engine = roster_engine(server)
        # Export is explicit and privileged, unlike the filtered session API.
        raw = engine.replay()
        envelope, status = roster_context(server, {'build':raw['build'], 'config':raw['config']})
        if status != 'sourced-partial':
            raise ValueError('This run has no validated sourced context. Export its raw synthetic replay instead.')
        return profiles.wrap_replay(envelope, raw)
    raise ValueError('Unknown sourced profile endpoint.')


class Handler(SimpleHTTPRequestHandler):
    # Fixed types for the files in web/: on Windows mimetypes reads the registry, where .js is
    # sometimes text/plain, and nosniff plus the CSP below would then block every script.
    extensions_map={**SimpleHTTPRequestHandler.extensions_map,'.html':'text/html','.js':'text/javascript',
                    '.css':'text/css','.json':'application/json','.svg':'image/svg+xml','.png':'image/png'}
    def __init__(self,*args,**kwargs):
        super().__init__(*args,directory=str(ROOT/'web'),**kwargs)
    def log_message(self,*args):
        pass
    def end_headers(self):
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Cache-Control','no-store')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        super().end_headers()
    def respond(self,value,status=200):
        encoded=json.dumps(value,separators=(',',':'),ensure_ascii=True).encode()
        self.send_response(status); self.send_header('Content-Type','application/json; charset=utf-8'); self.send_header('Content-Length',str(len(encoded))); self.end_headers();self.wfile.write(encoded)
    def valid_host(self):
        return self.headers.get('Host') in (f'127.0.0.1:{self.server.server_port}', f'localhost:{self.server.server_port}')
    def do_GET(self):
        if not self.valid_host():
            return self.respond({'error':'Loopback Host required.'},403)
        path=urlsplit(self.path).path
        if path=='/api/meta':return self.respond(META)
        if path.startswith('/api/roster/profiles/'):
            try:
                with LOCK:
                    result=roster_profiles_get(self.server,path,urlsplit(self.path).query)
                return self.respond(result)
            except ValueError as exc:return self.respond({'error':str(exc)},400)
            except (RuntimeError,OSError) as exc:return self.respond({'error':str(exc)},500)

        if path in ('/api/roster/meta','/api/roster/state','/api/roster/replay'):
            try:
                with LOCK:
                    engine=roster_engine(self.server)
                    if path=='/api/roster/state':
                        result=engine.state(roster_observer(urlsplit(self.path).query))
                    else:
                        if urlsplit(self.path).query:
                            raise ValueError('This roster endpoint does not accept query parameters.')
                        result=engine.meta() if path.endswith('/meta') else engine.replay()
                return self.respond(result)
            except ValueError as exc:return self.respond({'error':str(exc)},400)
            except (RuntimeError,OSError) as exc:return self.respond({'error':str(exc)},500)

        if path in ('/api/tactical/meta','/api/tactical/state','/api/tactical/replay'):
            try:
                with LOCK:
                    engine=tactical_engine(self.server)
                    if path=='/api/tactical/state':
                        result=engine.state(combat_observer(urlsplit(self.path).query))
                    else:
                        if urlsplit(self.path).query:
                            raise ValueError('This tactical endpoint does not accept query parameters.')
                        result=engine.meta() if path.endswith('/meta') else engine.replay()
                return self.respond(result)
            except ValueError as exc:return self.respond({'error':str(exc)},400)
            except (RuntimeError,OSError) as exc:return self.respond({'error':str(exc)},500)

        if path in ('/api/combat/meta','/api/combat/state','/api/combat/replay'):
            try:
                with LOCK:
                    engine=combat_engine(self.server)
                    if path=='/api/combat/state':
                        result=engine.state(combat_observer(urlsplit(self.path).query))
                    else:
                        if urlsplit(self.path).query:
                            raise ValueError('This combat endpoint does not accept query parameters.')
                        result=engine.meta() if path.endswith('/meta') else engine.replay()
                return self.respond(result)
            except ValueError as exc:return self.respond({'error':str(exc)},400)
            except (RuntimeError,OSError) as exc:return self.respond({'error':str(exc)},500)

        if path=='/api/lab/example':
            from match_lab import cli as lab
            return self.respond(lab.DEFAULT)
        if path=='/api/arena/sample':
            try:return self.respond(arena_sample())
            except (ValueError,OSError) as exc:return self.respond({'error':str(exc)},500)
        if path=='/api/layered/sample':
            try:return self.respond(layered_sample())
            except (ValueError,OSError) as exc:return self.respond({'error':str(exc)},500)
        if path in ('/api/layered/meta','/api/layered/state','/api/layered/replay'):
            try:
                with LOCK:
                    engine=layered_engine(self.server)
                    result={'/api/layered/meta':engine.meta,'/api/layered/state':engine.state,'/api/layered/replay':engine.replay}[path]()
                return self.respond(result)
            except (ValueError,RuntimeError,OSError) as exc:return self.respond({'error':str(exc)},500)
        if path in ('/api/objective/meta','/api/objective/state','/api/objective/replay'):
            try:
                with LOCK:
                    result=objective_engine(self.server).command({'/api/objective/meta':'META','/api/objective/state':'STATE','/api/objective/replay':'EXPORT'}[path])
                return self.respond(result)
            except (ValueError,RuntimeError,OSError) as exc:return self.respond({'error':str(exc)},500)
        if path in ('/api/state','/api/replay'):
            try:
                with LOCK:
                    result=self.server.engine.command('STATE' if path=='/api/state' else 'EXPORT')
                return self.respond(result)
            except (ValueError,RuntimeError) as exc:return self.respond({'error':str(exc)},500)
        if path.startswith('/api/'):return self.respond({'error':'Not found'},404)
        super().do_GET()
    def do_POST(self):
        origin=self.headers.get('Origin')
        allowed=(f'http://127.0.0.1:{self.server.server_port}',f'http://localhost:{self.server.server_port}')
        if not self.valid_host() or self.headers.get('X-Sandbox-Client')!='1' or (origin is not None and origin not in allowed):
            return self.respond({'error':'Same-origin local client required.'},403)
        try:
            length=int(self.headers.get('Content-Length','0'))
            limit=(ROSTER_MAX_BODY if self.path.startswith('/api/roster/') else
                   TACTICAL_MAX_BODY if self.path.startswith('/api/tactical/') else
                   COMBAT_MAX_BODY if self.path.startswith('/api/combat/') else
                   LAYERED_MAX_BODY if self.path.startswith('/api/layered/') else
                   ARENA_MAX_BODY if self.path.startswith('/api/arena/') else
                   LAB_MAX_BODY if self.path.startswith('/api/lab/') else
                   4_000_000 if self.path.startswith('/api/objective/') else MAX_BODY)
            if not 0<length<=limit:raise ValueError('Body size is invalid.')
            self.connection.settimeout(10)
            options={'object_pairs_hook':unique_object}
            if self.path.startswith('/api/layered/'):
                options.update(parse_float=layered_json_float,parse_constant=layered_json_constant)
            data=json.loads(self.rfile.read(length),**options)
            if not isinstance(data,dict):raise ValueError('Expected JSON object.')
            if self.path.startswith('/api/lab/'):
                if self.path not in ('/api/lab/run','/api/lab/sweep'):
                    return self.respond({'error':'Not found'},404)
                return self.respond(lab_post(self.path,data))
            if self.path.startswith('/api/arena/'):
                if self.path not in ('/api/arena/validate','/api/arena/query'):
                    return self.respond({'error':'Not found'},404)
                return self.respond(arena_post(self.path,data))
            with LOCK:
                if self.path.startswith('/api/roster/profiles/'):
                    result=roster_profiles_post(self.server,self.path,data)
                elif self.path.startswith('/api/roster/'):
                    if self.path not in ('/api/roster/reset','/api/roster/step','/api/roster/import'):
                        return self.respond({'error':'Not found'},404)
                    result=roster_post(self.server,self.path,data)
                elif self.path.startswith('/api/tactical/'):
                    if self.path not in ('/api/tactical/reset','/api/tactical/step','/api/tactical/import'):
                        return self.respond({'error':'Not found'},404)
                    result=tactical_post(self.server,self.path,data)
                elif self.path.startswith('/api/combat/'):
                    if self.path not in ('/api/combat/reset','/api/combat/step','/api/combat/import'):
                        return self.respond({'error':'Not found'},404)
                    result=combat_post(self.server,self.path,data)
                elif self.path.startswith('/api/layered/'):
                    if self.path not in ('/api/layered/reset','/api/layered/step','/api/layered/policy-step','/api/layered/import'):
                        return self.respond({'error':'Not found'},404)
                    result=layered_post(self.server,self.path,data)
                elif self.path.startswith('/api/objective/'):
                    result=objective_post(self.server,self.path,data)
                elif self.path=='/api/reset':result=self.server.engine.command('RESET')
                elif self.path=='/api/step':
                    count=integer(data.get('count'),1,12);mode=integer(data.get('mode'),0,2)
                    a=data.get('action')
                    if not isinstance(a,list) or len(a)!=5:raise ValueError('Expected five action fields.')
                    for j,limits in enumerate(((-1000,1000),(-1000,1000),(0,12000),(0,7600),(0,1))):integer(a[j],*limits)
                    result=self.server.engine.command('ADVANCE '+' '.join(map(str,[count,mode,*a])))
                elif self.path=='/api/import':
                    frames=validate_replay(data)
                    if data.get('build') != self.server.engine.command('STATE')['build']:
                        raise ValueError('Replay engine build differs. Use the original source build.')
                    candidate=EngineProcess()
                    try:
                        result=candidate.command('STATE')
                        if result['build']!=data['build']:
                            raise ValueError('New engine binary differs from the replay build. Restart the server.')
                        for frame in frames:
                            result=candidate.command('FRAME '+' '.join(map(str,frame[:11])))
                            if result['hash']!=frame[11]:raise ValueError(f'Replay divergence at tick {frame[0]}. Existing match unchanged.')
                        if result['hash']!=data['final_hash']:raise ValueError('Final replay hash mismatch. Existing match unchanged.')
                    except Exception:
                        candidate.close();raise
                    previous=self.server.engine;self.server.engine=candidate;previous.close()
                    result['verified_frames']=len(frames)
                else:return self.respond({'error':'Not found'},404)
            self.respond(result)
        except ArenaValidationError as exc:self.respond({'error':'Arena validation failed.','errors':exc.errors},400)
        except (ValueError,TypeError,json.JSONDecodeError,RecursionError) as exc:
            result={'error':str(exc)}
            if self.path.startswith('/api/layered/') and hasattr(exc,'errors'):
                result['errors']=exc.errors
            if getattr(exc,'resync_required',False):
                result['resync_required']=True
                self.respond(result,409)
            else:
                self.respond(result,400)
        except (RuntimeError,OSError) as exc:self.respond({'error':str(exc)},500)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',type=int,default=8765);args=parser.parse_args()
    if not executable(BINARY).is_file():raise SystemExit('Run ./tools/build.sh first (Windows: py tools/native_build.py).')
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler);server.engine=EngineProcess()
    print(f'RMUC tactics sandbox: http://127.0.0.1:{args.port} (local only; not official rules)',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:
        server.server_close();server.engine.close()
        if hasattr(server,'objective_engine'):server.objective_engine.close()
        if hasattr(server,'layered_engine'):server.layered_engine.close()
        if hasattr(server,'combat_engine'):server.combat_engine.close()
        if hasattr(server,'tactical_engine'):server.tactical_engine.close()
        if hasattr(server,'roster_engine'):server.roster_engine.close()
if __name__=='__main__':main()
