"""CMakeBuildCacheAdmission/1: declared build metadata, never GPU authority."""

import copy
import os
from pathlib import Path
import re
import shutil

from cmake_presets_document import CMakePresetDocument


class CMakeBuildCacheError(ValueError):
    pass


def _error(label, message):
    return CMakeBuildCacheError(f'RHI-BUILD-CACHE-{label}: {message}')


def _typed_value(value):
    if isinstance(value, dict):
        if (set(value) != {'type', 'value'} or not isinstance(value['type'], str)
                or value['type'] not in CMakeCacheDocument.TYPES):
            raise _error('INTENT', 'unsupported typed identity variable')
        return value['value']
    return value


class CMakeCacheDocument:
    VERSION = 1
    TYPES = frozenset(('BOOL', 'FILEPATH', 'PATH', 'STRING', 'INTERNAL', 'STATIC', 'UNINITIALIZED'))

    def __init__(self, path, entries):
        self.path = Path(path)
        self._entries = entries

    @classmethod
    def load(cls, path, *, allow_missing=False):
        path = Path(path)
        try:
            raw = path.read_bytes()
        except FileNotFoundError as error:
            if allow_missing:
                return None
            raise _error('MISSING', 'configure the selected build directory first') from error
        except OSError as error:
            raise _error('IO', 'cannot read the selected cache') from error
        try:
            source = raw.decode('utf-8')
        except UnicodeError as error:
            raise _error('ENCODING', 'cache must be UTF-8') from error
        if '\0' in source:
            raise _error('SHAPE', 'cache contains a NUL byte')
        entries = {}
        for line in source.splitlines():
            if not line.strip() or line.lstrip().startswith(('#', '//')):
                continue
            head, separator, value = line.partition('=')
            key, colon, kind = head.rpartition(':')
            if len(key) >= 2 and key.startswith('"') and key.endswith('"'):
                key = key[1:-1]
            if not separator or not colon or not key or kind not in cls.TYPES:
                raise _error('SHAPE', 'cache contains an unsupported record')
            if key in entries:
                raise _error('DUPLICATE', 'cache contains a duplicate key')
            entries[key] = (kind, value)
        return cls(path, entries)

    def value(self, key, *, required=True):
        entry = self._entries.get(key)
        if entry is None:
            if required:
                raise _error('MISSING', f'cache lacks required field {key}')
            return None
        return entry[1]

    def require_locations(self, source_root, binary_dir):
        for key, expected, diagnostic in (
                ('CMAKE_HOME_DIRECTORY', Path(source_root), 'SOURCE'),
                ('CMAKE_CACHEFILE_DIR', Path(binary_dir), 'DIRECTORY')):
            value = Path(self.value(key))
            if not value.is_absolute() or value.resolve() != expected.resolve():
                raise _error(diagnostic, f'{key} does not match the requested project')


