#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_graphics_member_memory_lowering.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
// Inspects the existing GraphicsDomains public Root; never executes or invents
// source/target layout evidence. Reused by Null, Vulkan and the browser runner.
class OriginalGraphicsRuntimeMemoryFixture final {
public:
    struct Binding {
        std::uint32_t set, binding;
        std::uint64_t stride, capacity;
        rhi::ShaderStage stage;
    };

    static constexpr std::array<Binding, 2u> domainBindings{
        {{2u, 3u, 4u, 4u, rhi::ShaderStage::Fragment},
            {2u, 4u, 4u, 4u, rhi::ShaderStage::Fragment}}};

    static bool cleared(const rhi::ShaderBufferMemberMemory& facts) noexcept
    {
        return !facts.sourceOccupiedBytes && !facts.targetMinimumBytes
            && facts.evidence == rhi::ShaderBufferMemberMemory::Evidence::None
            && !facts.sourceMinimumBytes && !facts.hasRuntimeArray
            && !facts.sourceRuntimeArrayOffsetBytes && !facts.sourceRuntimeArrayStrideBytes
            && !facts.targetRuntimeArrayOffsetBytes && !facts.targetRuntimeArrayStrideBytes
            && !facts.sourceElementCapacity;
    }

    template <class Root, class Sources>
    static void inspect(const Root& root,
        const Sources& sources,
        rhi::GpuDevice* device,
        bool fault,
        bool undersized = false,
        const std::array<Binding, 2u>& bindings = domainBindings)
    {
        using Source = rhi::OriginalCommandBindlessRootSource;
        const auto fragment = fault ? "faultFragment" : "cleanFragment";
        rhi::ShaderBufferMemberMemory members;
        rhi::BindlessResourceAccess access;
        std::size_t index = 0u;
        if (!device) {
            assert(Source::originalOrdinaryBufferMemberMemory(root,
                       fragment,
                       bindings.front().set,
                       bindings.front().binding,
                       members,
                       access,
                       index,
                       2u)
                && cleared(members) && !access.resource.isValid() && index == SIZE_MAX);
            return;
        }
        const auto& prepared =
            std::get<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
                root->originalPrepared());
        const auto& pipeline = *prepared->owners()->desc().pipeline;
        const auto quota = root->originalMetadataQuota();
        const auto decision =
            rhi::semantics::OriginalBindlessGraphicsMemberMemoryLowering::lowering(
                device->backend(), 2u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto& binding : bindings) {
                    const auto entry =
                        binding.stage == rhi::ShaderStage::Vertex ? "vertexMain" : fragment;
                    // A successful previous query must not leak any facts on refusal.
                    const auto reject = [&](const auto& candidate,
                                            const char* name,
                                            std::uint32_t version) {
                        members = {1u,
                            1u,
                            rhi::ShaderBufferMemberMemory::Evidence::NativeBinaryIdentity,
                            1u,
                            true,
                            1u,
                            1u,
                            1u,
                            1u,
                            1u};
                        assert(Source::originalOrdinaryBufferMemberMemory(candidate,
                                   name,
                                   binding.set,
                                   binding.binding,
                                   members,
                                   access,
                                   index,
                                   version)
                            && cleared(members) && !access.resource.isValid() && index == SIZE_MAX);
                    };
                    reject(root, entry, 1u);
                    reject(root, entry, 3u);
                    reject(root, fault ? "cleanFragment" : "faultFragment", 2u);
                    auto forged =
                        std::shared_ptr<const typename Root::element_type>(quota, root.get());
                    reject(forged, entry, 2u);
                    const auto* id = pipeline.bindlessOrdinaryBufferMemberMemory(
                        binding.stage, binding.set, binding.binding, members, 2u);
                    if (!decision.available) {
                        assert(
                            id && core::StringView(id) == decision.diagnostic && cleared(members));
                        reject(root, entry, 2u);
                        continue;
                    }
                    if (id)
                        std::fprintf(stderr, "Runtime member pipeline: %s\n", id);
                    assert(!id && members.hasRuntimeArray && !members.sourceOccupiedBytes
                        && members.sourceMinimumBytes == binding.stride
                        && members.targetMinimumBytes == binding.stride
                        && members.sourceRuntimeArrayStrideBytes == binding.stride
                        && members.targetRuntimeArrayStrideBytes == binding.stride
                        && !members.sourceRuntimeArrayOffsetBytes
                        && !members.targetRuntimeArrayOffsetBytes && !members.sourceElementCapacity
                        && members.evidence == decision.evidence);
                    if (undersized && binding.set == bindings.front().set
                        && binding.binding == bindings.front().binding) {
                        reject(root, entry, 2u);
                        continue;
                    }
                    id = Source::originalOrdinaryBufferMemberMemory(
                        root, entry, binding.set, binding.binding, members, access, index, 2u);
                    if (id)
                        std::fprintf(stderr, "Runtime member Root: %s\n", id);
                    assert(!id && members.hasRuntimeArray
                        && members.sourceElementCapacity == binding.capacity
                        && access.bufferSize == binding.stride * binding.capacity
                        && access.stages == binding.stage && index < sources->sources().size());
                    const auto* original =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            &sources->sources()[index]);
                    assert(original && (*original)->resource() == access.resource);
                    rhi::ShaderBufferMemoryRequirement range;
                    rhi::BindlessResourceAccess rangeAccess;
                    std::size_t rangeIndex = SIZE_MAX;
                    assert(!Source::originalOrdinaryBufferRequirement(root,
                        entry,
                        binding.set,
                        binding.binding,
                        range,
                        rangeAccess,
                        rangeIndex,
                        2u));
                    assert(rangeIndex == index && rangeAccess.resource == access.resource
                        && rangeAccess.bufferOffset == access.bufferOffset
                        && rangeAccess.bufferSize == access.bufferSize
                        && members.sourceElementCapacity == range.sourceElementCapacity);
                }
                return nullptr;
            }));
        std::printf(
            "BINDLESS_GRAPHICS_RUNTIME_MEMBER2_OK backend%u fault%u undersized%u elements4 pipeline-capacity0 legacy1-refused owner-refused hot0\n",
            static_cast<unsigned>(device->backend()),
            unsigned(fault),
            unsigned(undersized));
    }
};
} // namespace arp::test
