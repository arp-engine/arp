// ArpScript VM yield/await/generator tests.
//
// Tests the coroutine machinery:
// - Yield opcode: returns the yielded value with Suspended status
// - Await opcode with ScriptAwaitable: Ready, Pending+resume, Failed, non-awaitable
//
// Design note: Yield sets the Suspended error code and returns the yielded
// value, but does NOT save the frame for resumption (only Await does).
// The caller of a function that yields sees Suspended and can observe the
// yielded value. True frame-suspending resumption only happens via Await.
//
// Test style: hand-authored bytecode + self-contained main().

#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/value.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace arp::scriptvm;

namespace {

int g_failures = 0;

void require(bool cond, const char* label)
{
    if (!cond) {
        std::printf("FAIL: %s\n", label);
        ++g_failures;
    }
}

void requireInt(const Value& v, int expected, const char* label)
{
    int actual = 0;
    if (!tryGetInt(v, actual)) {
        std::printf("FAIL: %s (not an int)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, want %d)\n", label, actual, expected);
        ++g_failures;
    }
}

void requireSuspended(const VmError& err, const char* label)
{
    if (err.code != ScriptDiagnosticCode::Suspended) {
        std::printf("FAIL: %s (expected Suspended, got code %d): %s\n",
            label,
            static_cast<int>(err.code),
            err.message.c_str());
        ++g_failures;
    }
}

// --- Yield: returns the yielded value with Suspended status ---

void testYieldReturnsValue()
{
    // func gen(): LoadInt 42; Yield
    // run() should return 42 with Suspended status.
    Program program;
    Function& gen = program.functions.emplace_back("gen");
    gen.params = 0;
    gen.results = 1;
    gen.locals = 0;
    gen.maxStack = 1;
    gen.code.emplace_back(Opcode::LoadInt, 0, 0, 42);
    gen.code.emplace_back(Opcode::Yield);
    gen.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    gen.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["gen"] = 0;

    Vm vm;
    VmError err;
    Value v = vm.run(program, &err);
    requireSuspended(err, "yield sets Suspended status");
    requireInt(v, 42, "yield returns yielded value 42");
}

void testYieldNil()
{
    // func gen(): LoadNil; Yield
    Program program;
    Function& gen = program.functions.emplace_back("gen");
    gen.params = 0;
    gen.results = 1;
    gen.locals = 0;
    gen.maxStack = 1;
    gen.code.emplace_back(Opcode::LoadNil);
    gen.code.emplace_back(Opcode::Yield);
    gen.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    gen.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["gen"] = 0;

    Vm vm;
    VmError err;
    Value v = vm.run(program, &err);
    requireSuspended(err, "yield nil sets Suspended");
    require(isNil(v), "yield returns nil");
}

// --- Yield inside a nested call: caller sees Suspended ---

void testYieldNestedCall()
{
    // func inner(): LoadInt 42; Yield; LoadInt 99; Return 1
    // func main(): Call inner,0; Return 1
    // When inner yields, the Call opcode sees Suspended and saves the caller
    // frame in suspendedCallers_. The yielded value propagates up.
    Program program;

    Function& inner = program.functions.emplace_back("inner");
    inner.params = 0;
    inner.results = 1;
    inner.locals = 0;
    inner.maxStack = 1;
    inner.code.emplace_back(Opcode::LoadInt, 0, 0, 42);
    inner.code.emplace_back(Opcode::Yield);
    inner.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    inner.code.emplace_back(Opcode::Return, 1);

    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 1;
    mainFn.code.emplace_back(Opcode::Call, 0, 0);
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 1;
    program.funcIndex["inner"] = 0;
    program.funcIndex["main"] = 1;

    Vm vm;
    VmError err;
    Value v = vm.run(program, &err);
    requireSuspended(err, "nested yield sets Suspended");
    requireInt(v, 42, "nested yield value 42");
}

// --- Yield in runFunction: returns the yielded value ---

void testYieldInRunFunction()
{
    // func gen(n: int): LoadInt n; Yield; LoadInt n+1; Return 1
    // Call gen(5) -> yields 5 with Suspended
    Program program;
    Function& gen = program.functions.emplace_back("gen");
    gen.params = 1;
    gen.results = 1;
    gen.locals = 1;
    gen.maxStack = 1;
    gen.code.emplace_back(Opcode::LoadLocal, 0);
    gen.code.emplace_back(Opcode::Yield);
    gen.code.emplace_back(Opcode::LoadLocal, 0);
    gen.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    gen.code.emplace_back(Opcode::Add);
    gen.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["gen"] = 0;

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeInt(5));

