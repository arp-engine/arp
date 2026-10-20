# ArpScript: Design Document

**Status: Implementation In Progress**

Historical migration record:
[ArpScript AOT 与 Interpreter 混合运行设计](aot_hybrid_design.md). Hybrid is
retired and is not a current execution backend or fallback.

Normative backend contract:
[ArpScript Typed IR Backend Contract](typed_ir_backend_contract.md). The IR
contract is authoritative for every execution backend; this document remains
the language and implementation overview.

The current package and source boundaries are documented in
[`src/scriptvm/README.md`](../../src/scriptvm/README.md). DirectNative v5 is the
only production AOT path; the editor explicitly composes the Interpreter path.

AOT 当前未完成项的集中清单见
[feature_plan.md 的“未完成内容总览”](feature_plan.md)；该清单优先于各阶段的局部
完成描述，整体状态仍不能据此标记为 `Production Ready`。

ArpScript is a Python-syntax, strong-typed, all-reference-semantics scripting
language embedded in the arp engine. Source is lowered once to canonical Typed
IR. The Interpreter deterministically lowers that validated IR to versioned
stack bytecode; DirectNative and Web/Wasm consume the same validated IR.

---

## 1. Language Overview

### Syntax

ArpScript uses Python-style indentation-based blocks with mandatory type hints
(mypy-style). All `var`/`const` declarations, function parameters, return
types, and class fields must have explicit type annotations.

```python
# Scalars
var x: int = 5
const pi: float = 3.14
var s: string = "hello"
var ok: bool = true

# Containers (reference semantics)
var arr: [int] = [1, 2, 3]
var m: {string: int} = {"a": 1, "b": 2}

# Functions + closures
def make_counter(start: int) -> () -> int:
    var count: int = start
    def inc() -> int:
        count = count + 1   # closure capture, shared mutable
        return count
    return inc

# Control flow
def fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

for i: int in range(10):
    print(fib(i))

while x > 0:
    x -= 1
    if x == 5:
        continue
    if x == 3:
        break
```

### Type System

- **Basic types**: `bool`, `int` (32-bit), `int64`, `float` (double), `string`, `bytes`
- **Containers**: `[T]` (list), `{K: V}` (dict)
- **Optional**: `?T` (nullable)
- **Function type**: `(T1, T2) -> R`
- **User type**: `class Name:` (all-reference semantics)
- **nil**: null reference (assignable to any optional/reference type)
- **void**: function return type for no return value

`bytes` is an immutable binary sequence. It can be created by converting a
`string` with `as bytes`, has `len()` and integer indexing, and indexing returns
an unsigned byte in the range `0..255` as an `int`. Byte values cannot be
assigned through an index.

Strong typing is enforced at compile time (sema phase). Missing type hints
are a compile error, not inferred.

### Semantics

- **All-reference semantics**: All assignments and parameter passing share
  references (via `shared_ptr` in the thick bridge). Immutable scalars (int,
  float, bool, string, bytes) are effectively value-copied since they're immutable.
  Mutable containers (list, dict, instance) mutations are visible through all
  aliases.
- **No deep copy on store**: Unlike goes (CloneForStore), ArpScript never
  deep-copies on assignment. The VM does zero store-time cloning.
- **Closure captures**: Captured local variables are promoted to
  `shared_ptr<ScriptCell>` (upvalue cells, like Lua). The outer function and
  all closures share the same cell. Non-captured locals have no cell overhead.
- **const**: Prevents reassignment at compile time. Does not affect mutability
  of container contents.

### Resource module and await

The host can register the `resources` module with a `ResourceManager` and an
`AsyncRuntime`:

```python
import resources

def load_asset(path: string) -> any:
    return await resources.load_async(path)

def release_asset(resource_id: string) -> any:
    return resources.release(resource_id)
```

`resources.load` consumes a validated envelope YAML string synchronously.
`resources.load_async` submits a host-filesystem envelope read and returns a
ScriptVM awaitable. `resources.release`, `state`, `diagnostics`, and
`resolve_alias` expose identity/state/diagnostic data without exposing native
resource ownership. Completion callbacks only publish the result; the host
dispatches its `AsyncRuntime` and calls `Vm::resume(program)` on the script
thread. A failed load is surfaced as a deterministic `HostError` from `await`.

This is **Prototype Complete**: the contract and worker-thread tests are
present, while platform-specific resource backends remain governed by their
own validation status.

