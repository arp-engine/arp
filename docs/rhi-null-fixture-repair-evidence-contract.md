# NullFixtureRepairOverlayEvidence/1

This conformance tool builds and runs the entire registered general Null smoke
source from an independent copied test tree while a Graphics candidate is sealed.
It supplies no GPU, public bindless, NativeSync or platform authority. It cannot
replace repairing and rebuilding the registered target after the sealed browser
acceptance has been preserved.

The tool reuses the existing Ninja command tokenizer, dependency inventory
reader, immutable file fingerprints and closed capability fields. It reads the
configured general Null compiler/link recipes, checks active assertions and the
exact source/object/executable owners, and changes only the source location and
object/dependency/executable/import-library output locations. It invokes compiler
and linker argument lists directly with `shell=False`; the known Windows link
wrapper is inspected and removed, never evaluated as a shell command. Includes,
optimization flags, static-runtime options, linked libraries and original smoke
working directory remain unchanged.

The original complete smoke main is copied byte-for-byte. All fixture files and
the original compiler's test include closure are copied, including sibling test
helpers outside `tests/fixtures`. The existing two-frame runner, contract fixture
calls and assertions remain intact. Explicit repairs are limited to the three
known obsolete negative guards:

- A mismatched Freeze8 projection expects PROFILE; unsupported Freeze9 expects
  VERSION. This does not authorize any backend freeze, payload or execution.
- Payload8 input preparation follows the existing four-backend lowering:
  WebGPU private payload only, Null validator only, Vulkan/Metal diagnostic.
  Recording and execution stay false for all four. Unsupported Payload9 is
  independently rejected with the existing VERSION diagnostic.
- Closure10 has an ordinary-descriptor loss join and pipeline source /1 in the
  existing L2 domain. Table-only, mixed and empty domains remain rejected;
  Closure11 remains unsupported. Legacy Closure profiles retain their assertions.

An unapplied candidate records original fixture fingerprints and an exact patch.
The repaired copies must match these explicit transformations. Actual GCC
dependencies must include the copied repaired headers and exclude their original
counterparts. Compiler, libraries, original dependency inputs, candidate and the
24 sealed Graphics source/registration/payload inputs are fingerprinted and
checked through compilation, linking and full smoke execution. Copied inputs
are checked separately. Any drift or failed stage leaves `complete=false` and
`repairSmokeValidated=false`; each attempt retains its own logs and receipt.

`complete=true` is limited to a successfully executed overlay candidate.
`registeredTargetRepaired=false`, `candidateAppliedToLiveSource=false`,
`gpuExecuted=false`, `transitiveSdkProvenanceComplete=false` and all broader
bindless/Graphics/WOCC/platform/production gates stay closed even on success.
No compiler deadline, runtime/GPU budget, frozen helper, CMake registration or
live shared fixture is changed by this tool. CMake registration is deferred until
the current sealed candidate's browser acceptance has been preserved.

Quick test:

```powershell
python -B -X utf8 tests/contract/rhi_null_fixture_repair_evidence_test.py
```

Full copied smoke:

```powershell
python -B -X utf8 tests/contract/rhi_null_fixture_repair_evidence.py --build build/win-vulkan-release --candidate build/bindless-continuation/null-graphics-negative-guards-candidate2.json --sealed build/bindless-continuation/graphics-callback8-observer-candidate1/manifest.json --output build/bindless-continuation/null-graphics-overlay4 --ninja C:/mingw64/bin/ninja.exe
```

Each output directory must be new. Earlier failed attempts are retained.
The continued full-resource bindless plan and actual engine integration remain
required independently of this candidate evidence.
