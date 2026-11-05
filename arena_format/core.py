"""Dependency-free reference implementation for rmuc-layered-arena.v1."""
import json
import math
import re

SCHEMA = 'rmuc-layered-arena.v1'
MAX_TEXT_BYTES = 256 * 1024
MAX_DEPTH = 16
MAX_NODES = 20000
MAX_ITEMS = 256
MAX_VERTICES = 64
MAX_COORD = 1_000_000
EPS = 1e-7  # numerical comparison tolerance, not structural uncertainty
ID = re.compile(r'^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$')


class ArenaValidationError(ValueError):
    """First bounded error, as a stable JSON-safe list of {path, code, message}."""
    def __init__(self, path, code, message):
        self.errors = [{'path': path, 'code': code, 'message': message}]
        super().__init__(f'{path}: {code}: {message}')


def _fail(path, code, message):
    raise ArenaValidationError(path, code, message)


def _child(path, key):
    return f'{path}.{key}' if re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', key) else path + '[' + json.dumps(key) + ']'


class _Pairs(list):
    pass


class _Constant:
    pass


def parse_document(text):
    """Parse strict bounded JSON, preserving duplicate-key errors, then validate.

    Only str input is accepted. No files, URLs, includes, or external references
    are fetched. Input and parsed documents are never mutated.
    """
    if type(text) is not str:
        _fail('$', 'type', 'JSON input must be a string.')
    try:
        size = len(text.encode('utf-8'))
    except UnicodeEncodeError:
        _fail('$', 'encoding', 'JSON text must be valid UTF-8 without lone surrogates.')
    if size > MAX_TEXT_BYTES:
        _fail('$', 'limit', f'JSON text exceeds {MAX_TEXT_BYTES} UTF-8 bytes.')
    try:
        raw = json.loads(text, object_pairs_hook=_Pairs, parse_constant=lambda _: _Constant())
    except json.JSONDecodeError as exc:
        _fail('$', 'json_syntax', f'Invalid JSON at line {exc.lineno}, column {exc.colno}.')
    except (RecursionError, ValueError):
        _fail('$', 'limit', 'JSON nesting or numeric literal exceeds parser limits.')
    count = 0

    def materialize(value, path, depth):
        nonlocal count
        count += 1
        if depth > MAX_DEPTH or count > MAX_NODES:
            _fail(path, 'limit', 'JSON nesting or node count exceeds limits.')
        if isinstance(value, _Constant):
            _fail(path, 'number', 'NaN and Infinity are not JSON numbers.')
        if isinstance(value, _Pairs):
            result = {}
            for key, item in value:
                if len(key) > 128:
                    _fail(path, 'limit', 'Object keys must not exceed 128 characters.')
                child = _child(path, key)
                if key in result:
                    _fail(child, 'duplicate_key', 'Duplicate JSON object key.')
                result[key] = materialize(item, child, depth + 1)
            return result
        if type(value) is list:
            return [materialize(v, f'{path}[{i}]', depth + 1) for i, v in enumerate(value)]
        return value

    return validate_document(materialize(raw, '$', 0))


def _preflight(document):
    count = 0
    seen = set()

    def walk(value, path, depth):
        nonlocal count
        count += 1
        if depth > MAX_DEPTH or count > MAX_NODES:
            _fail(path, 'limit', 'Document nesting or node count exceeds limits.')
        kind = type(value)
        if kind in (dict, list):
            if id(value) in seen:
                _fail(path, 'cycle', 'Cyclic or shared containers are not supported; use a JSON tree.')
            seen.add(id(value))
            for key, item in (value.items() if kind is dict else enumerate(value)):
                if kind is dict:
                    if type(key) is not str:
                        _fail(path, 'type', 'Object keys must be strings.')
                    if len(key) > 128:
                        _fail(path, 'limit', 'Object keys must not exceed 128 characters.')
                walk(item, _child(path, key) if kind is dict else f'{path}[{key}]', depth + 1)
        elif kind is str:
            if len(value) > 4096:
                _fail(path, 'limit', 'String exceeds 4096 characters.')
            try:
                value.encode('utf-8')
            except UnicodeEncodeError:
                _fail(path, 'encoding', 'Lone surrogate characters are not supported.')
        elif kind in (int, float):
            if (kind is float and not math.isfinite(value)) or abs(value) > 1e12:
                _fail(path, 'number', 'Numbers must be finite and bounded by 1e12.')
        elif kind not in (bool, type(None)):
            _fail(path, 'type', 'Only JSON value types are supported.')

    walk(document, '$', 0)
    if len(json.dumps(document, ensure_ascii=True, separators=(',', ':')).encode()) > MAX_TEXT_BYTES:
        _fail('$', 'limit', f'Canonical JSON exceeds {MAX_TEXT_BYTES} bytes.')


