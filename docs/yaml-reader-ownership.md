# YAML Reader Ownership

Status: Prototype Complete

Do not move YAML helpers into `src/core`.

## Inventory

YAML parsing currently appears in runtime/resource-facing modules, not in generic core algorithms.

- `src/ui/ui_document.cpp` owns `.arpui` and `.arpui.ir.yaml` loading, validation, envelope extraction, YAML serialization, and UI-specific diagnostics. It uses fkYAML and local helpers such as mapping lookup, required string fields, optional string fields, numeric fields, integer ids, mapping fields, and sequence traversal.
- `src/resource/resource_manager.cpp` owns runtime resource envelope parsing, payload canonicalization, content hash calculation, and resource-package diagnostics. It also uses fkYAML and repeats mapping lookup/string extraction behavior.
- Asset importer tests and package tests exercise explicit YAML/resource metadata, but importer behavior is domain-specific and must keep explicit format contracts rather than guessing from YAML shape.

## Ownership Decision

The shared reader helper should live in a dependency-aware resource or serialization module, not in `src/core`.

Preferred implementation target: `src/resource/yaml_reader.h` with an optional `.cpp` if the helper needs non-template code.

Acceptable alternative: `src/serialization/yaml_reader.h` if a broader serialization module is created first and both UI/resource dependencies can point to it without cycles.

`src/core` must remain dependency-light. Adding fkYAML, YAML node types, resource-envelope concepts, `.arpui` concepts, or parser exception behavior to core would violate the core boundary and make pure algorithms depend on a file-format parser.

## Field Helper Contract

A future YAML reader contract must provide deterministic diagnostics for:

- required string fields;
- optional string fields;
- number fields that accept explicit YAML integer or float nodes where the caller asks for a number;
- integer fields with deterministic range handling;
- bool fields;
- mapping fields;
- sequence fields;
- missing fields;
- wrong node types;
- parser exceptions from fkYAML.

Diagnostics must include the owning format label supplied by the caller, the field path, and the expected type. The helper must fail instead of producing guessed fallback values.

The helper must not infer resource semantics from field names. Resource loading, import, redirection, compatibility, `.arpui`, package envelope, and authoring IR semantics remain in their owning modules and must be expressed through explicit format contracts and tests.

## Migration Policy

Keep this phase docs-only. Do not add `src/resource/yaml_reader.h` until a focused RED/GREEN contract test is added.

The first implementation should migrate one low-risk call site, likely a repeated mapping/string helper shared between `src/ui/ui_document.cpp` and `src/resource/resource_manager.cpp`, while preserving existing diagnostic wording through tests.

Do not migrate all `.arpui` parsing, resource envelope parsing, or importer parsing in one pass.

Do not claim Backend Validated, Platform Validated, or Production Ready from this document. This document is Prototype Complete only.
