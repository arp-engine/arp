#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"
#include "rhi/frontend/transfer_upload_input.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class BufferUploadArgumentsFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using A = BufferUploadArguments;
        using Scalar = CommandScalarArguments;
        using Store = CommandVariableArguments;
        static_assert(std::is_trivially_copyable_v<A> && !std::is_default_constructible_v<A>);
        UploadBufferDesc input;
        input.sourceUploadBuffer = {17u};
        input.destinationResource = {23u};
        input.sizeBytes = UINT64_MAX;
        input.frameSerial = UINT64_MAX;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto frozen = A::captureScalarFields(input);
        assert(frozen.matchesScalarFields(input) && frozen.source() == input.sourceUploadBuffer
            && frozen.destination() == input.destinationResource);
        const std::array<void (*)(UploadBufferDesc&), 6> mutate{{[](auto& d) {
                                                                     ++d.sourceUploadBuffer.value;
                                                                 },
            [](auto& d) {
                ++d.destinationResource.value;
            },
            [](auto& d) {
                --d.sizeBytes;
            },
            [](auto& d) {
                d.stagingIntent = static_cast<StagingIntent>(UINT32_MAX);
            },
            [](auto& d) {
                d.backendOwnedStaging = false;
            },
            [](auto& d) {
                --d.frameSerial;
            }}};
        for (auto change : mutate) {
            auto changed = input;
            change(changed);
            assert(!frozen.matchesScalarFields(changed));
            const auto exact = A::captureScalarFields(changed);
            assert(exact.matchesScalarFields(changed));
            frozen.writeScalarFields(changed);
            assert(frozen.matchesScalarFields(changed));
        }
        UploadBufferDesc invalid;
        invalid.sizeBytes = 0u;
        invalid.stagingIntent = static_cast<StagingIntent>(UINT32_MAX);
        invalid.backendOwnedStaging = false;
        const auto invalidFrozen = A::captureScalarFields(invalid);
        invalidFrozen.writeScalarFields(input);
        assert(invalidFrozen.matchesScalarFields(input) && !input.sourceUploadBuffer.isValid()
            && !input.destinationResource.isValid() && !input.sizeBytes
            && !input.backendOwnedStaging);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);

        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const char* diagnostic = nullptr;
        core::pmr::String label("原始上传✓");
        Store::Input raw{*Scalar::capture(CommandUsageKind::BufferUpload, Scalar::None{}), label};
        CommandVariableArgumentsBudget budget{
            1u, true, 1u, 0u, label.size(), 0u, quota->limitBytes()};
        const auto factory = [&] {
            return Store::prepareOriginal(&raw, 1u, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        --budget.maxTextBytes;
        assert(!factory() && std::string_view(diagnostic) == "RHI-COMMAND-ARGUMENTS-CAPACITY");
        ++budget.maxTextBytes;
        auto text = factory();
        assert(text && text->matches(0u, raw));
        label[0] = 'x';
        assert(!text->matches(0u, raw));
        label.clear();
        label.shrink_to_fit();
        raw.text = text->text(0u);
        assert(raw.text == "原始上传✓" && text->matches(0u, raw));
        input.label = "unrelated output label";
        invalidFrozen.writeScalarFields(input);
        assert(input.label == "unrelated output label");
        {
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto hot = core::defaultCpuAllocator().stats().allocationCount;
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes() - quota->liveBytes());
            assert(exhausted.charged());
            assert(invalidFrozen.matchesScalarFields(input) && text->matches(0u, raw));
            invalidFrozen.writeScalarFields(input);
            assert(WebGpuOriginalCommandInputs::family(CommandUsageKind::BufferUpload)
                == WebGpuOriginalCommandInputs::Family::BufferUpload);
            WebGpuCommandReference upload{
                CommandReferenceDomain::Upload, WebGpuCommandReferenceSource::Transfer, {17u}};
            WebGpuGenericCommandProjectionBudget graphBudget;
            graphBudget.contractVersion = 1u;
            graphBudget.prototypeOptIn = true;
            graphBudget.maxReferences = 1u;
            graphBudget.maxCpuMetadataBytes = quota->limitBytes();
            graphBudget
                .maxDomainReferences[static_cast<std::size_t>(CommandReferenceDomain::Upload)] = 1u;
            assert(!WebGpuGenericCommandProjection::prepare(
                       {}, &upload, 1u, graphBudget, quota, diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN");
            assert(core::defaultCpuAllocator().stats().allocationCount == hot);
            std::pmr::set_default_resource(previous);
        }
        text.reset();
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_BUFFER_UPLOAD_INPUT_OK %llu fields6 frozen-label source-closed allocation0\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
