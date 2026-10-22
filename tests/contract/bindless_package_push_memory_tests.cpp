#include "../fixtures/bindless_graphics_push_package_fixture.h"
#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/original_graphics_push_consumption_fixture.h"
#include "core/io/file_io.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/command_push_consumption.h"
#include "rhi/frontend/command_push_upload.h"
#include "rhi/frontend/pipeline_bindless_interface_merger.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/semantics/bindless_push_consumption_lowering.h"

#include <array>
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Push package memory tests require assertions
#endif

namespace {
void verifyPublicUploadAdmission()
{
    using namespace arp;
    using K = rhi::CommandUsageKind;
    using Contract = rhi::OriginalBindlessRootPushUploadContract;
    using Program = rhi::OriginalBindlessRootProgramContract;
    const Contract::Budget upload{1u, true, 2u, 448u, 512u, 2u, 1u};
    rhi::OriginalBindlessRootExecutionBudget budget{{2u, true},
        {4u, true, 8u, 8u, 128u, 65536u, 0u, 0u, 1u},
        {2u,
            true,
            16u,
            32u,
            128u,
            65536u,
            {1u, true, 1u, 1u, 1u, 4u, 4u, 65536u},
            {1u, true, 1u, 1u, 3u, 12u, 12u}},
        {12u, true, 1u, 12u, 12u, 128u, 128u, 128u, 65536u, 65536u, 65536u},
        {3u, true, 1u, 4u, 65536u, 128u},
        {2u, true, 128u, 65536u, {2u, true, 16u, 128u}},
        {1u, true, 1u, 12u, 128u, 65536u, 65536u},
        {1u, true, 1u, 1u, 128u, 65536u, 65536u, 65536u}};
    assert(!budget.validate());
    assert(Program::validateOperation(K::PushConstants, budget.command.program));
    assert(Program::validateOperation(K::PushConstants, 2u));
    budget.admission.pushUpload = budget.command.program.pushUpload = upload;
    assert(!budget.validate());
    assert(!Program::validateOperation(K::PushConstants, budget.command.program));
    assert(!Program::validateOperation(K::Draw, budget.command.program));
    assert(Program::validateOperation(K::Dispatch, budget.command.program));
    assert(Program::validateOperation(K::DrawIndexed, budget.command.program));
    for (const auto version : {0u, 1u, 3u, 4u, 5u, 6u}) {
        auto request = budget.command.program;
        request.contractVersion = version;
        assert(Program::validateIntent(request));
        assert(Program::validateOperation(K::PushConstants, request));
        auto admission = budget.admission;
        admission.contractVersion = version;
        assert(rhi::OriginalBindlessRootCallbackColdContract::validateIntent(admission));
    }
    auto shortCommands = budget.command.program;
    shortCommands.maxCommands = 1u;
    assert(Program::validateIntent(shortCommands));
    rhi::OriginalBindlessRootColdCommandRequest oldShell{1u, true, 128u, 65536u};
    oldShell.program.pushUpload = upload;
    assert(!oldShell.program.empty());
    assert(rhi::OriginalBindlessRootColdCommandContract::validateIntent(oldShell));
    for (unsigned field = 0u; field < 7u; ++field) {
        auto changed = upload;
        switch (field) {
        case 0u:
            ++changed.contractVersion;
            break;
        case 1u:
            changed.prototypeOptIn = false;
            break;
        case 2u:
            ++changed.maxUpdates;
            break;
        case 3u:
            ++changed.maxPayloadBytes;
            break;
        case 4u:
            ++changed.maxResidentBytes;
            break;
        case 5u:
            ++changed.maxNativePayloadObjects;
            break;
        case 6u:
            ++changed.maxRetainedOwners;
            break;
        }
        assert(!upload.matches(changed));
        assert(Contract::validateCorrespondence(upload, changed));
        auto mismatch = budget;
        mismatch.admission.pushUpload = changed;
        assert(mismatch.validate());
    }
    auto missing = budget;
    missing.admission.pushUpload = {};
    assert(missing.validate());
    missing = budget;
    missing.command.program.pushUpload = {};
    assert(missing.validate());
    assert(!Contract::validateCorrespondence({}, {}));

    rhi::OriginalCommandAdmissionBudget source;
    source.maxNativePayloadObjects = 10u;
    source.maxGraphOwners = 20u;
    auto lifecycle = budget.lifecycle;
    lifecycle.maxRecordingOwners = lifecycle.maxSubmissionOwners = lifecycle.maxCallbackOwners =
        20u;
    const auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            Contract::Footprint output;
            assert(!Contract::validateFootprint(
                upload, {8u, 19u}, {2u, 1u}, source, lifecycle, 20u, output));
            assert(output.nativePayloadObjects == 10u && output.retainedOwners == 20u);
            for (unsigned field = 0u; field < 12u; ++field) {
                auto shortSource = source;
                auto shortLifecycle = lifecycle;
                auto additional = Contract::Footprint{2u, 1u};
                auto original = Contract::Footprint{8u, 19u};
                std::uint32_t ownerLimit = 20u;
                switch (field) {
                case 0u:
                    --shortSource.maxNativePayloadObjects;
                    break;
                case 1u:
                    --shortSource.maxGraphOwners;
                    break;
                case 2u:
                    --shortLifecycle.maxRecordingOwners;
                    break;
                case 3u:
                    --shortLifecycle.maxSubmissionOwners;
                    break;
                case 4u:
                    --shortLifecycle.maxCallbackOwners;
                    break;
                case 5u:
                    --ownerLimit;
                    break;
                case 6u:
                    additional.nativePayloadObjects = 0u;
                    break;
                case 7u:
                    additional.retainedOwners = 0u;
                    break;
                case 8u:
                    ++additional.nativePayloadObjects;
                    break;
                case 9u:
                    ++additional.retainedOwners;
                    break;
                case 10u:
                    original.nativePayloadObjects = UINT32_MAX;
                    break;
                case 11u:
                    original.retainedOwners = UINT32_MAX;
                    break;
                }
                output = {123u, 456u};
                assert(Contract::validateFootprint(
                    upload, original, additional, shortSource, shortLifecycle, ownerLimit, output));
                assert(!output.nativePayloadObjects && !output.retainedOwners);
            }
            return nullptr;
        }));
    std::puts(
        "ORIGINAL_ROOT_PUSH_UPLOAD_ADMISSION1_OK explicit-budget correspondence exact-less footprint hot0 no-execution-grant");
}

