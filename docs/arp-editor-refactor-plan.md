# ARP Editor modular refactor

Preserve public EditorShell behavior, `arp_editor` targets, YAML/atlas bindings,
command-line options, clipboard semantics and the existing GPU intent chain.
Split by cohesive responsibility rather than one file per control or action.

1. [x] Audit existing helpers and establish baseline interaction/hidden smoke.
   Snapshot source and native captures for comparison.
2. [x] Separate application host, game preview, UI assets, workspace coordination,
   project browser, script editing and workspace presentation. Group resources,
   tooling and tests; keep state ownership explicit. Reuse core string/UTF-8
   helpers and consolidate shared geometry/path utilities where appropriate.
   Build with MinGW and run focused unit/interaction and hidden smoke tests.
3. [x] Verify ordered interaction/script/icon paths and RenderDoc output parity;
   update module documentation and final readiness/evidence.
4. [x] Define the versioned editor command/response contract, route control
   activation and text-value changes through the owner-thread dispatcher, and
   add focused dispatcher contract tests.
5. [x] Normalize platform input, UI runtime callbacks and preview-state
   completion into typed commands; remove direct shell mutation from these
   event handlers while preserving input order and focus barriers.
6. [ ] Move workspace, document/history, presentation and preview ownership out
   of EditorShell into coarse-grained modules.
   - [x] Route script undo/redo through versioned typed commands with stable
     per-open-document targets; reject stale targets without changing history.
   - [x] Store undo/redo history only on its owning open document; remove the
     duplicated active-document history mirrors and test multi-document isolation.
   - [x] Move document collection, stable ID allocation, per-document history,
     and text range edit/undo/redo operations into `ScriptDocumentSession`.
   - [x] Remove saved/newline/cursor/selection/scroll mirrors from `EditorShell`;
     move file I/O and selection/caret operations behind the document-session owner.
   - [x] Move project browser data, navigation history and filtering behind a
     `ProjectBrowserModel`; leave row widgets, selection feedback and scroll
     presentation in `EditorShell`.
   - [x] Move presentation state behind a coarse-grained owner.
   - [x] Move preview requests and lifecycle status into `GamePreviewModel`;
     keep native process/window handles inside `GamePreviewHost`.
7. [x] Deliver authenticated, bounded MCP and /api/v1 REST ingress over the
   shared dispatcher with idempotency storage. Loopback is the default; binding
   another numeric IPv4 address requires an explicit command-line option.
   - [x] Add opt-in loopback REST discovery/command routes, bearer validation,
     bounded requests, expected-state checks and process-local idempotency.
   - [x] Verify REST authorization, command dispatch and idempotent retry through
     a real local TCP connection; retain hidden-window smoke coverage.
   - [x] Validate Streamable HTTP initialize, tools/list, tools/call, tool
     errors, and idempotent command replay with the installed MCP client SDK.

8. [x] Fail closed for non-loopback remote binds while the listener is HTTP-only.
   - [x] Reject non-loopback addresses before listener startup with a stable
     HTTPS-required diagnostic.
   - [x] Cover the rejection path in the remote server integration test and
     document that current remote commands are synchronous.
   - [x] MinGW rebuilt `arp_editor` and `arp_editor_remote_server_tests`; the
     dispatcher/build contract tests and remote server plus three hidden-window
     smoke tests passed (6/6) in `build/ui-control-events-debug`.

Production hardening backlog (not part of the loopback prototype contract):

- [ ] Define async task/result recovery and durable idempotency retention.
- [ ] Implement an HTTPS server transport and define the explicit identity/TLS
  contract required before non-loopback access can be enabled. Non-loopback
  binds now fail closed because the current listener is HTTP-only.

GPU contract: existing L2 GUI descriptors, atlas uploads, FrameGraph, swapchain
acquisition and completion order are preserved. No new shader, native payload,
capability, lowering or fallback is introduced. Null remains validation-only;
Vulkan has native Windows validation; Metal/WebGPU paths must stay compilable
without implying native platform validation. Functional status stays Prototype
Complete; Production Ready is not claimed by this reorganization.

Baseline: existing interaction test and default native hidden smoke passed 2/2
in `build/ui-control-events-debug`. Source snapshots and native script capture
are retained in `build/arp-editor-evidence/refactor`.

