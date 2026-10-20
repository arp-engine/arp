#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_command_access_fixture.h"
#include "original_state_transaction_ownership_fixture.h"
#include "rhi/frontend/command_state.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandResourceStatesFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    template <class Factory, class Transition>
    static void verify(Factory&& create, Transition&& transition, bool normalized)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Inputs = rhi::OriginalCommandInputs;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using K = rhi::CommandUsageKind;
        using S = rhi::ResourceState;
        using Projection = rhi::common::OriginalCommandTextureRanges;
        auto domain = quota(), registry = quota(), sourceQuota = quota(), inputQuota = quota(),
             stateQuota = quota();
        {
            auto device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 1u, 2u, 1u, 11u, bytes}, registry));
            rhi::BufferDesc buffer;
            buffer.sizeBytes = 256u;
            buffer.usage = rhi::ResourceUsage::CopySrc;
            buffer.initialState = S::CopySrc;
            auto bufferObject = device->createBuffer(buffer);
            assert(bufferObject);
            rhi::TextureDesc desc;
            desc.extent = {4u, 4u, 1u};
            desc.format = rhi::TextureFormat::Rgba8Unorm;
            desc.mipLevelCount = 2u;
            desc.arrayLayerCount = 2u;
            desc.initialState = S::CopySrc;
            desc.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst
                | rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled;
            auto a = device->createTexture(desc);
            desc.initialState = S::CopyDst;
            auto b = device->createTexture(desc);
            assert(a && b);
            auto* originalA = a.get();
            auto* originalB = b.get();
            const auto whole = rhi::defaultTextureSubresourceRange(desc);
            const rhi::TextureSubresourceRange part{1u, 1u, 1u, 1u, rhi::TextureAspect::Color};
            const auto range = normalized ? part : whole;
            rhi::TextureViewDesc view;
            view.format = desc.format;
            view.usage = rhi::TextureUsage::RenderAttachment;
            view.baseMipLevel = range.baseMipLevel;
            view.mipLevelCount = range.mipLevelCount;
            view.baseArrayLayer = range.baseArrayLayer;
            view.arrayLayerCount = range.arrayLayerCount;
            view.dimension = rhi::TextureViewDimension::Texture2DArray;
            auto viewObject = device->createTextureView(*b, view);
            assert(viewObject);
            const auto viewId = viewObject->id();
            const char* diagnostic = nullptr;
            auto bufferLease = rhi::OriginalCommandBufferLease::prepareOriginal(
                bufferObject, {1u, true, 2u, bytes}, sourceQuota, diagnostic);
            auto parentA = rhi::OriginalCommandTextureLease::prepareOriginal(
                a, {1u, true, 2u, bytes}, sourceQuota, diagnostic);
            auto parentB = rhi::OriginalCommandTextureLease::prepareOriginal(
                b, {1u, true, 2u, bytes}, sourceQuota, diagnostic);
            auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                viewObject, {1u, true, 2u, bytes}, sourceQuota, diagnostic);
            auto viewSource = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                viewLease, parentB, {1u, true, 5u, bytes}, sourceQuota, diagnostic);
            assert(bufferLease && parentA && parentB && viewSource);
            std::array<Sources::Source, 4> leases{bufferLease, parentA, parentB, viewSource};
            rhi::OriginalCommandAdmissionBudget budget;
            budget.contractVersion = 1u;
            budget.prototypeOptIn = true;
            budget.maxCommands = 8u;
            budget.maxReferences = 6u;
            budget.maxGraphOwners = 16u;
            budget.maxInputDescriptors = 4u;
            budget.maxStateObservations = 6u;
            budget.maxTextureRanges = 5u;
            budget.maxMipMappings = 2u;
            budget.maxPassColorAttachments = 1u;
            budget.maxPassAttachmentReferences = 1u;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                1u;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                2u;
            budget.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::TextureView)] = 1u;
            budget.maxCpuBytes[0] = budget.maxCpuBytes[1] = bytes;
            budget.maxStateTransactions = 1u;
            budget.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)] = bytes;
            const std::array<rhi::CommandUsage, 8> commands{{{K::BeginTransferPass, 0u, 0u},
                {K::BufferToTextureCopy, 0u, 2u},
                {K::TextureToTextureCopy, 2u, 2u},
                {K::EndTransferPass, 4u, 0u},
                {K::TextureBarrier, 4u, 1u},
                {K::BeginRenderPass, 5u, 1u},
                {K::EndRenderPass, 6u, 0u},
                {K::Finish, 6u, 0u}}};
            const std::array<Usage::Reference, 6> refs{{{0u}, {1u}, {1u}, {2u}, {1u}, {3u}}};
            const std::array<Store::Input, 8> fields{
                {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::BufferToTextureCopy, Scalar::None{})},
                    {*Scalar::capture(K::TextureToTextureCopy, Scalar::None{})},
                    {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::TextureBarrier, Scalar::None{})},
                    {*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                    {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                    {*Scalar::capture(K::Finish, Scalar::None{})}}};
            auto store = Store::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, 8u, 0u, 0u, 0u, bytes},
                inputQuota,
                diagnostic);
            assert(store);
            rhi::BufferToTextureCopyDesc upload;
            upload.sourceBuffer = bufferLease->resource();
            upload.destinationTexture = parentA->resource();
            upload.textureSubresources = range;
            upload.extent = {2u, 2u, 1u};
            upload.bytesPerRow = 8u;
            upload.rowsPerImage = 2u;
            rhi::TextureToTextureCopyDesc copy;
            copy.sourceTexture = parentA->resource();
            copy.destinationTexture = parentB->resource();
            copy.sourceSubresources = range;
            copy.destinationSubresources = range;
            copy.extent = {2u, 2u, 1u};
            copy.mipLayouts.push_back({range.baseMipLevel, {2u, 2u, 1u}});
            auto fullCopy = rhi::TransferCopyInputSnapshot::prepareOriginal(
                copy, {1u, true, 2u, bytes}, inputQuota, diagnostic);
            assert(fullCopy);
            rhi::ResourceBarrierDesc barrier{parentA->resource(), range, S::CopySrc, S::CopyDst};
            rhi::RenderPassDesc pass;
            rhi::RenderPassColorAttachmentDesc color;
            color.target = viewId;
            color.format = desc.format;
            pass.colorAttachments.push_back(color);
            auto frozenPass = rhi::RenderPassArguments::prepareOriginal(
                pass, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, inputQuota, diagnostic);
            assert(frozenPass);
            std::array<Inputs::OriginalDescriptor, 4> descriptors{
                {{1u, *rhi::TransferCopyArguments::capture(upload)},
                    {2u, fullCopy},
                    {4u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier)},
                    {5u, frozenPass}}};
            const auto inputFactory = [&] {
                auto sources = Sources::prepareOriginal(
                    leases.data(), leases.size(), budget, sourceQuota, diagnostic);
                if (!sources)
                    return std::shared_ptr<const Inputs>{};
                auto usage = Usage::prepareOriginal(sources,
                    commands.data(),
                    commands.size(),
                    refs.data(),
                    refs.size(),
                    inputQuota,
                    diagnostic);
                if (!usage)
                    return std::shared_ptr<const Inputs>{};
                auto scalars = Scalars::prepareOriginal(usage, store, inputQuota, diagnostic);
                if (!scalars)
                    return std::shared_ptr<const Inputs>{};
                return Inputs::prepareOriginal(
                    scalars, descriptors.data(), descriptors.size(), inputQuota, diagnostic);
            };
            auto inputs = inputFactory(), equalInputs = inputFactory();
            assert(inputs && equalInputs && inputs->retainedOwnerCount() == 16u);
            rhi::OriginalCommandStateAdmissionBudget stateBudget{
                1u, true, 1u, 4u, 6u, 5u, 24u, bytes};
            const auto factory = [&] {
                return Graph::prepareOriginal(inputs, *device, stateBudget, stateQuota, diagnostic);
            };
            CpuQuotaFactoryFixture pressure(stateQuota);
            const auto peak = pressure.measure(factory);
            pressure.verify(peak, factory);
            // Resource-wide intents have an explicit whole-resource projection,
            // independent of the later partial copy/view observations.
            {
                auto sources = Sources::prepareOriginal(
                    leases.data(), leases.size(), budget, sourceQuota, diagnostic);
                assert(sources);
                const std::array<rhi::CommandUsage, 4> wholeCommands{
                    {{K::TextureMemoryBarrier, 0u, 1u},
                        {K::AliasBarrier, 1u, 2u},
                        {K::RetainTexture, 3u, 1u},
                        {K::Finish, 4u, 0u}}};
                const std::array<Usage::Reference, 4> wholeReferences{{{1u}, {1u}, {2u}, {1u}}};
                auto usage = Usage::prepareOriginal(sources,
                    wholeCommands.data(),
                    wholeCommands.size(),
                    wholeReferences.data(),
                    wholeReferences.size(),
                    inputQuota,
                    diagnostic);
                assert(usage);
                const std::array<Store::Input, 4> wholeFields{
                    {{*Scalar::capture(K::TextureMemoryBarrier, Scalar::None{})},
                        {*Scalar::capture(K::AliasBarrier, Scalar::None{})},
                        {*Scalar::capture(K::RetainTexture, Scalar::None{})},
                        {*Scalar::capture(K::Finish, Scalar::None{})}}};
                auto wholeStore = Store::prepareOriginal(wholeFields.data(),
                    wholeFields.size(),
                    {1u, true, 4u, 0u, 0u, 0u, bytes},
                    inputQuota,
                    diagnostic);
                assert(wholeStore);
                auto scalars = Scalars::prepareOriginal(usage, wholeStore, inputQuota, diagnostic);
                assert(scalars);
                rhi::ResourceMemoryBarrierDesc memory;
                memory.resource = parentA->resource();
                rhi::AliasingBarrierDesc alias;
                alias.beforeResource = parentA->resource();
                alias.afterResource = parentB->resource();
                std::array<Inputs::OriginalDescriptor, 2> wholeDescriptors{
                    {{0u,
                         *rhi::ResourceDependencyArguments::capture(
                             K::TextureMemoryBarrier, memory)},
                        {1u, rhi::ResourceDependencyArguments::capture(alias)}}};
                auto wholeInputs = Inputs::prepareOriginal(scalars,
                    wholeDescriptors.data(),
                    wholeDescriptors.size(),
                    inputQuota,
                    diagnostic);
                assert(wholeInputs && wholeInputs->retainedOwnerCount() == 14u);
                auto wholeGraph = Graph::prepareOriginal(wholeInputs,
                    *device,
                    {1u, true, 1u, 4u, 5u, 4u, 22u, bytes},
                    stateQuota,
                    diagnostic);
                assert(wholeGraph && !wholeGraph->validate());
                assert(wholeGraph->textureEntries().size() == 4u
                    && wholeGraph->retainedOwnerCount() == 22u);
                for (const auto& entry : wholeGraph->textureEntries())
                    assert(entry.observation.range() == whole);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{domain, registry, sourceQuota, inputQuota, stateQuota},
                    [&]() noexcept {
                        return wholeGraph->validate();
                    }));
                const rhi::OriginalCommandStateSequenceBudget wholeIntent{
                    1u, true, 4u, 0u, 17u, 16u};
                is(rhi::common::OriginalCommandStateSequence::inspect(wholeGraph, wholeIntent)
                        .diagnostic,
                    "RHI-RESOURCE-MEMORY-BARRIER-SOURCE-STAGE-MISSING");
                memory.sourceStages = memory.destinationStages = rhi::PipelineStage::Transfer;
                memory.sourceAccess = memory.destinationAccess = rhi::ResourceAccess::TransferRead;
                wholeDescriptors[0].arguments =
                    *rhi::ResourceDependencyArguments::capture(K::TextureMemoryBarrier, memory);
                wholeInputs = Inputs::prepareOriginal(scalars,
                    wholeDescriptors.data(),
                    wholeDescriptors.size(),
                    inputQuota,
                    diagnostic);
                assert(wholeInputs);
                wholeGraph = Graph::prepareOriginal(
                    wholeInputs, *device, stateBudget, stateQuota, diagnostic);
                assert(wholeGraph);
                const auto rejectedAlias =
                    rhi::common::OriginalCommandStateSequence::inspect(wholeGraph, wholeIntent);
                is(rejectedAlias.diagnostic, "RHI-ORIGINAL-SEQUENCE-ALIAS-BINDING-UNPROVEN");
                assert(rejectedAlias.commandIndex == 1u);
            }
            {
                core::CpuMemoryQuotaLease full(stateQuota, bytes - stateQuota->liveBytes());
                assert(full.charged() && !factory());
                is(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-ALLOCATION");
            }
            const auto saved = stateBudget;
            --stateBudget.maxResourceRegistrations;
            assert(!factory());
            is(diagnostic, "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
            stateBudget = saved;
            --stateBudget.maxStateObservations;
            assert(!factory());
            is(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY");
            stateBudget = saved;
            --stateBudget.maxTextureRanges;
            assert(!factory());
            is(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-RANGE-CAPACITY");
            stateBudget = saved;
            --stateBudget.maxRetainedOwners;
            assert(!factory());
            is(diagnostic, "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
            stateBudget = saved;
            assert(!Graph::prepareOriginal(inputs, *device, stateBudget, inputQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-QUOTA-DOMAIN");
            assert(!Graph::prepareOriginal(inputs, *device, stateBudget, sourceQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-QUOTA-DOMAIN");
            auto savedInputs = inputs;
            --budget.maxTextureRanges;
            inputs = inputFactory();
            assert(inputs && !factory());
            is(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-RANGE-CAPACITY");
            ++budget.maxTextureRanges;
            --budget.maxStateObservations;
            inputs = inputFactory();
            assert(inputs && !factory());
            is(diagnostic, "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY");
            ++budget.maxStateObservations;
            inputs = savedInputs;
            savedInputs.reset();
            auto graph = factory();
            assert(graph && !graph->validate() && graph->retainedOwnerCount() == 24u);
            OriginalStateTransactionOwnershipFixture::verify(graph,
                *device,
                factory,
                transition,
                std::array{domain, registry, sourceQuota, inputQuota, stateQuota});
            // Independently authored valid original timeline; the mixed recipe
            // above intentionally tests ownership and is semantically rejected.
            {
                using Sequence = rhi::common::OriginalCommandStateSequence;
                const std::array<rhi::CommandUsage, 6> timeline{{{K::TextureMemoryBarrier, 0u, 1u},
                    {K::TextureBarrier, 1u, 1u},
                    {K::TextureBarrier, 2u, 1u},
                    {K::TextureBarrier, 3u, 1u},
                    {K::RetainTexture, 4u, 1u},
                    {K::Finish, 5u, 0u}}};
                const std::array<Usage::Reference, 5> references{{{1u}, {1u}, {1u}, {1u}, {1u}}};
                core::pmr::Vector<Store::Input> arguments;
                for (const auto& command : timeline)
                    arguments.push_back({*Scalar::capture(command.kind, Scalar::None{})});
                auto timelineStore = Store::prepareOriginal(arguments.data(),
                    arguments.size(),
                    {1u, true, 6u, 0u, 0u, 0u, bytes},
                    inputQuota,
                    diagnostic);
                assert(timelineStore);
                rhi::ResourceMemoryBarrierDesc memory;
                memory.resource = parentA->resource();
                memory.sourceStages = memory.destinationStages = rhi::PipelineStage::Transfer;
                memory.sourceAccess = memory.destinationAccess = rhi::ResourceAccess::TransferRead;
                const rhi::TextureSubresourceRange separate{
                    0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
                const auto selected = normalized ? part : whole;
                const auto other = normalized ? separate : whole;
                rhi::ResourceBarrierDesc first{parentA->resource(), whole, S::CopySrc, S::CopyDst};
                first.sourceStages = first.destinationStages = rhi::PipelineStage::Transfer;
                first.sourceAccess = rhi::ResourceAccess::TransferRead;
                first.destinationAccess = rhi::ResourceAccess::TransferWrite;
                auto second = first;
                second.textureSubresources = selected;
                second.before = S::CopyDst;
                second.after = normalized ? S::ShaderRead : S::CopySrc;
                second.sourceAccess = rhi::ResourceAccess::TransferWrite;
                second.destinationStages =
                    normalized ? rhi::PipelineStage::FragmentShader : rhi::PipelineStage::Transfer;
                second.destinationAccess = normalized ? rhi::ResourceAccess::ShaderRead
                                                      : rhi::ResourceAccess::TransferRead;
                auto third = first;
                third.textureSubresources = other;
                third.before = normalized ? S::CopyDst : S::CopySrc;
                third.after = normalized ? S::CopySrc : S::CopyDst;
                third.sourceAccess = normalized ? rhi::ResourceAccess::TransferWrite
                                                : rhi::ResourceAccess::TransferRead;
                third.destinationAccess = normalized ? rhi::ResourceAccess::TransferRead
                                                     : rhi::ResourceAccess::TransferWrite;
                std::array<Inputs::OriginalDescriptor, 4> dependencies{
                    {{0u,
                         *rhi::ResourceDependencyArguments::capture(
                             K::TextureMemoryBarrier, memory)},
                        {1u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, first)},
                        {2u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, second)},
                        {3u,
                            *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, third)}}};
                const auto timelineFactory = [&] {
                    auto sources = Sources::prepareOriginal(
                        leases.data(), leases.size(), budget, sourceQuota, diagnostic);
                    assert(sources);
                    auto usage = Usage::prepareOriginal(sources,
                        timeline.data(),
                        timeline.size(),
                        references.data(),
                        references.size(),
                        inputQuota,
                        diagnostic);
                    assert(usage);
                    auto scalars =
                        Scalars::prepareOriginal(usage, timelineStore, inputQuota, diagnostic);
                    assert(scalars);
                    auto inputs = Inputs::prepareOriginal(
                        scalars, dependencies.data(), dependencies.size(), inputQuota, diagnostic);
                    assert(inputs);
                    return Graph::prepareOriginal(
                        inputs, *device, stateBudget, stateQuota, diagnostic);
                };
                auto valid = timelineFactory();
                assert(valid);
                const auto cells = normalized ? 15u : 21u;
                const auto steps = cells - 1u;
                rhi::OriginalCommandStateSequenceBudget intent{1u, true, 6u, 0u, cells, steps};
                auto result = Sequence::inspect(valid, intent);
                assert(!result.diagnostic && result.stateCells == cells && result.steps == steps
                    && result.commandIndex == 6u);
                rhi::ResourceState final;
                assert(!Sequence::resolveOriginalTextureCell(valid, 1u, 0u, 6u, final)
                    && final == (normalized ? S::CopySrc : S::CopyDst));
                assert(!Sequence::resolveOriginalTextureCell(valid, 1u, 3u, 6u, final)
                    && final == (normalized ? S::ShaderRead : S::CopyDst));
                assert(!Sequence::resolveOriginalTextureCell(valid, 1u, 1u, 6u, final)
                    && final == S::CopyDst);
                is(Sequence::resolveOriginalTextureCell(valid, 1u, 4u, 6u, final),
                    "RHI-ORIGINAL-SEQUENCE-CELL");
                is(Sequence::resolveOriginalTextureCell(valid, 5u, 0u, 6u, final),
                    "RHI-ORIGINAL-SEQUENCE-OBSERVATION");
                is(Sequence::resolveOriginalTextureCell(valid, 1u, 0u, 7u, final),
                    "RHI-STATE-SEQUENCE-INPUT");
                const auto unchanged = originalA->observeOriginalCommittedState(whole);
                assert(unchanged && unchanged->state() == S::CopySrc);
                --intent.maxCommands;
                is(Sequence::inspect(valid, intent).diagnostic,
                    "RHI-ORIGINAL-SEQUENCE-COMMAND-CAPACITY");
                ++intent.maxCommands;
                --intent.maxStateCells;
                is(Sequence::inspect(valid, intent).diagnostic,
                    "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY");
                ++intent.maxStateCells;
                --intent.maxSteps;
                is(Sequence::inspect(valid, intent).diagnostic,
                    "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
                ++intent.maxSteps;
                intent.prototypeOptIn = false;
                is(Sequence::inspect(valid, intent).diagnostic, "RHI-ORIGINAL-SEQUENCE-OPT-IN");
                intent.prototypeOptIn = true;
                intent.contractVersion = 6u;
                is(Sequence::inspect(valid, intent).diagnostic, "RHI-ORIGINAL-SEQUENCE-VERSION");
                intent.contractVersion = 1u;
                intent.maxImplicitAccesses = 1u;
                is(Sequence::inspect(valid, intent).diagnostic,
                    "RHI-ORIGINAL-SEQUENCE-LEGACY-SCHEMA");
                intent.maxImplicitAccesses = 0u;
                intent.contractVersion = 2u;
                intent.maxImplicitAccesses = 1u;
                is(Sequence::inspect(valid, intent).diagnostic,
                    "RHI-ORIGINAL-SEQUENCE-STATE-PROFILE");
                intent.contractVersion = 1u;
                intent.maxImplicitAccesses = 0u;
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{domain, registry, sourceQuota, inputQuota, stateQuota},
                    [&]() noexcept {
                        if (const auto* id = Sequence::inspect(valid, intent).diagnostic)
                            return id;
                        if (const auto* id =
                                Sequence::resolveOriginalTextureCell(valid, 1u, 3u, 6u, final))
                            return id;
                        return originalA->validateOriginalCommittedState(*unchanged);
                    }));
                auto broken = second;
                broken.before = S::CopySrc;
                dependencies[2].arguments =
                    *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, broken);
                auto invalid = timelineFactory();
                assert(invalid);
                result = Sequence::inspect(invalid, intent);
                is(result.diagnostic, "RHI-STATE-SEQUENCE-BEFORE-MISMATCH");
                assert(result.commandIndex == 2u);
                dependencies[2].arguments =
                    *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, second);
                auto missingScope = memory;
                missingScope.destinationAccess = rhi::ResourceAccess::None;
                dependencies[0].arguments = *rhi::ResourceDependencyArguments::capture(
                    K::TextureMemoryBarrier, missingScope);
                invalid = timelineFactory();
                assert(invalid);
                result = Sequence::inspect(invalid, intent);
                is(result.diagnostic, "RHI-RESOURCE-MEMORY-BARRIER-DESTINATION-ACCESS-MISSING");
                assert(result.commandIndex == 0u);
                dependencies[0].arguments =
                    *rhi::ResourceDependencyArguments::capture(K::TextureMemoryBarrier, memory);
                if (normalized) {
                    auto overlapping = third;
                    overlapping.textureSubresources = whole;
                    dependencies[3].arguments =
                        *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, overlapping);
                    invalid = timelineFactory();
                    assert(invalid);
                    result = Sequence::inspect(invalid, {1u, true, 6u, 0u, 18u, 17u});
                    is(result.diagnostic, "RHI-STATE-SEQUENCE-BEFORE-MISMATCH");
                    assert(result.commandIndex == 3u);
                    dependencies[3].arguments =
                        *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, third);
                }
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto lowering =
                        arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(
                            backend);
                    assert(!lowering.originalIssuerAvailable && !lowering.executionAvailable
                        && lowering.diagnostic);
                }
                assert(
                    !valid->validate() && !originalA->validateOriginalCommittedState(*unchanged));
                auto recording = quota();
                auto root = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                    valid, *device, {1u, true, 1u, 4u, 6u, 5u, 25u, bytes}, recording, diagnostic);
                assert(root);
                assert(!root->materializeOriginalPrototypePlan(intent)
                    && root->plannedFinalCellCount() == 5u && root->plannedTransitionCount() == 3u);
                const auto initialRevision = unchanged->revision();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{domain, registry, sourceQuota, inputQuota, stateQuota, recording},
                    [&]() noexcept {
                        if (const auto* id = root->applyOriginalPrototypePlan())
                            return id;
                        if (const auto* id = root->validate())
                            return id;
                        if (originalA->subresourceState(other)
                                != (normalized ? S::CopySrc : S::CopyDst)
                            || originalA->subresourceState(selected)
                                != (normalized ? S::ShaderRead : S::CopyDst))
                            return "TEST-ORIGINAL-PLAN-FINAL-CELLS";
                        root->abandonOriginalPendingOwnership();
                        return root->journalEmpty() ? nullptr : "TEST-ORIGINAL-PLAN-ROLLBACK";
                    }));
                const auto restored = originalA->observeOriginalCommittedState(whole);
                assert(restored && restored->state() == S::CopySrc
                    && restored->revision() == initialRevision + 4u);
                root.reset();
                valid = timelineFactory();
                assert(valid);
                root = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                    valid, *device, {1u, true, 1u, 4u, 6u, 5u, 25u, bytes}, recording, diagnostic);
                assert(root);
                assert(!root->materializeOriginalPrototypePlan(intent));
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{domain, registry, sourceQuota, inputQuota, stateQuota, recording},
                    [&]() noexcept {
                        if (const auto* id = root->applyOriginalPrototypePlan())
                            return id;
                        if (const auto* id = root->commitOriginalPendingOwnership())
                            return id;
                        return root->validate();
                    }));
                assert(valid->validate());
                root.reset();
                if (normalized) {
                    const auto geometry = rhi::semantics::OriginalTextureStateRange::capture(
                        originalA->desc(), 4u, diagnostic);
                    assert(geometry);
                    for (std::uint64_t cell = 0u; cell < 4u; ++cell) {
                        rhi::TextureSubresourceRange point;
                        assert(geometry->cellRange(cell, point));
                        const auto current = originalA->subresourceState(point);
                        if (current != S::CopySrc)
                            assert(transition(*originalA, point, current, S::CopySrc, nullptr)
                                    .success);
                    }
                } else
                    assert(transition(*originalA, whole, S::CopyDst, S::CopySrc, nullptr).success);
                graph = factory();
                assert(graph);
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_PLAN_OVERLAP_OK %s unique-cells5 transitions3 exact-normalized-finals barrier-revision3 rollback-revision1 hot-allocation0 gates4-closed\n",
                    normalized ? "normalized" : "uniform");
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_STATE_SEQUENCE_OK %s cells%u steps%u exact-original-graph ordered-scopes before-mismatch overlapping-cells final-cell-results independent-limits no-mutation hot-allocation0 gates4-closed\n",
                    normalized ? "normalized" : "uniform",
                    cells,
                    steps);
            }
            {
                // State/access intent of a genuine transfer then attachment
                // stream. Native copy/pass lowering remains independently gated.
                const auto previousBudget = budget;
                budget.maxCommands = 12u;
                budget.maxReferences = 8u;
                budget.maxInputDescriptors = 6u;
                budget.maxStateObservations = 8u;
                budget.maxTextureRanges = 7u;
                const std::array<rhi::CommandUsage, 12> transfer{{{K::TextureBarrier, 0u, 1u},
                    {K::BeginTransferPass, 1u, 0u},
                    {K::BufferToTextureCopy, 1u, 2u},
                    {K::EndTransferPass, 3u, 0u},
                    {K::TextureBarrier, 3u, 1u},
                    {K::BeginTransferPass, 4u, 0u},
                    {K::TextureToTextureCopy, 4u, 2u},
                    {K::EndTransferPass, 6u, 0u},
                    {K::TextureBarrier, 6u, 1u},
                    {K::BeginRenderPass, 7u, 1u},
                    {K::EndRenderPass, 8u, 0u},
                    {K::Finish, 8u, 0u}}};
                const std::array<Usage::Reference, 8> references{
                    {{1u}, {0u}, {1u}, {1u}, {1u}, {2u}, {2u}, {3u}}};
                core::pmr::Vector<Store::Input> arguments;
                for (const auto& command : transfer)
                    arguments.push_back({*Scalar::capture(command.kind, Scalar::None{})});
                auto transferStore = Store::prepareOriginal(arguments.data(),
                    arguments.size(),
                    {1u, true, 12u, 0u, 0u, 0u, bytes},
                    inputQuota,
                    diagnostic);
                assert(transferStore);
                auto enter = barrier;
                enter.sourceStages = enter.destinationStages = rhi::PipelineStage::Transfer;
                enter.sourceAccess = rhi::ResourceAccess::TransferRead;
                enter.destinationAccess = rhi::ResourceAccess::TransferWrite;
                auto leave = enter;
                leave.before = S::CopyDst;
                leave.after = S::CopySrc;
                leave.sourceAccess = rhi::ResourceAccess::TransferWrite;
                leave.destinationAccess = rhi::ResourceAccess::TransferRead;
                auto attach = enter;
                attach.resource = parentB->resource();
                attach.before = S::CopyDst;
                attach.after = S::ColorAttachment;
                attach.sourceAccess = rhi::ResourceAccess::TransferWrite;
                attach.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
                attach.destinationAccess = rhi::ResourceAccess::ColorAttachmentRead
                    | rhi::ResourceAccess::ColorAttachmentWrite;
                std::array<Inputs::OriginalDescriptor, 6> sequenceDescriptors{
                    {{0u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, enter)},
                        {2u, *rhi::TransferCopyArguments::capture(upload)},
                        {4u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, leave)},
                        {6u, fullCopy},
                        {8u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, attach)},
                        {9u, frozenPass}}};
                const auto transferFactory = [&] {
                    auto sources = Sources::prepareOriginal(
                        leases.data(), leases.size(), budget, sourceQuota, diagnostic);
                    assert(sources);
                    auto usage = Usage::prepareOriginal(sources,
                        transfer.data(),
                        transfer.size(),
                        references.data(),
                        references.size(),
                        inputQuota,
                        diagnostic);
                    assert(usage);
                    auto scalars =
                        Scalars::prepareOriginal(usage, transferStore, inputQuota, diagnostic);
                    assert(scalars);
                    auto inputs = Inputs::prepareOriginal(scalars,
                        sequenceDescriptors.data(),
                        sequenceDescriptors.size(),
                        inputQuota,
                        diagnostic);
                    assert(inputs);
                    return Graph::prepareOriginal(inputs,
                        *device,
                        {1u, true, 1u, 4u, 8u, 7u, 24u, bytes},
                        stateQuota,
                        diagnostic);
                };
                auto valid = transferFactory();
                assert(valid);
                const rhi::OriginalCommandStateSequenceBudget intent{
                    1u, true, 12u, 0u, normalized ? 8u : 29u, normalized ? 8u : 29u};
                using Sequence = rhi::common::OriginalCommandStateSequence;
                const auto accepted = Sequence::inspect(valid, intent);
                assert(!accepted.diagnostic);
                auto broken = attach;
                broken.after = S::CopyDst;
                broken.destinationStages = rhi::PipelineStage::Transfer;
                broken.destinationAccess = rhi::ResourceAccess::TransferWrite;
                sequenceDescriptors[4].arguments =
                    *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, broken);
                auto invalid = transferFactory();
                assert(invalid);
                const auto rejected = Sequence::inspect(invalid, intent);
                is(rejected.diagnostic, "RHI-ORIGINAL-SEQUENCE-TEXTURE-ACCESS-STATE");
                assert(rejected.commandIndex == 9u);
                assert(!valid->validate() && !graph->validate());
                budget = previousBudget;
            }
            assert(graph->bufferEntries().size() == 1u && graph->textureEntries().size() == 5u);
            const std::array<std::size_t, 5> expectedSources{1u, 1u, 2u, 1u, 3u},
                expectedCommands{1u, 2u, 2u, 4u, 5u};
            for (std::size_t i = 0u; i < 5u; ++i) {
                const auto& entry = graph->textureEntries()[i];
                assert(entry.point.sourceIndex == expectedSources[i]
                    && entry.point.commandIndex == expectedCommands[i]
                    && entry.observation.range() == range);
                assert(entry.observation.state()
                    == (expectedSources[i] == 1u ? S::CopySrc : S::CopyDst));
            }
            is(graph->compareOriginalInputs(equalInputs, *device),
                "RHI-ORIGINAL-RESOURCE-STATES-INPUTS");
            auto forged = std::shared_ptr<const Graph>(sourceQuota, graph.get());
            is(Graph::validateOriginal(forged), "RHI-ORIGINAL-RESOURCE-STATES-OWNER");
            forged.reset();
            auto foreign = create();
            auto foreignDomain = quota(), foreignRegistry = quota();
            assert(foreign);
            assert(
                !foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, foreignDomain));
            assert(!foreign->prepareOriginalResourceRegistration(
                {1u, true, 1u, 2u, 1u, 11u, bytes}, foreignRegistry));
            is(graph->compareOriginalInputs(inputs, *foreign),
                "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
            assert(!Graph::prepareOriginal(inputs, *foreign, stateBudget, stateQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
            auto foreignRecording = quota();
            auto transactionIntent = stateBudget;
            ++transactionIntent.maxRetainedOwners;
            assert(!rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                graph, *foreign, transactionIntent, foreignRecording, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
            assert(!foreignRecording->liveBytes());
            auto invalid = barrier;
            invalid.textureSubresources.baseMipLevel = 2u;
            const auto old = descriptors[2];
            descriptors[2].arguments =
                *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, invalid);
            auto invalidInputs = inputFactory();
            assert(invalidInputs
                && !Graph::prepareOriginal(
                    invalidInputs, *device, stateBudget, stateQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-TEXTURE-RANGE-BOUNDS");
            descriptors[2] = old;
            invalidInputs.reset();
            if (!normalized) {
                invalid.textureSubresources = part;
                descriptors[2].arguments =
                    *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, invalid);
                auto partial = inputFactory();
                assert(partial
                    && !Graph::prepareOriginal(
                        partial, *device, stateBudget, stateQuota, diagnostic));
                is(diagnostic, "RHI-ORIGINAL-TEXTURE-STATE-PARTIAL-CONTROL-UNPROVEN");
                descriptors[2] = old;
            }
            core::MutationJournal journal;
            assert(transition(*originalA, range, S::CopySrc, S::CopyDst, &journal).success);
            is(graph->validate(), "RHI-ORIGINAL-TEXTURE-STATE-PENDING");
            assert(!factory());
            journal.rollback();
            is(graph->validate(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
            graph = factory();
            assert(graph);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, registry, sourceQuota, inputQuota, stateQuota}, [&]() noexcept {
                    if (const auto* id = Graph::validateOriginal(graph))
                        return id;
                    if (const auto* id = graph->compareOriginalInputs(inputs, *device))
                        return id;
                    for (const auto backend : {rhi::Backend::Null,
                             rhi::Backend::Vulkan,
                             rhi::Backend::Metal,
                             rhi::Backend::WebGPU})
                        if (!graph->executionDiagnostic(backend) || graph->nativeExecutionProven()
                            || arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(
                                backend)
                                .originalIssuerAvailable)
                            return "TEST-EXECUTION-OPEN";
                    if (!transition(*originalA, range, S::CopySrc, S::CopyDst, &journal).success)
                        return "TEST-ORIGINAL-WRITE";
                    if (!graph->validate())
                        return "TEST-PENDING-ACCEPTED";
                    journal.rollback();
                    return graph->validate() ? nullptr : "TEST-ROLLBACK-STALE-ACCEPTED";
                }));
            graph = factory();
            assert(graph);
            assert(transition(*originalA, range, S::CopySrc, S::CopyDst, &journal).success);
            journal.commit();
            is(graph->validate(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
            graph = factory();
            assert(graph);
            const auto limit = stateQuota->limitBytes();
            stateQuota->setLimitBytes(limit - 1u);
            is(graph->validate(), "RHI-ORIGINAL-RESOURCE-STATES-QUOTA");
            stateQuota->setLimitBytes(limit);
            auto& mutableView =
                const_cast<rhi::TextureViewDesc&>(viewLease->originalObject()->desc());
            ++mutableView.baseMipLevel;
            assert(graph->validate());
            --mutableView.baseMipLevel;
            assert(!graph->validate());
            std::weak_ptr<const Inputs> retained = inputs;
            equalInputs.reset();
            inputs.reset();
            store.reset();
            fullCopy.reset();
            frozenPass.reset();
            leases = {};
            for (auto& entry : descriptors)
                entry.arguments = rhi::TransferCopyArguments::finish();
            bufferLease.reset();
            parentA.reset();
            parentB.reset();
            viewLease.reset();
            viewSource.reset();
            assert(!retained.expired() && !graph->validate() && originalB);
            auto recording = quota();
            auto transactionBudget = stateBudget;
            ++transactionBudget.maxRetainedOwners;
            auto transaction = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                graph, *device, transactionBudget, recording, diagnostic);
            assert(transaction && !transaction->beginOriginalPendingOwnership());
            std::weak_ptr<const Graph> retainedGraph = graph;
            graph.reset();
            assert(!retainedGraph.expired() && !transaction->validate());
            device.reset();
            is(transaction->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            transaction->abandonOriginalPendingOwnership();
            is(transaction->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            transaction.reset();
            assert(retainedGraph.expired() && !recording->liveBytes());
            retained.reset();
            foreign.reset();
        }
        assert(!domain->liveBytes() && !registry->liveBytes() && !sourceQuota->liveBytes()
            && !inputQuota->liveBytes() && !stateQuota->liveBytes());
        OriginalCommandAccessFixture::verify(create);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_RESOURCE_STATES_OK exact-input-device-source copy-dependency-view-ranges5 buffer1 owner-union24 independent-limits cold-exact-less-exhausted pending-stale-ABA retained-drop-shutdown hot-allocation0 gates4-closed");
    }
};
} // namespace arp::test
