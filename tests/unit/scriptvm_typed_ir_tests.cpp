#include "scriptvm/compile/frontend.h"
#include "scriptvm/ir/typed_ir.h"

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

TypedIrModule buildModule()
{
    auto frontend = ScriptFrontend::compile("def add(left: int, right: int) -> int:\n"
                                            "    return left + right\n"
                                            "def invoke(value: int) -> int:\n"
                                            "    return add(value, 2)\n",
        "typed-ir-v7.arp",
        "typed-ir-v7");
    require(frontend.ok(), "frontend accepts typed IR fixture");
    auto lowered = ScriptLowerer::lower(frontend.module);
    require(lowered.ok(), "frontend lowers fixture to typed IR");
    return std::move(lowered.module);
}

TypedIrOperationRecord* findOperation(TypedIrModule& module, TypedIrOpcode opcode)
{
    for (auto& function : module.functions)
        for (auto& block : function.blocks)
            for (auto& operation : block.operations)
                if (operation.opcode == opcode)
                    return &operation;
    return nullptr;
}

void testVersionAndCanonicalFingerprint()
{
    auto module = buildModule();
    require(module.typedIrVersion == 7u, "frontend emits Typed IR v7");
    require(module.fingerprint == computeTypedIrFingerprint(module),
        "v7 canonical fingerprint is stable");
    require(validateTypedIr(module).ok(), "v7 module validates");

    auto changed = module;
    auto* operation = findOperation(changed, TypedIrOpcode::LoadInt);
    require(operation != nullptr, "fixture contains LoadInt");
    operation->opcode = TypedIrOpcode::LoadBool;
    operation->operation = typedIrOperationDescriptor(operation->opcode).operation;
    operation->callKind = typedIrOperationDescriptor(operation->opcode).callKind;
    operation->effectFlags = typedIrOperationDescriptor(operation->opcode).requiredEffects;
    require(computeTypedIrFingerprint(changed) != module.fingerprint,
        "opcode identity participates in fingerprint");

    module.typedIrVersion = 6u;
    module.fingerprint = computeTypedIrFingerprint(module);
    require(!validateTypedIr(module).ok(), "pre-v7 module is rejected");
}

void testOpcodeDescriptorContract()
{
    for (std::uint32_t value = 0u; value < static_cast<std::uint32_t>(TypedIrOpcode::OpcodeCount);
        ++value) {
        const auto opcode = static_cast<TypedIrOpcode>(value);
        const auto& descriptor = typedIrOperationDescriptor(opcode);
        require(descriptor.name != nullptr && descriptor.name[0] != '\0',
            "every opcode has a stable descriptor name");
        require(typedIrOpcodeName(opcode) == descriptor.name,
            "descriptor and canonical opcode name agree");
    }
    require(typedIrOperationDescriptor(TypedIrOpcode::Call).callKind == TypedIrCallKind::Direct,
        "direct call descriptor is canonical");
    require(typedIrOperationDescriptor(TypedIrOpcode::Await).suspension,
        "await descriptor records suspension");
    require(typedIrOperationDescriptor(TypedIrOpcode::JumpIfFalse).conditionalBranch,
        "conditional branch descriptor is canonical");
    const auto& call = typedIrOperationDescriptor(TypedIrOpcode::Call);
    require((call.operandFields & static_cast<std::uint16_t>(TypedIrOperandField::FunctionTargetId))
            != 0u,
        "direct call descriptor requires a stable function target");
    require(call.stackInputs == kTypedIrDynamicStackEffect
            && call.stackOutputs == kTypedIrDynamicStackEffect,
        "variable-arity call has an explicit dynamic stack contract");
    const auto& add = typedIrOperationDescriptor(TypedIrOpcode::IntAdd);
    require(add.stackInputs == 2 && add.stackOutputs == 1,
        "typed integer add has a fixed stack contract");
    const auto& branch = typedIrOperationDescriptor(TypedIrOpcode::JumpIfLocalIntLtConstFalse);
    require(
        (branch.operandFields & static_cast<std::uint16_t>(TypedIrOperandField::BranchTarget)) != 0u
            && branch.stackInputs == 0 && branch.stackOutputs == 0,
        "typed local branch declares operands without consuming the stack");
}

void testStableFunctionReferences()
{
    auto module = buildModule();
    auto* call = findOperation(module, TypedIrOpcode::Call);
    require(call != nullptr, "fixture contains direct call");
    require(call->targetId == makeTypedIrFunctionId(module.moduleId, "add"),
        "direct call carries stable target id");

    call->targetId = "arp-fn:typed-ir-v7/missing";
    module.fingerprint = computeTypedIrFingerprint(module);
    require(!validateTypedIr(module).ok(), "unknown function target is rejected");
}

void testCfgAndDescriptorMismatchReject()
{
    auto module = buildModule();
    module.functions.front().blocks.front().successors.push_back("missing-block");
    module.fingerprint = computeTypedIrFingerprint(module);
    require(!validateTypedIr(module).ok(), "unknown CFG successor is rejected");

    module = buildModule();
    auto* operation = findOperation(module, TypedIrOpcode::Call);
    require(operation != nullptr, "descriptor mismatch fixture contains Call");
    operation->operation = TypedIrOperation::Host;
    module.fingerprint = computeTypedIrFingerprint(module);
    require(
        !validateTypedIr(module).ok(), "operation metadata cannot disagree with opcode descriptor");
}

} // namespace

int main()
{
    testVersionAndCanonicalFingerprint();
    testOpcodeDescriptorContract();
    testStableFunctionReferences();
    testCfgAndDescriptorMismatchReject();
    std::puts("ScriptVM Typed IR v7 tests passed");
    return 0;
}
