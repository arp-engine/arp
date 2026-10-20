"""Compiler-authored Make depfile parsing and shader input identity.

This tooling records source inputs only; it grants no runtime capability.
"""
from __future__ import annotations

import hashlib
from pathlib import Path


def parse_depfile(text: str) -> tuple[list[str], list[str]]:
    """One compiler output with escaped spaces/colons/backslashes and continuations."""
    targets, dependencies, token = [], [], []
    destination = targets
    separator = False
    index = 0
    while index < len(text):
        character = text[index]
        index += 1
        if character == "\\":
            if index == len(text):
                raise ValueError("SHADER-DEPENDENCY-ESCAPE")
            character = text[index]
            index += 1
            if character == "\r" and index < len(text) and text[index] == "\n":
                index += 1
                character = "\n"
            if character == "\n":
                continue
            token.append(character)
        elif character == ":":
            if separator:
                raise ValueError("SHADER-DEPENDENCY-RULE")
            if token:
                destination.append("".join(token))
                token.clear()
            separator = True
            destination = dependencies
        elif character.isspace():
            if token:
                destination.append("".join(token))
                token.clear()
        elif character == "#":
            raise ValueError("SHADER-DEPENDENCY-COMMENT")
        else:
            token.append(character)
    if token:
        destination.append("".join(token))
    if not separator or len(targets) != 1 or not dependencies:
        raise ValueError("SHADER-DEPENDENCY-RULE")
    return targets, dependencies


def freeze_dependencies(depfile: Path, artifact: Path, source: Path,
                        owners: dict[str, Path]) -> list[dict]:
    """Resolve exact compiler paths, reject foreign inputs and hash original bytes."""
    targets, dependencies = parse_depfile(depfile.read_text(encoding="utf-8"))
    if Path(targets[0]).resolve() != artifact.resolve():
        raise ValueError("SHADER-DEPENDENCY-TARGET")
    source = source.resolve()
    roots = {name: path.resolve() for name, path in owners.items()}
    inputs, seen = [], set()
    for dependency in dependencies:
        path = Path(dependency).resolve()
        if path in seen:
            continue
        seen.add(path)
        matching = [(name, root) for name, root in roots.items() if path.is_relative_to(root)]
        if len(matching) != 1:
            raise ValueError("SHADER-DEPENDENCY-OWNER")
        name, root = matching[0]
        inputs.append({"owner": name, "path": path.relative_to(root).as_posix(),
                       "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    if source not in seen:
        raise ValueError("SHADER-DEPENDENCY-SOURCE-MISSING")
    return sorted(inputs, key=lambda item: (item["owner"], item["path"]))