Phase 2: MinGW build passed in the same build tree. All 7 focused CTests passed
(interaction, shared UTF-8/path/scrollbar helpers, build workflow, window icon,
default/ordered/script hidden frames). The interaction test additionally covers
idle preview state and repeated deterministic launch failure with no executable.
Moved generator import and reference YAML emitter roundtrip passed. Test log:
`build/arp-editor-evidence/refactor/tests.txt`.

Implementation: main 536 -> 29 lines; shell implementation 1521 lines grouped into
five feature files (browser, script document, input, presentation, coordination).
`app` owns application/preview lifecycle, `ui` owns atlas/font setup; resources,
tools and tests have separate directories. Public shell include and target names
are retained. No per-control source files were introduced. Idle preview layout
resolution and ordinary command copies are removed; shared UTF-8/scrollbar/path
logic is centralized and tested. Scoped clang-format makes the split readable.

Phase 3: RenderDoc replay and AI inspection of all three native screenshots
passed. `build/arp-editor-evidence/refactor/final/` contains:

- `before_capture.rdc/.png`: default editor, 1296 indexed draws.
- `after_capture.rdc/.png`: ordered selection/Inspector/Output changes, 1360 draws.
- `script_capture.rdc/.png`: script tab, highlighting and caret, 1450 draws.
- `analysis.txt`: 1440x810, zero high/error replay diagnostics; all three views
  have **zero changed pixels** and unchanged draw counts against their original
  captures. Selection, name, intensity, checkbox and Update Mode retain their
  deterministic interaction changes (4573/42/251/48/2388 pixels respectively).

Quick unit/interaction checks and default/after/script Windows hidden smoke
remain registered in CTest for subsequent changes. Generator import, YAML
roundtrip, moved resource bindings and scoped whitespace checks passed.

Readiness:

- **Contract Complete**: module/public include/build/resource bindings preserved
  and behavior covered by focused tests; no RHI or shader semantics changed.
- **Prototype Complete**: existing reference viewport, basic script editing and
  explicit WOCC preview launcher remain the editor's feature boundary.
- **Backend Validated**: native Vulkan capture/replay on this Windows machine.
- **Platform Validated**: Windows MinGW build, icon test and three hidden frames.
- **Production Ready**: not claimed. Metal/WebGPU source paths are retained but
  their native builds/runtime are not validated in this refactor; Null remains
  validation-only. Existing per-redraw GPU completion wait is unchanged.

Command phase:

- Added editor/command/editor_command.h/.cpp with versioned typed payloads,
  origin/correlation/sequence/expected-version/idempotency metadata, canonical
  responses and an owner-thread dispatcher.
- Routed EditorAction activation and EditorEntry text changes through the
  dispatcher. Existing activate and entryChanged are compatibility forwarders.
  At that phase boundary, platform keyboard/mouse handling, workspace
  ownership, preview callbacks, script undo/redo, MCP and REST remained
  unmigrated; later phase notes below record their current status.
- Idempotency keys are rejected until a bounded deduplication store exists;
  this phase does not claim remote retry support.
- arp_editor_command_tests, arp_editor_tests and the default, after and script
  hidden smoke tests passed in build/ui-control-events-debug with MinGW.
- Contract Complete: initial command envelope/dispatch contract and focused
  rejection/state-version tests are in place. This is not full editor contract
  completion.
- Prototype Complete: existing editor feature scope is unchanged.
- Backend Validated: inherited from the prior UI-only refactor capture; this
  command change introduces no GPU behavior.
- Platform Validated: Windows MinGW build plus five focused CTests passed.
- Production Ready: not claimed. Remote ingress, ownership migration and
  command history remain open.

Input phase:

- Added a platform ingress adapter that copies keyboard, pointer, wheel, text
  and focus lifecycle data into platform-neutral `EditorInputCommand` payloads.
- `EditorShell::handlePlatformInput` is now a compatibility ingress wrapper;
  the command executor handles only normalized editor input. UI control
  activation, text changes, Inspector scrolling, thumb dragging and preview
  state updates also enter through typed commands.
- The adapter preserves input call order and the focus-lost barrier. The
  focused dispatcher test verifies key, UTF-8 text and focus-loss normalization.
- `arp_editor_command_tests`, `arp_editor_tests` and default, after and script
  hidden smoke tests passed again in `build/ui-control-events-debug` with
  MinGW.
- Script undo/redo now enter through typed commands carrying a stable
  per-open-document ID. The legacy `ScriptEdit` payload implementation and
  duplicated active-document stack mirrors still need extraction. The shell
  still owns workspace, documents, UI state and preview state.
