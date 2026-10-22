#!/usr/bin/env python3
"""
Reflection code generator — parses C++ headers for ACLASS/ASTRUCT/AENUM/APROP/AFUNC
markers and generates reflection metadata (JSON) + registration code (C++).

Usage:
  python reflect_codegen.py --module <name> --out-json <path> --out-cpp <path> --header <path> [--header <path> ...]
"""

import argparse
import json
import os
import re
import sys


# ── model ──────────────────────────────────────────────────────────────────


class PropertyModel:
    __slots__ = ('name', 'cpp_type', 'access', 'flags', 'doc', 'reference_target')

    def __init__(self):
        self.name = ''
        self.cpp_type = ''
        self.access = 'public'
        self.flags = []
        self.doc = ''
        self.reference_target = ''


class MethodModel:
    __slots__ = ('name', 'reflect_name', 'return_type', 'args', 'access', 'flags')

    def __init__(self):
        self.name = ''
        self.reflect_name = ''
        self.return_type = ''
        self.args = ''
        self.access = 'public'
        self.flags = []


class ClassModel:
    __slots__ = ('kind', 'namespace', 'name', 'cpp_name', 'reflect_name',
                 'default_construct', 'properties', 'methods')

    def __init__(self):
        self.kind = 'class'
        self.namespace = ''
        self.name = ''
        self.cpp_name = ''
        self.reflect_name = ''
        self.default_construct = False
        self.properties = []
        self.methods = []


class EnumModel:
    __slots__ = ('namespace', 'name', 'cpp_name', 'reflect_name', 'values')

    def __init__(self):
        self.namespace = ''
        self.name = ''
        self.cpp_name = ''
        self.reflect_name = ''
        self.values = []


class HeaderModel:
    __slots__ = ('path', 'classes', 'enums')

    def __init__(self):
        self.path = ''
        self.classes = []
        self.enums = []


# ── parser ─────────────────────────────────────────────────────────────────


def parse_marker_body(marker):
    m = re.search(r'\((.*)\)', marker)
    return m.group(1) if m else ''


def parse_name_override(marker, fallback):
    body = parse_marker_body(marker)
    m = re.search(r'Name\s*=\s*"([^"]*)"', body)
    return m.group(1) if m else fallback


def contains_default_construct(marker):
    body = parse_marker_body(marker)
    return 'Construct' in body and 'Default' in body


def parse_marker_flags(marker):
    body = parse_marker_body(marker)
    flags = []
    for token in body.split(','):
        token = token.strip()
        if token and '=' not in token:
            flags.append(token)
    return flags


def parse_marker_string(marker, name):
    match = re.search(rf'\b{name}\s*=\s*"((?:\\.|[^"\\])*)"', parse_marker_body(marker))
    if not match:
        return ''
    return match.group(1).replace('\\"', '"').replace('\\\\', '\\')


def parse_field_decl(line):
    """Parse `APROP` followed by a field declaration. Returns (ok, prop, diag)."""
    stripped = _strip_comment(line).strip().rstrip(';')
    # Strip initializer
    stripped = re.sub(r'\s*[={].*$', '', stripped)
    if not stripped:
        return False, None, 'APROP field declaration is empty'

    # Handle trailing array `name[4]`
    stripped = re.sub(r'\[\d+\]$', '', stripped)

    # Split into type + name
    m = re.search(r'^(.+?)\s+(\w+)$', stripped)
    if not m:
        return False, None, f'failed to parse APROP field: {stripped[:60]}'

    prop = PropertyModel()
    prop.cpp_type = m.group(1).strip()
    prop.name = m.group(2)
    if not prop.cpp_type:
        return False, None, 'APROP field type is empty'
    return True, prop, ''


def parse_method_decl(line):
    """Parse `AFUNC` followed by a method declaration. Returns (ok, method, diag)."""
    stripped = _strip_comment(line).strip()
    # Strip body and semicolon
    stripped = re.sub(r'\s*\{.*', '', stripped)
    stripped = stripped.rstrip(';').strip()

    # Methods may be const-qualified after the argument list. Keep the
    # qualifier out of the reflected signature while accepting the same
    # declaration shape as the v2 C++ generator.
    m = re.match(r'^(.+?)\s+(\w+)\s*\((.*)\)\s*(?:const)?$', stripped)
    if not m:
        return False, None, f'failed to parse AFUNC method: {stripped[:60]}'

    method = MethodModel()
    method.return_type = m.group(1).strip()
    method.name = m.group(2)
    method.args = m.group(3).strip()
    return True, method, ''


