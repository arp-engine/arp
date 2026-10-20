# Selected compute-final build correspondence preflight

2026-10-13 08:04 checkpoint: capture2/independent copied-reader replay passes
47 selected files,26 probe artifacts and9 required/shared frozen inputs. Four
temporary-copy negative checks and registered Graphics Null smoke1/1 pass.
Capture1's missing copied browser runner dependency is preserved; fresh capture2
includes it. Frozen Callback24/tools6/registration31 are unchanged. Original
final210 PID52468 is live with174 accepted/0 nonzero exits/complete=false;
stage validation is absent. All runtime/aggregate/readiness gates remain closed.
Reports: build/bindless-continuation/compute-final-build-correspondence-independent-replay2.json
and compute-final-build-correspondence-negative-checks1.json.

This CPU-only selected evidence prerequisite compares the running final210's
immutable input ledger with the previously archived real probe build. It grants
no stage acceptance, aggregate, GPU execution, public capability or readiness.

Reuse the existing artifact_inventory, configuration, verify_file, fingerprint,
require_fields and probe build constants. Compare every shared selected source;
require root CMakeLists, ordinary backend, original main, probe source, probe
fixture, CMakeCache and all three payloads. Verify the archived replay's payload
identities and archive digest. Do not read live compiler inputs to fill gaps.

The separately preserved 613 MB probe manifest retains its authoritative digest.
Its final top-level artifacts section is extracted under a finite 64 KiB read
ceiling, using the existing duplicate-key reader. Missing/truncated/nonconforming
sections reject; this is not a replacement parser or full-manifest replay.

Preserve all 26 selected probe artifact bytes, the complete final input ledger,
the nine shared final input bytes, extracted inventory, prior replay receipt,
reader modules and this driver in a fresh archive. An independent process with
unrelated CWD uses only copied reader code and copied selected evidence. Explicitly
keep finalAcceptanceInspected, probeBuildReplayedThisRun, allFrozenInputBytesVerified,
transitiveSdkProvenanceComplete and all execution/readiness gates false.

Negative checks must reject missing required final input, duplicate source identity,
a rehashed altered cache, and an altered payload. They use temporary copies only.
No running/frozen source, GPU job, budget, deadline or capability is modified.

After final210 is terminal, the ordinary stage audit, independent replay, cohort
comparison and strict aggregate still run unchanged; this preflight cannot replace
any of those steps. Full transitive SDK proof is a separate full-readiness task.
