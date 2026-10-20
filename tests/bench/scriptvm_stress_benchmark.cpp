// Identical source corpus for Interpreter and generated v5 DirectNative AOT.
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/interpreter/vm.h"
#include "stress_corpus_source.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <psapi.h>
#include <windows.h>
#endif

namespace {
using namespace arp::scriptvm;
using Clock = std::chrono::steady_clock;

struct BenchmarkCase {
    const char* name;
    int iterations;
    int quickIterations;
};

constexpr std::array<BenchmarkCase, 24> cases{{{"int_loop", 200000, 2000},
    {"function_calls", 50000, 500},
    {"recursive_fib", 100, 1},
    {"list_index", 100000, 1000},
    {"list_build", 100000, 100},
    {"dict_index", 50000, 500},
    {"string_concat", 2000, 20},
    {"range_sum", 50000, 20},
    {"branch_mix", 50000, 500},
    {"power_floor", 20000, 200},
    {"list_mutate", 50000, 500},
    {"exception_loop", 100000, 2000},
    {"float_math", 50000, 500},
    {"bitwise", 50000, 500},
    {"nested_loop", 4000, 20},
    {"dict_build", 20000, 100},
    {"dict_mutate", 50000, 500},
    {"dict_string", 50000, 500},
    {"string_compare", 50000, 500},
    {"string_build", 20000, 100},
    {"list_iterate", 10000, 20},
    {"object_method", 20000, 100},
    {"generator_sum", 10000, 20},
    {"closure_calls", 20000, 100}}};
const AotPackageDescriptorV5* registeredPackage = nullptr;

bool capturePackage(const AotPackageDescriptorV5* package, void*)
{
    registeredPackage = package;
    return package != nullptr;
}

[[noreturn]] void fail(const char* label, const char* message)
{
    std::fprintf(stderr, "%s: %s\n", label, message);
    std::exit(1);
}

double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Measurement {
    int checksum = 0;
    double milliseconds = 0;
    std::uint64_t allocations = 0;
    std::int64_t liveBytesDelta = 0;
};

class BenchmarkRuntime final {
public:
    void initialize(bool interpreter, bool aot)
    {
        if (interpreter) {
            const auto start = Clock::now();
            auto compiled =
                arp::scriptvm::test::buildInterpreter(stressCorpusSource, "stress.arp", "stress");
            if (!compiled.ok())
                fail("Interpreter compile", compiled.error.message.c_str());
            const auto validation = ValidatedInterpreterProgram::create(compiled.program, program);
            if (!validation.ok())
                fail("Interpreter artifact", validation.diagnostic().message.c_str());
            const auto binding = vm.bindProgram(program);
            if (!binding.ok())
                fail("Interpreter binding", binding.diagnostic().message.c_str());
            compileMs = elapsed(start);
        }
        if (aot) {
            const auto start = Clock::now();
            AotRegistrationHostV5 host{kAotContractVersion, &capturePackage, nullptr};
            if (arp_register_script_aot_package_v5(&host) != 1 || registeredPackage == nullptr)
                fail("AOT", "package registration failed");
            const auto validation = validateAotPackageV5(*registeredPackage, currentAotPlatform());
            if (!validation.ok())
                fail("AOT", validation.diagnostic().message.c_str());
            if (!registry.registerPackage(*registeredPackage).ok()
                || !createAotOnlyModuleInstanceV5(
                    registry, registeredPackage->packageIdentity, "stress", instance)
                    .ok()
                || instance.context == nullptr)
                fail("AOT", "module setup failed");
            setupMs = elapsed(start);
        }
    }

    Measurement invoke(const char* name, int iterations, bool aot, int batches = 1)
    {
        const auto* function = aot ? findFunction(name) : nullptr;
        auto& resource = arp::core::defaultCpuMemoryResource();
        arp::core::pmr::Vector<Value> args{&resource};
        args.push_back(makeInt(iterations));
        arp::core::pmr::String functionName(name, &resource);
        if (aot)
            instance.context->resetBudget(instance.module->budget);
        VmError error;
        const auto before = arp::core::defaultCpuAllocator().stats();
        const auto start = Clock::now();
        Value result;
        for (int batch = 0; batch < batches; ++batch) {
            result = aot
                ? function->entry(*instance.context, args.data(), args.size(), nullptr, &error)
                : vm.runFunctionByName(program.program(), functionName, args, &error);
            if (error.code != ScriptDiagnosticCode::None)
                fail(name, error.message.c_str());
        }
        const auto milliseconds = elapsed(start) / batches;
        const auto after = arp::core::defaultCpuAllocator().stats();
        if (error.code != ScriptDiagnosticCode::None)
            fail(name, error.message.c_str());
        int checksum = 0;
        if (!tryGetInt(result, checksum))
            fail(name, "result is not an int");
        return {checksum,
            milliseconds,
            after.allocationCount - before.allocationCount,
            static_cast<std::int64_t>(after.liveBytes)
                - static_cast<std::int64_t>(before.liveBytes)};
    }