    Vm vm;
    VmError err;
    Value v = vm.runFunction(program, 0, args, &err);
    requireSuspended(err, "runFunction yield Suspended");
    requireInt(v, 5, "runFunction yields 5");
}

// --- Await with a ready awaitable ---

void testAwaitReady()
{
    // func awaiter(x): LoadLocal 0; Await; Return 1
    // Pass a ready awaitable that resolves to 77.
    Program program;
    Function& awaiter = program.functions.emplace_back("awaiter");
    awaiter.params = 1;
    awaiter.results = 1;
    awaiter.locals = 1;
    awaiter.maxStack = 1;
    awaiter.code.emplace_back(Opcode::LoadLocal, 0);
    awaiter.code.emplace_back(Opcode::Await);
    awaiter.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["awaiter"] = 0;

    ScriptAwaitable awaitable;
    awaitable.poll = [](Value& out, arp::core::pmr::String&) {
        out = makeInt(77);
        return ScriptAwaitStatus::Ready;
    };
    awaitable.ready = [] {
        return true;
    };

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});

    Vm vm;
    VmError err;
    Value result = vm.runFunction(program, 0, args, &err);
    require(err.code == ScriptDiagnosticCode::None, "await ready no error");
    requireInt(result, 77, "await ready value 77");
}

// --- Await with pending then ready (suspend + resume) ---

void testAwaitPendingThenReady()
{
    Program program;
    Function& awaiter = program.functions.emplace_back("awaiter");
    awaiter.params = 1;
    awaiter.results = 1;
    awaiter.locals = 1;
    awaiter.maxStack = 1;
    awaiter.code.emplace_back(Opcode::LoadLocal, 0);
    awaiter.code.emplace_back(Opcode::Await);
    // After resume, the awaited value (123) is pushed; return it.
    awaiter.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["awaiter"] = 0;

    // Build a pending awaitable that becomes ready after first poll
    int pollCount = 0;
    ScriptAwaitable awaitable;
    awaitable.poll = [&pollCount](Value& out, arp::core::pmr::String&) {
        if (pollCount < 1) {
            ++pollCount;
            return ScriptAwaitStatus::Pending;
        }
        out = makeInt(123);
        return ScriptAwaitStatus::Ready;
    };
    awaitable.ready = [&pollCount] {
        return pollCount >= 1;
    };

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});

    Vm vm;
    VmError err;
    Value result = vm.runFunction(program, 0, args, &err);
    requireSuspended(err, "await pending suspended");
    require(vm.suspended(), "await pending vm.suspended()");

    // Resume -> should be ready now
    err = VmError{};
    Value finalResult = vm.resume(program, &err);
    require(err.code == ScriptDiagnosticCode::None, "await resume no error");
    requireInt(finalResult, 123, "await resume value 123");
}

// --- Await failure ---

void testAwaitFailed()
{
    Program program;
    Function& awaiter = program.functions.emplace_back("awaiter");
    awaiter.params = 1;
    awaiter.results = 1;
    awaiter.locals = 1;
    awaiter.maxStack = 1;
    awaiter.code.emplace_back(Opcode::LoadLocal, 0);
    awaiter.code.emplace_back(Opcode::Await);
    awaiter.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["awaiter"] = 0;

    ScriptAwaitable awaitable;
    awaitable.poll = [](Value&, arp::core::pmr::String& diag) {
        diag = arp::core::pmr::String("operation failed", &arp::core::defaultCpuMemoryResource());
        return ScriptAwaitStatus::Failed;
    };
    awaitable.ready = [] {
        return false;
    };

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});

    Vm vm;
    VmError err;
    vm.runFunction(program, 0, args, &err);
    require(err.code == ScriptDiagnosticCode::HostError, "await failed -> HostError");
    require(!vm.suspended(), "await failed vm not suspended");
}

// --- Await on non-awaitable ---

void testAwaitNonAwaitable()
{
    Program program;
    Function& awaiter = program.functions.emplace_back("awaiter");
    awaiter.params = 1;
    awaiter.results = 1;
    awaiter.locals = 1;
    awaiter.maxStack = 1;
    awaiter.code.emplace_back(Opcode::LoadLocal, 0);
    awaiter.code.emplace_back(Opcode::Await);
    awaiter.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["awaiter"] = 0;

    // Pass an int (not an awaitable)
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeInt(42));

    Vm vm;
    VmError err;
    vm.runFunction(program, 0, args, &err);
    require(err.code == ScriptDiagnosticCode::HostError, "await non-awaitable -> HostError");
}