void verifyUploadPlan()
{
    using namespace arp;
    using Plan = rhi::OriginalCommandPushUploadPlan;
    using Lowering = rhi::semantics::OriginalBindlessPushConsumptionLowering;
    const Plan::Budget exact{1u, true, 2u, 448u, 512u, 2u, 1u};
    const Plan::Limits limits{4096u, 256u, 256u};
    assert(!exact.diagnostic() && !exact.empty());
    assert(Plan::Budget{}.empty() && Plan::Budget{}.diagnostic());
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto decision = Lowering::upload(backend, 1u);
        assert(
            decision.projectionAvailable && !decision.executionAvailable && !decision.diagnostic);
        assert(decision.validatorOnly == (backend == rhi::Backend::Null));
        assert(Lowering::upload(backend, 0u).diagnostic);
        assert(Lowering::upload(backend, 2u).diagnostic);
    }
    assert(Lowering::upload(rhi::Backend::WebGL, 1u).diagnostic);
    const auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            Plan plan(exact, limits);
            Plan::Slice slice;
            assert(plan.validateComplete(2u, 1u));
            assert(!plan.append(224u, 256u, slice));
            assert(!slice.dynamicOffset && slice.writeEnd == 224u && slice.bindingEnd == 256u);
            assert(!plan.append(224u, 256u, slice));
            assert(
                slice.dynamicOffset == 256u && slice.writeEnd == 480u && slice.bindingEnd == 512u);
            assert(plan.updates() == 2u && plan.payloadBytes() == 448u
                && plan.residentBytes() == 512u);
            assert(!plan.validateComplete(2u, 1u));
            assert(plan.validateComplete(3u, 1u) && plan.validateComplete(2u, 2u));
            assert(plan.validateComplete(0u, 1u) && plan.validateComplete(2u, 0u));
            assert(plan.append(224u, 256u, slice) && !slice.bindingEnd);
            assert(plan.updates() == 2u && plan.payloadBytes() == 448u
                && plan.residentBytes() == 512u);

            // 480 fits both writes but not the last 256-byte binding. A failed
            // append must leave the first complete slice/counters unchanged.
            auto shortBudget = exact;
            shortBudget.maxResidentBytes = 480u;
            Plan shortPlan(shortBudget, limits);
            assert(!shortPlan.append(224u, 256u, slice));
            assert(shortPlan.append(224u, 256u, slice) && !slice.writeEnd && !slice.bindingEnd);
            assert(shortPlan.updates() == 1u && shortPlan.payloadBytes() == 224u
                && shortPlan.residentBytes() == 256u);
            assert(!shortPlan.append(224u, 224u, slice));
            assert(slice.dynamicOffset == 256u && slice.bindingEnd == 480u);

            for (const auto invalid : {0u, 220u, 226u, 260u}) {
                Plan bad(exact, limits);
                assert(bad.append(224u, invalid, slice) && !slice.bindingEnd && !bad.updates());
            }
            for (const auto invalid : {0u, 223u, 260u}) {
                Plan bad(exact, limits);
                assert(bad.append(invalid, 256u, slice) && !slice.bindingEnd && !bad.updates());
            }
            for (auto invalid : {Plan::Limits{0u, 256u, 256u},
                     Plan::Limits{4096u, 0u, 256u},
                     Plan::Limits{4096u, 256u, 0u},
                     Plan::Limits{4096u, 256u, 192u},
                     Plan::Limits{511u, 256u, 256u},
                     Plan::Limits{4096u, 223u, 256u}}) {
                Plan bad(exact, invalid);
                assert(bad.append(224u, 256u, slice) && !slice.bindingEnd && !bad.updates());
            }
            for (unsigned field = 0u; field < 8u; ++field) {
                auto invalid = exact;
                switch (field) {
                case 0u:
                    invalid.contractVersion = 2u;
                    break;
                case 1u:
                    invalid.prototypeOptIn = false;
                    break;
                case 2u:
                    invalid.maxUpdates = 0u;
                    break;
                case 3u:
                    invalid.maxPayloadBytes = 0u;
                    break;
                case 4u:
                    invalid.maxResidentBytes = 0u;
                    break;
                case 5u:
                    invalid.maxNativePayloadObjects = 0u;
                    break;
                case 6u:
                    invalid.maxRetainedOwners = 0u;
                    break;
                case 7u:
                    invalid.maxPayloadBytes = 223u;
                    break;
                }
                Plan bad(invalid, limits);
                assert(bad.append(224u, 256u, slice) && !slice.writeEnd && !bad.updates());
            }
            // The native dynamic offset ABI is uint32, even when a device
            // permits a larger buffer. Overflow and unknown limits refuse.
            const Plan::Limits large{UINT64_MAX, 256u, 256u};
            assert(rhi::semantics::planPushConstantUploadSlice(
                       UINT64_MAX, 224u, 256u, UINT64_MAX, large, slice)
                && !slice.bindingEnd);
            assert(rhi::semantics::planPushConstantUploadSlice(
                       UINT32_MAX, 224u, 256u, UINT64_MAX, large, slice)
                && !slice.bindingEnd);
            assert(!rhi::semantics::planPushConstantUploadSlice(
                std::uint64_t(UINT32_MAX) - 255u, 224u, 256u, UINT64_MAX, large, slice));
            assert(slice.dynamicOffset == UINT32_MAX - 255u
                && slice.bindingEnd == std::uint64_t(UINT32_MAX) + 1u);
            return nullptr;
        }));
    std::puts(
        "ORIGINAL_PUSH_UPLOAD_PLAN1_OK queried alignment binding-tail payload resident owners native hot0 no-execution");
}

