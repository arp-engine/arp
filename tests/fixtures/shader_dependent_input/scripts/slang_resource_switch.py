"""Shared deterministic offline Slang scalar resource selection emitter."""
import re


class SlangResourceSwitch:
    def __init__(self, capacity, max_cases):
        if type(capacity) is not int or type(max_cases) is not int or not 1 <= capacity <= max_cases <= 0xFFFFFFFF:
            raise ValueError("SHADER-SCALAR-SWITCH-BUDGET")
        self.capacity = capacity

    @staticmethod
    def resource_name(name, element):
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name) or type(element) is not int or not 0 <= element <= 0xFFFFFFFF:
            raise ValueError("SHADER-SCALAR-RESOURCE-IDENTITY")
        return f"{name}_{element}"

    def emit(self, index, body, indent="    "):
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", index):
            raise ValueError("SHADER-SCALAR-SWITCH-INDEX")
        lines = [f"{indent}switch ({index}) {{"]
        for element in range(self.capacity):
            lines.append(f"{indent}case {element}u: {{")
            lines.extend(indent + "    " + line for line in body(element))
            lines.append(indent + "}")
        lines.extend([indent + "default: break;", indent + "}"])
        return lines