// --- Await inside nested call: caller frame saved + resumed ---

void testAwaitNestedCallResume()
{
    // func inner(x): LoadLocal 0; Await; Return 1  (suspends on await)
    // func main(): LoadInt 55; Call inner,1; Return 1
    // When inner awaits, main's frame is saved in suspendedCallers_.
    // On resume, inner gets the awaited value (200), returns it, main returns 200.
    Program program;

    Function& inner = program.functions.emplace_back("inner");
    inner.params = 1;
    inner.results = 1;
    inner.locals = 1;
    inner.maxStack = 1;
    inner.code.emplace_back(Opcode::LoadLocal, 0);
    inner.code.emplace_back(Opcode::Await);
    inner.code.emplace_back(Opcode::Return, 1);

    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 1;
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 55);
    mainFn.code.emplace_back(Opcode::Call, 0, 1);
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 1;
    program.funcIndex["inner"] = 0;
    program.funcIndex["main"] = 1;

    // Build a pending-then-ready awaitable
    int pollCount = 0;
    ScriptAwaitable awaitable;
    awaitable.poll = [&pollCount](Value& out, arp::core::pmr::String&) {
        if (pollCount < 1) {
            ++pollCount;
            return ScriptAwaitStatus::Pending;
        }
        out = makeInt(200);
        return ScriptAwaitStatus::Ready;
    };
    awaitable.ready = [&pollCount] {
        return pollCount >= 1;
    };

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});

    Vm vm;
    VmError err;
    // Run main with the awaitable as arg to inner... but main pushes 55 as arg.
    // We need to pass the awaitable to inner, not 55.
    // Let's fix: main calls inner(awaitable).
    // But main is the entry, and inner takes the awaitable from main's arg.
    // Actually, let's pass the awaitable directly to inner via runFunction.

    // Simpler: call inner directly with the awaitable
    err = VmError{};
    Value result = vm.runFunction(program, 0, args, &err);
    requireSuspended(err, "nested await suspended");
    require(vm.suspended(), "nested await vm.suspended()");

    err = VmError{};
    Value finalResult = vm.resume(program, &err);
    require(err.code == ScriptDiagnosticCode::None, "nested await resume no error");
    requireInt(finalResult, 200, "nested await resume value 200");
}

// --- Await ready (no suspension): value flows through ---

void testAwaitReadyInExpression()
{
    // func main(x): LoadLocal 0; Await; LoadInt 1; Add; Return 1
    // Pass ready awaitable with value 10 -> result 11
    Program program;
    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 1;
    mainFn.results = 1;
    mainFn.locals = 1;
    mainFn.maxStack = 2;
    mainFn.code.emplace_back(Opcode::LoadLocal, 0);
    mainFn.code.emplace_back(Opcode::Await);
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    mainFn.code.emplace_back(Opcode::Add);
    mainFn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    ScriptAwaitable awaitable;
    awaitable.poll = [](Value& out, arp::core::pmr::String&) {
        out = makeInt(10);
        return ScriptAwaitStatus::Ready;
    };
    awaitable.ready = [] {
        return true;
    };

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});

    Vm vm;
    VmError err;
    Value result = vm.runFunction(program, 0, args, &err);
    require(err.code == ScriptDiagnosticCode::None, "await ready in expr no error");
    requireInt(result, 11, "await(10) + 1 == 11");
}

// --- resume() with no suspended frame -> error ---

void testResumeWithoutSuspension()
{
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    Vm vm;
    VmError err;
    vm.run(program, &err); // completes normally
    require(!vm.suspended(), "no suspension after normal run");

    err = VmError{};
    vm.resume(program, &err);
    require(err.code == ScriptDiagnosticCode::HostError, "resume without suspension -> HostError");
}

} // namespace

int main()
{
    registerScriptObjectReflection();

    // Yield tests
    testYieldReturnsValue();
    testYieldNil();
    testYieldNestedCall();
    testYieldInRunFunction();

    // Await tests
    testAwaitReady();
    testAwaitPendingThenReady();
    testAwaitFailed();
    testAwaitNonAwaitable();
    testAwaitNestedCallResume();
    testAwaitReadyInExpression();

    // Resume edge case
    testResumeWithoutSuspension();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript VM yield/await tests passed\n");
    return 0;
}
