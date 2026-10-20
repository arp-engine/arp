"""Explicit benchmark-only ArpScript-to-CPython syntax adapter.

This is not a general language translator. Workloads use common Python/ArpScript
syntax except typed local declarations and the one string exception. Those
finite transformations are checked and documented; new syntax fails at compile.
"""
from __future__ import annotations

import re
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "fixtures/aot/stress.arp"
CASE_NAMES = (
    "int_loop", "function_calls", "recursive_fib", "list_index", "list_build",
    "dict_index", "string_concat", "range_sum", "branch_mix", "power_floor",
    "list_mutate", "exception_loop", "float_math", "bitwise", "nested_loop",
    "dict_build", "dict_mutate", "dict_string", "string_compare", "string_build",
    "list_iterate", "object_method", "generator_sum", "closure_calls",
)


class PythonCorpus:
    def __init__(self, source: str | None = None):
        self.source = SOURCE.read_text(encoding="utf-8") if source is None else source
        types = r"(?:int|float|string|\[int\]|\[string\]|\{int: int\}|\{string: int\}|BenchBase)"
        self.python_source = re.sub(
            rf"(?m)^( +)var ([a-zA-Z_][a-zA-Z_0-9]*): {types} = ",
            r"\1\2 = ", self.source,
        )
        self.python_source = self.python_source.replace('raise "periodic"', 'raise RuntimeError("periodic")')
        self.python_source = self.python_source.replace("except as error:", "except RuntimeError as error:")
        if re.search(r"(?m)^\s*var\s", self.python_source):
            raise ValueError("unsupported typed local in benchmark corpus")
        self.functions: dict[str, object] = {}

    def compile(self):
        return compile(self.python_source, str(SOURCE), "exec")

    def initialize(self, code):
        namespace = {"string": str}
        exec(code, namespace)
        self.functions = {name: namespace[name] for name in CASE_NAMES}

    def invoke(self, name: str, iterations: int) -> int:
        result = self.functions[name](iterations)
        if type(result) is not int:
            raise TypeError(f"{name} did not return an int")
        return result