def _obj(value, path, fields):
    if type(value) is not dict:
        _fail(path, 'type', 'Expected an object.')
    for key in value:
        if key not in fields:
            _fail(_child(path, key), 'unknown_field', 'Unknown field.')
    for key in fields:
        if key not in value:
            _fail(_child(path, key), 'required', 'Required field is missing.')


def _array(value, path, minimum=0, maximum=MAX_ITEMS):
    if type(value) is not list:
        _fail(path, 'type', 'Expected an array.')
    if not minimum <= len(value) <= maximum:
        _fail(path, 'limit', f'Array must contain {minimum}..{maximum} items.')


def _str(value, path, maximum=1024):
    if type(value) is not str or not value.strip() or len(value) > maximum:
        _fail(path, 'string', f'Expected a nonempty string of at most {maximum} characters.')


def _id(value, path):
    if type(value) is not str or not ID.fullmatch(value):
        _fail(path, 'id', 'Expected a 1..64 character identifier starting with a letter.')


def _num(value, path, low=-MAX_COORD, high=MAX_COORD):
    if type(value) not in (int, float) or not low <= value <= high or (type(value) is float and not math.isfinite(value)):
        _fail(path, 'number', f'Expected a finite number in [{low}, {high}], not boolean.')


def _bool(value, path):
    if type(value) is not bool:
        _fail(path, 'type', 'Expected boolean.')


def _enum(value, path, choices):
    if type(value) is not str or value not in choices:
        _fail(path, 'enum', 'Expected one of: ' + ', '.join(choices) + '.')


def _point(value, path):
    _array(value, path, 2, 2)
    _num(value[0], path + '[0]')
    _num(value[1], path + '[1]')


def _cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _polygon(value, path):
    _array(value, path, 3, MAX_VERTICES)
    for i, point in enumerate(value):
        _point(point, f'{path}[{i}]')
    # Every other vertex must lie strictly left of every directed edge. This
    # rejects clockwise, concave, collinear, duplicate and self-crossing rings.
    for i, a in enumerate(value):
        b = value[(i + 1) % len(value)]
        length = math.dist(a, b)
        if length <= EPS:
            _fail(path, 'polygon', 'Polygon has duplicate or degenerate vertices.')
        for j, c in enumerate(value):
            if j not in (i, (i + 1) % len(value)) and _cross(a, b, c) / length <= EPS:
                _fail(path, 'polygon', 'Polygon must be simple, strictly convex and counterclockwise.')


def _edge_distances(polygon, point):
    return [_cross(a, polygon[(i + 1) % len(polygon)], point) /
            math.dist(a, polygon[(i + 1) % len(polygon)]) for i, a in enumerate(polygon)]


def _inside(polygon, point, radius=0):
    return min(_edge_distances(polygon, point)) >= radius - EPS


def _segment_distance(point, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / (dx * dx + dy * dy)))
    return math.hypot(point[0] - a[0] - t * dx, point[1] - a[1] - t * dy)


def _intersects(polygon, point, radius):
    return _inside(polygon, point) or any(
        _segment_distance(point, a, polygon[(i + 1) % len(polygon)]) <= radius + EPS
        for i, a in enumerate(polygon))


def _height(surface, point):
    h = surface['height']
    return h['z_mm'] + h['dx'] * point[0] + h['dy'] * point[1]


def _refs(value, path, known, minimum=1):
    _array(value, path, minimum)
    used = set()
    for i, item in enumerate(value):
        p = f'{path}[{i}]'
        _id(item, p)
        if item not in known:
            _fail(p, 'reference', f'Unknown identifier: {item}.')
        if item in used:
            _fail(p, 'duplicate_id', 'Duplicate reference.')
        used.add(item)