- **Contract Complete**: platform/UI/preview input adapters now enter the typed
  command path for the covered operations; remote command contracts are not
  implemented.
- **Prototype Complete**: existing editor feature scope is unchanged.
- **Backend Validated**: no GPU behavior changed in this phase.
- **Platform Validated**: Windows MinGW build plus five focused CTests passed.
- **Production Ready**: not claimed. History ownership, workspace ownership,
  MCP/REST, authorization and idempotency remain open.

History command phase:

- Ctrl+Z/Ctrl+Y and explicit `UndoScriptCommand` / `RedoScriptCommand` share
  the command dispatcher. Each command targets the stable ID assigned when a
  script document opens; a closed or inactive target fails with
  `StaleDocumentTarget`, and empty history completes without changing the
  state version.
- `arp_editor_command_tests` covers text edit, undo/redo roundtrip and stale
  target rejection, inactive target rejection and history isolation across two
  open documents. `scriptUndo_` / `scriptRedo_` active-document mirrors were
  removed; history vectors now live only in each open document. The same five
  editor/hidden smoke tests passed with MinGW.
- **Contract Complete**: undo and redo are typed, versioned dispatcher
  operations with target validation and per-document history isolation. The
  document store and editing operations are still shell-owned.
- **Prototype Complete**: script editing behavior remains prototype scope.
- **Backend Validated**: no GPU behavior changed.
- **Platform Validated**: Windows MinGW build and five focused CTests passed.
- **Production Ready**: not claimed; document/history ownership and remote
  history operations remain open.

Project browser ownership phase (2026-10-14):

- Added `ProjectBrowserModel` to own the authorized root/current directory,
  deterministic entry scan and sort, search index, and back/forward history.
  `EditorShell` retains UI row population, selected-file feedback and scroll
  offset; navigation and filesystem facts now come from the model.
- Added a filesystem-backed fast test covering directory-first ordering,
  case-insensitive search, root escape rejection, and navigation history.
- MinGW build passed for `arp_editor_support_tests`, command/interaction tests
  and `arp_editor`. Seven focused CTests passed, including default, after-action
  and script hidden-window smoke tests.
- **Contract Complete**: the project-browser model boundaries and root check
  are covered by deterministic tests; REST/MCP query contracts are still open.
- **Prototype Complete**: existing browser behavior remains unchanged.
- **Backend Validated**: no GPU behavior changed.
- **Platform Validated**: Windows MinGW build and seven focused CTests passed.
- **Production Ready**: not claimed; document-session, presentation, preview,
  MCP/REST, authorization and idempotency work remains open.

Document session phase (2026-10-14):

- Added `ScriptDocumentSession` to own the open-document collection, stable ID
  allocation, active document index, per-document text/save/caret/history data,
  range edits and undo/redo operations. The editor dispatcher still validates
  command document IDs before asking the session to apply history operations.
- The active document body is now canonical in `ScriptDocumentSession`; input,
  presentation, save and text queries read that owned buffer directly. `EditorShell`
  still mirrors saved/newline/caret/selection state and owns open/save file I/O
  and caret navigation. Synchronization before edit preserves the prior cursor/
  selection when undo restores state; an interaction regression test covers it.
- The support test now exercises stable IDs across removal and text edit,
  dirty-state and undo/redo roundtrip. MinGW build passed, then all seven
  focused CTests passed in `build/ui-control-events-debug`, including all three
  hidden-window paths.
- **Contract Complete**: stable document targeting, canonical text ownership and
  per-session edit/history operations are covered; file I/O remains shell-owned.
- **Prototype Complete**: current script editor behavior remains unchanged.
- **Backend Validated**: no GPU behavior changed.
- **Platform Validated**: Windows MinGW build and seven focused CTests passed.
- **Production Ready**: not claimed; saved/newline/caret mirrors, file I/O,
  selection ownership, presentation, preview and MCP/REST work remains open.

Document ownership completion (2026-10-14):

- `ScriptDocumentSession` now owns canonical path validation and `.arp` open/save,
  the 2 MiB input bound, LF-normalized text, source newline style, saved baseline,
  dirty derivation, caret/selection and per-document scroll position. Failed open
  and save operations return stable result codes; shell feedback remains a UI concern.
- Removed the corresponding active-document mirrors and store/load synchronization
  from `EditorShell`. Input and presentation read the active session document;
  edit undo/redo restores the caret and selection snapshot.