    double compileMs = 0;
    double setupMs = 0;

    void dumpInterpreterPlan(std::string_view name) const
    {
        for (const auto& function : program.program().functions) {
            if (!name.empty() && name != function.name)
                continue;
            const auto* plan = program.scalarExpressions(&function);
            std::printf("FUNCTION %s stack_proven=%d argument_leaf=%d\n",
                function.name.c_str(),
                program.hasVerifiedStackBound(&function),
                plan != nullptr && plan->argumentLeaf);
            for (std::size_t pc = 0; pc != function.code.size(); ++pc) {
                const auto& instruction = function.code[pc];
                std::printf("  %zu %s a=%d b=%d i64=%lld",
                    pc,
                    formatOpcode(instruction.op).c_str(),
                    instruction.a,
                    instruction.b,
                    static_cast<long long>(instruction.i64));
                if (plan && plan->entries[pc].count) {
                    const auto& entry = plan->entries[pc];
                    std::printf(
                        " plan: nodes=%d calculations=%d cost=%d next=%d step=%d float=%d fixed=%d",
                        entry.count,
                        entry.calculationCount,
                        entry.cost,
                        entry.nextPc,
                        entry.stepLocal,
                        entry.floating,
                        entry.evaluator != nullptr || entry.floatEvaluator != nullptr);
                    if (entry.loopCondition >= 0)
                        std::printf(" loop_condition=%d", entry.loopCondition);
                }
                if (plan && plan->entries[pc].appendLocal >= 0) {
                    const auto& entry = plan->entries[pc];
                    std::printf(
                        " string: destination=%d source=%d suffix_pc=%d cost=%d step=%d next=%d",
                        entry.appendLocal,
                        entry.stringSource,
                        entry.appendLiteralPc,
                        entry.cost,
                        entry.stepLocal,
                        entry.nextPc);
                }
                if (plan && plan->entries[pc].listDestination >= 0)
                    std::printf(" private_list: destination=%d", plan->entries[pc].listDestination);
                if (plan && plan->entries[pc].dictionaryDestination >= 0)
                    std::printf(" private_dictionary: destination=%d",
                        plan->entries[pc].dictionaryDestination);
                std::printf("\n");
            }
        }
    }

private:
    const AotFunctionDescriptorV5* findFunction(const char* name) const
    {
        arp::core::pmr::String id("arp-fn:stress/", &arp::core::defaultCpuMemoryResource());
        id += name;
        for (std::size_t i = 0; i < instance.module->functionCount; ++i) {
            const auto& function = instance.module->functions[i];
            if (function.functionId != nullptr && id == function.functionId) {
                if (function.entry == nullptr
                    || function.executionKind != AotFunctionExecutionKind::DirectNative)
                    fail(name, "v5 DirectNative entry missing; fallback forbidden");
                return &function;
            }
        }
        fail(name, "v5 function missing");
    }

    ValidatedInterpreterProgram program;
    Vm vm;
    ScriptAotPackageRegistry registry;
    ScriptAotModuleInstanceV5 instance;
};

int parsePositive(const char* text)
{
    int value = 0;
    const auto end = text + std::char_traits<char>::length(text);
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value < 1 || value > 100000000)
        fail("arguments", "expected integer in [1,100000000]");
    return value;
}

void printMeasurement(
    const char* prefix, const char* name, const char* mode, int n, const Measurement& m)
{
    std::printf("%s %s %s %d %d %.9f %llu %lld\n",
        prefix,
        name,
        mode,
        n,
        m.checksum,
        m.milliseconds,
        static_cast<unsigned long long>(m.allocations),
        static_cast<long long>(m.liveBytesDelta));
}
} // namespace

