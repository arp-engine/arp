#pragma once
#include "rhi/backends/webgpu/webgpu_original_resource_states.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"
#include "webgpu_original_texture_readback_projection_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackInputsFixture final {
public:
    using Graph = WebGpuOriginalTextureReadbackProjectionFixture::Graph;
    using Inputs = rhi_webgpu::WebGpuOriginalCommandInputs;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;
    using Arg = rhi::TextureReadbackOriginalInputArguments;
    static constexpr std::uint64_t cpuBytes = 65536u;
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>, 5>;

    static void verifyContract()
    {
        static_assert(std::is_copy_constructible_v<Arg>);
        assert(!Arg::captureOriginal({}));
        Arg empty;
        assert(!empty.matches(empty));
        assert(Inputs::family(rhi::CommandUsageKind::TextureReadback)
            == Inputs::Family::TextureReadback);
        rhi_webgpu::WebGpuOriginalCommandInputsBudget b;
        assert(!b.maxReadbackDescriptors && !b.maxReadbackMipMappings
            && !b.maxReadbackUnpublishedResultBytes);
        std::puts(
            "BINDLESS_RHI_READBACK_INPUTS_CONTRACT_OK original-snapshot full-positional budgets-default-closed state-coverage3 recording-unproven");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static std::shared_ptr<const States> prepare(
        const std::shared_ptr<const Graph>& graph, const Quotas& quotas, bool verify)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        const char* diagnostic = nullptr;
        const auto& pair = graph->originalReadbacks()[0];
        const auto& snapshot = pair->layout()->inputOwner();
        const std::array<CommandUsage, 4> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::TextureReadback, 0u, 2u},
            {K::EndTransferPass, 2u, 0u},
            {K::Finish, 2u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 4u, 2u, cpuBytes},
            quotas[0],
            diagnostic);
        assert(usage);
        const std::array<Store::Input, 4> raw{
            {{*A::capture(K::BeginTransferPass, A::None{}), "原始读回"},
                {*A::capture(K::TextureReadback, A::None{}), snapshot->label()},
                {*A::capture(K::EndTransferPass, A::None{})},
                {*A::capture(K::Finish, A::None{})}}};
        const auto labels = raw[0].text.size() + raw[1].text.size();
        auto arguments = Store::prepareOriginal(raw.data(),
            raw.size(),
            {1u, true, 4u, 0u, labels, 0u, cpuBytes},
            quotas[1],
            diagnostic);
        assert(arguments);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 4u, 0u, cpuBytes}, quotas[2], diagnostic);
        assert(scalars);
        const auto argument = *Arg::captureOriginal(snapshot);
        Inputs::OriginalDescriptor descriptor{1u, argument};
        WebGpuOriginalCommandInputsBudget b{1u, true, 1u, cpuBytes};
        b.maxReadbackDescriptors = 1u;
        b.maxReadbackMipMappings = 2u;
        b.maxReadbackUnpublishedResultBytes = 1536u;
        const auto factory = [&] {
            return Inputs::prepareOriginal(scalars, {}, &descriptor, 1u, b, quotas[3], diagnostic);
        };
        std::uint64_t inputPeak = 0u, statePeak = 0u;
        if (verify) {
            const auto reject = [&](const char* id) {
                const auto baseline = quotas[3]->liveBytes();
                const auto rejected = factory();
                const bool matched = !rejected && diagnostic && std::string_view(diagnostic) == id
                    && quotas[3]->liveBytes() == baseline;
                if (!matched)
                    std::fprintf(stderr,
                        "Readback input rejection: expected=%s actual=%s accepted=%d baseline=%llu live=%llu\n",
                        id,
                        diagnostic ? diagnostic : "<none>",
                        !!rejected,
                        static_cast<unsigned long long>(baseline),
                        static_cast<unsigned long long>(quotas[3]->liveBytes()));
                assert(matched);
            };
            --b.maxReadbackDescriptors;
            reject("RHI-WEBGPU-ORIGINAL-READBACK-CAPACITY");
            ++b.maxReadbackDescriptors;
            --b.maxReadbackMipMappings;
            reject("RHI-WEBGPU-ORIGINAL-READBACK-MIP-CAPACITY");
            ++b.maxReadbackMipMappings;
            --b.maxReadbackUnpublishedResultBytes;
            reject("RHI-WEBGPU-ORIGINAL-READBACK-RESULT-CAPACITY");
            ++b.maxReadbackUnpublishedResultBytes;
            --b.maxDescriptorRecords;
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-CAPACITY");
            ++b.maxDescriptorRecords;
            --b.maxCpuMetadataBytes;
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-QUOTA");
            ++b.maxCpuMetadataBytes;
            descriptor.commandIndex = 0u;
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-ORDER");
            descriptor.commandIndex = 1u;
            descriptor.arguments = TransferCopyArguments::finish();
            reject("RHI-WEBGPU-ORIGINAL-INPUTS-KIND");
            descriptor.arguments = Arg{};
            reject("RHI-WEBGPU-ORIGINAL-READBACK-INPUT");
            descriptor.arguments = argument;
            auto forged =
                std::shared_ptr<const TextureReadbackInputSnapshot>(quotas[3], snapshot.get());
            assert(!Arg::captureOriginal(forged));
            forged.reset();
            TextureReadbackLayoutFixture::Context cloned;
            ReadbackTextureDesc copy;
            snapshot->scalarFields().writeScalarFields(copy);
            copy.label = snapshot->label();
            copy.mipLayouts.assign(snapshot->mipLayouts().begin(), snapshot->mipLayouts().end());
            auto equal = cloned.capture(copy, diagnostic);
            assert(equal && snapshot->matches(copy) && equal->matches(copy));
            descriptor.arguments = *Arg::captureOriginal(equal);
            reject("RHI-WEBGPU-ORIGINAL-READBACK-FOREIGN-INPUT");
            descriptor.arguments = argument;
            equal.reset();
            cloned.retired();
            auto changed = raw;
            changed[1].text = "changed original readback label";
            auto wrong = Store::prepareOriginal(changed.data(),
                changed.size(),
                {1u, true, 4u, 0u, changed[0].text.size() + changed[1].text.size(), 0u, cpuBytes},
                quotas[1],
                diagnostic);
            assert(wrong);
            auto saved = scalars;
            scalars = WebGpuFrozenCommandScalars::prepareOriginal(
                usage, wrong, {1u, true, 4u, 0u, cpuBytes}, quotas[2], diagnostic);
            assert(scalars);
            reject("RHI-WEBGPU-ORIGINAL-READBACK-LABEL");
            scalars = std::move(saved);
            wrong.reset();
            CpuQuotaFactoryFixture pressure(quotas[3]);
            inputPeak = pressure.measure(factory);
            pressure.verify(inputPeak, factory);
            {
                core::CpuMemoryQuotaLease full(quotas[3], cpuBytes);
                assert(full.charged());
                reject("RHI-WEBGPU-ORIGINAL-INPUTS-ALLOCATION");
            }
        }
        auto inputs = factory();
        assert(inputs && !inputs->validate());
        WebGpuOriginalResourceStatesBudget sb{1u, true, 3u, 3u, cpuBytes};
        const auto stateFactory = [&] {
            return States::prepareOriginal(inputs, sb, quotas[4], diagnostic);
        };
        if (verify) {
            --sb.maxSourceReferences;
            assert(!stateFactory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-REFERENCE-CAPACITY");
            ++sb.maxSourceReferences;
            --sb.maxStateControls;
            assert(!stateFactory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-CONTROL-CAPACITY");
            ++sb.maxStateControls;
            CpuQuotaFactoryFixture pressure(quotas[4]);
            statePeak = pressure.measure(stateFactory);
            pressure.verify(statePeak, stateFactory);
            {
                core::CpuMemoryQuotaLease full(quotas[4], cpuBytes);
                assert(full.charged() && !stateFactory()
                    && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-ALLOCATION");
            }
        }
        auto states = stateFactory();
        assert(states && !states->validate() && states->sourceReferences() == 3u
            && states->states().size() == 3u);
        const auto& source = *pair->destination()->source();
        const auto* staging = source.retainedNativeResourceAt(0u);
        const auto* target = source.retainedNativeResourceAt(1u);
        assert(staging && staging->buffer && target && target->texture
            && !source.retainedNativeResourceAt(2u));
        assert(states->find(staging->id)->source.control
                == rhi::common::RecordedWholeResourceStateView{staging->buffer->recordedState()}
            && states->find(target->id)->source.control == target->texture->recordedState()
            && states->find(pair->destination()->resourceId())->source.control
                == rhi::common::RecordedWholeResourceStateView{
                    pair->destination()->nativeResource()->recordedState()});
        if (verify) {
            const std::array<WebGpuGenericCommandUsage::RawReference, 2> refs{
                {{CommandReferenceDomain::Texture, target->texture.get()},
                    {CommandReferenceDomain::Readback, pair.get()}}};
            Inputs::RawInput incoming{raw[1], argument};
            const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(
                !inputs->compareRaw(1u, incoming, refs.data(), refs.size()) && !states->validate());
            incoming.descriptor = Arg{};
            assert(std::string_view(inputs->compareRaw(1u, incoming, refs.data(), refs.size()))
                == "RHI-WEBGPU-ORIGINAL-INPUTS-PAYLOAD");
            incoming.descriptor = argument;
            auto foreign = refs;
            foreign[1].original = pair->destination()->nativeResource().get();
            assert(inputs->compareRaw(1u, incoming, foreign.data(), foreign.size()));
            assert(std::string_view(inputs->executionDiagnostic())
                == webGpuGenericRecordingUsageDiagnostic);
            const auto access = WebGpuOriginalTransferStateValidation::validate(states, 0u);
            if (target->texture->recordedState().state() == ResourceState::CopySrc)
                assert(!access.diagnostic);
            else
                assert(access.diagnostic
                    && std::string_view(access.diagnostic)
                        == "RHI-WEBGPU-ORIGINAL-READBACK-STATE-MISMATCH");
            core::MutationJournal journal;
            assert(
                !staging->buffer->recordState(staging->buffer->recordedState().state(), &journal));
            assert(std::string_view(states->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
            journal.rollback();
            assert(std::string_view(states->validate()) == "RHI-WEBGPU-ORIGINAL-STATES-STALE"
                && !inputs->validate());
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
            states.reset();
            states = stateFactory();
            assert(states && !states->validate());
            std::printf(
                "BINDLESS_RHI_READBACK_INPUTS_NATIVE_OK %llu %llu stream4 refs2 states3 original-input label mips2 result1536 staging-control allocation0 recording-closed\n",
                static_cast<unsigned long long>(inputPeak),
                static_cast<unsigned long long>(statePeak));
        }
        return states;
    }
#endif
};
} // namespace arp::test