def _records(document, field, fields, used):
    items = document[field]
    _array(items, '$.' + field, 1 if field in ('sources', 'layers', 'surfaces') else 0)
    for i, item in enumerate(items):
        path = f'$.{field}[{i}]'
        _obj(item, path, fields)
        _id(item['id'], path + '.id')
        if item['id'] in used:
            _fail(path + '.id', 'duplicate_id', 'Identifiers must be globally unique.')
        used.add(item['id'])
        yield item, path


def validate_document(document):
    """Validate a JSON-compatible dict without changing it; return that dict.

    A successful result certifies this experimental data contract only, not
    surveyed geometry, official rules, safe passage or real-world robot motion.
    """
    _preflight(document)
    _obj(document, '$', ('schema', 'metadata', 'frame', 'calibration', 'sources',
                        'boundary_mm', 'layers', 'surfaces', 'collision_volumes',
                        'trigger_volumes', 'transitions'))
    _enum(document['schema'], '$.schema', (SCHEMA,))
    meta = document['metadata']
    _obj(meta, '$.metadata', ('id', 'name', 'description', 'official_map'))
    _id(meta['id'], '$.metadata.id')
    _str(meta['name'], '$.metadata.name', 128)
    _str(meta['description'], '$.metadata.description', 2048)
    if meta['official_map'] is not False:
        _fail('$.metadata.official_map', 'scope', 'This experimental format must declare official_map=false.')
    frame = document['frame']
    _obj(frame, '$.frame', ('units', 'handedness', 'up_axis', 'x_axis', 'y_axis', 'origin'))
    for key, fixed in (('units', 'mm'), ('handedness', 'right'), ('up_axis', '+z')):
        _enum(frame[key], '$.frame.' + key, (fixed,))
    for key in ('x_axis', 'y_axis', 'origin'):
        _str(frame[key], '$.frame.' + key)
    cal = document['calibration']
    _obj(cal, '$.calibration', ('status', 'notes'))
    _enum(cal['status'], '$.calibration.status', ('synthetic', 'unverified', 'partial', 'calibrated'))
    _array(cal['notes'], '$.calibration.notes', 1, 32)
    for i, note in enumerate(cal['notes']):
        _str(note, f'$.calibration.notes[{i}]', 2048)
    boundary = document['boundary_mm']
    _polygon(boundary, '$.boundary_mm')
    used = set()
    sources = {}
    for source, p in _records(document, 'sources', ('id', 'kind', 'reference', 'sha256', 'notes'), used):
        _enum(source['kind'], p + '.kind', ('synthetic', 'manual', 'cad', 'survey'))
        _str(source['reference'], p + '.reference')
        _str(source['notes'], p + '.notes', 2048)
        if source['sha256'] is not None and (type(source['sha256']) is not str or not re.fullmatch('[0-9a-f]{64}', source['sha256'])):
            _fail(p + '.sha256', 'digest', 'Expected lowercase SHA-256 hex or null.')
        sources[source['id']] = source
    layers = {}
    for layer, p in _records(document, 'layers', ('id', 'name', 'source_ids'), used):
        _str(layer['name'], p + '.name', 128)
        _refs(layer['source_ids'], p + '.source_ids', sources)
        layers[layer['id']] = layer
    surfaces = {}
    for surface, p in _records(document, 'surfaces', ('id', 'layer_id', 'polygon_mm', 'height', 'ceiling_z_mm', 'traversable', 'source_ids'), used):
        _id(surface['layer_id'], p + '.layer_id')
        if surface['layer_id'] not in layers:
            _fail(p + '.layer_id', 'reference', 'Unknown layer.')
        _polygon(surface['polygon_mm'], p + '.polygon_mm')
        if not all(_inside(boundary, vertex) for vertex in surface['polygon_mm']):
            _fail(p + '.polygon_mm', 'boundary', 'Surface vertices must lie within the arena boundary.')
        h = surface['height']
        _obj(h, p + '.height', ('z_mm', 'dx', 'dy'))
        _num(h['z_mm'], p + '.height.z_mm')
        _num(h['dx'], p + '.height.dx', -10, 10)
        _num(h['dy'], p + '.height.dy', -10, 10)
        for vertex in surface['polygon_mm']:
            _num(_height(surface, vertex), p + '.height', -MAX_COORD, MAX_COORD)
        ceiling = surface['ceiling_z_mm']
        if ceiling is not None:
            _num(ceiling, p + '.ceiling_z_mm')
            if any(ceiling <= _height(surface, vertex) + EPS for vertex in surface['polygon_mm']):
                _fail(p + '.ceiling_z_mm', 'vertical_range', 'Ceiling must be above the floor at every vertex.')
        _bool(surface['traversable'], p + '.traversable')
        _refs(surface['source_ids'], p + '.source_ids', sources)
        surfaces[surface['id']] = surface
    for field in ('collision_volumes', 'trigger_volumes'):
        fields = ('id', 'polygon_mm', 'z_min_mm', 'z_max_mm', 'source_ids')
        if field == 'trigger_volumes':
            fields += ('surface_ids', 'rule_tag')
        for volume, p in _records(document, field, fields, used):
            _polygon(volume['polygon_mm'], p + '.polygon_mm')
            for z in ('z_min_mm', 'z_max_mm'):
                _num(volume[z], p + '.' + z)
            if volume['z_max_mm'] <= volume['z_min_mm'] + EPS:
                _fail(p + '.z_max_mm', 'vertical_range', 'Volume top must be strictly above its bottom.')
            _refs(volume['source_ids'], p + '.source_ids', sources)
            if field == 'trigger_volumes':
                _refs(volume['surface_ids'], p + '.surface_ids', surfaces)
                _str(volume['rule_tag'], p + '.rule_tag', 128)
    for trans, p in _records(document, 'transitions', ('id', 'from_surface', 'to_surface', 'portal_mm', 'bidirectional', 'max_step_mm', 'source_ids'), used):
        for key in ('from_surface', 'to_surface'):
            _id(trans[key], p + '.' + key)
            if trans[key] not in surfaces:
                _fail(p + '.' + key, 'reference', 'Unknown surface.')
        if trans['from_surface'] == trans['to_surface']:
            _fail(p + '.to_surface', 'reference', 'Transition must join different surfaces.')
        _array(trans['portal_mm'], p + '.portal_mm', 2, 2)
        for i, point in enumerate(trans['portal_mm']):
            _point(point, p + f'.portal_mm[{i}]')
        a, b = trans['portal_mm']
        if math.dist(a, b) <= EPS:
            _fail(p + '.portal_mm', 'portal', 'Portal endpoints must be distinct.')
        _num(trans['max_step_mm'], p + '.max_step_mm', 0, 10000)
        _bool(trans['bidirectional'], p + '.bidirectional')
        _refs(trans['source_ids'], p + '.source_ids', sources)
        # Portal is an authored shared boundary segment, not a teleport or a
        # link inferred merely because two stacked polygons overlap in XY.
        for sid in (trans['from_surface'], trans['to_surface']):
            poly = surfaces[sid]['polygon_mm']
            if not any(_segment_distance(a, e, poly[(i + 1) % len(poly)]) <= EPS and
                       _segment_distance(b, e, poly[(i + 1) % len(poly)]) <= EPS
                       for i, e in enumerate(poly)):
                _fail(p + '.portal_mm', 'portal', 'Portal must lie on one boundary edge of each surface.')
        if any(abs(_height(surfaces[trans['from_surface']], v) - _height(surfaces[trans['to_surface']], v)) > trans['max_step_mm'] + EPS for v in (a, b)):
            _fail(p + '.max_step_mm', 'portal', 'Declared max_step_mm is smaller than the endpoint height difference.')
    return document