---

## 2. Architecture: Thick Bridge (Value = reflection::object)

Every runtime value in ArpScript is an `arp::reflection::object`, which is a
`SharedPtr<variant>` (internally `SharedPtr<storage<T>>`). This means each
scalar value passes through two `shared_ptr` layers.

### Mapping

| ArpScript Concept | reflection::object Mapping |
|---|---|
| `int/float/bool/string` | `object(42)`, `object(3.14)`, etc. |
| `bytes` | `ScriptBytes` (PMR-backed immutable byte sequence) |
| `class` instance | `reflection::instance` (dynamic dict) |
| `[T]` list | `reflection::hlist` (`Vector<object>`) |
| `{K:V}` dict | `reflection::hdict` (`Map<object,object>`) |
| Closure | `ScriptClosure` (C++ struct, reflection-registered) |
| nil | `object::undefined()` |
| Captured cell | `ScriptCell` (C++ struct, reflection-registered) |
| Host function | `HostRegistry` + `HostFn` (`std::function`) |

### Operator Reuse

`reflection::object` dispatches `__add__/__sub__/__mul__/__div__/__mod__`,
comparison, bitwise, indexing, and conversion magic functions. The VM's
arithmetic, comparison, bitwise, and conversion opcodes route through these
operators or `object::cast<T>()`; the VM only normalizes built-in numeric
operands to the sema-selected common representation. A missing or failed magic
function is a deterministic runtime diagnostic.

### Performance Characteristics

The two-layer `shared_ptr` design means each scalar arithmetic operation
allocates. This is the inherent cost of the thick bridge. Mitigation:

1. **MVP (Phase 1-6)**: Stack uses `object` directly. Correctness first.
   Perf status: `Prototype Complete`.
2. **Fast-path (Phase 7)**: `LocalAddIntConst` / `LocalSubIntConst` opcodes
   modify int locals in-place (`locals[a] = makeInt(cur + k)`), avoiding
   the stack push/pop + Add + StoreLocal sequence. Generated for patterns
   like `x += 1` where x is an uncaptured int local.

---

## 3. Bytecode IR

### Instruction Format

```cpp
struct Instruction {
    Opcode op;
    int a = 0, b = 0;       // operands (slot indices, arg counts, etc.)
    int64_t i64 = 0;         // immediate value (int literal, packed range)
    core::pmr::String s;     // string immediate (field name, etc.)
    SourcePos pos;           // debug info (file, line, column)
};
```

### Opcode Set (~70 opcodes)

- **Literals**: `LoadNil`, `LoadBool`, `LoadInt`, `LoadInt64`, `LoadFloat`,
  `LoadString`
- **Load/Store**: `LoadLocal`, `StoreLocal`, `LoadCapture`, `StoreCapture`,
  `LoadGlobal`, `StoreGlobal`, `Pop`
- **Arithmetic**: `Add`, `Sub`, `Mul`, `Div`, `Mod`, `Neg`, `Not`
- **Bitwise**: `BitAnd`, `BitOr`, `BitXor`, `BitClear`, `ShiftLeft`,
  `ShiftRight`
- **Comparison**: `CmpEq`, `CmpNe`, `CmpLt`, `CmpLe`, `CmpGt`, `CmpGe`
- **Jump**: `Jump`, `JumpIfFalse`, `JumpIfTrue`
- **Call**: `Call`, `CallValue`, `CallHost`, `Return`
- **Function values**: `MakeFuncValue`, `MakeClosure`, `MakeMethodValue`
- **Container**: `MakeList`, `MakeDict`, `MakeRange`, `MakeRangeFromStack`,
  `LoadIndex`, `StoreIndex`, `LoadField`, `StoreField`
- **Addressing**: `AddrLocal`, `AddrCapture`, `AddrGlobal`, `Deref`,
  `StoreIndirect`
- **Builtins**: `BuiltinLen`, `RangeInit`, `RangeNext`, `BuiltinAppend`,
  `BuiltinDelete`, `BuiltinClear`
  - **Conversions**: `ConvertInt`, `ConvertInt64`, `ConvertFloat`,
   `ConvertString`, `ConvertBool`, `ConvertBytes`
- **Fast-path**: `LocalAddIntConst`, `LocalSubIntConst`, `LocalAddLocal`,
  `JumpIfLocalIntLtConstFalse`