int main(int argc, char** argv)
{
    int scale = 1, iterations = 0, targetMs = 0;
    bool quick = false, list = false, dumpPlan = false;
    std::string_view selected, mode = "both";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--quick")
            quick = true;
        else if (arg == "--list")
            list = true;
        else if (arg == "--dump-plan")
            dumpPlan = true;
        else if (i + 1 < argc && arg == "--scale")
            scale = parsePositive(argv[++i]);
        else if (i + 1 < argc && arg == "--iterations")
            iterations = parsePositive(argv[++i]);
        else if (i + 1 < argc && arg == "--target-ms")
            targetMs = parsePositive(argv[++i]);
        else if (i + 1 < argc && arg == "--case")
            selected = argv[++i];
        else if (i + 1 < argc && arg == "--mode")
            mode = argv[++i];
        else
            fail("arguments",
                "usage: [--quick|--scale N] [--case NAME --iterations N] [--mode both|aot|interpreter] [--list]");
    }
    if (scale > 1000 || (mode != "both" && mode != "aot" && mode != "interpreter"))
        fail("arguments", "invalid scale or mode");
    if (!selected.empty() && std::none_of(cases.begin(), cases.end(), [&](auto& c) {
            return selected == c.name;
        }))
        fail("arguments", "unknown case");
#ifdef NDEBUG
    std::printf("PROFILE Release\n");
#else
    std::printf("PROFILE Debug\n");
#endif
    if (list) {
        for (const auto& c : cases)
            std::printf("CASE %s %d %d\n", c.name, c.iterations, c.quickIterations);
        return 0;
    }
    BenchmarkRuntime runtime;
    runtime.initialize(dumpPlan || mode != "aot", !dumpPlan && mode != "interpreter");
    if (dumpPlan) {
        runtime.dumpInterpreterPlan(selected);
        return 0;
    }
    std::printf("SETUP %.9f %.9f\n", runtime.compileMs, runtime.setupMs);
    for (const auto& c : cases) {
        if (!selected.empty() && selected != c.name)
            continue;
        // This known runtime failure is still callable explicitly, and is
        // measured by the suite as a negative AOT capability probe.
        if (std::string_view(c.name) == "generator_sum" && mode == "both") {
            std::printf(
                "UNSUPPORTED generator_sum aot ARP-BENCH-AOT-GENERATOR-ITERATION-UNSUPPORTED\n");
            continue;
        }
#if defined(__EMSCRIPTEN__)
        constexpr int divisor = 100;
#else
        constexpr int divisor = 1;
#endif
        const int n = iterations
            ? iterations
            : (quick ? c.quickIterations : std::max(1, c.iterations * scale / divisor));
        Measurement interp, aot;
        for (const bool native : {false, true}) {
            if ((native && mode == "interpreter") || (!native && mode == "aot"))
                continue;
            const auto cold = runtime.invoke(c.name, n, native);
            printMeasurement("COLD", c.name, native ? "aot" : "interpreter", n, cold);
            const int batches = targetMs == 0
                ? 1
                : std::clamp(static_cast<int>(std::min(
                                 10000.0, targetMs / std::max(0.000001, cold.milliseconds)))
                          + 1,
                      1,
                      10000);
            std::printf("BATCH %s %s %d\n", c.name, native ? "aot" : "interpreter", batches);
            auto& sample = native ? aot : interp;
            sample = runtime.invoke(c.name, n, native, batches);
            if (sample.checksum != cold.checksum)
                fail(c.name, "first/warm result mismatch");
            printMeasurement("SAMPLE", c.name, native ? "aot" : "interpreter", n, sample);
        }
        if (mode == "both") {
            if (interp.checksum != aot.checksum)
                fail(c.name, "Interpreter/AOT checksum mismatch");
            std::printf("RESULT %s %d %d %.9f %d %.9f\n",
                c.name,
                n,
                interp.checksum,
                interp.milliseconds,
                aot.checksum,
                aot.milliseconds);
        }
        std::fflush(stdout);
    }
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        std::printf("PROCESS_PEAK_WORKING_SET %llu\n",
            static_cast<unsigned long long>(counters.PeakWorkingSetSize));
    else
        fail("memory", "GetProcessMemoryInfo failed");
#endif
    return 0;
}
