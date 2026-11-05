"""python -m arena_format validate FILE | query FILE --x-mm X --y-mm Y"""
import argparse
import json
from pathlib import Path
import sys
from . import ArenaValidationError, MAX_TEXT_BYTES, parse_document, query


def main(argv=None):
    parser = argparse.ArgumentParser(description='Experimental layered geometry only; not an official map or motion simulator.')
    commands = parser.add_subparsers(dest='command', required=True)
    validate = commands.add_parser('validate', help='Strict JSON and semantic validation')
    validate.add_argument('file')
    probe = commands.add_parser('query', help='Read-only point or upright-cylinder feasibility evidence')
    probe.add_argument('file')
    probe.add_argument('--x-mm', required=True, type=float)
    probe.add_argument('--y-mm', required=True, type=float)
    probe.add_argument('--radius-mm', type=float, default=0)
    probe.add_argument('--height-mm', type=float, default=0)
    probe.add_argument('--max-slope-deg', type=float, default=90)
    probe.add_argument('--surface-id')
    args = parser.parse_args(argv)
    try:
        with Path(args.file).open('rb') as handle:
            raw = handle.read(MAX_TEXT_BYTES + 1)
        if len(raw) > MAX_TEXT_BYTES:
            raise ArenaValidationError('$', 'limit', f'JSON file exceeds {MAX_TEXT_BYTES} bytes.')
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            raise ArenaValidationError('$', 'encoding', 'Input file must be UTF-8.') from None
        document = parse_document(text)
        if args.command == 'validate':
            result = {'valid': True, 'schema': document['schema'], 'official_map': False,
                      'calibration_status': document['calibration']['status'],
                      'counts': {key: len(document[key]) for key in ('layers', 'surfaces', 'collision_volumes', 'trigger_volumes', 'transitions')}}
        else:
            result = query(document, args.x_mm, args.y_mm, radius_mm=args.radius_mm,
                           height_mm=args.height_mm, max_slope_deg=args.max_slope_deg,
                           surface_id=args.surface_id)
    except ArenaValidationError as exc:
        print(json.dumps({'valid': False, 'errors': exc.errors}, ensure_ascii=False, allow_nan=False))
        return 2
    except OSError as exc:
        print(json.dumps({'valid': False, 'errors': [{'path': '$', 'code': 'file', 'message': str(exc)}]}))
        return 2
    print(json.dumps(result, ensure_ascii=False, allow_nan=False, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
