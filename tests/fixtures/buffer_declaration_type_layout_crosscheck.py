"""Cross-check native declaration DAGs against the existing binary fact reader."""
import subprocess
from enum import IntEnum

import yaml


class TypeKind(IntEnum):
    SCALAR = 1
    VECTOR = 2
    MATRIX = 3
    FIXED_ARRAY = 4
    RUNTIME_ARRAY = 5
    STRUCT = 6
    ATOMIC = 7
    DESCRIPTOR_ARRAY = 8


class BufferTypeLayoutCrossCheck:
    def __init__(self, executable):
        self.executable = executable

    def invoke(self, path, mode, expect_failure=False, limits=(4096, 4096, 64)):
        result = subprocess.run([self.executable, mode, str(path), str(path.stat().st_size),
                                 *map(str, limits)], capture_output=True, text=True)
        if expect_failure:
            assert result.returncode == 1 and result.stdout == '' and 'SHADER-' in result.stderr, result
            return None
        assert result.returncode == 0 and result.stderr == '', result
        graph = yaml.safe_load(result.stdout)
        assert graph['type'] == 'BufferDeclarationTypeLayouts' and graph['contractVersion'] == 1 and graph['status'] == 'DeclarationOnly'
        return graph

    def binary(self, path, binary, expected):
        graph = self.invoke(path, '--spirv')
        nodes, members, roots = graph['nodes'], graph['members'], graph['roots']
        assert len(roots) == len(expected)
        actual = {(r['set'], r['binding']): r for r in roots}
        assert len(actual) == len(roots) and set(actual) == set(expected)
        for index, node in enumerate(nodes):
            assert node['targetSizeKnown'] is False and node['targetSizeBytes'] == 0
            assert node['alignmentKnown'] is False and node['alignmentBytes'] == 0
            assert node['child'] is None or 0 <= node['child'] < index
            begin, count = node['memberBegin'], node['memberCount']
            assert 0 <= begin <= len(members) and 0 <= count <= len(members) - begin
            for member in members[begin:begin + count]:
                assert 0 <= member['type'] < index
                assert member['alignmentKnown'] is False and member['alignmentBytes'] == 0
                assert member['reservedSizeKnown'] is False and member['reservedSizeBytes'] == 0
        for key, facts in expected.items():
            root = actual[key]
            assert root['descriptorCount'] == facts['descriptorCount']
            assert nodes[root['type']]['occupiedExtentBytes'] == facts['staticOccupiedExtentBytes']
            assert root['storage'] == (1 if facts['storageClass'] == 2 else 2)
            variable = binary.definition(facts['spirvId'], 59)[1]
            pointer = binary.definition(variable[0], 32, 3)[1]
            type_id = pointer[2]
            outer = root['descriptorType']
            descriptor_count = 1
            while binary.definition(type_id)[0] == 28:
                _, array = binary.definition(type_id)
                length = binary.array_length(array[2]); descriptor_count *= length
                assert outer is not None
                descriptor = nodes[outer]
                assert descriptor['kind'] == TypeKind.DESCRIPTOR_ARRAY and descriptor['count'] == length
                assert descriptor['strideBytes'] == 0 and descriptor['occupiedExtentBytes'] == 0
                outer = descriptor['child']; type_id = array[1]
            assert descriptor_count == root['descriptorCount']
            assert outer is None if root['descriptorType'] is None else outer == root['type']
            self.walk(binary, nodes, members, type_id, root['type'])
        return graph

    @staticmethod
    def walk(binary, nodes, members, root_id, root_index):
        work = [(root_id, None, root_index)]
        visited = set()
        while work:
            identifier, matrix, index = work.pop()
            key = (identifier, matrix, index)
            if key in visited:
                continue
            visited.add(key)
            opcode, values = binary.definition(identifier)
            n = nodes[index]
            if opcode in (21, 22):
                assert n['kind'] == TypeKind.SCALAR and n['scalarBits'] == values[1]
                assert n['scalar'] == (3 if opcode == 22 else 2 if values[2] else 1)
                assert n['child'] is None and n['occupiedExtentBytes'] == values[1] // 8
            elif opcode == 23:
                assert n['kind'] == TypeKind.VECTOR and n['count'] == values[2]
                assert n['occupiedExtentBytes'] == nodes[n['child']]['occupiedExtentBytes'] * values[2]
                work.append((values[1], None, n['child']))
            elif opcode == 24:
                vector = binary.definition(values[1], 23, 3)[1]
                assert n['kind'] == TypeKind.MATRIX and n['rows'] == vector[2] and n['columns'] == values[2]
                assert n['matrixEncoding'] == 1 and n['strideBytes'] == matrix
                assert n['occupiedExtentBytes'] == matrix * (values[2] - 1) + nodes[n['child']]['occupiedExtentBytes']
                work.append((values[1], None, n['child']))
            elif opcode in (28, 29):
                assert n['kind'] == (TypeKind.FIXED_ARRAY if opcode == 28 else TypeKind.RUNTIME_ARRAY)
                assert n['count'] == (binary.array_length(values[2]) if opcode == 28 else 0)
                assert n['strideBytes'] == binary.decoration(identifier, 6)
                assert n['occupiedExtentBytes'] == (n['strideBytes'] * (n['count'] - 1) + nodes[n['child']]['occupiedExtentBytes'] if opcode == 28 else 0)
                work.append((values[1], matrix, n['child']))
            elif opcode == 30:
                assert n['kind'] == TypeKind.STRUCT and n['memberCount'] == len(values) - 1
                extent = 0
                for ordinal, child_id in enumerate(values[1:]):
                    member = members[n['memberBegin'] + ordinal]
                    assert member['offsetBytes'] == binary.member_decoration(identifier, ordinal, 35)
                    if binary.definition(child_id)[0] != 29:
                        extent = member['offsetBytes'] + nodes[member['type']]['occupiedExtentBytes']
                    stride = binary.matrix_stride(identifier, ordinal) if (identifier, ordinal, 7) in binary.members else None
                    work.append((child_id, stride, member['type']))
                assert n['occupiedExtentBytes'] == extent
            else:
                raise AssertionError(f'unsupported binary test type opcode {opcode}')