class CMakePresetIdentity:
    """Owned flat configure metadata shared by both build entrypoints."""

    VERSION = 1
    SINGLE_CONFIG_GENERATORS = frozenset(('Ninja', 'Unix Makefiles', 'MinGW Makefiles'))

    def __init__(self, source_root, preset):
        if not isinstance(preset, dict) or 'inherits' in preset:
            raise _error('INTENT', 'a root flat configure preset is required')
        self.source_root = Path(source_root).resolve()
        self.preset = copy.deepcopy(preset)
        for key in ('name', 'generator', 'binaryDir'):
            if not isinstance(preset.get(key), str) or not preset[key]:
                raise _error('INTENT', f'configure preset requires {key}')
        if not isinstance(preset.get('cacheVariables', {}), dict) or not isinstance(preset.get('environment', {}), dict):
            raise _error('INTENT', 'preset cacheVariables/environment must be objects')
        binary = Path(self._expand(preset['binaryDir']))
        self.binary_dir = (binary if binary.is_absolute() else self.source_root / binary).resolve()

    def _expand(self, value, stack=()):
        if not isinstance(value, str) or not value or any(c in value for c in '\0\r\n'):
            raise _error('INTENT', 'identity fields require nonempty single-line strings')
        result = value
        for key, replacement in (('sourceDir', str(self.source_root)),
                ('sourceParentDir', str(self.source_root.parent)), ('presetName', self.preset['name'])):
            result = result.replace('${' + key + '}', replacement)

        def environment(match):
            kind, key = match.groups()
            overrides = self.preset.get('environment', {})
            if kind == 'env' and key in overrides:
                if key in stack:
                    raise _error('UNRESOLVED', 'cyclic preset environment reference')
                return self._expand(overrides[key], stack + (key,))
            if key not in os.environ:
                raise _error('UNRESOLVED', 'identity refers to a missing environment value')
            return os.environ[key]

        try:
            result = re.sub(r'\$(env|penv)\{([^{}]+)\}', environment, result)
        except RecursionError as error:
            raise _error('UNRESOLVED', 'environment expansion exceeds supported recursion') from error
        if re.search(r'\$[a-zA-Z]*\{', result):
            raise _error('UNRESOLVED', 'unsupported or unresolved identity macro')
        if not result or any(c in result for c in '\0\r\n'):
            raise _error('UNRESOLVED', 'expanded identity is empty or not single-line')
        return result

    def _variable(self, key):
        return self._expand(_typed_value(self.preset.get('cacheVariables', {}).get(key)))

    def _absolute(self, value, diagnostic):
        path = Path(value)
        if not path.is_absolute():
            raise _error(diagnostic, 'identity path must be absolute')
        return path.resolve()

    def _compiler(self, key):
        value = self._variable(key)
        if Path(value).is_absolute():
            return Path(value).resolve()
        if '/' in value or '\\' in value or ':' in value:
            raise _error('UNRESOLVED', 'relative compiler paths are not supported')
        environment = self.preset.get('environment', {})
        if os.name == 'nt' and 'PATHEXT' in environment:
            raise _error('UNRESOLVED', 'bare compiler with preset PATHEXT requires a resolved tool intent')
        search = self._expand(environment['PATH']) if 'PATH' in environment else os.environ.get('PATH', '')
        resolved = shutil.which(value, path=search)
        if not resolved:
            raise _error('UNRESOLVED', 'bare compiler is not found in the declared PATH')
        return Path(resolved).resolve()

class CMakeBuildIntent(CMakePresetIdentity):
    """Declared build metadata, never an inferred native backend or ABI."""

    def __init__(self, source_root, preset, backend, configuration):
        super().__init__(source_root, preset)
        self.backend = backend
        self.configuration = configuration
        if backend not in ('none', 'vulkan', 'metal', 'webgpu') or configuration not in ('Debug', 'Release'):
            raise _error('INTENT', 'unsupported declared product or configuration')

    def _declared_toolchain(self):
        variables = self.preset.get('cacheVariables', {})
        toolchain = self.preset.get('toolchainFile')
        if toolchain is None and 'CMAKE_TOOLCHAIN_FILE' in variables:
            toolchain = self._variable('CMAKE_TOOLCHAIN_FILE')
        elif toolchain is not None:
            toolchain = self._expand(toolchain)
        if toolchain is not None:
            self._absolute(toolchain, 'TOOLCHAIN')
        return toolchain

    def validate_existing(self, *, allow_missing=False):
        cache = CMakeCacheDocument.load(self.binary_dir / 'CMakeCache.txt', allow_missing=allow_missing)
        if cache is not None:
            cache.require_locations(self.source_root, self.binary_dir)
            if cache.value('CMAKE_GENERATOR') != self.preset['generator']:
                raise _error('GENERATOR', 'cached generator differs from selected preset')
        variables = self.preset.get('cacheVariables', {})
        # Missing cache permits new configure, never unresolved declarations.
        for key in ('CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER'):
            if key in variables:
                self._variable(key)
        toolchain = self._declared_toolchain()
        if cache is None:
            if self.preset['generator'] in self.SINGLE_CONFIG_GENERATORS:
                if 'CMAKE_BUILD_TYPE' not in variables or self._variable('CMAKE_BUILD_TYPE') != self.configuration:
                    raise _error('CONFIG', 'new configure must declare the requested build type')
            return None
        configurations = cache.value('CMAKE_CONFIGURATION_TYPES', required=False)
        if configurations:
            if '\\' in configurations or self.configuration not in configurations.split(';'):
                raise _error('CONFIG', 'requested configuration is absent from the multi-config cache')
        else:
            if cache.value('CMAKE_BUILD_TYPE') != self.configuration:
                raise _error('CONFIG', 'cached build type differs from requested configuration')
            if 'CMAKE_BUILD_TYPE' in self.preset.get('cacheVariables', {}):
                if self._variable('CMAKE_BUILD_TYPE') != self.configuration:
                    raise _error('INTENT', 'preset build type contradicts requested configuration')
        requested = 'webgpu' if self.backend == 'webgpu' else 'none'
        selected = cache.value('ARP_WEB_RHI')
        if selected not in ('', requested):
            raise _error('BACKEND', 'cached Web product differs from the declared product')
        cached_toolchain = cache.value('CMAKE_TOOLCHAIN_FILE', required=False)
        if bool(cached_toolchain) != bool(toolchain) or (toolchain and
                self._absolute(cached_toolchain, 'TOOLCHAIN') != self._absolute(toolchain, 'TOOLCHAIN')):
            raise _error('TOOLCHAIN', 'cached toolchain differs from selected preset')
        for key in ('CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER'):
            if key in variables:
                cached = self._absolute(cache.value(key), 'COMPILER')
                if cached != self._compiler(key):
                    raise _error('COMPILER', f'{key} differs from selected preset')
        return cache


