"""Strict, versioned source projections; never an official robot implementation.

The catalog is release-pinned, not supplied by the caller. Every source claim is
reconstructed from that catalog. Existing raw roster configs and replays are
unchanged; these wrappers are a distinct opt-in format.
"""
import copy
import hashlib
import json
from pathlib import Path

from combat_mode.bridge import canonical, exact, integer
from roster_mode import bridge as native
from .pins import CATALOG_SHA256, SOURCE_AUDIT_SHA256

ROOT = Path(__file__).resolve().parent
CATALOG_ID = 'rmuc2027-robot-parameter-catalog'
CATALOG_VERSION = '1.0.0'
CATALOG_SCHEMA = 'rmuc-sourced2027-catalog.v1'
SELECTION_SCHEMA = 'rmuc-sourced2027-selection.v1'
CONFIG_SCHEMA = 'rmuc-sourced2027-roster-config.v1'
REPLAY_SCHEMA = 'rmuc-sourced2027-roster-replay.v1'
NATIVE_SOURCE_PIN = '2328a3428507c59f156683eb4afc14adcdb1ba5513115c899cb747ffbeeb1428'
SCOPE = 'partial-parameter-projection-into-synthetic-roster-physics'
MAX_CONFIG_BYTES = 1024 * 1024
SUPPORTED_FIELDS = frozenset(('max_hp', 'heat_limit_tenths', 'cooling_per_second'))
UNITS = {'max_hp':'HP', 'ammo':'rounds', 'radius_mm':'mm',
         'speed_mm_tick':'mm/tick', 'projectile_speed_mm_tick':'mm/tick',
         'projectile_damage':'HP/impact', 'cooldown_ticks':'ticks',
         'sensor_range_mm':'mm', 'heat_limit_tenths':'tenths of heat',
         'cooling_per_second':'heat/second'}


def _digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _same(actual, expected, where):
    # Python equality treats True==1 and 1.0==1; canonical JSON does not.
    if canonical(actual) != canonical(expected):
        raise ValueError(where + ' differs from the trusted, reconstructed record.')


def _json(raw, where):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(where + ': duplicate JSON key.')
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Non-finite JSON.')))
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as exc:
        raise ValueError(where + ': invalid JSON.') from exc


def _trusted_catalog():
    """Check bytes on every operation, including existing-session re-exports."""
    raw = (ROOT / 'catalog.json').read_bytes()
    audit_raw = (ROOT / 'source_audit.json').read_bytes()
    if hashlib.sha256(raw).hexdigest() != CATALOG_SHA256:
        raise ValueError('Sourced catalog changed or is untrusted; use its original version.')
    if hashlib.sha256(audit_raw).hexdigest() != SOURCE_AUDIT_SHA256:
        raise ValueError('Sourced audit changed or is untrusted; use its original version.')
    result = _json(raw, 'catalog')
    if (result['schema'] != CATALOG_SCHEMA or result['id'] != CATALOG_ID or
            result['version'] != CATALOG_VERSION or result['official'] is not False or
            result['complete'] is not False or result['scope'] != SCOPE or
            result['native_source_pin'] != NATIVE_SOURCE_PIN or
            result['source_audit_sha256'] != SOURCE_AUDIT_SHA256):
        raise ValueError('Trusted catalog identity is inconsistent.')
    return result


def _identity():
    return {'id': CATALOG_ID, 'version': CATALOG_VERSION,
            'sha256': CATALOG_SHA256, 'source_audit_sha256': SOURCE_AUDIT_SHA256}


def catalog():
    """Public catalog and identity; no native launch or simulation mutation."""
    result = _trusted_catalog()
    result['identity'] = _identity()
    result['selection_schema'] = SELECTION_SCHEMA
    result['configuration_schema'] = CONFIG_SCHEMA
    result['replay_schema'] = REPLAY_SCHEMA
    result['profile_bounds'] = {k:list(v) for k,v in native.PROFILE_BOUNDS.items()}
    result['units'] = copy.deepcopy(UNITS)
    return result