void verifyFrozenConsumption(
    const arp::rhi::ShaderPackageDesc& shader, const arp::rhi::PipelineLayoutDesc& layout)
{
    using namespace arp;
    using K = rhi::CommandUsageKind;
    using Scalar = rhi::CommandScalarArguments;
    using Arguments = rhi::CommandVariableArguments;
    using Consumer = rhi::OriginalCommandPushConsumption;
    constexpr auto stages = static_cast<rhi::ShaderStage>(
        std::uint32_t(rhi::ShaderStage::Vertex) | std::uint32_t(rhi::ShaderStage::Fragment));
    std::array<unsigned char, 224u> authored;
    for (std::size_t i = 0u; i < authored.size(); ++i)
        authored[i] = static_cast<unsigned char>(i);
    const auto expected = authored;
    auto secondAuthored = authored;
    for (auto& byte : secondAuthored)
        byte ^= 0xA5u;
    const auto secondExpected = secondAuthored;
    const std::array pushes{Scalar::Push{stages, 0u, 224u},
        Scalar::Push{stages, 0u, 224u},
        Scalar::Push{rhi::ShaderStage::None, 0u, 224u},
        Scalar::Push{stages, 4u, 224u},
        Scalar::Push{stages, 0u, 220u},
        Scalar::Push{rhi::ShaderStage::Vertex, 0u, 224u},
        Scalar::Push{rhi::ShaderStage::Compute, 0u, 224u}};
    core::pmr::Vector<Arguments::Input> fields;
    std::uint64_t totalBytes = 0u;
    for (const auto& push : pushes) {
        fields.push_back({*Scalar::capture(K::PushConstants, push),
            {},
            nullptr,
            0u,
            fields.size() == 1u ? secondAuthored.data() : authored.data(),
            push.size});
        totalBytes += push.size;
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(16u * 1024u);
    const char* diagnostic = nullptr;
    auto arguments = Arguments::prepareOriginal(fields.data(),
        fields.size(),
        {1u,
            true,
            static_cast<std::uint32_t>(fields.size()),
            0u,
            0u,
            totalBytes,
            quota->limitBytes()},
        quota,
        diagnostic);
    assert(arguments && !diagnostic);
    authored.fill(0u);
    secondAuthored.fill(0u);
    fields.clear();
    rhi::OriginalCommandPushConsumptionBudget budget{1u, true, 2u, 448u};
    auto forged = std::shared_ptr<const Arguments>(quota, arguments.get());
    auto malformed = rhi::ShaderPackageStateCopy::copy(shader);
    malformed.reflection.pushConstantStages.assign(256u, 'X');
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto lowering =
            rhi::semantics::OriginalBindlessPushConsumptionLowering::lowering(backend, 1u);
        assert(
            lowering.projectionAvailable && !lowering.executionAvailable && !lowering.diagnostic);
    }
    assert(
        rhi::semantics::OriginalBindlessPushConsumptionLowering::lowering(rhi::Backend::WebGL, 1u)
            .diagnostic);
    assert(
        rhi::semantics::OriginalBindlessPushConsumptionLowering::lowering(rhi::Backend::Vulkan, 2u)
            .diagnostic);
    using Recording = rhi::semantics::OriginalBindlessPushConsumptionLowering;
    assert(Recording::recording(rhi::Backend::Vulkan, 1u).executionAvailable);
    assert(Recording::recording(rhi::Backend::Metal, 1u).executionAvailable);
    assert(Recording::recording(rhi::Backend::WebGPU, 1u).executionAvailable);
    for (const auto backend : {rhi::Backend::Null, rhi::Backend::WebGL}) {
        const auto result = Recording::recording(backend, 1u);
        assert(!result.executionAvailable && result.diagnostic);
    }
    assert(Recording::recording(rhi::Backend::Vulkan, 0u).diagnostic);
    assert(!Recording::validateNativeSize(224u, 224u, 224u));
    assert(Recording::validateNativeSize(224u, 223u, 256u));
    assert(Recording::validateNativeSize(224u, 224u, 128u));
    assert(Recording::validateNativeSize(224u, 224u, 0u));
    assert(Recording::validateNativeSize(223u, 224u, 256u));
    rhi::BindlessGraphicsPushRequest pipelineRequest{1u, true, 224u};
    assert(!pipelineRequest.diagnostic());
    pipelineRequest.contractVersion = 2u;
    assert(pipelineRequest.diagnostic());
    pipelineRequest = {1u, false, 224u};
    assert(pipelineRequest.diagnostic());
    pipelineRequest = {1u, true, 257u};
    assert(pipelineRequest.diagnostic());
    auto recordingBudget = budget;
    recordingBudget.recordingContractVersion = 1u;
    assert(recordingBudget.diagnostic() && !recordingBudget.matches(budget));
    recordingBudget.recordingOptIn = true;
    assert(!recordingBudget.diagnostic() && !recordingBudget.empty());
    recordingBudget.recordingContractVersion = 2u;
    assert(recordingBudget.diagnostic());
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            Consumer consumer;
            rhi::OriginalCommandPushUploadPlan upload(
                {1u, true, 2u, 448u, 512u, 2u, 1u}, {4096u, 256u, 256u});
            rhi::OriginalCommandPushUploadPlan::Slice slice;
            rhi::PushConstantsDesc block;
            assert(consumer.consume(shader.reflection, budget));
            assert(consumer.update(forged, 0u, layout, shader.reflection, stages, budget, block)
                && !block.data && !consumer.updates());
            for (std::size_t i = 2u; i < pushes.size(); ++i)
                assert(
                    consumer.update(arguments, i, layout, shader.reflection, stages, budget, block)
                    && !block.data && !consumer.updates());
            assert(consumer.update(
                arguments, pushes.size(), layout, shader.reflection, stages, budget, block));
            assert(consumer.update(
                arguments, 0u, layout, malformed.reflection, stages, budget, block));
            auto bad = budget;
            bad.contractVersion = 2u;
            assert(consumer.update(arguments, 0u, layout, shader.reflection, stages, bad, block));
            bad = budget;
            bad.prototypeOptIn = false;
            assert(consumer.update(arguments, 0u, layout, shader.reflection, stages, bad, block));
            bad = budget;
            bad.maxBytes = 223u;
            assert(consumer.update(arguments, 0u, layout, shader.reflection, stages, bad, block));
            assert(
                !consumer.update(arguments, 0u, layout, shader.reflection, stages, budget, block));
            assert(block.data == arguments->bytes(0u) && block.sizeBytes == expected.size()
                && !std::memcmp(block.data, expected.data(), expected.size()));
            assert(!upload.append(block.sizeBytes, 256u, slice) && !slice.dynamicOffset);
            assert(consumer.update(arguments, 1u, layout, shader.reflection, stages, budget, block)
                && !block.data && consumer.updates() == 1u && consumer.bytes() == 224u);
            assert(!consumer.consume(shader.reflection, budget));
            assert(!consumer.consume(
                shader.reflection, budget)); // Unchanged bound bytes remain valid.
            consumer.onBind();
            assert(consumer.consume(shader.reflection, budget));
            assert(
                !consumer.update(arguments, 1u, layout, shader.reflection, stages, budget, block));
            assert(!upload.append(block.sizeBytes, 256u, slice) && slice.dynamicOffset == 256u);
            assert(!upload.validateComplete(2u, 1u) && upload.payloadBytes() == consumer.bytes()
                && upload.residentBytes() == 512u && block.data == arguments->bytes(1u));
            assert(!std::memcmp(block.data, secondExpected.data(), secondExpected.size())
                && std::memcmp(arguments->bytes(0u), block.data, secondExpected.size()));
            assert(!consumer.consume(shader.reflection, budget));
            assert(consumer.update(arguments, 0u, layout, shader.reflection, stages, budget, block)
                && !block.data && consumer.updates() == 2u && consumer.bytes() == 448u);
            Consumer sequential;
            assert(!sequential.update(
                arguments, 0u, layout, shader.reflection, stages, budget, block));
            assert(!sequential.consume(shader.reflection, budget));
            assert(!sequential.update(
                arguments, 1u, layout, shader.reflection, stages, budget, block));
            assert(!sequential.consume(shader.reflection, budget) && sequential.updates() == 2u
                && sequential.bytes() == 448u);
            return nullptr;
        }));
    forged.reset();
    arguments.reset();
    assert(quota->liveBytes() == 0u);
    std::puts(
        "ORIGINAL_PUSH_CONSUMPTION1_OK frozen224 owner full-stage duplicate budget hot0 no-execution");
}