class CMakePresetBuildAdmission:
    """CMakePresetBuildAdmission/1; no name-based custom product inference."""

    VERSION = 1
    SINGLE_CONFIG_GENERATORS = CMakePresetIdentity.SINGLE_CONFIG_GENERATORS

    def __init__(self, source_root, preset_name, category, configuration=None,
                 *, registered_backend_for=None):
        document = CMakePresetDocument.load(Path(source_root) / 'CMakePresets.json')
        operation = document.preset(category, preset_name)
        if not operation or 'inherits' in operation:
            raise _error('INTENT', 'selected operation requires a root flat preset')
        configure_name = preset_name if category == 'configurePresets' else operation.get('configurePreset')
        if not isinstance(configure_name, str) or not configure_name:
            raise _error('INTENT', 'operation requires an explicit configure preset')
        if (registered_backend_for and registered_backend_for(preset_name) is not None
                and configure_name != preset_name):
            raise _error('INTENT', 'registered operation cannot redirect its configure identity')
        if category == 'testPresets':
            build = document.preset('buildPresets', preset_name)
            if not build or 'inherits' in build or build.get('configurePreset') != configure_name:
                raise _error('INTENT', 'test and build must reference the same configure preset')
        preset = document.preset('configurePresets', configure_name)
        identity = CMakePresetIdentity(source_root, preset)
        variables = identity.preset.get('cacheVariables', {})
        declared = _typed_value(variables.get('ARP_WEB_RHI'))
        if declared == 'webgl1':
            raise CMakeBuildCacheError('RHI-WEBGL1-BUILD-UNAVAILABLE: production WebGL1 target is not implemented')
        if 'ARP_WEB_RHI' in variables and declared not in ('none', 'webgpu'):
            raise _error('BACKEND', 'product identity requires a literal none or webgpu')
        # The one versioned registry resolves the referenced configure identity.
        registered_backend = registered_backend_for(configure_name) if registered_backend_for else None
        expected = ('webgpu' if registered_backend == 'webgpu' else 'none') if registered_backend else None
        if expected and declared is not None and declared != expected:
            raise _error('BACKEND', 'preset product contradicts its registered identity')
        product = declared if declared is not None else expected
        if product is None:
            raise _error('BACKEND', 'custom preset requires an explicit ARP_WEB_RHI product')
        if configuration is None:
            if category != 'configurePresets' and 'configuration' in operation:
                configuration = operation['configuration']
            else:
                cache = CMakeCacheDocument.load(identity.binary_dir / 'CMakeCache.txt', allow_missing=True)
                if (cache and cache.value('CMAKE_CONFIGURATION_TYPES', required=False)) or (
                        cache is None and identity.preset['generator'] not in self.SINGLE_CONFIG_GENERATORS):
                    raise _error('CONFIG', 'multi-config selection requires an explicit configuration')
                configuration = _typed_value(variables.get('CMAKE_BUILD_TYPE'))
        if not isinstance(configuration, str) or configuration not in ('Debug', 'Release'):
            raise _error('CONFIG', 'configuration must explicitly resolve to Debug or Release')
        self.intent = CMakeBuildIntent(source_root, preset, product, configuration)
        self.configuration = configuration
        self.explicit_product = declared

    def validate_existing(self, *, allow_missing=False):
        cache = self.intent.validate_existing(allow_missing=allow_missing)
        if cache is not None and self.explicit_product is not None:
            if cache.value('ARP_WEB_RHI') != self.explicit_product:
                raise _error('BACKEND', 'explicit product requires the same explicit cached product')
        return cache