def _strip_comment(line):
    idx = line.find('//')
    return line[:idx] if idx >= 0 else line


def _marker_match(line, marker):
    """Check if line starts with marker (word boundary)."""
    stripped = _strip_comment(line).strip()
    if not stripped.startswith(marker):
        return False, ''
    rest = stripped[len(marker):]
    if rest and not rest[0] in ('(', ' ', '\t'):
        return False, ''
    return True, stripped


def parse_headers(header_paths):
    """Parse all headers and return list of HeaderModel."""
    headers = []
    for path in header_paths:
        try:
            with open(path, encoding='utf-8') as f:
                lines = f.readlines()
        except IOError as e:
            print(f'ERROR: failed to open {path}: {e}', file=sys.stderr)
            sys.exit(1)

        header = HeaderModel()
        header.path = path
        namespace_stack = []

        i = 0
        while i < len(lines):
            # Check the raw line before stripping comments. Namespace closes
            # are conventionally written as `} // namespace foo`; losing the
            # comment first makes them indistinguishable from other braces.
            raw_stripped = lines[i].strip()
            if re.match(r'\}\s*//\s*namespace', raw_stripped) and namespace_stack:
                namespace_stack.pop()
                i += 1
                continue
            stripped = _strip_comment(lines[i]).strip()
            if not stripped:
                i += 1
                continue

            # Namespace tracking
            ns_m = re.match(
                r'namespace\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\{',
                stripped)
            if ns_m:
                namespace_stack.append(ns_m.group(1))
                i += 1
                continue
            ns_str = '::'.join(namespace_stack)

            # ACLASS / ASTRUCT
            for marker, kind in [('ACLASS', 'class'), ('ASTRUCT', 'struct')]:
                ok, marker_line = _marker_match(stripped, marker)
                if ok:
                    cls = ClassModel()
                    cls.kind = kind

                    # Next meaningful line should be the class/struct declaration
                    i += 1
                    while i < len(lines) and not _strip_comment(lines[i]).strip():
                        i += 1
                    if i >= len(lines):
                        print(f'ERROR: {marker} must be followed by type declaration in {path}', file=sys.stderr)
                        sys.exit(1)

                    decl = _strip_comment(lines[i]).strip()
                    keyword = 'class' if kind == 'class' else 'struct'
                    type_m = re.search(rf'{keyword}\s+(\w+)', decl)
                    if not type_m:
                        print(f'ERROR: {marker} must be followed by "{keyword} <Name>" in {path}', file=sys.stderr)
                        sys.exit(1)

                    cls.name = type_m.group(1)
                    cls.namespace = ns_str
                    cls.cpp_name = f'{ns_str}::{cls.name}' if ns_str else cls.name
                    cls.reflect_name = parse_name_override(marker_line, cls.name)
                    cls.default_construct = contains_default_construct(marker_line)

                    current_access = 'public' if kind == 'struct' else 'private'
                    saw_brace = '{' in decl

                    i += 1
                    while i < len(lines):
                        s = _strip_comment(lines[i]).strip()
                        if not s:
                            i += 1
                            continue
                        if not saw_brace:
                            saw_brace = '{' in s
                            i += 1
                            continue
                        if s == '};':
                            break
                        if s in ('public:', 'protected:', 'private:'):
                            current_access = s[:-1]
                            i += 1
                            continue

                        # APROP
                        pok, _ = _marker_match(s, 'APROP')
                        if pok:
                            props_flags = parse_marker_flags(s)
                            i += 1
                            while i < len(lines) and not _strip_comment(lines[i]).strip():
                                i += 1
                            if i >= len(lines):
                                print(f'ERROR: APROP must be followed by field declaration in {path}', file=sys.stderr)
                                sys.exit(1)
                            ok, prop, diag = parse_field_decl(lines[i])
                            if not ok:
                                print(f'ERROR: {diag} in {path}:{i+1}', file=sys.stderr)
                                sys.exit(1)
                            prop.access = current_access
                            prop.flags = props_flags
                            prop.doc = parse_marker_string(s, 'Doc')
                            prop.reference_target = parse_marker_string(s, 'ReferenceTarget')
                            cls.properties.append(prop)
                            i += 1
                            continue

                        # AFUNC
                        fok, _ = _marker_match(s, 'AFUNC')
                        if fok:
                            func_flags = parse_marker_flags(s)
                            i += 1
                            while i < len(lines) and not _strip_comment(lines[i]).strip():
                                i += 1
                            if i >= len(lines):
                                print(f'ERROR: AFUNC must be followed by method declaration in {path}', file=sys.stderr)
                                sys.exit(1)
                            # Accept declarations split across lines (for
                            # example a pointer return type followed by a
                            # multi-line parameter list), matching the v2
                            # C++ generator's declaration grammar.
                            method_decl = _strip_comment(lines[i]).strip()
                            while ')' not in method_decl and i + 1 < len(lines):
                                i += 1
                                method_decl += ' ' + _strip_comment(lines[i]).strip()
                            ok, method, diag = parse_method_decl(method_decl)
                            if not ok:
                                print(f'ERROR: {diag} in {path}:{i+1}', file=sys.stderr)
                                sys.exit(1)
                            method.access = current_access
                            method.flags = func_flags
                            method.reflect_name = parse_name_override(s, method.name)
                            cls.methods.append(method)
                            i += 1
                            continue

                        i += 1

                    header.classes.append(cls)
                    break  # out of marker loop
            else:
                # AENUM
                eok, marker_line = _marker_match(stripped, 'AENUM')
                if eok:
                    enum_model = EnumModel()
                    i += 1
                    while i < len(lines) and not _strip_comment(lines[i]).strip():
                        i += 1
                    if i >= len(lines):
                        print(f'ERROR: AENUM must be followed by enum declaration in {path}', file=sys.stderr)
                        sys.exit(1)

                    decl = _strip_comment(lines[i]).strip()
                    type_m = re.search(r'enum\s+class\s+(\w+)', decl)
                    if not type_m:
                        print(f'ERROR: AENUM must be followed by "enum class <Name>" in {path}', file=sys.stderr)
                        sys.exit(1)

                    enum_model.name = type_m.group(1)
                    enum_model.namespace = ns_str
                    enum_model.cpp_name = f'{ns_str}::{enum_model.name}' if ns_str else enum_model.name
                    enum_model.reflect_name = parse_name_override(marker_line, enum_model.name)

                    saw_brace = '{' in decl
                    i += 1
                    while i < len(lines):
                        s = _strip_comment(lines[i]).strip()
                        if not s:
                            i += 1
                            continue
                        if not saw_brace:
                            saw_brace = '{' in s
                            i += 1
                            continue
                        if s == '};':
                            break
                        # Parse enum values
                        for item in s.split(','):
                            item = item.strip()
                            if item:
                                item = re.sub(r'\s*=.*$', '', item).strip()
                                if item:
                                    enum_model.values.append(item)
                        i += 1

                    header.enums.append(enum_model)
                else:
                    i += 1

        headers.append(header)
    return headers