- Fast tests cover stable IDs, multi-document caret isolation, selection undo/redo,
  CRLF save/reopen, project-root escape rejection, and oversize-file rejection.
  MinGW build passed; all eight focused CTests passed in
  `build/ui-control-events-debug`, including the three hidden-window smoke paths.
- **Contract Complete**: document ownership, file boundary and edit-state roundtrip
  are covered by fast tests; command-only mutation for every document operation and
  remote query/command contracts remain open.
- **Prototype Complete**: existing script editing behavior and 2 MiB limit remain.
- **Backend Validated**: no GPU behavior changed.
- **Platform Validated**: Windows MinGW build and eight focused CTests passed.
- **Production Ready**: not claimed; presentation/preview owners and MCP/REST,
  authorization and idempotency work remain open.

Preview state phase (2026-10-14):

- Added `GamePreviewModel` for start/stop request coalescing and running/starting
  state. `EditorShell` no longer stores preview request/status fields; the native
  child process and window embedding remain private to `GamePreviewHost`.
- Preview completion continues to re-enter through `SetGamePreviewStateCommand`.
  Stop can supersede a queued start; if the host has no process yet, it returns a
  completed stopped state so the model cannot remain stuck at Starting.
- Fast support tests cover duplicate start, start/stop queue replacement and
  state updates. MinGW build passed and all eight focused CTests passed again,
  including default, after-action and script hidden-window smoke.
- **Contract Complete**: preview command/status ownership and queue transitions
  have focused coverage; the broader editor state and ingress contract remain open.
- **Prototype Complete**: OS process launching/embedding behavior remains the
  existing prototype implementation.
- **Backend Validated**: no GPU behavior changed.
- **Platform Validated**: Windows MinGW build and eight focused CTests passed.
- **Production Ready**: not claimed; workspace presentation state and MCP/REST,
  authorization and idempotency work remain open.

Presentation ownership completion (2026-10-14):

- Added `EditorPresentationState` to own workspace/scene/dock selection, stable
  control-source mappings, disabled-source masks, update mode, and output/menu
  presentation state. `EditorShell` keeps widget coordination and draw-plan
  construction, with presentation values flowing through the owner.
- MinGW-built editor targets and all eight focused editor CTests passed:
  support/command/interaction/build-contract/icon tests plus default, after-action
  and script hidden-window smoke tests.
- RenderDoc replay evidence is in
  `build/arp-editor-evidence/refactor/final/presentation_state_capture_capture.rdc`;
  the inspected backbuffer is
  `build/arp-editor-evidence/refactor/final/presentation_state_capture_capture_backbuffer.bmp`.
  The 1440x810 capture contains 1329 indexed UI draws and zero high/error replay
  diagnostics. 3914 changed pixels are confined to the project file list, whose
  contents vary with this checkout; stable UI changed pixels are zero. The
  comparison report is `presentation_state_capture_capture_analysis.txt`.
- **Contract Complete**: coarse-grained presentation ownership and rendering
  parity outside checkout-dependent project-list contents are verified.
- **Prototype Complete**: existing editor workflows remain within their current
  prototype feature boundary.
- **Backend Validated**: Windows/Vulkan RenderDoc capture and replay completed.
- **Platform Validated**: MinGW build and eight Windows editor tests passed.
- **Production Ready**: not claimed. MCP/REST ingress, authorization,
  idempotency storage and remote integration tests remain unimplemented.

Initial remote ingress checkpoint (2026-10-14):

- REST remains an opt-in, loopback-only prototype over the shared dispatcher.
  The local TCP test covers bearer rejection, discovery, script replacement and
  idempotent replay. It is not evidence of production remote access.
- At this checkpoint MCP ingress had been removed from the HTTP route table
  pending client interoperability validation. The later MCP SDK verification
  below supersedes this interim state.
- The follow-up report mentioned an exception during registry key validation
  and an unreachable key-release path, but no matching code path was found under
  `editor/`, its tests, or the current input adapter. No speculative key event
  change was made; provide a file/symbol or failing test to target that repair.
- The REST-only MinGW build and focused tests are recorded after this checkpoint
  below; they do not change the incomplete MCP phase status.
- After disabling the MCP route, MinGW rebuilt `arp_editor`,
  `arp_editor_command_tests`, and `arp_editor_remote_server_tests`; the two
  focused CTests passed 2/2. The loopback test asserts that `/mcp` is not routed.

