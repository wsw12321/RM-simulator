"""Vehicle garage: built-in default vehicles plus vehicles saved on this machine.

A vehicle is an rmuc-vehicle.v1 document. The native engine is the only
validator (range table and consistency rules live in vehicle.cpp); this module
stores normalised documents, names them and reports their content hash.

Saved vehicles live as one JSON file each under <data dir>/vehicles/, outside
version control, so they survive server restarts and updates of the checkout.
"""
import copy
import hashlib
import json
import os
import re
import secrets
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILTIN_DIR = ROOT / 'field_sim/vehicles'
ENTRY_FORMAT = 'rmuc-garage-entry.v1'
DEFAULT_VEHICLE = 'builtin:infantry_mecanum'
MAX_SAVED = 200
MAX_FILE_BYTES = 64 * 1024
SAVED_ID = re.compile(r'^garage:[0-9a-f]{12}$')
BUILTIN_ID = re.compile(r'^builtin:[a-z0-9_]{1,40}$')


class GarageError(ValueError):
    """A garage request that was refused; nothing was changed."""


def data_dir():
    return Path(os.environ.get('RMUC_DATA_DIR') or ROOT / 'user_data')


def vehicle_hash(vehicle):
    """SHA-256 of the normalised document in canonical JSON."""
    return hashlib.sha256(json.dumps(vehicle, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()).hexdigest()


class Garage:
    def __init__(self, check, directory=None):
        """check(vehicle) -> {'vehicle': normalised, 'attributes': [...]} or raises ValueError."""
        self._check = check
        self._dir = Path(directory) if directory is not None else data_dir() / 'vehicles'
        self._lock = threading.RLock()
        self._cache = {}   # raw file bytes hash -> checked entry or error text

    # ---- reading
    def _checked(self, vehicle):
        try:
            out = self._check(vehicle)
        except ValueError as exc:
            raise GarageError(str(exc)) from None
        return out['vehicle'], out['attributes']

    def _load(self, path, expect_entry):
        raw = path.read_bytes()
        key = hashlib.sha256(raw).hexdigest() + ('e' if expect_entry else 'b')
        if key not in self._cache:
            try:
                if len(raw) > MAX_FILE_BYTES:
                    raise GarageError('file is too large')
                doc = json.loads(raw)
                saved_at = None
                if expect_entry:
                    if not isinstance(doc, dict) or doc.get('entry_format') != ENTRY_FORMAT or 'vehicle' not in doc:
                        raise GarageError('not a garage entry')
                    saved_at = doc.get('saved_at')
                    doc = doc['vehicle']
                vehicle, attributes = self._checked(doc)
                self._cache[key] = {'vehicle': vehicle, 'attributes': attributes, 'hash': vehicle_hash(vehicle), 'saved_at': saved_at}
            except (GarageError, ValueError, UnicodeDecodeError) as exc:
                self._cache[key] = str(exc) or 'unreadable file'
        return self._cache[key]

    def _entries(self):
        out = []
        for path in sorted(BUILTIN_DIR.glob('*.json')):
            item = self._load(path, False)
            if isinstance(item, str):
                raise RuntimeError(f'Built-in vehicle {path.name} is invalid: {item}')
            out.append({'id': f'builtin:{path.stem}', 'readonly': True, **item})
        if self._dir.is_dir():
            saved = []
            for path in self._dir.glob('*.json'):
                vid = f'garage:{path.stem}'
                if not SAVED_ID.match(vid):
                    continue
                item = self._load(path, True)
                if isinstance(item, str):
                    saved.append({'id': vid, 'readonly': False, 'invalid': item})
                else:
                    saved.append({'id': vid, 'readonly': False, **item})
            saved.sort(key=lambda e: (e.get('saved_at') or '', e['id']))
            out.extend(saved)
        return out

    @staticmethod
    def _summary(entry):
        if 'invalid' in entry:
            return {'id': entry['id'], 'readonly': False, 'invalid': entry['invalid']}
        v = entry['vehicle']
        return {'id': entry['id'], 'readonly': entry['readonly'], 'name': v['name'], 'class': v['class'], 'notes': v['notes'],
                'hash': entry['hash'], 'saved_at': entry['saved_at'], 'attributes': entry['attributes'][0]}

    def list(self):
        with self._lock:
            return {'default': DEFAULT_VEHICLE, 'vehicles': [self._summary(e) for e in self._entries()]}

    def get(self, vehicle_id):
        if not isinstance(vehicle_id, str) or not (SAVED_ID.match(vehicle_id) or BUILTIN_ID.match(vehicle_id)):
            raise GarageError('Unknown vehicle id.')
        with self._lock:
            for entry in self._entries():
                if entry['id'] == vehicle_id:
                    if 'invalid' in entry:
                        raise GarageError(f'Saved vehicle {vehicle_id} is no longer valid: {entry["invalid"]}')
                    # copies: callers may edit what they get, the cache must not change
                    return {'id': entry['id'], 'readonly': entry['readonly'], 'vehicle': copy.deepcopy(entry['vehicle']), 'hash': entry['hash'],
                            'saved_at': entry['saved_at'], 'attributes': copy.deepcopy(entry['attributes'])}
        raise GarageError(f'Vehicle {vehicle_id} does not exist.')

    def check(self, vehicle):
        vehicle, attributes = self._checked(vehicle)
        return {'vehicle': vehicle, 'hash': vehicle_hash(vehicle), 'attributes': attributes}

    # ---- writing
    def save(self, vehicle, vehicle_id=None):
        """Create (vehicle_id None) or overwrite a saved vehicle. Built-in vehicles are read-only."""
        if vehicle_id is not None and not (isinstance(vehicle_id, str) and SAVED_ID.match(vehicle_id)):
            raise GarageError('Only saved vehicles (garage:…) can be overwritten; save built-in ones as a new vehicle.')
        vehicle, attributes = self._checked(vehicle)
        with self._lock:
            entries = self._entries()
            saved = [e for e in entries if not e['readonly']]
            if vehicle_id is None:
                if len(saved) >= MAX_SAVED:
                    raise GarageError(f'The garage holds at most {MAX_SAVED} saved vehicles.')
                vehicle_id = f'garage:{secrets.token_hex(6)}'
            elif not any(e['id'] == vehicle_id for e in saved):
                raise GarageError(f'Vehicle {vehicle_id} does not exist.')
            for e in entries:
                if e['id'] != vehicle_id and 'vehicle' in e and e['vehicle']['name'] == vehicle['name']:
                    raise GarageError(f'Another vehicle is already called {vehicle["name"]!r}; choose a different name.')
            entry = {'entry_format': ENTRY_FORMAT, 'id': vehicle_id,
                     'saved_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'vehicle': vehicle}
            self._dir.mkdir(parents=True, exist_ok=True)
            path = self._dir / f'{vehicle_id.split(":", 1)[1]}.json'
            tmp = path.with_suffix('.tmp')
            tmp.write_text(json.dumps(entry, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
            os.replace(tmp, path)
            return {'id': vehicle_id, 'readonly': False, 'vehicle': vehicle, 'hash': vehicle_hash(vehicle),
                    'saved_at': entry['saved_at'], 'attributes': attributes}

    def delete(self, vehicle_id):
        if not isinstance(vehicle_id, str) or not SAVED_ID.match(vehicle_id):
            raise GarageError('Only saved vehicles (garage:…) can be deleted.')
        with self._lock:
            path = self._dir / f'{vehicle_id.split(":", 1)[1]}.json'
            if not path.is_file():
                raise GarageError(f'Vehicle {vehicle_id} does not exist.')
            path.unlink()
        return {'deleted': vehicle_id}