# ── codegen ────────────────────────────────────────────────────────────────


def write_json(out_path, module, headers):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    data = {
        'module': module,
        'headers': [h.path for h in headers],
        'classes': [],
        'enums': [],
    }
    for h in headers:
        for c in h.classes:
            data['classes'].append({
                'kind': c.kind,
                'cppName': c.cpp_name,
                'reflectName': c.reflect_name,
                'defaultConstruct': c.default_construct,
                'properties': [
                    {'name': p.name, 'cppType': p.cpp_type, 'access': p.access,
                     **({'doc': p.doc} if p.doc else {}),
                     **({'referenceTarget': p.reference_target} if p.reference_target else {}), 'flags': p.flags}
                    for p in c.properties
                ],
                'methods': [
                    {'name': m.name, 'reflectName': m.reflect_name,
                     'returnType': m.return_type, 'args': m.args}
                    for m in c.methods
                ],
            })
        for e in h.enums:
            data['enums'].append({
                'cppName': e.cpp_name,
                'reflectName': e.reflect_name,
                'values': e.values,
            })

    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


def _register_function_name(module):
    result = 'register_'
    for ch in module:
        result += ch if ch.isalnum() or ch == '_' else '_'
    return result + '_reflection'


def _json_escape(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')


def _property_hint_expression(flags):
    hints = []
    if 'Editor' in flags:
        hints.append('::arp::reflection::PropertyHint::Editor')
    if 'Serialize' in flags:
        hints.append('::arp::reflection::PropertyHint::Serialize')
    if 'Runtime' in flags:
        hints.append('::arp::reflection::PropertyHint::Runtime')
    if not hints:
        return '0'
    return 'static_cast<std::uint8_t>(' + ' | '.join(hints) + ')'


def write_cpp(out_path, module, headers):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    lines = []
    lines.append('// Generated by reflect_codegen.py. Do not edit.')
    for h in headers:
        lines.append(f'#include "{h.path}"')
    lines.append('#include "reflection/reflection.h"')
    lines.append('')
    lines.append('namespace arp::reflection::generated {')
    lines.append('')
    fn_name = _register_function_name(module)
    lines.append(f'void {fn_name}()')
    lines.append('{')
    lines.append('    using namespace ::arp::reflection;')
    lines.append('')

    for h in headers:
        for c in h.classes:
            lines.append('    {')
            lines.append(f'        auto cls = registration::class_<{c.cpp_name}>("{_json_escape(c.reflect_name)}");')
            if c.default_construct:
                lines.append('        cls.construct<>();')
                lines.append('        cls.destruct();')
            for p in c.properties:
                if p.access != 'public':
                    print(f'ERROR: non-public APROP not supported: {c.cpp_name}::{p.name}', file=sys.stderr)
                    sys.exit(1)
                lines.append(
                    f'        cls.property("{_json_escape(p.name)}", &{c.cpp_name}::{p.name}, "{_json_escape(p.doc)}", '
                    f'{_property_hint_expression(p.flags)});')
                reference_kind = 'SceneObject' if 'SceneObjectReference' in p.flags else 'Resource' if 'ResourceReference' in p.flags else None
                if reference_kind:
                    if not p.reference_target:
                        raise ValueError(f'reference requires ReferenceTarget: {c.cpp_name}::{p.name}')
                    required = 'false' if 'OptionalReference' in p.flags else 'true'
                    lines.append(f'        cls.reference("{_json_escape(p.name)}", ReferenceKind::{reference_kind}, "{_json_escape(p.reference_target)}", {required});')
            for m in c.methods:
                if m.access != 'public':
                    print(f'ERROR: non-public AFUNC not supported: {c.cpp_name}::{m.name}', file=sys.stderr)
                    sys.exit(1)
                lines.append(f'        cls.method("{_json_escape(m.reflect_name)}", &{c.cpp_name}::{m.name});')
            lines.append('    }')
            lines.append('')

    for h in headers:
        for e in h.enums:
            lines.append(f'    registration::class_<{e.cpp_name}>("{_json_escape(e.reflect_name)}")')
            lines.append(f'        .constant<{e.cpp_name}>(')
            lines.append(f'            "{_json_escape(e.reflect_name)}",')
            lines.append('            {')
            for vi, v in enumerate(e.values):
                comma = ',' if vi < len(e.values) - 1 else ''
                lines.append(f'                enum_value("{_json_escape(v)}", {e.cpp_name}::{v}){comma}')
            lines.append('            });')
            lines.append('')

    lines.append('}')
    lines.append('')
    lines.append('} // namespace arp::reflection::generated')
    lines.append('')

    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))


# ── main ───────────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(description='arp-engine reflection code generator')
    parser.add_argument('--module', required=True, help='Module name for registration function')
    parser.add_argument('--out-json', required=True, help='JSON metadata output path')
    parser.add_argument('--out-cpp', required=True, help='C++ registration output path')
    parser.add_argument('--header', action='append', required=True,
                        help='Input header path (repeatable)')
    args = parser.parse_args()

    headers = parse_headers(args.header)
    write_json(args.out_json, args.module, headers)
    write_cpp(args.out_cpp, args.module, headers)

    print(f'[reflect_codegen] module={args.module}')
    print(f'  headers: {len(args.header)}')
    classes = sum(len(h.classes) for h in headers)
    enums = sum(len(h.enums) for h in headers)
    print(f'  classes: {classes}, enums: {enums}')
    print(f'  JSON → {args.out_json}')
    print(f'  C++  → {args.out_cpp}')


if __name__ == '__main__':
    main()
