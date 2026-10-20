"""Read declared preload file ranges without executing generated JavaScript."""
from pathlib import Path, PurePosixPath
import re
import math
from javascript_data_literal import JavaScriptDataLiteral


class EmscriptenPreloadPackage:
    @staticmethod
    def _byte_offset(value):
        # JavaScript Number has one numeric type. Minifiers may emit 28e3
        # for 28000. Accept only exact nonnegative safe integer values.
        if (type(value) not in (int, float) or not math.isfinite(value)
                or value < 0 or value > 2**53 - 1 or int(value) != value):
            raise ValueError('EMSCRIPTEN-PRELOAD-BYTE-OFFSET')
        return int(value)

    def __init__(self, javascript):
        javascript = Path(javascript)
        text = javascript.read_text(encoding='utf-8')
        manifests = []
        cursor = 0
        while match := re.search(r'\bloadPackage\s*\(\s*(?=\{)', text[cursor:]):
            manifest, end = JavaScriptDataLiteral.parse(text, cursor + match.end(), max_nesting=3)
            suffix = re.match(r'\s*\)', text[end:])
            if not suffix:
                raise ValueError('EMSCRIPTEN-PRELOAD-MANIFEST-EXECUTABLE')
            manifests.append(manifest)
            cursor = end + suffix.end()
        if len(manifests) != 1:
            raise ValueError('EMSCRIPTEN-PRELOAD-MANIFEST-AMBIGUOUS')
        manifest = manifests[0]
        if (set(manifest) != {'files', 'remote_package_size'}
                or not isinstance(manifest['files'], list) or not manifest['files']):
            raise ValueError('EMSCRIPTEN-PRELOAD-MANIFEST-SCHEMA')
        manifest['remote_package_size'] = self._byte_offset(manifest['remote_package_size'])
        self.data = javascript.with_suffix('.data').read_bytes()
        if len(self.data) != manifest['remote_package_size']:
            raise ValueError('EMSCRIPTEN-PRELOAD-SIZE')
        self.entries = {}
        previous = 0
        for entry in manifest['files']:
            if (not isinstance(entry, dict) or set(entry) != {'filename', 'start', 'end'}
                    or not isinstance(entry['filename'], str)):
                raise ValueError('EMSCRIPTEN-PRELOAD-MANIFEST-SCHEMA')
            entry['start'] = self._byte_offset(entry['start'])
            entry['end'] = self._byte_offset(entry['end'])
            name = PurePosixPath(entry['filename'])
            if (not name.is_absolute() or str(name) != entry['filename']
                    or '..' in name.parts or '\\' in entry['filename']):
                raise ValueError('EMSCRIPTEN-PRELOAD-FILENAME')
            if (entry['filename'] in self.entries or entry['start'] != previous
                    or entry['end'] < previous or entry['end'] > len(self.data)):
                raise ValueError('EMSCRIPTEN-PRELOAD-RANGE')
            self.entries[entry['filename']] = (entry['start'], entry['end'])
            previous = entry['end']
        if previous != len(self.data):
            raise ValueError('EMSCRIPTEN-PRELOAD-COVERAGE')

    def read(self, filename):
        start, end = self.entries[filename]
        return self.data[start:end]
