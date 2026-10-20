#include "scriptvm/interpreter/bytecode_lowerer.h"
#include "scriptvm/interpreter/pipeline.h"
#include "scriptvm/interpreter/vm.h"

#include <cstdio>
#include <cstdlib>

namespace {

using namespace arp::scriptvm;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

arp::core::pmr::String hash(char digit)
{
    arp::core::pmr::String value("sha256:", &arp::core::defaultCpuMemoryResource());
    value.append(64u, digit);
    return value;
}

ScriptInterpreterBuildRequest request(bool withEntry = true)
{
    ScriptInterpreterBuildRequest value;
    value.source = "var base: int = 40\n"
                   "def add(value: int) -> int:\n"
                   "    return base + value\n"
                   "def invoke(value: int) -> int:\n"
                   "    if value > 0:\n"
                   "        return add(value)\n"
                   "    return 0\n";
    value.filename = "bytecode-lowering.arp";
    value.moduleId = "bytecode-lowering";
    value.moduleVersion = 3u;
    value.dependencyGraphHash = hash('1');
    value.reflectionRegistryHash = hash('2');
    if (withEntry)
        value.entryFunctionId = "arp-fn:bytecode-lowering/invoke";
    return value;
}

ScriptInterpreterBuildRequest requestFor(const char* source, const char* moduleId)
{
    auto value = request(false);
    value.source = source;
    value.filename = moduleId;
    value.filename += ".arp";
    value.moduleId = moduleId;
    return value;
}

void testPipelineBuildsVersionedProgram()
{
    const auto built = ScriptInterpreterPipeline::build(request());
    require(built.ok(), "source pipeline lowers through typed IR");
    require(built.typedIr.moduleVersion == 3u, "pipeline preserves explicit module version");
    require(built.program.bytecodeVersion == kInterpreterBytecodeVersion,
        "program records bytecode version");
    require(built.program.artifactIdentity.typedIrFingerprint == built.typedIr.fingerprint,
        "program identity is bound to typed IR");
    require(built.receipt.bytecodeFingerprint == built.program.bytecodeFingerprint,
        "lowering receipt matches emitted program");
    require(built.program.entry >= 0, "explicit entry id resolves");
}

void testNoEntryDoesNotSynthesizeOne()
{
    const auto built = ScriptInterpreterPipeline::build(request(false));
    require(built.ok(), "pipeline permits function-only modules");
    require(built.program.entry == -1, "pipeline does not synthesize an entry");
}

void testInvalidInputHashesFailClosed()
{
    auto invalid = request();
    invalid.dependencyGraphHash = "not-a-sha256";
    const auto built = ScriptInterpreterPipeline::build(invalid);
    require(!built.ok() && built.error.code == ScriptDiagnosticCode::TypedIrInvalid,
        "pipeline rejects non-contract dependency hash");
}

void testIdentityMismatchFailsClosed()
{
    const auto built = ScriptInterpreterPipeline::build(request());
    require(built.ok(), "identity fixture builds");
    InterpreterBytecodeOptions options;
    options.artifactIdentity = built.program.artifactIdentity;
    options.artifactIdentity.moduleVersion += 1u;
    options.entryFunctionId = "arp-fn:bytecode-lowering/invoke";
    const auto lowered = InterpreterBytecodeLowerer::lower(built.typedIr, options);
    require(!lowered.ok() && lowered.error.code == ScriptDiagnosticCode::AotFingerprintMismatch,
        "lowerer rejects identity mismatch");
    require(lowered.program.functions.empty(), "failed lowering publishes no program");
}

void testDamagedBytecodeFailsClosedAtLoad()
{
    const auto built = ScriptInterpreterPipeline::build(request());
    require(built.ok(), "damaged bytecode fixture builds");

    auto damagedVersion = built.program;
    ++damagedVersion.bytecodeVersion;
    require(
        !validateInterpreterBytecode(damagedVersion).ok(), "unknown bytecode version is rejected");
    Vm vm;
    VmError error;
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeInt(2));
    (void)vm.runFunctionByName(damagedVersion, "invoke", args, &error);
    require(error.code == ScriptDiagnosticCode::TypedIrInvalid,
        "VM rejects a damaged version before execution");

    auto damagedFingerprint = built.program;
    damagedFingerprint.bytecodeFingerprint.back() =
        damagedFingerprint.bytecodeFingerprint.back() == '0' ? '1' : '0';
    require(!validateInterpreterBytecode(damagedFingerprint).ok(),
        "damaged bytecode fingerprint is rejected");

    auto damagedBranch = built.program;
    bool changedBranch = false;
    bool changedFunction = false;
    for (auto& function : damagedBranch.functions) {
        for (auto& instruction : function.code) {
            if (!changedBranch
                && (instruction.op == Opcode::Jump || instruction.op == Opcode::JumpIfFalse
                    || instruction.op == Opcode::JumpIfTrue)) {
                instruction.a = static_cast<int>(function.code.size());
                changedBranch = true;
            }
            if (!changedFunction && instruction.op == Opcode::Call) {
                instruction.a = static_cast<int>(damagedBranch.functions.size());
                changedFunction = true;
            }
        }
    }
    require(
        changedBranch && changedFunction, "damaged fixture contains branch and function targets");
    require(!validateInterpreterBytecode(damagedBranch).ok(),
        "invalid branch/function mappings are rejected");

