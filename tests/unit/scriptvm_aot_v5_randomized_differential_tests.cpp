#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/interpreter/vm.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

struct EnduranceOptions {
    std::uint64_t iterations = 10000u;
    std::uint32_t seed = 0xA075EEDu;
    std::uint64_t durationSeconds = 0u;
};

extern "C" int arp_register_script_aot_package_v5(arp::scriptvm::AotRegistrationHostV5* host);

namespace {

using namespace arp::scriptvm;

const AotPackageDescriptorV5* package = nullptr;

bool capturePackage(const AotPackageDescriptorV5* value, void*)
{
    package = value;
    return true;
}

Program interpreterProgram()
{
    auto built =
        arp::scriptvm::test::buildInterpreter("def add(a: int, b: int) -> int:\n"
                                              "    return a + b\n"
                                              "def add_then_increment(a: int, b: int) -> int:\n"
                                              "    return add(a, b) + 1\n"
                                              "def sum_loop(count: int) -> int:\n"
                                              "    var index: int = 0\n"
                                              "    var total: int = 0\n"
                                              "    while index < count:\n"
                                              "        total = total + index\n"
                                              "        index = index + 1\n"
                                              "    return total\n",
            "aot-v5-randomized.arp",
            "basic");
    return built.ok() ? std::move(built.program) : Program{};
}

Program delegatedInterpreterProgram()
{
    auto built = arp::scriptvm::test::buildInterpreter("def child(start: int) -> int:\n"
                                                       "    yield start\n"
                                                       "    return start + 1\n"
                                                       "def parent(start: int) -> int:\n"
                                                       "    yield from child(start)\n"
                                                       "    return 99\n"
                                                       "def factory(start: int) -> any:\n"
                                                       "    return parent(start)\n",
        "aot-v5-delegated.arp",
        "basic");
    return built.ok() ? std::move(built.program) : Program{};
}

class RandomizedDifferentialGate final {
public:
    int run(const EnduranceOptions& options)
    {
        AotRegistrationHostV5 host{kAotContractVersion, &capturePackage, nullptr};
        if (arp_register_script_aot_package_v5(&host) != 1 || package == nullptr)
            return 1;
        ScriptAotPackageRegistry registry;
        if (!registry.registerPackage(*package).ok())
            return 2;
        ScriptAotModuleInstanceV5 instance;
        if (!createAotOnlyModuleInstanceV5(registry, package->packageIdentity, "basic", instance)
                .ok())
            return 3;
        const char* initializerId = instance.module->moduleInitFunctionId;
        DirectNativeDispatcherV5 direct(std::move(instance));
        Program program = interpreterProgram();
        if (program.functions.empty())
            return 4;
        Vm interpreter;
        auto& resource = arp::core::defaultCpuMemoryResource();
        arp::core::pmr::Vector<Value> arguments{&resource};
        arguments.resize(2u);
        std::uint32_t seed = options.seed;
        const auto deadline = options.durationSeconds == 0u
            ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + std::chrono::seconds(options.durationSeconds);
        std::uint64_t completed = 0u;
        while ((options.durationSeconds == 0u && completed < options.iterations)
            || (options.durationSeconds != 0u
                && (completed == 0u || std::chrono::steady_clock::now() < deadline))) {
            const int lhs = next(seed);
            const int rhs = next(seed);
            arguments[0] = makeInt(lhs);
            arguments[1] = makeInt(rhs);
            if (!compare(interpreter, program, direct, "add", "arp-fn:basic/add", arguments))
                return 5;
            if (!compare(interpreter,
                    program,
                    direct,
                    "add_then_increment",
                    "arp-fn:basic/add_then_increment",
                    arguments))
                return 6;
            ++completed;
            if (options.durationSeconds != 0u && direct.telemetry().size() >= 4096u) {
                direct.clearTelemetry();
            }
        }
        if (options.durationSeconds == 0u) {
            std::size_t calls = 0u;
            std::size_t initializers = 0u;
            for (const auto& event : direct.telemetry()) {
                if (event.diagnostic != ScriptDiagnosticCode::None)
                    return 7;
                if (event.functionId == "arp-fn:basic/add"
                    || event.functionId == "arp-fn:basic/add_then_increment") {
                    ++calls;
                } else if (initializerId != nullptr && event.functionId == initializerId) {
                    ++initializers;
                } else {
                    return 7;
                }
            }
            const auto expectedInitializers =
                initializerId != nullptr && initializerId[0] != '\0' ? 1u : 0u;
            if (calls != options.iterations * 2u || initializers != expectedInitializers) {
                std::fprintf(stderr,
                    "telemetry mismatch: calls=%zu initializers=%zu\n",
                    calls,
                    initializers);
                return 7;
            }
        }
        const Value capturedArguments[] = {makeInt(10)};
        VmError generatorError;
        Value generatorValue = direct.invoke(
            "arp-fn:basic/captured_generator", capturedArguments, 1u, &generatorError);
        auto* generator = asDirectNativeGeneratorV5(generatorValue);
        if (generatorError.code != ScriptDiagnosticCode::None || generator == nullptr)
            return 8;
        int generatorValueInt = 0;
        Value yielded = resumeDirectNativeGeneratorV5(*generator, &generatorError);
        if (generatorError.code != ScriptDiagnosticCode::None
            || !tryGetInt(yielded, generatorValueInt) || generatorValueInt != 15)
            return 9;
        const Value returned = resumeDirectNativeGeneratorV5(*generator, &generatorError);
        if (generatorError.code != ScriptDiagnosticCode::None
            || !tryGetInt(returned, generatorValueInt) || generatorValueInt != 16)
            return 10;

        Program delegatedProgram = delegatedInterpreterProgram();
        if (delegatedProgram.functions.empty()) {
            std::fprintf(stderr, "delegation: compile failed\n");
            return 12;
        }
        Vm delegatedInterpreter;
        arp::core::pmr::Vector<Value> delegatedArguments{&resource};
        delegatedArguments.push_back(makeInt(10));
        VmError interpretedDelegatedError;
        Value interpretedDelegatedValue = delegatedInterpreter.runFunctionByName(delegatedProgram,
            arp::core::pmr::String("factory", &resource),
            delegatedArguments,
            &interpretedDelegatedError);
        auto* interpretedDelegated = asScriptGenerator(interpretedDelegatedValue);
        VmError directDelegatedError;
        Value directDelegatedValue = direct.invoke("arp-fn:basic/delegated_parent",
            delegatedArguments.data(),
            delegatedArguments.size(),
            &directDelegatedError);
        auto* directDelegated = asDirectNativeGeneratorV5(directDelegatedValue);
        if (interpretedDelegatedError.code != ScriptDiagnosticCode::None
            || directDelegatedError.code != ScriptDiagnosticCode::None
            || interpretedDelegated == nullptr || directDelegated == nullptr) {
            std::fprintf(stderr,
                "delegation: create failed interp=%d direct=%d interp_gen=%p direct_gen=%p\n",
                static_cast<int>(interpretedDelegatedError.code),
                static_cast<int>(directDelegatedError.code),
                static_cast<void*>(interpretedDelegated),
                static_cast<void*>(directDelegated));
            std::fprintf(stderr,
                "delegation: interp message=%s direct message=%s\n",
                interpretedDelegatedError.message.c_str(),
                directDelegatedError.message.c_str());
            return 13;
        }
        int interpretedDelegatedResult = 0;
        int directDelegatedResult = 0;
        interpretedDelegatedError = {};
        directDelegatedError = {};
        const Value interpretedDelegatedYield = delegatedInterpreter.resumeGeneratorValue(
            *interpretedDelegated, interpretedDelegatedError);
        const Value directDelegatedYield =
            resumeDirectNativeGeneratorV5(*directDelegated, &directDelegatedError);
        if (interpretedDelegatedError.code != directDelegatedError.code
            || !tryGetInt(interpretedDelegatedYield, interpretedDelegatedResult)
            || !tryGetInt(directDelegatedYield, directDelegatedResult)
            || interpretedDelegatedResult != directDelegatedResult) {
            std::fprintf(stderr,
                "delegation: first yield mismatch interp_err=%d direct_err=%d interp=%d direct=%d\n",
                static_cast<int>(interpretedDelegatedError.code),
                static_cast<int>(directDelegatedError.code),
                interpretedDelegatedResult,
                directDelegatedResult);
            return 14;
        }
        interpretedDelegatedError = {};
        directDelegatedError = {};
        const Value interpretedDelegatedReturn = delegatedInterpreter.resumeGeneratorValueSend(
            *interpretedDelegated, makeInt(42), interpretedDelegatedError);
        const Value directDelegatedReturn =
            sendDirectNativeGeneratorV5(*directDelegated, makeInt(42), &directDelegatedError);
        if (interpretedDelegatedError.code != directDelegatedError.code
            || !tryGetInt(interpretedDelegatedReturn, interpretedDelegatedResult)
            || !tryGetInt(directDelegatedReturn, directDelegatedResult)
            || interpretedDelegatedResult != directDelegatedResult
            || interpretedDelegatedResult != 99) {
            std::fprintf(stderr,
                "delegation: send result mismatch interp_err=%d direct_err=%d interp=%d direct=%d\n",
                static_cast<int>(interpretedDelegatedError.code),
                static_cast<int>(directDelegatedError.code),
                interpretedDelegatedResult,
                directDelegatedResult);
            std::fprintf(stderr,
                "delegation: send messages interp=%s direct=%s\n",
                interpretedDelegatedError.message.c_str(),
                directDelegatedError.message.c_str());
            return 15;
        }
        if (options.durationSeconds == 0u && !performanceGate(program))
            return 11;
        std::printf("AOT_ENDURANCE status=passed iterations=%llu seed=%u duration_seconds=%llu\n",
            static_cast<unsigned long long>(completed),
            seed,
            static_cast<unsigned long long>(options.durationSeconds));
        return 0;
    }

private:
    static int next(std::uint32_t& seed)
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 8u) % 200001u) - 100000;
    }

    static bool compare(Vm& interpreter,
        const Program& program,
        DirectNativeDispatcherV5& direct,
        const char* interpreterName,
        const char* directId,
        const arp::core::pmr::Vector<Value>& arguments)
    {
        VmError interpreterError;
        VmError directError;
        const Value interpreted = interpreter.runFunctionByName(program,
            arp::core::pmr::String(interpreterName, &arp::core::defaultCpuMemoryResource()),
            arguments,
            &interpreterError);
        const Value generated =
            direct.invoke(directId, arguments.data(), arguments.size(), &directError);
        int interpretedValue = 0;
        int generatedValue = 0;
        return interpreterError.code == directError.code
            && interpreterError.message == directError.message
            && tryGetInt(interpreted, interpretedValue) && tryGetInt(generated, generatedValue)
            && interpretedValue == generatedValue;
    }

    static bool performanceGate(const Program& program)
    {
        const AotModuleDescriptorV5* module = nullptr;
        for (std::size_t index = 0; index < package->moduleCount; ++index) {
            if (std::strcmp(package->modules[index].moduleId, "basic") == 0) {
                module = &package->modules[index];
                break;
            }
        }
        if (module == nullptr)
            return false;
        const AotFunctionDescriptorV5* function = nullptr;
        for (std::size_t index = 0; index < module->functionCount; ++index) {
            if (std::strcmp(module->functions[index].functionId, "arp-fn:basic/sum_loop") == 0) {
                function = &module->functions[index];
                break;
            }
        }
        if (function == nullptr || function->entry == nullptr)
            return false;
        // Keep roughly four million loop bodies while reducing timer noise.
        // Each entry still uses the package's normal instruction budget.
        constexpr std::size_t iterations = 1024u;
        constexpr int loopCount = 4096;
        constexpr std::size_t rounds = 5u;
        std::array<double, rounds> interpreterTimes{};
        std::array<double, rounds> directTimes{};
        const Value arguments[] = {makeInt(loopCount)};
        arp::core::pmr::Vector<Value> interpreterArguments{&arp::core::defaultCpuMemoryResource()};
        interpreterArguments.push_back(arguments[0]);
        const arp::core::pmr::String functionName(
            "sum_loop", &arp::core::defaultCpuMemoryResource());
        for (std::size_t round = 0; round < rounds; ++round) {
            Vm interpreter;
            VmError error;
            int checksum = 0;
            const auto interpreterStart = std::chrono::steady_clock::now();
            for (std::size_t index = 0; index < iterations; ++index) {
                const Value result = interpreter.runFunctionByName(
                    program, functionName, interpreterArguments, &error);
                int value = 0;
                if (error.code != ScriptDiagnosticCode::None || !tryGetInt(result, value))
                    return false;
                checksum += value;
            }
            const auto interpreterEnd = std::chrono::steady_clock::now();
            DirectNativeExecutionContext context;
            context.memoryQuota =
                arp::core::makeShared<arp::core::CpuMemoryQuota>(module->budget.maxMemoryBytes);
            int directChecksum = 0;
            const auto directStart = std::chrono::steady_clock::now();
            for (std::size_t index = 0; index < iterations; ++index) {
                context.resetBudget(module->budget);
                error = {};
                const Value result = function->entry(context, arguments, 1u, nullptr, &error);
                int value = 0;
                if (error.code != ScriptDiagnosticCode::None || !tryGetInt(result, value))
                    return false;
                directChecksum += value;
            }
            const auto directEnd = std::chrono::steady_clock::now();
            if (checksum != directChecksum)
                return false;
            interpreterTimes[round] =
                std::chrono::duration<double, std::milli>(interpreterEnd - interpreterStart)
                    .count();
            directTimes[round] =
                std::chrono::duration<double, std::milli>(directEnd - directStart).count();
        }
        std::sort(interpreterTimes.begin(), interpreterTimes.end());
        std::sort(directTimes.begin(), directTimes.end());
        const double ratio = interpreterTimes[rounds / 2u] / directTimes[rounds / 2u];
        std::printf("AOT_V5_PERF interpreter_ms=%.3f direct_ms=%.3f ratio=%.3f\n",
            interpreterTimes[rounds / 2u],
            directTimes[rounds / 2u],
            ratio);
        return ratio >= 2.0;
    }
};

} // namespace

