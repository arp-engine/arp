# Graphics Callback8 observed-context repair contract

The original frozen Callback8 pilot failed before observer allocation. Its
Graphics fixture passed a null observed-loss intent into the existing accepted
pipeline context, which therefore created the base native loss binding. Typed
callback custody correctly rejected that binding with
`RHI-WEBGPU-TYPED-CALLBACK-OBSERVED-PROFILE`. Allocation-pressure tests and the
positive observer path require the existing observed-loss binding first.

After selecting the observed binding, a diagnostic-only derived pilot proved
that custody creation and validation both succeeded; the actual owner demand was
252 while the inherited command ceiling was 273. The existing L2 validateOwners
contract admits demand below the command/admission/lifecycle ceilings. The
fixture incorrectly asserted actual demand equals the command ceiling. The
derived Callback request instead uses the existing command.retainedOwnerCount()
plus additionalOwners(request) formula as its exact offered owner budget; it
must remain within the unchanged command ceiling. Negative tests retain actual
demand minus one and original command ceiling plus one. The positive assertion
continues to require actual demand equals the now accurately authored request.
No backend check is weakened, and no budget ceiling is increased.

This repair expresses the already defined L2 observer intent only when the
explicit Callback8 workload is selected. It uses the existing
OriginalDeviceNativeLossObserverRequest/1 profile (three observer slots, four
retained owners, the unchanged 65536-byte metadata domain), passed before the
original registration. It does not add a native registration, executor, quota,
deadline, public capability, issuer, recording or completion authority. Earlier
Graphics workloads retain their base loss intent.

| Backend | Existing observed loss contract | Execution authority from repair |
|---|---|---|
| Null | Validator-only refusal | None |
| Vulkan | DiagnosticFailure for this original observer adapter | None |
| Metal | DiagnosticFailure for this original observer adapter | None |
| WebGPU | Existing opted-in native binding and private Callback8 observer | None |

Preserve the rejected original pilot and all 31 frozen inputs. Build a derived
fixture and integration object in a separate directory; never overwrite the
original source, payload or registration. The RHI headers/backends do not change,
so original libraries may be linked only after their bytes are held and checked.
Compile the complete affected integration translation unit with assertions and
the original Release recipe, with every dependency recorded. Preserve original
ASYNCIFY, memory and authored resource/owner/CPU budgets.

Required gates: original rejection saved; unchanged frozen inputs; dependency
selection and complete new compile/link; same frozen browser validator; corrected
pilot; exact 96 selectors from the original registration against one derived
payload; observer caller-drop/abandon, forged owner, mutation, exhaustion and
retirement checks. No candidate application or general Null integration before
the corrected 96-case acceptance and archive/replay. Graphics native draw,
submission, output and real completion remain separate open requirements.

Retirement diagnosis checkpoint: the exact-owner derived pilot reaches positive
custody and caller-drop retention, then fails equality against the quota baseline
captured while Cold8/Command8 still hold the original reservation. A separate
diagnostic copy must report both domains before the unchanged assertion, plus
original root/reservation state. No `<=` relaxation, extra owner destruction or
backend change is permitted without identifying which charged owner the baseline
includes and proving its intended retirement boundary.

The diagnostic context5 pilot confirms submission current=0 versus baseline=132,
callback current=0 versus baseline=0, and zero reservation/ticket/registration
counts after command retirement. The reservation is allocated under the
submission quota in OriginalCommandLifecycleReservation::prepareOriginal;
the earlier baseline therefore includes its live allocation, rather than a leak.
The derived repair captures both quotas immediately after creation and before
seal/reservation preparation. It requires exact pre-custody baseline equality
after custody release while the authentic command remains held, then exact
creation-baseline equality after command/admission/reservation retirement.
Existing duplicate, caller-drop, capacity, journal and counter assertions remain.
These are pre-submit cancellation checks, not GPU-completion proof or a backend
execution grant. The same four-backend decisions and original ceilings apply.
