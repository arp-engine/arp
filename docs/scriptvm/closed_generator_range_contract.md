# Closed generator range context reuse

Status: Round63 and Round64 candidates rejected and withdrawn. Candidate contract/unit and Windows hidden/headless validation passed; no production implementation or performance acceptance remains. Frozen implementations and negative measurements are retained in the round artifacts. This document is a design reference, not a Production Ready claim.

The path reuses the existing `rangeBody` proof and checked-stack frame executor. It does not
replace producer instructions, Yield, actual Value carriers, or public resume.

The immutable producer must be a generator with no captures, variadics,
delegation, handlers, calls, await, globals, containers, or host operations.
Only the explicit integer/local/control/Yield opcode set qualifies. The
consumer must already have the exact pure integer range-body proof and a
specialized immutable evaluator. General calculation bodies retain the
original executor; this path invokes the existing evaluator directly and
does not instantiate another general evaluateSlots implementation.
The optimized executor is selected only for the exact adjacent static
generator factory Call -> matching RangeInit -> RangeNext shape. It uses a
compile-time extended-loop executor policy, shared with the existing method
loop executor. Ordinary loop executors do not contain the new resume hook.
Dynamic sources without an existing method-loop qualification retain the
original executor. This is a selection proof, not a producer stack proof.

Runtime admission proves the generator belongs to this validated Program,
has its original root Frame and no continuation, and every slot that can be
released in the interval has an actual owned primitive carrier. Native/cv and
host-owned carriers decline. VM and TLS quotas, active generators, call bridges,
nonempty consumer stacks/handlers and incompatible frame limits retain the
existing path. The fixed consumer evaluator requires no calculation scratch.
No input-size threshold is used.

The context scope retains and restores the original generator, Program, and
Program owner, including on C++ exceptions. Each producer step still creates
its own VmError and calls the original executeFrameImpl<true, false>, preserving every
opcode's budget, PC, stack check, arithmetic, boxing and yieldedValue update.
Only the callback-free consumer and producer interval shares this context.
Admission already proves exact snapshot/function, original frame shape and
the independent producer plan. The closed operations cannot change these
facts or invoke a host callback, so repeated executeFrameRef selection is
unnecessary. The original checked operand stack, per-opcode budget,
diagnostics and real Yield remain authoritative.

The consumer commits the existing RangeNext index/key/value effects, then the
original JumpIfFalse and checked scalar body, with their exact costs and PCs.
Short budgets, overflow, or unsupported actual operands return to the original
consumer PC without resuming a producer twice. Actual producer errors transfer
the original diagnostic after restoring the context. Every yielded value is
read from the real producer result; no closed form, index substitution, cache,
AOT, JIT, or benchmark-specific selection is permitted.

Verification: qualification positives/negatives, raw versus validated VM for
every instruction budget with and without shared sessions, overflow/native/cv,
retained generator state and yield carrier identity, frame limits and quotas,
host destructor callbacks, public send/throw/close, quick unit plus headless.
Then freeze source/binary, run independent controls, and require the unchanged
full 72-input matrix before performance acceptance.