def query(document, x_mm, y_mm, *, radius_mm=0, height_mm=0, max_slope_deg=90, surface_id=None):
    """Return conservative static feasibility evidence at one XY location.

    An upright circular cylinder uses the full floor-height range under its
    footprint. Each surface is tested independently, including stacked floors;
    seams are not merged. Trigger hits and nearby portals are annotations only.
    This is not swept collision detection, reachability, or mobility simulation.
    """
    validate_document(document)
    for key, value, low, high in (('x_mm', x_mm, -MAX_COORD, MAX_COORD),
                                ('y_mm', y_mm, -MAX_COORD, MAX_COORD),
                                ('radius_mm', radius_mm, 0, 10000),
                                ('height_mm', height_mm, 0, 10000),
                                ('max_slope_deg', max_slope_deg, 0, 90)):
        _num(value, '$.probe.' + key, low, high)
    if surface_id is not None:
        _id(surface_id, '$.probe.surface_id')
        if surface_id not in {s['id'] for s in document['surfaces']}:
            _fail('$.probe.surface_id', 'reference', 'Unknown support surface.')
    point = [x_mm, y_mm]
    candidates = []
    for surface in document['surfaces']:
        if surface_id is not None and surface['id'] != surface_id:
            continue
        if not _inside(surface['polygon_mm'], point):
            continue
        h = surface['height']
        gradient = math.hypot(h['dx'], h['dy'])
        slope = math.degrees(math.atan(gradient))
        z = _height(surface, point)
        low, high = z - radius_mm * gradient, z + radius_mm * gradient
        top = high + height_mm
        clearance = None if surface['ceiling_z_mm'] is None else surface['ceiling_z_mm'] - high
        reasons = []

        def reason(code, message):
            reasons.append({'code': code, 'message': message})

        if not surface['traversable']:
            reason('not_traversable', 'Surface is explicitly not traversable.')
        if not _inside(surface['polygon_mm'], point, radius_mm):
            reason('footprint_outside_surface', 'Full circular footprint must fit on this single support surface.')
        if not _inside(document['boundary_mm'], point, radius_mm):
            reason('footprint_outside_boundary', 'Circular footprint extends outside the declared arena boundary.')
        if slope > max_slope_deg + EPS:
            reason('slope_exceeded', 'Surface slope exceeds the supplied robot limit.')
        if clearance is not None and height_mm >= clearance - EPS:
            reason('ceiling_clearance', 'Cylinder touches or exceeds the declared ceiling.')
        collisions = []
        for volume in document['collision_volumes']:
            # Supporting contact at a solid top is allowed. Side and overhead
            # contact count as blocked, with EPS as arithmetic tolerance only.
            if volume['z_max_mm'] <= low + EPS or volume['z_min_mm'] > top + EPS:
                continue
            if _intersects(volume['polygon_mm'], point, radius_mm):
                collisions.append(volume['id'])
        if collisions:
            reason('collision', 'Cylinder intersects or touches a physical collision volume.')
        triggers = []
        for volume in document['trigger_volumes']:
            if surface['id'] not in volume['surface_ids']:
                continue
            if volume['z_max_mm'] < low - EPS or volume['z_min_mm'] > top + EPS:
                continue
            if _intersects(volume['polygon_mm'], point, radius_mm):
                triggers.append(volume['id'])
        transitions = []
        for trans in document['transitions']:
            allowed = surface['id'] == trans['from_surface'] or (trans['bidirectional'] and surface['id'] == trans['to_surface'])
            if allowed and _segment_distance(point, *trans['portal_mm']) <= radius_mm + EPS:
                transitions.append(trans['id'])
        candidates.append({'surface_id': surface['id'], 'layer_id': surface['layer_id'],
                           'support_z_mm': z, 'support_range_mm': [low, high],
                           'slope_deg': slope, 'clearance_mm': clearance,
                           'feasible': not reasons, 'reasons': reasons,
                           'collision_ids': collisions, 'trigger_ids': triggers,
                           'transition_ids': transitions})
    warnings = ['Experimental static geometry only; no official map, motion, pathfinding or rule adjudication.',
                'Each support surface is checked independently. Portals do not certify passage; triggers do not apply rules.',
                'Cylinder uses the full slope height range. Numeric tolerance is 1e-7 mm, not a safety margin.']
    if document['calibration']['status'] != 'calibrated':
        warnings.append('Geometry calibration status: ' + document['calibration']['status'] + '.')
    if not candidates:
        warnings.append('No selected support surface contains the probe center.')
    elif len(candidates) > 1:
        warnings.append('Multiple support surfaces share this XY. Select a surface; no layer transition is inferred.')
    return {'schema': 'rmuc-layered-query.v1', 'feasible': any(c['feasible'] for c in candidates),
            'point_mm': point, 'robot': {'radius_mm': radius_mm, 'height_mm': height_mm, 'max_slope_deg': max_slope_deg},
            'selected_surface_id': surface_id, 'candidates': candidates, 'warnings': warnings}
