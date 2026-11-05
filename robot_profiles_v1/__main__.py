"""Read-only local resolver: python -m robot_profiles_v1 catalog|example|resolve|validate."""
import argparse
import json
import sys
from .api import _json, catalog, example_selection, resolve_config, validate_config_envelope, validate_replay_wrapper

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('command', choices=('catalog','example','resolve','validate','validate-replay'))
parser.add_argument('input', nargs='?', help='Input JSON file; omit to read stdin. For example, optional preset ID.')
args = parser.parse_args()
try:
    if args.command == 'catalog':
        result = catalog()
    elif args.command == 'example':
        result = example_selection(args.input)
    else:
        with open(args.input, 'rb') if args.input else sys.stdin.buffer as source:
            data = _json(source.read(), 'input')
        result = {'resolve':resolve_config, 'validate':validate_config_envelope,
                  'validate-replay':validate_replay_wrapper}[args.command](data)
    print(json.dumps(result, indent=2, ensure_ascii=False))
except (ValueError, OSError) as exc:
    parser.exit(2, str(exc)+'\n')
