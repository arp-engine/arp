"""VectorRecord/1, authored 32-bit four-vector records with target proof.

Every member is aligned to and occupies 16 bytes in this portable subset.
Other shapes require their own proof; no guessed padding or data rewrite.
"""
import hashlib
import re


class BufferVectorRecord:
    ABI = "Slang.vector-record/1"
    TYPES = ("float4", "uint4", "int4")

    def __init__(self, record):
        if not isinstance(record, dict) or set(record) != {"fields"}:
            raise ValueError("RHI-BINDLESS-RECORD-SCHEMA")
        fields = record["fields"]
        if not isinstance(fields, list) or not fields or len(fields) > 0xffffffff // 16:
            raise ValueError("RHI-BINDLESS-RECORD-FIELDS")
        seen = set()
        for field in fields:
            if (not isinstance(field, dict) or set(field) != {"name", "type"}
                    or not isinstance(field["name"], str)
                    or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", field["name"])
                    or field["name"] in seen or field["type"] not in self.TYPES):
                raise ValueError("RHI-BINDLESS-RECORD-FIELD")
            seen.add(field["name"])
        self.fields = tuple((field["name"], field["type"]) for field in fields)
        self.stride = 16 * len(fields)

    def layout(self):
        canonical = self.ABI + "|" + "".join(name + ":" + kind + ";" for name, kind in self.fields)
        return dict(abi=self.ABI, identity=hashlib.sha256(canonical.encode()).hexdigest()[:16],
                    elementStrideBytes=self.stride, elementAlignmentBytes=16,
                    minBindingSizeBytes=self.stride,
                    record=dict(fields=[dict(name=name, type=kind) for name, kind in self.fields]))

    def source(self, name):
        return "struct " + name + " {\n" + "\n".join(
            "    " + kind + " " + field + ";" for field, kind in self.fields) + "\n};"

    def verify_spirv(self, facts, layout, uniform):
        record = layout["blockTypeId"]
        if (layout["storageClass"] != (2 if uniform else 12)
                or layout["hasRuntimeArray"] == uniform):
            raise ValueError("RHI-BINDLESS-RECORD-BINARY-SHAPE")
        if uniform:
            if layout["staticOccupiedExtentBytes"] != self.stride:
                raise ValueError("RHI-BINDLESS-RECORD-BINARY-SIZE")
        else:
            _, block = facts.definition(record, 30, 2)
            if facts.member_decoration(record, 0, 35) != 0:
                raise ValueError("RHI-BINDLESS-RECORD-BINARY-OFFSET")
            _, array = facts.definition(block[1], 29, 2)
            record = array[1]
            if (layout["runtimeArrayOffsetBytes"] or layout["runtimeArrayStrideBytes"] != self.stride
                    or layout["runtimeElementOccupiedExtentBytes"] != self.stride):
                raise ValueError("RHI-BINDLESS-RECORD-BINARY-STRIDE")
        _, structure = facts.definition(record, 30, 1 + len(self.fields))
        for index, (_, kind) in enumerate(self.fields):
            if facts.member_decoration(record, index, 35) != index * 16:
                raise ValueError("RHI-BINDLESS-RECORD-BINARY-OFFSET")
            _, vector = facts.definition(structure[1 + index], 23, 3)
            scalar_opcode = 22 if kind == "float4" else 21
            _, scalar = facts.definition(vector[1], scalar_opcode, 2 if scalar_opcode == 22 else 3)
            if vector[2] != 4 or scalar[1] != 32 or (scalar_opcode == 21 and scalar[2] != (kind == "int4")):
                raise ValueError("RHI-BINDLESS-RECORD-BINARY-TYPE")
