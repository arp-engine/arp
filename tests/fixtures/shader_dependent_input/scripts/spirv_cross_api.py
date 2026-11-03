"""Cold tooling adapter for the versioned SPIRV-Cross C API, never a runtime compiler.

Numeric constants are the public spirv_cross_c.h / spirv.h enum values. The exact
shared-library and API version are included in the target artifact identity.
"""
from __future__ import annotations

import ctypes as C
from pathlib import Path
import struct


class Resource(C.Structure):
    _fields_ = [("id", C.c_uint), ("base_type_id", C.c_uint),
                ("type_id", C.c_uint), ("name", C.c_char_p)]


class CombinedSampler(C.Structure):
    _fields_ = [("combined_id", C.c_uint), ("image_id", C.c_uint),
                ("sampler_id", C.c_uint)]


class SpirvCrossCompiler:
    GLSL_BIT = 0x2000000
    COMMON_BIT = 0x1000000
    OPTIONS = {"version": (GLSL_BIT | 8, 100), "es": (GLSL_BIT | 9, True),
        "floatHighp": (GLSL_BIT | 11, True), "intHighp": (GLSL_BIT | 12, True),
        "plainUniforms": (GLSL_BIT | 35, True),
        "fixupDepth": (COMMON_BIT | 3, True), "flipVertexY": (COMMON_BIT | 4, True)}
    UNIFORM_BUFFER, STORAGE_BUFFER, INPUT, OUTPUT = 1, 2, 3, 4
    SUBPASS, STORAGE_IMAGE, SAMPLED_IMAGE, ATOMIC_COUNTER = 5, 6, 7, 8
    PUSH, IMAGE, SAMPLER = 9, 10, 11
    LOCATION, BINDING, DESCRIPTOR_SET, COL_MAJOR = 30, 33, 34, 5
    INT32, FP32, STRUCT = 7, 13, 15

    def __init__(self, library: Path, spirv: Path):
        self.library = C.CDLL(str(Path(library).resolve()))
        self.context = C.c_void_p()
        self.compiler = C.c_void_p()
        self._bind()
        self.check(self.api("context_create")(C.byref(self.context)))
        try:
            data = Path(spirv).read_bytes()
            if len(data) < 20 or len(data) % 4 or struct.unpack_from('<I', data)[0] != 0x07230203:
                raise RuntimeError('SHADER-WEBGL1-SPIRV-BINARY')
            words = (C.c_uint * (len(data) // 4)).from_buffer_copy(data)
            ir = C.c_void_p()
            self.check(self.api("context_parse_spirv")(
                self.context, words, len(words), C.byref(ir)))
            # SPVC_BACKEND_GLSL=1, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP=1.
            self.check(self.api("context_create_compiler")(
                self.context, 1, ir, 1, C.byref(self.compiler)))
        except BaseException:
            self.close()
            raise

    def _bind(self):
        V, U, S, B, T = C.c_void_p, C.c_uint, C.c_size_t, C.c_ubyte, C.c_char_p
        P = C.POINTER
        signatures = {
            "get_version": (None, [P(U), P(U), P(U)]),
            "context_create": (C.c_int, [P(V)]), "context_destroy": (None, [V]),
            "context_get_last_error_string": (T, [V]),
            "context_parse_spirv": (C.c_int, [V, P(U), S, P(V)]),
            "context_create_compiler": (C.c_int, [V, C.c_int, V, C.c_int, P(V)]),
            "compiler_create_compiler_options": (C.c_int, [V, P(V)]),
            "compiler_options_set_bool": (C.c_int, [V, C.c_int, B]),
            "compiler_options_set_uint": (C.c_int, [V, C.c_int, U]),
            "compiler_install_compiler_options": (C.c_int, [V, V]),
            "compiler_compile": (C.c_int, [V, P(T)]),
            "compiler_require_extension": (C.c_int, [V, T]),
            "compiler_create_shader_resources": (C.c_int, [V, P(V)]),
            "resources_get_resource_list_for_type": (C.c_int, [V, C.c_int, P(P(Resource)), P(S)]),
            "compiler_set_name": (None, [V, U, T]),
            "compiler_set_member_name": (None, [V, U, U, T]),
            "compiler_get_name": (T, [V, U]),
            "compiler_get_decoration": (U, [V, U, C.c_int]),
            "compiler_has_member_decoration": (B, [V, U, U, C.c_int]),
            "compiler_get_type_handle": (V, [V, U]),
            "compiler_get_declared_struct_size": (C.c_int, [V, V, P(S)]),
            "compiler_type_struct_member_offset": (C.c_int, [V, V, U, P(U)]),
            "compiler_type_struct_member_array_stride": (C.c_int, [V, V, U, P(U)]),
            "compiler_type_struct_member_matrix_stride": (C.c_int, [V, V, U, P(U)]),
            "compiler_build_combined_image_samplers": (C.c_int, [V]),
            "compiler_get_combined_image_samplers": (C.c_int, [V, P(P(CombinedSampler)), P(S)]),
            "compiler_get_num_required_extensions": (S, [V]),
            "compiler_get_required_extension": (T, [V, S]),
        }
        for name in ('base_type_id', 'basetype', 'bit_width', 'vector_size', 'columns',
                     'num_array_dimensions', 'num_member_types', 'image_sampled_type',
                     'image_dimension'):
            signatures['type_get_' + name] = (U, [V])
        for name in ('image_is_depth', 'image_arrayed', 'image_multisampled', 'image_is_storage'):
            signatures['type_get_' + name] = (B, [V])
        for name in ('array_dimension', 'member_type'):
            signatures['type_get_' + name] = (U, [V, U])
        signatures['type_array_dimension_is_literal'] = (B, [V, U])
        for name, (result, args) in signatures.items():
            f = self.api(name)
            f.restype, f.argtypes = result, args

    def api(self, name):
        return getattr(self.library, 'spvc_' + name)

    def check(self, result):
        if result:
            text = self.api('context_get_last_error_string')(self.context)
            raise RuntimeError('SHADER-WEBGL1-CONVERTER: ' + (text.decode() if text else str(result)))

    def call(self, name, *arguments):
        return self.api('compiler_' + name)(self.compiler, *arguments)

    def type_value(self, name, type_handle, *arguments):
        return self.api('type_get_' + name)(type_handle, *arguments)

    def resources(self, kind):
        owner = C.c_void_p()
        self.check(self.call('create_shader_resources', C.byref(owner)))
        values, count = C.POINTER(Resource)(), C.c_size_t()
        self.check(self.api('resources_get_resource_list_for_type')(
            owner, kind, C.byref(values), C.byref(count)))
        # Values are context-owned and never escape the with block.
        return [values[i] for i in range(count.value)]

    def combined_samplers(self):
        self.check(self.call('build_combined_image_samplers'))
        values, count = C.POINTER(CombinedSampler)(), C.c_size_t()
        self.check(self.call('get_combined_image_samplers', C.byref(values), C.byref(count)))
        return [values[i] for i in range(count.value)]

    def member_value(self, kind, type_handle, index):
        result = C.c_uint()
        self.check(self.call('type_struct_member_' + kind, type_handle, index, C.byref(result)))
        return result.value

    def configure(self):
        options = C.c_void_p()
        self.check(self.call('create_compiler_options', C.byref(options)))
        for name, (option, value) in self.OPTIONS.items():
            setter = 'compiler_options_set_bool' if type(value) is bool else 'compiler_options_set_uint'
            self.check(self.api(setter)(options, option, value))
        self.check(self.call('install_compiler_options', options))

    def compile(self):
        output = C.c_char_p()
        self.check(self.call('compile', C.byref(output)))
        if not output.value:
            raise RuntimeError('SHADER-WEBGL1-CONVERTER-EMPTY')
        return output.value.decode('utf-8')

    def extensions(self):
        count = self.call('get_num_required_extensions')
        return [self.call('get_required_extension', index).decode() for index in range(count)]

    def version(self):
        major, minor, patch = C.c_uint(), C.c_uint(), C.c_uint()
        self.api('get_version')(C.byref(major), C.byref(minor), C.byref(patch))
        return [major.value, minor.value, patch.value]

    def close(self):
        if self.context:
            self.api('context_destroy')(self.context)
            self.context = C.c_void_p()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