- **Host**: `Yield`

### Calling Convention

- Arguments are pushed on the stack in order, then `Call funcIdx, argC` pops
  them and calls `runFunctionInternal`.
- Results (typically 1) are pushed back on the caller's stack.
- `CallValue` is used for closure calls: pops the callee value first, then
  args.
- `CallHost` pops args and invokes a registered `HostFn`.

### Closure Capture

When a nested function references an outer local variable:
1. **Compile-time**: `analyzeCaptures()` scans the nested function body for
   identifier references to outer locals.
2. **Mark captured**: The outer local's `isCaptured` flag is set.
3. **Runtime promotion**: `AddrLocal` opcode promotes the slot: moves the
   value into a `shared_ptr<ScriptCell>`, replaces the slot with a
   `ScriptPointer` to it. Subsequent `AddrLocal` calls reuse the cell.
4. **Closure creation**: `MakeClosure funcIdx, capC` pops `capC` cells from
   the stack and bundles them into a `ScriptClosure`.
5. **Capture access**: In the nested function, `LoadCapture`/`StoreCapture`
   read/write through the cell's `value` field.

---

## 4. Module Layout

```
src/scriptvm/
  value.h/.cpp         Value = reflection::object + scalar helpers
  script_object.h/.cpp ScriptClosure / ScriptCell / ScriptPointer
  bytecode.h/.cpp      Opcode / Instruction / Function / Program
  source_pos.h          SourcePos (file, line, column)
  diagnostics.h/.cpp    ScriptDiagnostic + ScriptResult
  lexer.h/.cpp          Token + Lexer (INDENT/DEDENT)
  ast.h                 AST nodes (Expr / Stmt / Type / Module)
  parser.h/.cpp         Recursive-descent parser (Pratt expressions)
  sema.h/.cpp           Scope/Symbol + type checking + capture analysis
  compiler.h/.cpp      AST -> bytecode (slot alloc, jump patch, fast-path)
  frame.h/.cpp          Frame + call stack
  vm.h/.cpp             Execution loop (switch dispatch) + host + yield
  host.h                HostRegistry (header-only)
  eval.h/.cpp           Public API: runProgram / runFunction
```

Dependencies: `arp_core`, `arp_reflection`. All containers use `core::pmr`,
owned objects use `core::makeUnique`/`core::makeShared` (engine-wide constraints compliant).

---

## 5. Compilation Pipeline

```
Source text
    |  Lexer (lexer.cpp)
    v
Token stream (with INDENT/DEDENT)
    |  Parser (parser.cpp)
    v
AST (Module with declarations)
    |  Sema (sema.cpp)
    v
Typed AST (type-checked, captures analyzed)
    |  Compiler (compiler.cpp)
    v
Program (bytecode functions)
    |  VM (vm.cpp)
    v
Result value (reflection::object)
```

### Public API

```cpp
// One-shot: source -> result
auto pr = parseSource(source, filename);
auto sr = analyze(pr.module);
auto cr = compile(sr.module);
auto result = runFunctionByName(cr.program, "test", args);

// Or: run the entry function
auto result = runProgram(cr.program);
```

---

## 6. Status Declarations

| Phase | Status | Tests |
|---|---|---|
| Phase 1: Runtime Core + VM | Contract Complete | 12 unit + 1 smoke |
| Phase 2: Lexer | Contract Complete | 21 unit + 1 smoke |
| Phase 3: Parser + AST | Contract Complete | 28 unit + 1 smoke |
| Phase 4: Sema | Contract Complete | 26 unit + 1 smoke |
| Phase 5: Frontend, Typed IR and bytecode lowering | Backend Validated | Typed IR, bytecode lowering and Interpreter pipeline unit + smoke |
| Phase 6: E2E + Host + Closures + Yield | Prototype Complete | 9 contract + 1 smoke |
| Phase 7: Fast-path + Reflection Dispatch + Docs | Prototype Complete | unit + smoke |

**Total**: 127 unit/contract tests + 7 smoke tests, all passing.

### Feature Extension Status