void verifyComposition(const std::shared_ptr<const arp::rhi::BindlessShaderPackage>& package)
{
    using namespace arp;
    using Selection = rhi::BindlessGraphicsPipelineEntries;
    core::CpuMemoryQuotaScope scope(package->quota());
    rhi::ShaderPackageDesc shader;
    rhi::PipelineLayoutDesc layout;
    Selection entries;
    shader::TargetPushMemoryLayout target;
    const bool sourceOnly = package->artifact().execution == rhi::BindlessExecution::Emulated
        && rhi::BindlessShaderPackage::originalTargetPushMemory(package, "vertexMain", target);
    auto composed =
        Selection::compose(package, "vertexMain", "fragmentMain", shader, layout, entries);
    for (const auto stage : {rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment}) {
        const auto name = stage == rhi::ShaderStage::Vertex ? "vertexMain" : "fragmentMain";
        rhi::ShaderPackageDesc view;
        const rhi::BindlessPackageModule* module = nullptr;
        const auto result = Selection::composeStage(package, name, stage, view, module);
        assert(result.success != sourceOnly);
        if (sourceOnly) {
            assert(!module && view.entrypoints.empty());
            continue;
        }
        assert(module && view.entrypoints.size() == 1u
            && view.reflection.pushConstantByteCount == 224u);
        assert(view.reflection.pushConstantStages
            == (stage == rhi::ShaderStage::Vertex ? "VS" : "FS"));
        assert(!Selection::composeStage(package, name, stage, view, module, 2u).success && !module
            && view.entrypoints.empty());
        assert(!Selection::composeStage(package, "absent", stage, view, module).success && !module
            && view.entrypoints.empty());
        auto forged =
            std::shared_ptr<const rhi::BindlessShaderPackage>(package->quota(), package.get());
        assert(!Selection::composeStage(forged, name, stage, view, module).success && !module
            && view.entrypoints.empty());
        assert(
            !Selection::composeStage(package, name, rhi::ShaderStage::Compute, view, module).success
            && !module && view.entrypoints.empty());
    }
    if (sourceOnly) {
        assert(!composed.success
            && composed.diagnostics == "RHI-BINDLESS-GRAPHICS-PUSH-TARGET-PROOF"
            && shader.entrypoints.empty() && layout.pushConstantRanges.empty()
            && !entries.module(rhi::ShaderStage::Vertex, 1u));
        return;
    }
    if (!composed.success)
        std::fprintf(stderr, "Compose: %s\n", composed.diagnostics.c_str());
    assert(composed.success && shader.entrypoints.size() == 2u);
    if (package->artifact().execution == rhi::BindlessExecution::Native) {
        // PipelineInterface/1 explicitly maps a primary set even when there
        // are no ordinary domains. Compute's earlier ABI is unchanged.
        auto artifact = package->artifact();
        assert(artifact.set == 1u && artifact.additionalDomains.empty()
            && artifact.pipelineInterfaceContractVersion == 1u);
        assert(rhi_metal::validateMetalBindlessGraphicsArtifact(artifact).success);
        assert(!rhi_metal::validateMetalBindlessComputeArtifact(artifact).success);
        artifact.pipelineInterfaceContractVersion = 0u;
        assert(!rhi_metal::validateMetalBindlessGraphicsArtifact(artifact).success);
        artifact.pipelineInterfaceContractVersion = 2u;
        assert(!rhi_metal::validateMetalBindlessGraphicsArtifact(artifact).success);
    }
    assert(shader.reflection.pushConstantStages == "VS|FS"
        && shader.reflection.pushConstantByteCount == 224u
        && layout.pushConstantRanges.size() == 1u);
    const auto& range = layout.pushConstantRanges.front();
    assert(range.offsetBytes == 0u && range.sizeBytes == 224u
        && range.stages == (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment));
    assert(rhi::validatePipelineLayoutAgainstShaderReflection(layout, shader.reflection).success);
    verifyFrozenConsumption(shader, layout);
    if (package->artifact().execution == rhi::BindlessExecution::Native)
        test::validateOriginalBindlessRootSource(package,
            {},
            "unused",
            false,
            test::OriginalGraphicsPushConsumptionFixture{},
            test::OriginalGraphicsPushPipelineFixture{},
            false,
            "fragmentMain");
    rhi::RenderPipelineDesc desc;
    desc.vertexShader = shader.entrypoints[0];
    desc.fragmentShader = shader.entrypoints[1];
    Selection captured;
    assert(!Selection::capture(*package, desc, captured));
    for (const auto stage : {rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment})
        assert(entries.module(stage, 1u) == captured.module(stage, 1u));
    auto fragment = rhi::ShaderPackageStateCopy::copy(shader);
    fragment.entrypoints.erase(fragment.entrypoints.begin());
    fragment.reflection.pushConstantStages = "FS";
    rhi::ShaderPackageDesc vertex;
    rhi::PipelineLayoutDesc vertexLayout;
    assert(Selection::compose(package, "vertexMain", {}, vertex, vertexLayout, entries).success);
    assert(vertex.reflection.pushConstantStages == "VS"
        && vertexLayout.pushConstantRanges.size() == 1u
        && vertexLayout.pushConstantRanges[0].stages == rhi::ShaderStage::Vertex
        && !entries.module(rhi::ShaderStage::Fragment, 1u));
    rhi::ShaderPackageDesc merged;
    assert(rhi::mergeBindlessGraphicsShaderInterface(1u, vertex, fragment, merged).success);
    fragment.reflection.pushConstantMetalBufferIndex += 1u;
    assert(!rhi::mergeBindlessGraphicsShaderInterface(1u, vertex, fragment, merged).success
        && merged.entrypoints.empty());
    fragment.reflection.pushConstantMetalBufferIndex -= 1u;
    fragment.reflection.pushConstantMembers[0].arrayStride += 16u;
    assert(!rhi::mergeBindlessGraphicsShaderInterface(1u, vertex, fragment, merged).success
        && merged.entrypoints.empty());
    for (const auto version : {0u, 2u}) {
        assert(!Selection::compose(
                   package, "vertexMain", "fragmentMain", shader, layout, entries, version)
                   .success
            && shader.entrypoints.empty() && layout.pushConstantRanges.empty()
            && !entries.module(rhi::ShaderStage::Vertex, 1u));
    }
    auto forged =
        std::shared_ptr<const rhi::BindlessShaderPackage>(package->quota(), package.get());
    assert(!Selection::compose(forged, "vertexMain", {}, shader, layout, entries).success);
    assert(!Selection::compose(package, "fragmentMain", "vertexMain", shader, layout, entries)
            .success);
    assert(!Selection::compose(package, "vertexMain", "absent", shader, layout, entries).success);
    assert(shader.entrypoints.empty() && layout.pushConstantRanges.empty()
        && !entries.module(rhi::ShaderStage::Vertex, 1u));
    std::puts(
        "BINDLESS_GRAPHICS_PUSH_INTERFACE1_OK selected-stages full-block224 original-modules no-execution-grant");
}