    auto damagedHostBinding = built.program;
    require(
        !damagedHostBinding.functions.empty() && !damagedHostBinding.functions.front().code.empty(),
        "host binding corruption fixture contains bytecode");
    auto& hostInstruction = damagedHostBinding.functions.front().code.front();
    hostInstruction.op = Opcode::CallHost;
    hostInstruction.a = 0;
    hostInstruction.s.clear();
    require(!validateInterpreterBytecode(damagedHostBinding).ok(),
        "host calls without a stable binding id are rejected");

    auto damagedSourceMap = built.program;
    damagedSourceMap.functions.front().code.front().pos.line += 1;
    require(!validateInterpreterBytecode(damagedSourceMap).ok(),
        "source position changes invalidate the bytecode fingerprint");
}

void testStructuredMetadataIsMappedAndValidated()
{
    const auto built =
        ScriptInterpreterPipeline::build(requestFor("var root: int = 1\n"
                                                    "class Box:\n"
                                                    "    var value: int\n"
                                                    "    def get(self) -> int:\n"
                                                    "        return self.value\n"
                                                    "def make_counter(start: int) -> () -> int:\n"
                                                    "    var count: int = start\n"
                                                    "    def next() -> int:\n"
                                                    "        count = count + 1\n"
                                                    "        return count\n"
                                                    "    return next\n"
                                                    "def guarded(value: any) -> any:\n"
                                                    "    try:\n"
                                                    "        return await value\n"
                                                    "    except as error:\n"
                                                    "        return error\n"
                                                    "def delegated(source: any) -> any:\n"
                                                    "    yield from source\n"
                                                    "    return nil\n",
            "bytecode-metadata"));
    if (!built.ok())
        std::fprintf(stderr, "structured metadata diagnostic: %s\n", built.error.message.c_str());
    require(built.ok(), "structured metadata fixture lowers");
    require(built.program.globalSlots.size() == 1u
            && built.program.globalSlots.find("root") != built.program.globalSlots.end(),
        "global slot contract is copied");
    require(!built.program.classSchemas.empty() && !built.program.methodSets.empty(),
        "class schema and method targets are copied");
    require(built.program.functionIdIndex.size() == built.program.functions.size(),
        "stable function id index is complete");

    const Function* guarded = nullptr;
    const Function* delegated = nullptr;
    const Function* closure = nullptr;
    for (const auto& function : built.program.functions) {
        if (function.name == "guarded")
            guarded = &function;
        if (function.name == "delegated")
            delegated = &function;
        if (function.captureCount != 0)
            closure = &function;
    }
    require(
        guarded != nullptr && !guarded->safepoints.empty() && !guarded->exceptionRegions.empty(),
        "await and exception metadata map to bytecode");
    require(delegated != nullptr && delegated->hasYieldFrom && !delegated->delegationSites.empty(),
        "delegation metadata maps to bytecode");
    require(closure != nullptr, "closure capture count maps to bytecode");

    auto damagedSafepoint = built.program;
    for (auto& function : damagedSafepoint.functions) {
        if (!function.safepoints.empty()) {
            function.safepoints.front().pc = static_cast<int>(function.code.size());
            break;
        }
    }
    require(!validateInterpreterBytecode(damagedSafepoint).ok(),
        "invalid safepoint metadata is rejected");

    auto damagedException = built.program;
    for (auto& function : damagedException.functions) {
        if (!function.exceptionRegions.empty()) {
            function.exceptionRegions.front().handlerPc = static_cast<int>(function.code.size());
            break;
        }
    }
    require(!validateInterpreterBytecode(damagedException).ok(),
        "invalid exception metadata is rejected");

    auto damagedDelegation = built.program;
    for (auto& function : damagedDelegation.functions) {
        if (!function.delegationSites.empty()) {
            function.delegationSites.front().valueSlot = function.locals;
            break;
        }
    }
    require(!validateInterpreterBytecode(damagedDelegation).ok(),
        "invalid delegation metadata is rejected");
}

void testVariadicSignatureUsesFixedParameterContracts()
{
    const auto built =
        ScriptInterpreterPipeline::build(requestFor("def collect(head: int, *tail: int) -> int:\n"
                                                    "    return head\n",
            "bytecode-variadic"));
    require(built.ok(), "variadic signature lowers to bytecode");
    const auto found = built.program.functionIdIndex.find("arp-fn:bytecode-variadic/collect");
    require(found != built.program.functionIdIndex.end(),
        "variadic fixture preserves the stable function id");
    const auto& function = built.program.functions[static_cast<std::size_t>(found->second)];
    require(function.isVariadic && function.params == 2 && function.fixedParams == 1,
        "variadic bytecode preserves fixed and storage parameter counts");
    require(function.paramTypes.size() == 1u && function.variadicElementType == RuntimeType::Int,
        "variadic bytecode stores fixed and element type contracts separately");

    auto damaged = built.program;
    damaged.functions[static_cast<std::size_t>(found->second)].params = 3;
    require(!validateInterpreterBytecode(damaged).ok(),
        "variadic bytecode rejects an extra implicit parameter slot");
}

} // namespace

int main()
{
    testPipelineBuildsVersionedProgram();
    testNoEntryDoesNotSynthesizeOne();
    testInvalidInputHashesFailClosed();
    testIdentityMismatchFailsClosed();
    testDamagedBytecodeFailsClosedAtLoad();
    testStructuredMetadataIsMappedAndValidated();
    testVariadicSignatureUsesFixedParameterContracts();
    std::puts("ScriptVM bytecode lowering tests passed");
    return 0;
}