def _selection(request, trusted):
    exact(request, ('schema', 'catalog', 'strategies', 'robots'), 'selection')
    if request['schema'] != SELECTION_SCHEMA:
        raise ValueError('Unsupported sourced selection schema; raw synthetic formats are separate.')
    _same(request['catalog'], _identity(), 'selection.catalog')
    exact(request['strategies'], native.SELECTED_CONFIG, 'selection.strategies')
    if type(request['robots']) is not list or not 2 <= len(request['robots']) <= native.MAX_ROBOTS:
        raise ValueError('selection.robots requires 2–8 explicitly selected records.')
    presets = {item['id']:item for item in trusted['presets']}
    robots, provenance = [], []
    selection = copy.deepcopy(request)
    for item in selection['robots']:
        exact(item, ('id', 'team', 'spawn', 'eligible', 'preset_id', 'conditions', 'synthetic'), 'selection.robot')
        native.robot_id(item['id'], 'selection.robot.id')
        preset_id = item['preset_id']
        if type(preset_id) is not str or preset_id not in presets:
            raise ValueError('Unsupported sourced role, level, option or preset ID.')
        preset = presets[preset_id]
        _same(item['conditions'], preset['conditions'], 'selection.robot.conditions')
        overrides = preset['overrides']
        if not set(overrides) <= SUPPORTED_FIELDS or not overrides:
            raise ValueError('Trusted preset contains an unsupported engine projection.')
        remainder = tuple(key for key in native.PROFILE_BOUNDS if key not in overrides)
        exact(item['synthetic'], ('id', *remainder), 'selection.robot.synthetic')
        # Native validation below enforces all numeric bounds, strict types,
        # SYNTHETIC- IDs, profile reuse, team counts, walls and spawn clearance.
        profile = {**copy.deepcopy(item['synthetic']), **copy.deepcopy(overrides)}
        robots.append({k:copy.deepcopy(item[k]) for k in ('id','team','spawn','eligible')} | {'profile':profile})
        fields = {}
        for key in native.PROFILE_BOUNDS:
            if key in overrides:
                fields[key] = {'classification':'sourced-parameter', 'value':profile[key],
                               'unit':UNITS[key], 'source':copy.deepcopy(preset['field_provenance'][key])}
            else:
                fields[key] = {'classification':'user-supplied-synthetic-assumption',
                               'value':profile[key], 'unit':UNITS[key], 'source':None}
        provenance.append({'id':item['id'], 'profile_id':profile['id'],
            'preset_id':preset_id, 'role':preset['role'], 'label':preset['label'],
            'projection_kind':preset['projection_kind'], 'conditions':copy.deepcopy(preset['conditions']),
            'complete':False, 'official':False, 'fields':fields,
            'excluded_fields':copy.deepcopy(preset['excluded_fields']),
            'unsupported_behaviors':copy.deepcopy(preset['unsupported_behaviors']),
            'initialization_note':preset['initialization_note']})
    config = native.validate_config({**selection['strategies'], 'robots':robots})
    selection['robots'].sort(key=lambda item:item['id'])
    provenance.sort(key=lambda item:item['id'])
    return selection, config, provenance


def resolve_config(request):
    """Resolve only audited fields; every other field must be explicitly supplied.

    The returned envelope is self-describing but not self-authenticating. It must
    always pass validate_config_envelope before use, not just have a valid hash.
    """
    if len(canonical(request)) > MAX_CONFIG_BYTES:
        raise ValueError('Sourced configuration exceeds the 1 MiB bound.')
    trusted = _trusted_catalog()
    selection, config, records = _selection(request, trusted)
    result = {'schema':CONFIG_SCHEMA, 'official':False, 'complete':False,
              'scope':SCOPE, 'selection':selection, 'resolved_config':config,
              'provenance':{'catalog':_identity(), 'native_source_pin':NATIVE_SOURCE_PIN,
                  'documents':copy.deepcopy(trusted['documents']), 'robots':records,
                  'synthetic_context':{
                      'strategies':copy.deepcopy(selection['strategies']),
                      'robots':[{k:copy.deepcopy(robot[k]) for k in ('id','team','spawn','eligible')}
                                for robot in selection['robots']]},
                  'limitations':copy.deepcopy(trusted['limitations']),
                  'completeness':'partial-parameters-only; all unlisted behavior remains synthetic'}}
    result['configuration_sha256'] = _digest(result)
    return result


def validate_config_envelope(envelope):
    """Return the canonical trusted reconstruction, rejecting every fake claim."""
    exact(envelope, ('schema','official','complete','scope','selection','resolved_config',
                     'provenance','configuration_sha256'), 'configuration')
    if len(canonical(envelope)) > MAX_CONFIG_BYTES:
        raise ValueError('Sourced configuration exceeds the 1 MiB bound.')
    if envelope['schema'] != CONFIG_SCHEMA or envelope['official'] is not False or envelope['complete'] is not False:
        raise ValueError('Sourced configuration is partial and never official or complete.')
    expected = resolve_config(envelope['selection'])
    _same(envelope, expected, 'configuration')
    return expected