| Feature | Status | Verification |
|---|---|---|
| F1: String methods | Backend Validated | core string utility unit + ScriptVM e2e contract + e2e smoke |
| F2: Type assertions/conversions | Backend Validated | parser unit + ScriptVM e2e contract + BadCast negative path + e2e smoke |
| F3: Dict tuple iteration | Backend Validated | parser/sema unit + ScriptVM e2e contract + e2e smoke |
| F4: Lambda expressions | Backend Validated | parser unit + direct/captured lambda e2e + e2e smoke |
| F5: Variadic functions | Backend Validated | parser unit + VM e2e + invalid-position negative path + e2e smoke |
| F6: try/except/raise | Backend Validated | parser unit + nested re-raise e2e contract + e2e smoke |
| F7: Generators | Prototype Complete | parser unit + generator e2e contract + hidden smoke; bounded yield-from/send/close/throw, delegated-child forwarding, fixed bounded four-level Native delegated-yield child shape, exact single-handler Native yield shape, root delegated-state preservation across AOT/Interpreter adoption, proven Native await-handler subset, bounded two-level delegated child-await migration, and generic cross-backend migration contract complete; fifth-level Native delegation, deeper delegated-await, and complex Native combinations remain fail-closed |

### Yield/Suspension Status

The `Yield` bytecode operation returns a yielded value with `Suspended` status
internally. Generator functions preserve their frame and resume from explicit
delegation/safepoint metadata emitted by the frontend and carried through
Typed IR. The Interpreter implements `send`/`throw`/`close`, delegated-child
forwarding and pending-await retention. DirectNative implements only shapes
proved by its Typed IR validators; unsupported combinations fail closed with
the existing deterministic diagnostic. There is no bytecode-shape recovery,
generic executor, Hybrid adoption or Interpreter fallback in production AOT.

### Matrix Layout Hard Constraint

ArpScript has no shader matrix interaction. The VM is pure C++ with no GPU
backend. The `core::math` row-major convention and RHI contract obligations
do not apply to the script VM.

---

## 7. Build Integration

```powershell
# Build the ScriptVM packages
python build.py configure --platform win --backend vulkan
cd build/win-vulkan-debug
ninja arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling

# Build and run tests
ninja arp_scriptvm_bytecode_lowering_tests arp_scriptvm_interpreter_pipeline_smoke
./arp_scriptvm_bytecode_lowering_tests.exe
./arp_scriptvm_interpreter_pipeline_smoke.exe
```

The runtime, interpreter, and AOT packages are discovered separately by
`build.py` from their package directories. There is no aggregate ScriptVM
target. All test targets are registered in `build.py:test_configs`.

---

## 8. Key Design Decisions

1. **Thick bridge over thin bridge**: Using `reflection::object` as the VM
   value carrier means operator dispatch, type conversion, and container
   access all go through reflection. This maximizes code reuse (no duplicate
   type dispatch in the VM) at the cost of per-operation allocation overhead.

2. **Python-style indentation**: INDENT/DEDENT tokens are generated by the
   lexer using a whitespace stack (CPython-style). Brackets `()[]{}` suppress
   newline significance.

3. **All-reference semantics (no CloneForStore)**: Simplifies the mental model
   and removes store-time cloning overhead. Mutable containers share state
   through aliases, like Python.

4. **Capture cells (Lua-style upvalues)**: Captured locals are promoted to
   `shared_ptr<ScriptCell>` on first `AddrLocal`, ensuring all closures and
   the outer frame share one cell. Non-captured locals have zero overhead.

5. **Fast-path int optimization**: `LocalAddIntConst` / `LocalSubIntConst`
   bypass the stack for the common `x += <int>` pattern, directly modifying
   the local slot. Only generated for uncaptured int locals.

6. **Module system (import)**: Python-style `from module import name` and
   `import module as alias`. Two module sources:
   - **Plugin modules**: C++ registered through Plugin ABI v2
     (`ScriptPluginRegistrar`), exporting controlled `Value`s, reflected
     classes/enums, and service-backed callables.
   - **Source modules**: `.arp` files resolved on demand through a
     `ModuleRegistry::setSourceResolver` callback. The resolver uses the
     canonical frontend -> Typed IR -> bytecode pipeline and returns a
     `ModuleExports` table whose owner retains the immutable Program.
   - Imports are stored in Program global slots and initialized by the
     explicit module-initializer function before a selected entry runs. No
     hidden `__entry__` is synthesized. Sema validates
   import names at compile time when a `ModuleRegistry` is available;
   otherwise validation is deferred to runtime.
   - **Status: Backend Validated** (plugin module + source module e2e tests pass).