bool parseUnsigned(const char* text, std::uint64_t& output)
{
    if (text == nullptr || text[0] == '\0')
        return false;
    char* end = nullptr;
    const auto value = std::strtoull(text, &end, 0);
    if (end == text || *end != '\0')
        return false;
    output = static_cast<std::uint64_t>(value);
    return true;
}

int main(int argc, char** argv)
{
    EnduranceOptions options;
    for (int index = 1; index < argc; ++index) {
        std::uint64_t value = 0u;
        if (std::string_view(argv[index]) == "--iterations" && index + 1 < argc
            && parseUnsigned(argv[++index], value)) {
            options.iterations = value;
        } else if (std::string_view(argv[index]) == "--seed" && index + 1 < argc
            && parseUnsigned(argv[++index], value)) {
            options.seed = static_cast<std::uint32_t>(value);
        } else if (std::string_view(argv[index]) == "--duration-seconds" && index + 1 < argc
            && parseUnsigned(argv[++index], value)) {
            options.durationSeconds = value;
        } else {
            std::fprintf(stderr,
                "usage: scriptvm_aot_v5_randomized_differential_tests "
                "[--iterations N] [--seed N] [--duration-seconds N]\n");
            return 12;
        }
    }
    if (options.iterations == 0u && options.durationSeconds == 0u) {
        std::fprintf(stderr, "iterations must be greater than zero\n");
        return 12;
    }
    return RandomizedDifferentialGate{}.run(options);
}