void verify(const std::filesystem::path& publication, const char* mode)
{
    using namespace arp;
    using Package = rhi::BindlessShaderPackage;
    using Request = rhi::BindlessPackagePushLoadRequest;
    using Graph = shader::BufferDeclarationTypeLayouts;
    using MemoryRequest = shader::TargetBufferMemoryLoadRequest;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;
    test::BindlessGraphicsPushPackageFixture proof(publication, mode);
    auto& graphs = proof.graphs;
    auto& rules = proof.rules;
    auto& plans = proof.plans;
    auto& entries = proof.entries;
    auto& targets = proof.targets;
    auto& targetRequests = proof.targetRequests;
    auto& targetBindings = proof.targetBindings;
    auto& matrices = proof.matrices;
    auto& request = proof.request;
    constexpr auto names = test::BindlessGraphicsPushPackageFixture::names;
    constexpr auto graphBudget = test::BindlessGraphicsPushPackageFixture::graphBudget;
    rhi::BindlessPackageLoadBudget budget{1024u * 1024u, 4u * 1024u * 1024u, 32u, 4u, 16u, 4u};
    auto quota = core::makeShared<core::CpuMemoryQuota>(2u * 1024u * 1024u);
    core::pmr::String diagnostic;
    diagnostic.reserve(256u);
    const auto load = [&]() {
        core::CpuMemoryQuotaScope scope(quota);
        return Package::load(publication, budget, diagnostic);
    };
    assert(!load() && diagnostic == "RHI-BINDLESS-PACKAGE-ENTRY-ABI");
    budget.push = &request;
    auto value = load();
    if (!value)
        std::fprintf(stderr, "Push custody %s: %s\n", mode, diagnostic.c_str());
    assert(value && value->modules().size() == 2u);
    verifyComposition(value);
    const auto live = quota->liveBytes();
    const auto reject = [&](const char* expected) {
        assert(!load());
        if (diagnostic.find(expected) == diagnostic.npos)
            std::fprintf(stderr, "Expected %s got %s\n", expected, diagnostic.c_str());
        assert(diagnostic.find(expected) != diagnostic.npos && quota->liveBytes() == live);
    };
    request.contractVersion = 2u;
    reject("PUSH-REQUEST");
    request.contractVersion = 1u;
    request.maxBytes = 0u;
    reject("PUSH-REQUEST");
    request.maxBytes = 257u;
    reject("PUSH-REQUEST");
    request.maxBytes = 223u;
    reject("PUSH-BYTE-BUDGET");
    request.maxBytes = 224u;
    request.entries = nullptr;
    reject("PUSH-REQUEST");
    request.entries = entries.data();
    entries[1].entry = entries[0].entry;
    reject("PUSH-ENTRY-DUPLICATE");
    entries[1].entry = "absent";
    reject("PUSH-ENTRY");
    entries[1].entry = names[1];
    request.entryCount = 1u;
    reject("PUSH-COVERAGE");
    request.entryCount = entries.size();
    entries[0].binary = nullptr;
    reject("PUSH-BINARY-ARRAY");
    entries[0].binary = &plans[0];
    plans[0].contractVersion = 2u;
    reject("PUSH-BINARY-PLAN-VERSION");
    plans[0].contractVersion = 1u;
    plans[0].maxMembers = 0u;
    reject("PUSH-BINARY-PLAN-BUDGET");
    plans[0].maxMembers = 1u;
    const auto savedRule = rules[0];
    rules[0].sourceType = Graph::absent;
    reject("PUSH-BINARY-PLAN-FRAMING");
    rules[0] = savedRule;
    entries[0].sourceTypes.maxNodes = 1u;
    reject("NODE-BUDGET");
    entries[0].sourceTypes = graphBudget;

    // Queries consume only immutable original facts; caller plan mutations cannot
    // influence them. Every failure resets outputs and hot queries allocate zero.
    request.entries = nullptr;
    plans[0].members = nullptr;
    rhi::BindlessPackagePushDeclaration facts;
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            assert(!Package::originalPushDeclaration(value, names[0], facts));
            assert(facts.sizeBytes == 224u && facts.occupiedBytes == 224u
                && facts.stages == rhi::ShaderStage::Vertex);
            assert(!Package::originalPushDeclaration(value, names[1], facts)
                && facts.stages == rhi::ShaderStage::Fragment);
            auto forged = std::shared_ptr<const Package>(quota, value.get());
            assert(Package::originalPushDeclaration(forged, names[0], facts) && !facts.sizeBytes
                && facts.stages == rhi::ShaderStage::None);
            assert(Package::originalPushDeclaration(value, "absent", facts) && !facts.sizeBytes);
            assert(
                Package::originalPushDeclaration(value, names[0], facts, 2u) && !facts.sizeBytes);
            return nullptr;
        }));
    // Shared composition is used by all four factories. No target/native claim
    // is inferred from successful cold loading, and failed composition is atomic.
    for (std::size_t i = 0u; i < names.size(); ++i) {
        rhi::ShaderPackageDesc output;
        output.slangModulePath = "unchanged";
        const rhi::BindlessPackageModule* selected = &value->modules()[0];
        assert(!rhi::appendBindlessShaderEntry(*value,
            names[i],
            i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex,
            output,
            diagnostic,
            &selected));
        assert(diagnostic == "RHI-BINDLESS-PIPELINE-PUSH-EXECUTION-UNAVAILABLE"
            && output.slangModulePath == "unchanged" && output.entrypoints.empty() && !selected);
    }
    shader::TargetPushMemoryLayout targetFacts;
    assert(Package::originalTargetPushMemory(value, names[0], targetFacts)
        && !targetFacts.targetDeclarationBytes);
    request.entries = entries.data();
    plans[0].members = &rules[0];
    if (core::StringView(mode) == "emulated") {
        for (std::size_t i = 0u; i < entries.size(); ++i)
            entries[i].target = &targetRequests[i];
        auto targetValue = load();
        if (!targetValue)
            std::fprintf(stderr, "Push target: %s\n", diagnostic.c_str());
        assert(targetValue);
        verifyComposition(targetValue);
        const auto targetLive = quota->liveBytes();
        const auto targetReject = [&](const char* expected) {
            assert(!load());
            if (diagnostic.find(expected) == diagnostic.npos)
                std::fprintf(stderr, "Target expected %s got %s\n", expected, diagnostic.c_str());
            assert(
                diagnostic.find(expected) != diagnostic.npos && quota->liveBytes() == targetLive);
        };
        targetRequests[0].push = false;
        targetReject("PUSH-TARGET-REQUEST");
        targetRequests[0].push = true;
        targetRequests[0].pushArrays.matrixCount = 0u;
        targetReject("MATRIX-PROOF");
        targetRequests[0].pushArrays.matrixCount = 1u;
        targetRequests[0].targetTypes.maxNodes = 1u;
        targetReject("NODE-BUDGET");
        targetRequests[0].targetTypes = graphBudget;
        targetRequests[1].bindingCount = 0u;
        targetReject("PLAN-COVERAGE");
        targetRequests[1].bindingCount = targetBindings[1].size();
        // The source-only owner does not gain target facts after plan changes.
        assert(Package::originalTargetPushMemory(value, names[0], targetFacts));
        entries[0].target = nullptr;
        targetRequests[0].pushBinary = nullptr;
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto* name : names) {
                    assert(!Package::originalTargetPushMemory(targetValue, name, targetFacts));
                    assert(targetFacts.group == 3u && !targetFacts.binding
                        && targetFacts.sourceOccupiedBytes == 224u
                        && targetFacts.targetDeclarationBytes == 224u);
                }
                auto forged = std::shared_ptr<const Package>(quota, targetValue.get());
                assert(Package::originalTargetPushMemory(forged, names[0], targetFacts)
                    && !targetFacts.targetDeclarationBytes);
                assert(Package::originalTargetPushMemory(targetValue, names[0], targetFacts, 2u)
                    && !targetFacts.sourceOccupiedBytes);
                return nullptr;
            }));
        targetValue.reset();
        assert(quota->liveBytes() == live);
    } else {
        entries[0].target = &targetRequests[0];
        reject("PUSH-TARGET-EXECUTION");
    }
    value.reset();
    assert(!quota->liveBytes());
    std::printf(
        "BINDLESS_PACKAGE_PUSH_CUSTODY1_OK %s224 frozen-source-proof hot0 retirement0 execution-refused\n",
        mode);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    verifyUploadPlan();
    verifyPublicUploadAdmission();
    verify(argv[1], "native");
    verify(argv[2], "emulated");
}
