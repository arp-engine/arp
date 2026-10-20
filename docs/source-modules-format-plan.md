# Engine source module formatting

Status: F0 Contract Complete; F1 Complete; F2 Complete. Formatting scope is
Contract Complete; this does not claim backend, platform, or production readiness.

Use clang-format 21.1.8 and the RHI style from the repository-root `.clang-format`
for all first-party C/C++ and Objective-C++ modules, tests and tools. Exclude
third-party, generated, build, and artifact files.

- [x] F0: Inventory all engine modules, preserve user changes, and snapshot the
  current `src/` sources under `build/source-formatting/baseline/`.
- [x] F1: Apply the shared format to all first-party modules. The full
  clang-format dry run and source-style tests pass; embedded JavaScript macro
  bodies are protected and pass Node syntax validation.
- [x] F2: Build affected MinGW targets and run focused unit/contract and smoke
  tests. Record evidence and readiness limits.

All newly added or modified first-party C-family code must pass clang-format
before completion. Formatting does not claim backend, platform, or production
readiness.

The initial `src/` pass covered 1,344 C-family files. The repository-wide pass
currently covers 2,448 first-party C-family files, including files added after
the original inventory. The root `.clang-format` is the only active formatting
configuration.

Embedded Emscripten JavaScript is formatted with the JavaScript profile and
protected from C++ formatting. All 665 macro bodies have explicit formatter
boundaries; their lexical tokens match the pre-format snapshot after reversing
the formatter's `===`, `!==` and `=>` token splits, and Node syntax validation
passes. The repository-wide clang-format 21.1.8 dry run also passes.

Snapshot reconciliation limitation: the shared worktree changed after the
initial 2,442-file snapshot. The comparison reports include membership
differences in 37 existing files and non-include token differences in several
files, including added RHI and editor scene-persistence code. These changes
were retained, and the snapshot comparison is not treated as proof that
formatting caused them. Formatting completion is instead established by the
whole-tree clang-format dry run, source-style contract, JavaScript token/syntax
checks, and build/test results below.

F2 evidence: MinGW `win-vulkan-debug` engine build passed, including
`arp_engine.dll`, `arp_editor.exe`, and `arp_editor_cli`. The source-style test
passed 6/6; focused 2D resource, service, codec, world and runtime contracts
passed; 2D Vulkan hidden-window and Null hidden-frame smokes passed. The diff
whitespace check passed. A final repository-wide clang-format 21.1.8 dry run over 2,448
files and the source-style test passed again after formatting late shared
worktree changes. The `arp_physics2d_tests` target compiled but did not link
because its reflection implementation symbols/library are missing; this is
recorded as an existing target dependency issue and was not changed by this
formatting work.

