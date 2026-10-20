"""CMakePresetDocument/1: strict envelope custody for preset generators.

This is not a full CMake schema validator or a multi-process file transaction.
"""

import json
import copy
import math
import os
from pathlib import Path
import tempfile


class CMakePresetDocumentError(ValueError):
    """Stable tooling refusal; does not grant GPU capability or readiness."""


class CMakePresetDocument:
    VERSION = 1
    SCHEMA_VERSION = 6
    CATEGORIES = ('configurePresets', 'buildPresets', 'testPresets',
                  'packagePresets', 'workflowPresets')
    MANAGED_CATEGORIES = CATEGORIES[:3]

    def __init__(self, path, snapshot, data):
        self.path = Path(path)
        self._snapshot = snapshot
        self._data = data

    @staticmethod
    def _error(label, message):
        return CMakePresetDocumentError(f'RHI-BUILD-PRESETS-{label}: {message}')

    @classmethod
    def _object(cls, pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise cls._error('DUPLICATE-KEY', 'document contains a duplicate object key')
            result[key] = value
        return result

    @classmethod
    def _constant(cls, value):
        raise cls._error('JSON', 'non-finite JSON number is not permitted')

    @classmethod
    def _float(cls, value):
        result = float(value)
        if not math.isfinite(result):
            raise cls._error('JSON', 'JSON number exceeds finite representation')
        return result

    @classmethod
    def _validate(cls, data):
        if not isinstance(data, dict):
            raise cls._error('SHAPE', 'root must be an object')
        version = data.get('version')
        if type(version) is not int or version != cls.SCHEMA_VERSION:
            raise cls._error('VERSION', 'only explicit integer schema version 6 is supported')
        for category in cls.CATEGORIES:
            if category not in data:
                continue
            entries = data[category]
            if not isinstance(entries, list):
                raise cls._error('SHAPE', f'{category} must be an array')
            names = set()
            for entry in entries:
                if not isinstance(entry, dict) or not isinstance(entry.get('name'), str) or not entry['name']:
                    raise cls._error('SHAPE', f'{category} entries require nonempty string names')
                if entry['name'] in names:
                    raise cls._error('SHAPE', f'{category} contains duplicate preset names')
                names.add(entry['name'])
        if 'include' in data:
            paths = data['include']
            if not isinstance(paths, list) or any(not isinstance(p, str) or not p for p in paths):
                raise cls._error('SHAPE', 'include must be an array of nonempty paths')
        if 'vendor' in data and not isinstance(data['vendor'], dict):
            raise cls._error('SHAPE', 'vendor must be an object')
        if 'cmakeMinimumRequired' in data:
            minimum = data['cmakeMinimumRequired']
            if not isinstance(minimum, dict) or any(
                    key not in ('major', 'minor', 'patch') or type(value) is not int or value < 0
                    for key, value in minimum.items()):
                raise cls._error('SHAPE', 'cmakeMinimumRequired must contain nonnegative integer version fields')

    @classmethod
    def load(cls, path):
        path = Path(path)
        try:
            snapshot = path.read_bytes()
        except FileNotFoundError:
            return cls(path, None, {'version': cls.SCHEMA_VERSION})
        except OSError as error:
            raise cls._error('IO', f'cannot read preset document ({type(error).__name__})') from error
        try:
            source = snapshot.decode('utf-8')
        except UnicodeError as error:
            raise cls._error('ENCODING', 'preset document must be UTF-8') from error
        try:
            data = json.loads(source, object_pairs_hook=cls._object,
                              parse_constant=cls._constant, parse_float=cls._float)
        except CMakePresetDocumentError:
            raise
        except (ValueError, RecursionError) as error:
            raise cls._error('JSON', 'preset document is not valid supported JSON') from error
        cls._validate(data)
        return cls(path, snapshot, data)

    def write_managed(self, generated, managed_names=None):
        """Replace owned names, preserving all other document values."""
        self._validate(generated)
        if managed_names is None:
            managed_names = {p['name'] for category in self.MANAGED_CATEGORIES
                             for p in generated.get(category, [])}
        merged = dict(self._data)
        for category in self.MANAGED_CATEGORIES:
            merged[category] = list(generated.get(category, [])) + [
                p for p in self._data.get(category, []) if p['name'] not in managed_names]
        self._validate(merged)
        try:
            content = (json.dumps(merged, indent=2, ensure_ascii=False, allow_nan=False) + '\n').encode('utf-8')
        except (ValueError, UnicodeError, RecursionError) as error:
            raise self._error('JSON', 'merged document cannot be serialized') from error
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode='wb', dir=self.path.parent,
                    prefix=f'.{self.path.name}.', suffix='.tmp', delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(content)
            try:
                current = self.path.read_bytes()
            except FileNotFoundError:
                current = None
            if current != self._snapshot:
                raise self._error('CHANGED', 'preset document changed since preflight; retry from current input')
            os.replace(temporary, self.path)
        except OSError as error:
            raise self._error('WRITE', f'cannot publish preset document ({type(error).__name__})') from error
        finally:
            if temporary is not None:
                try:
                    temporary.unlink(missing_ok=True)
                except OSError as error:
                    raise self._error('WRITE', 'cannot remove unpublished preset temporary file') from error
        self._snapshot = content
        self._data = merged

    def preset(self, category, name):
        """Copy a root entry; includes/inheritance are not resolved here."""
        if category not in self.CATEGORIES:
            raise self._error('SHAPE', 'unknown preset category')
        for entry in self._data.get(category, []):
            if entry['name'] == name:
                return copy.deepcopy(entry)
        return None
