"""SPIR-V 1.x member layout facts from the binary instruction contract."""
import pathlib
import struct


class SpirvLayoutFacts:
    MAGIC = 0x07230203
    OP_MEMBER_DECORATE = 72
    DECORATION_COL_MAJOR = 5
    DECORATION_ROW_MAJOR = 4
    DECORATION_MATRIX_STRIDE = 7

    def __init__(self, path):
        data = pathlib.Path(path).read_bytes()
        if len(data) < 20 or len(data) % 4:
            raise RuntimeError("SPIRV-LAYOUT-INVALID-BINARY")
        words = struct.unpack(f"<{len(data) // 4}I", data)
        if words[0] != self.MAGIC:
            raise RuntimeError("SPIRV-LAYOUT-INVALID-BINARY")
        if not 0x00010000 <= words[1] <= 0x00010600 or words[1] & 0xff or not words[3] or words[4]:
            raise RuntimeError("SPIRV-LAYOUT-INVALID-HEADER")
        self.instructions = []
        self.members = {}
        self.decorations = {}
        self.definitions = {}
        self.variables = []
        self.runtime_length_operations = []
        # Core grammar result operands for the declarations consumed here.
        result_operands = {op: 0 for op in range(19, 34)}
        result_operands.update({43: 1, 50: 1, 59: 1})
        offset = 5  # SPIR-V header has exactly five words.
        while offset < len(words):
            size, opcode = words[offset] >> 16, words[offset] & 0xffff
            if not size or offset + size > len(words):
                raise RuntimeError("SPIRV-LAYOUT-INVALID-INSTRUCTION")
            if opcode == self.OP_MEMBER_DECORATE:
                operands = words[offset + 1:offset + size]
                if len(operands) < 3:
                    raise RuntimeError("SPIRV-LAYOUT-INVALID-DECORATION")
                key = operands[:3]
                if key in self.members:
                    raise RuntimeError("SPIRV-LAYOUT-CONFLICTING-DECORATION")
                self.members[key] = operands[3:]
            operands = words[offset + 1:offset + size]
            self.instructions.append((offset, opcode, operands))
            if opcode == 71:  # OpDecorate
                if len(operands) < 2:
                    raise RuntimeError("SPIRV-LAYOUT-INVALID-DECORATION")
                key = operands[:2]
                if key in self.decorations:
                    raise RuntimeError("SPIRV-LAYOUT-DUPLICATE-DECORATION")
                self.decorations[key] = operands[2:]
            if opcode in result_operands:
                index = result_operands[opcode]
                if len(operands) <= index or not 0 < operands[index] < words[3]:
                    raise RuntimeError("SPIRV-LAYOUT-INVALID-DEFINITION")
                identifier = operands[index]
                if identifier in self.definitions:
                    raise RuntimeError("SPIRV-LAYOUT-DUPLICATE-DEFINITION")
                self.definitions[identifier] = (opcode, operands)
            if opcode == 68:  # OpArrayLength
                if len(operands) != 4:
                    raise RuntimeError("SPIRV-RUNTIME-LENGTH-INVALID-INSTRUCTION")
                self.runtime_length_operations.append(operands)
            if opcode == 59:
                self.variables.append(operands)
            offset += size

    def definition(self, identifier, opcode=None, count=None):
        result = self.definitions.get(identifier)
        if result is None or (opcode is not None and result[0] != opcode) or (
                count is not None and len(result[1]) != count):
            raise RuntimeError("SPIRV-BUFFER-LAYOUT-TYPE")
        return result

    def decoration(self, identifier, kind, marker=False):
        values = self.decorations.get((identifier, kind))
        if values is None or len(values) != (0 if marker else 1):
            raise RuntimeError("SPIRV-BUFFER-LAYOUT-DECORATION")
        return 0 if marker else values[0]

    def member_decoration(self, identifier, index, kind, marker=False):
        values = self.members.get((identifier, index, kind))
        if values is None or len(values) != (0 if marker else 1):
            raise RuntimeError("SPIRV-BUFFER-LAYOUT-MEMBER-DECORATION")
        return 0 if marker else values[0]

    @staticmethod
    def _bounded(value):
        if not 0 <= value <= (1 << 64) - 1:
            raise RuntimeError("SPIRV-BUFFER-LAYOUT-OVERFLOW")
        return value

    def array_length(self, identifier):
        _, constant = self.definition(identifier, 43)  # no OpSpecConstant
        _, scalar = self.definition(constant[0], 21, 3)
        width, signed = scalar[1:]
        if width not in (32, 64) or signed not in (0, 1) or len(constant) != 2 + width // 32:
            raise RuntimeError("SPIRV-BUFFER-LAYOUT-ARRAY-LENGTH")
        value = sum(word << (32 * index) for index, word in enumerate(constant[2:]))
        if not value or (signed and value >> (width - 1)):
            raise RuntimeError("SPIRV-BUFFER-LAYOUT-ARRAY-LENGTH")
        return value

    def _type_shape(self, identifier, matrix=None):
        """Iterative layout dependency walk; no recursion or inferred alignment.

        Returns occupied extent and an optional runtime tail (offset,stride,
        element occupied extent). Matrix facts follow the containing member
        through arrays, as specified by SPIR-V member decorations.
        """
        root = (identifier, matrix)
        work, active, completed = [(root, False)], set(), {}
        while work:
            key, finish = work.pop()
            if key in completed:
                continue
            current, matrix = key
            opcode, values = self.definition(current)
            if not finish:
                if key in active:
                    raise RuntimeError("SPIRV-BUFFER-LAYOUT-CYCLIC-TYPE")
                active.add(key)
                if opcode in (21, 22):
                    if matrix is not None:
                        raise RuntimeError("SPIRV-BUFFER-LAYOUT-MATRIX-TYPE")
                    if len(values) != (3 if opcode == 21 else 2) or values[1] not in (8, 16, 32, 64) or (
                            opcode == 21 and values[2] not in (0, 1)):
                        raise RuntimeError("SPIRV-BUFFER-LAYOUT-SCALAR")
                    completed[key] = (values[1] // 8, None)
                    active.remove(key)
                    continue
                dependencies = []
                if opcode in (23, 24):
                    if len(values) != 3 or values[2] not in (2, 3, 4):
                        raise RuntimeError("SPIRV-BUFFER-LAYOUT-VECTOR-MATRIX")
                    # A matrix component is a vector, not a scalar float.
                    if opcode == 24:
                        _, vector = self.definition(values[1], 23, 3)
                        self.definition(vector[1], 22, 2)
                        if matrix is None:
                            raise RuntimeError("SPIRV-BUFFER-LAYOUT-MATRIX-MISSING")
                    else:
                        if matrix is not None:
                            raise RuntimeError("SPIRV-BUFFER-LAYOUT-MATRIX-TYPE")
                        if self.definition(values[1])[0] not in (21, 22):
                            raise RuntimeError("SPIRV-BUFFER-LAYOUT-VECTOR-MATRIX")
                    dependencies.append((values[1], None))
                elif opcode in (28, 29):
                    if len(values) != (3 if opcode == 28 else 2):
                        raise RuntimeError("SPIRV-BUFFER-LAYOUT-TYPE")
                    dependencies.append((values[1], matrix))
                elif opcode == 30 and len(values) > 1:
                    if matrix is not None:
                        raise RuntimeError("SPIRV-BUFFER-LAYOUT-MATRIX-TYPE")
                    for index, child in enumerate(values[1:]):
                        stride = self.members.get((current, index, 7))
                        layout = None
                        if stride is None and any((current, index, kind) in self.members for kind in (4, 5)):
                            raise RuntimeError("SPIRV-BUFFER-LAYOUT-MATRIX-MISSING")
                        if stride is not None:
                            self.member_decoration(current, index, 5, True)
                            if (current, index, 4) in self.members:
                                raise RuntimeError("SPIRV-BUFFER-LAYOUT-MATRIX-MAJOR")
                            layout = self.member_decoration(current, index, 7)
                            if not layout:
                                raise RuntimeError("SPIRV-BUFFER-LAYOUT-STRIDE")
                        dependencies.append((child, layout))
                else:
                    raise RuntimeError("SPIRV-BUFFER-LAYOUT-TYPE-UNSUPPORTED")
                work.append((key, True))
                # Store exact dependency keys for this node; the worklist is
                # bounded by relationships present in the input declarations.
                for dependency in reversed(dependencies):
                    work.append((dependency, False))
                continue
            if opcode == 23:
                extent, runtime = completed[(values[1], None)]
                shape = (self._bounded(extent * values[2]), runtime)
            elif opcode == 24:
                extent, runtime = completed[(values[1], None)]
                if runtime or matrix < extent:
                    raise RuntimeError("SPIRV-BUFFER-LAYOUT-STRIDE")
                shape = (self._bounded(matrix * (values[2] - 1) + extent), None)
            elif opcode in (28, 29):
                extent, runtime = completed[(values[1], matrix)]
                stride = self.decoration(current, 6)
                if runtime or not stride or stride < extent:
                    raise RuntimeError("SPIRV-BUFFER-LAYOUT-STRIDE")
                shape = ((self._bounded(stride * (self.array_length(values[2]) - 1) + extent), None)
                         if opcode == 28 else (0, (0, stride, extent)))
            else:  # OpTypeStruct
                extent, runtime = 0, None
                for index, child in enumerate(values[1:]):
                    layout = self.members.get((current, index, 7))
                    child_extent, child_runtime = completed[(child, layout[0] if layout else None)]
                    offset = self.member_decoration(current, index, 35)
                    if offset < extent:
                        raise RuntimeError("SPIRV-BUFFER-LAYOUT-OVERLAP")
                    if child_runtime:
                        if index + 2 != len(values) or self.definition(child)[0] != 29:
                            raise RuntimeError("SPIRV-BUFFER-LAYOUT-RUNTIME-PLACEMENT")
                        runtime = (offset, child_runtime[1], child_runtime[2])
                    else:
                        extent = self._bounded(offset + child_extent)
                shape = (extent, runtime)
            completed[key] = shape
            active.remove(key)
        return completed[root]

    def push_constant_layouts(self):
        """Actual PushConstant blocks, using the same bounded type-layout walk."""
        result = []
        for variable in self.variables:
            if len(variable) not in (3, 4):
                raise RuntimeError("SPIRV-BUFFER-LAYOUT-VARIABLE")
            if variable[2] != 9:  # SPIR-V PushConstant storage class.
                continue
            _, pointer = self.definition(variable[0], 32, 3)
            if pointer[1] != 9:
                raise RuntimeError("SPIRV-PUSH-LAYOUT-STORAGE-CLASS")
            self.definition(pointer[2], 30)
            self.decoration(pointer[2], 2, True)
            extent, runtime = self._type_shape(pointer[2])
            if runtime or not extent:
                raise RuntimeError("SPIRV-PUSH-LAYOUT-RUNTIME-OR-EMPTY")
            result.append(dict(spirvId=variable[1], blockTypeId=pointer[2],
                               occupiedExtentBytes=extent))
        return result

    def buffer_layouts(self):
        """SpirvBufferDeclarationLayout/1; occupied bytes are NOT target minima."""
        result = {}
        for variable in self.variables:
            if len(variable) not in (3, 4):
                raise RuntimeError("SPIRV-BUFFER-LAYOUT-VARIABLE")
            if variable[2] not in (2, 12):
                continue  # Uniform / StorageBuffer only; exclude push constants.
            _, pointer = self.definition(variable[0], 32, 3)
            if pointer[1] != variable[2]:
                raise RuntimeError("SPIRV-BUFFER-LAYOUT-STORAGE-CLASS")
            identifier, count, visited = pointer[2], 1, set()
            while self.definition(identifier)[0] == 28:
                if identifier in visited:
                    raise RuntimeError("SPIRV-BUFFER-LAYOUT-CYCLIC-TYPE")
                visited.add(identifier)
                _, array = self.definition(identifier, 28, 3)
                count = self._bounded(count * self.array_length(array[2]))
                # Descriptor arrays do not carry an in-buffer ArrayStride.
                if array[1] == identifier:
                    raise RuntimeError("SPIRV-BUFFER-LAYOUT-CYCLIC-TYPE")
                identifier = array[1]
            self.definition(identifier, 30)
            self.decoration(identifier, 2, True)  # Explicit Block; legacy BufferBlock closed.
            extent, runtime = self._type_shape(identifier)
            if runtime and variable[2] != 12:
                raise RuntimeError("SPIRV-BUFFER-LAYOUT-RUNTIME-UNIFORM")
            key = (self.decoration(variable[1], 34), self.decoration(variable[1], 33))
            if key in result:
                raise RuntimeError("SPIRV-BUFFER-LAYOUT-BINDING-DUPLICATE")
            result[key] = {
                "contractVersion": 1, "spirvId": variable[1], "blockTypeId": identifier,
                "storageClass": variable[2], "descriptorCount": count,
                "staticOccupiedExtentBytes": extent, "hasRuntimeArray": runtime is not None,
                "runtimeArrayOffsetBytes": runtime[0] if runtime else 0,
                "runtimeArrayStrideBytes": runtime[1] if runtime else 0,
                "runtimeElementOccupiedExtentBytes": runtime[2] if runtime else 0,
            }
        return result

    def descriptor_image_types(self):
        """Exact OpTypeImage/OpTypeSampler facts, without device/view authority.

        Numeric SPIR-V dimension, depth and format remain binary facts. A
        float texture declaration alone never proves a depth view or format.
        """
        result = {}
        for variable in self.variables:
            if len(variable) not in (3, 4):
                raise RuntimeError("SPIRV-IMAGE-TYPE-VARIABLE")
            if variable[2] != 0:  # UniformConstant
                continue
            _, pointer = self.definition(variable[0], 32, 3)
            if pointer[1] != 0:
                raise RuntimeError("SPIRV-IMAGE-TYPE-STORAGE-CLASS")
            identifier, count, visited = pointer[2], 1, set()
            while self.definition(identifier)[0] == 28:
                if identifier in visited:
                    raise RuntimeError("SPIRV-IMAGE-TYPE-CYCLIC")
                visited.add(identifier)
                _, array = self.definition(identifier, 28, 3)
                count = self._bounded(count * self.array_length(array[2]))
                identifier = array[1]
            opcode, values = self.definition(identifier)
            if opcode not in (25, 26):
                raise RuntimeError("SPIRV-IMAGE-TYPE-UNSUPPORTED")
            key = (self.decoration(variable[1], 34), self.decoration(variable[1], 33))
            if key in result:
                raise RuntimeError("SPIRV-IMAGE-TYPE-DUPLICATE")
            shape = {"contractVersion": 1, "spirvId": variable[1], "descriptorCount": count,
                     "kind": "Sampler" if opcode == 26 else "Image"}
            if opcode == 26:
                if len(values) != 1:
                    raise RuntimeError("SPIRV-IMAGE-TYPE-SAMPLER")
            else:
                if len(values) not in (8, 9) or values[2] > 6 or values[3] > 2 or (
                        values[4] > 1 or values[5] > 1 or values[6] not in (1, 2)):
                    raise RuntimeError("SPIRV-IMAGE-TYPE-SHAPE")
                scalar_op, scalar = self.definition(values[1])
                if scalar_op == 22 and len(scalar) == 2:
                    scalar_kind = "Float"
                elif scalar_op == 21 and len(scalar) == 3 and scalar[2] in (0, 1):
                    scalar_kind = "Int" if scalar[2] else "UInt"
                else:
                    raise RuntimeError("SPIRV-IMAGE-TYPE-SCALAR")
                if scalar[1] not in (16, 32, 64) or (len(values) == 9 and values[8] > 2):
                    raise RuntimeError("SPIRV-IMAGE-TYPE-SCALAR-OR-ACCESS")
                shape.update(dimension=values[2], depth=values[3], arrayed=bool(values[4]),
                             multisampled=bool(values[5]), sampled=values[6], imageFormat=values[7],
                             scalarKind=scalar_kind, scalarWidth=scalar[1])
                if len(values) == 9:
                    shape["accessQualifier"] = values[8]
            result[key] = shape
        return result

    def matrix_stride(self, struct_type, member_index):
        key = (struct_type, member_index)
        col = self.members.get((*key, self.DECORATION_COL_MAJOR)) == ()
        row = (*key, self.DECORATION_ROW_MAJOR) in self.members
        value = self.members.get((*key, self.DECORATION_MATRIX_STRIDE))
        if not col or row or value is None or len(value) != 1 or not value[0]:
            raise RuntimeError("SPIRV-LAYOUT-MATRIX-FACTS-MISSING-OR-INVALID")
        return value[0]