def _validate_native_replay(replay, config):
    """Structural checks only; RosterProcess is the final deterministic authority."""
    exact(replay, ('schema','profile','engine','build','observation_profile','observation_version',
                   'official','config','initial_hash','frames','final_hash'), 'native_replay')
    if len(canonical(replay)) > native.MAX_BYTES:
        raise ValueError('Roster replay exceeds 96 MiB.')
    if (replay['schema'] != native.REPLAY_SCHEMA or replay['profile'] != native.PROFILE or
            replay['engine'] != native.ENGINE or replay['build'] != NATIVE_SOURCE_PIN or
            replay['observation_profile'] != native.OBSERVATION or
            type(replay['observation_version']) is not int or replay['observation_version'] != 1 or
            replay['official'] is not False):
        raise ValueError('Sourced replay must contain the unchanged, pinned synthetic native replay.')
    _same(replay['config'], config, 'native_replay.config')
    native.validate_config(replay['config'])
    for key in ('initial_hash','final_hash'):
        integer(replay[key], 0, 0xffffffff, 'native_replay.'+key)
    if type(replay['frames']) is not list or len(replay['frames']) > native.MAX_TICKS:
        raise ValueError('Replay must contain at most 25200 frames.')
    for tick, frame in enumerate(replay['frames']):
        native.validate_frame(frame, tick, config)
    tail = replay['frames'][-1]['hash'] if replay['frames'] else replay['initial_hash']
    if tail != replay['final_hash']:
        raise ValueError('Replay final hash does not match its recorded tail.')


def wrap_replay(envelope, native_replay):
    configuration = validate_config_envelope(envelope)
    _validate_native_replay(native_replay, configuration['resolved_config'])
    return {'schema':REPLAY_SCHEMA, 'official':False, 'complete':False,
            'configuration':configuration, 'native_replay':copy.deepcopy(native_replay),
            'native_replay_sha256':_digest(native_replay)}


def validate_replay_wrapper(wrapper):
    exact(wrapper, ('schema','official','complete','configuration','native_replay',
                    'native_replay_sha256'), 'sourced_replay')
    if wrapper['schema'] != REPLAY_SCHEMA or wrapper['official'] is not False or wrapper['complete'] is not False:
        raise ValueError('Unsupported sourced replay schema or completeness claim.')
    expected = wrap_replay(wrapper['configuration'], wrapper['native_replay'])
    _same(wrapper, expected, 'sourced_replay')
    return expected


def _guard_native():
    if native.build_id() != NATIVE_SOURCE_PIN:
        raise ValueError('Sourced projection requires its original unchanged synthetic native source.')


def reset_process(process, observer, envelope):
    """Validate everything before touching the unchanged transactional bridge."""
    configuration = validate_config_envelope(envelope)
    _guard_native()
    return process.reset({'observer':observer, 'config':configuration['resolved_config']})


def import_process(process, observer, wrapper):
    """Never route legacy raw replays here or silently attach source claims."""
    verified = validate_replay_wrapper(wrapper)
    _guard_native()
    return process.import_replay({'observer':observer, 'replay':verified['native_replay']})


def example_selection(preset_id=None):
    """An explicitly labeled editable example, not resolver fallback defaults.

    Its residual values are copied from the existing synthetic demo. Callers must
    submit the entire selection, including those values, to resolve_config.
    """
    trusted = _trusted_catalog()
    presets = {item['id']:item for item in trusted['presets']}
    if preset_id is None:
        preset_id = next(iter(presets))
    if type(preset_id) is not str or preset_id not in presets:
        raise ValueError('Unknown example preset.')
    preset = presets[preset_id]
    robots = []
    for robot in copy.deepcopy(native.DEFAULT_CONFIG['robots']):
        profile = robot.pop('profile')
        profile['id'] = 'SYNTHETIC-src-' + robot['id']
        for field in preset['overrides']:
            del profile[field]
        robots.append({**robot, 'preset_id':preset_id,
                       'conditions':copy.deepcopy(preset['conditions']), 'synthetic':profile})
    return {'schema':SELECTION_SCHEMA, 'catalog':_identity(),
            'strategies':{key:native.DEFAULT_CONFIG[key] for key in native.SELECTED_CONFIG}, 'robots':robots}
