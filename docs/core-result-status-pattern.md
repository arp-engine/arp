# Core Result / Status Pattern

Status: Prototype Complete

This is a docs-only decision for the current phase. Do not add `src/core/result.h` in this phase.

## Inventory

The current codebase has two repeated result shapes.

- OOP result classes: UI, input, renderer, and scene-facing systems commonly expose `XxxResult::success(...)`, `XxxResult::failure(...)`, `ok()`, and a domain diagnostic object. Examples include `UiResult`, `CanvasLayoutResult`, `UiDocumentValidationResult`, `UiFontLoadResult`, `InputResult`, and `RendererSceneRuntimeFrameRhiPlanResult`.
- plain status structs: resource, assets, shader, platform, RHI, and tool-facing paths commonly expose public `success`, `diagnostics`, payload, and telemetry fields. Examples include file IO results, shader compile results, reflection serialization results, platform contract results, staging upload slices, and frame-plan build results.

Both shapes are valid in their owning layers today. The immediate duplication is not enough to justify broad migration because these results encode domain-specific diagnostic codes, payload access rules, telemetry, and ABI-facing field names.

## Requirements

A future reusable result utility must be dependency-light and must not import UI, renderer, resource, shader, RHI backend, YAML, or platform semantics into `src/core`.

Any implementation must preserve deterministic diagnostics. Failure construction must require an explicit diagnostic payload or message; it must not silently create generic fallback text in production paths.

Runtime-owned strings and containers must use `core::pmr`. Move-only payloads must be supported without default STL owned allocation helpers, raw `new/delete`, or hidden allocator bypasses.

Status-only results and value results must both be supported. The API must make success state, payload access, and diagnostic access unambiguous enough that call sites do not need magic sentinel values.

## Decision

Keep this phase docs-only. Do not add `src/core/result.h` yet.

The preferred future direction is a small `core::StatusResult<Diagnostic>` and `core::Result<T, Diagnostic>` pair only after a focused RED/GREEN test proves:

- status-only success and failure;
- value success and failure;
- deterministic diagnostic propagation;
- move-only payload support;
- allocator-safe diagnostic strings through `core::pmr`;
- no accidental dependency on domain diagnostic enums.

The first migration must be a single low-risk call site, preferably new code or a small local helper whose public ABI is not already consumed by renderer, asset, RHI, or UI contracts. Existing domain result classes should be adapted only after their diagnostic codes and payload invariants are preserved by tests.

## Non-Goals

Do not rewrite all `XxxResult` classes in one pass.

Do not replace backend staging slices, renderer frame plans, UI document validation, resource package loading, or shader compiler results with a generic type without a dedicated contract test for that domain.

Do not treat `bool success` structs and OOP result classes as interchangeable unless their diagnostic and payload semantics have been explicitly mapped.

Do not claim Backend Validated, Platform Validated, or Production Ready from this document. This document is Prototype Complete only.