MCP client interoperability update (2026-10-14):

- Rebuilt `arp_editor`, interaction/support tests, command and remote loopback
  tests, and the Windows icon target with the MinGW toolchain in
  `build/ui-control-events-debug`.
- The local Trae installation provides
  `@byted/modelcontextprotocol-client` version `2.0.0-alpha.2.byted.5`; its SDK
  reports MCP protocol version `2025-11-25`. No dependency was downloaded.
- The loopback integration test uses that SDK's `Client` and
  `StreamableHTTPClientTransport` to initialize, list the seven editor tools,
  query editor state, submit a script replacement, receive a tool error for an
  unknown tool, and replay the same idempotency key. The replay returns the
  original state version and does not apply another edit.
- The test also verifies bearer rejection for both REST and MCP and an explicit
  405 response for unsupported MCP GET. The MCP server only advertises JSON
  request/response and does not create a session for this stateless command set.
- **Contract Complete**: local REST and MCP loopback integration, shared typed
  command dispatch, bearer authorization, bounded request handling and
  process-local idempotency are exercised over TCP.
- **Prototype Complete**: loopback-default remote operation; idempotency state
  is bounded and process-local. Non-loopback binding is explicit opt-in and
  uses plain HTTP, so it is not production-ready.
- **Backend Validated**: no GPU/backend behavior changed.
- **Platform Validated**: MinGW build and nine focused Windows CTests passed.
- **Production Ready**: not claimed. Async task/result recovery, persistent
  idempotency, and non-loopback HTTPS remain future work. HTTP-only server binds
  are now restricted to exact `127.0.0.1`; other addresses fail before listen.

Bind-address security verification (2026-10-14):

- `--remote-port` still defaults to numeric `127.0.0.1`; the listener passes
  the parsed numeric IPv4 endpoint directly to the HTTP server. A non-loopback
  address can only be supplied with `--remote-bind-address` and is identified
  separately in startup output. Hostnames and IPv6 bind inputs are rejected.
- Loopback classification compares the parsed endpoint for exact equality with
  `127.0.0.1`. Contract assertions cover `127.0.0.1`, `0.0.0.0`, a LAN address,
  and a hostname, preventing wildcard/LAN binds from being labelled loopback.
- The existing 32..256-byte token requirement and constant-time Bearer check
  apply on both MCP and REST regardless of the selected bind address. Short
  credentials and unauthenticated REST/MCP requests are rejected in tests.
- Non-loopback binds are rejected before listener startup with a stable
  HTTPS-required diagnostic; no plaintext LAN listener is created. The
  integration test asserts the server remains stopped after rejection.
- The seven advertised editor commands are synchronous. There is no accepted
  asynchronous editor task to recover yet, so task/result recovery stays open
  until an asynchronous command is introduced with a stable task contract.
- After these changes, the MinGW editor targets rebuilt and all nine focused
  CTests passed, including the SDK-driven MCP loopback test and three hidden
  window smoke paths.

ArpScript CLI phase (2026-10-14):

- Added `arp_editor_cli` as a desktop-native executable. The C++ launcher only
  starts the ScriptVM/network service, passes argv and environment values, and
  drives awaits; command parsing, JSON envelopes, Bearer configuration and
  response output are implemented in `editor/cli/arp_editor_cli.arp`.
- The CLI submits only to `/api/v1/commands`, using the same loopback URL and
  token contract as the editor REST service. `state`, `activate`, `search`,
  `script.replace`, `script.save`, `script.undo` and `script.redo` are exposed.
- Added a VM-backed help smoke and extended the local TCP server integration to
  invoke the built CLI for authenticated state query and script replacement.
  The test asserts the query leaves state unchanged and replacement updates the
  targeted document through the shared command dispatcher.
- The standard `editor` and `engine` build commands include the CLI; Web and
  mobile configurations do not register this native executable.
- **Contract Complete**: ArpScript command-to-REST mapping, Authorization
  header setup and loopback request/response are exercised by the CLI smoke and
  remote server integration test.
- **Prototype Complete**: CLI supports the current synchronous editor command
  set and requires a running editor with remote REST enabled.
- **Backend Validated**: no GPU/backend behavior changed.
- **Platform Validated**: MinGW build and focused CLI/REST plus editor
  hidden-window smoke tests passed.
- **Production Ready**: not claimed; existing REST/MCP limits, process-local
  idempotency and lack of HTTPS remain unchanged.
