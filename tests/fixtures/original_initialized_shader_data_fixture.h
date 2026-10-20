#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/gpu_extensions.h"

namespace arp::test {
// Runs inside each genuine backend's initialized-geometry smoke. It checks the
// original mapped initializer, exact lease/content association and rollback;
// numerical GPU consumption is a separate visibility execution gate.
template <class Content> class OriginalInitializedShaderDataFixture final {
public:
    static void verify(rhi::GpuDevice& device, std::uint64_t queriedAllocationCeiling)
    {
        using namespace rhi;
        using Contract = OriginalHostGeometryContentContract;
        constexpr std::uint64_t cpu = 65536u;
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        for (const auto type :
            {DescriptorType::UniformBuffer, DescriptorType::ReadOnlyStorageBuffer}) {
            const typename Content::Quotas quotas{core::makeShared<core::CpuMemoryQuota>(cpu),
                core::makeShared<core::CpuMemoryQuota>(64u),
                core::makeShared<core::CpuMemoryQuota>(cpu),
                core::makeShared<core::CpuMemoryQuota>(cpu)};
            std::array<std::uint8_t, 64u> bytes{};
            for (std::size_t i = 0u; i < bytes.size(); ++i)
                bytes[i] = static_cast<std::uint8_t>(i * 3u);
            const auto original = bytes;
            const BufferDesc desc{"sealed-shader-input",
                bytes.size(),
                type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                      : ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead};
            const Contract::Budget budget{2u,
                true,
                1u,
                Contract::retainedOwnerBound,
                queriedAllocationCeiling,
                cpu,
                bytes.size(),
                cpu,
                cpu,
                Contract::Staging::ShaderContentIntent{1u, true, type, bytes.size()}};
            const char* id = nullptr;
            const auto allocation = extension->queryOriginalInitializedBufferAllocation(
                device, desc, budget.contractVersion, id);
            assert(!id && allocation >= desc.sizeBytes && allocation <= queriedAllocationCeiling);
            const auto prepare = [&]() {
                return std::dynamic_pointer_cast<const Content>(
                    extension->prepareOriginalInitializedBuffer(
                        device, {desc, bytes.data(), bytes.size(), budget, quotas}, id));
            };
            {
                core::CpuMemoryQuotaLease pressure(quotas.metadata, cpu);
                assert(pressure.charged() && !prepare() && id);
            }
            for (const auto& quota :
                {quotas.metadata, quotas.payload, quotas.resource, quotas.lease})
                assert(!quota->liveBytes());
            auto actor = prepare();
            if (!actor)
                std::fprintf(stderr, "Initialized shader: %s\n", id ? id : "missing");
            assert(actor && !id);
            const auto& lease = actor->originalLease();
            bytes.fill(0xffu);
            bool visited = false;
            const auto inspect = [&]() noexcept {
                return Content::visitInitializedShader(
                    actor, lease, type, [&](const void* frozen, std::size_t size) noexcept {
                        visited = true;
                        return Contract::validateShaderBytes(budget.definition,
                            type,
                            frozen,
                            size,
                            original.data(),
                            original.size());
                    });
            };
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                       std::array{quotas.metadata, quotas.payload, quotas.resource, quotas.lease},
                       inspect)
                && visited);
            auto forgedActor = std::shared_ptr<const Content>(quotas.metadata, actor.get());
            visited = false;
            assert(Content::visitInitializedShader(forgedActor,
                       lease,
                       type,
                       [&](const void*, std::size_t) noexcept -> const char* {
                           visited = true;
                           return nullptr;
                       })
                && !visited);
            forgedActor.reset();
            auto forgedLease =
                std::shared_ptr<const OriginalCommandBufferLease>(quotas.metadata, lease.get());
            assert(Content::visitInitializedShader(actor,
                       forgedLease,
                       type,
                       [&](const void*, std::size_t) noexcept -> const char* {
                           visited = true;
                           return nullptr;
                       })
                && !visited);
            forgedLease.reset();
            assert(!device
                    .writeBuffer(*const_cast<GpuBuffer*>(lease->originalObject()),
                        bytes.data(),
                        bytes.size())
                    .success);
            {
                using Custody = OriginalBindlessInitializedShaderContract;
                const std::array<std::shared_ptr<const OriginalInitializedGeometrySource>, 1u>
                    inputs{actor};
                const Custody::Budget custody{1u, true, 1u, bytes.size(), 16u};
                assert(!Custody::validatePublicInputs(custody, inputs.data(), inputs.size()));
                auto small = custody;
                --small.maxBytes;
                assert(Custody::validatePublicInputs(small, inputs.data(), inputs.size()));
                OriginalCommandAdmissionBudget admission;
                admission.contractVersion = 1u;
                admission.prototypeOptIn = true;
                admission.maxCommands = 1u;
                admission.maxGraphOwners = 16u;
                admission
                    .maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Buffer)] = 1u;
                admission.maxCpuBytes.fill(cpu);
                const OriginalCommandResourceSources::Source source{lease};
                auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(cpu);
                auto inventory = OriginalCommandResourceSources::prepareOriginal(
                    &source, 1u, admission, sourceQuota, id);
                assert(inventory && !id);

                struct Entry {
                    std::size_t sourceIndex = 0u;
                    std::shared_ptr<const Content> initializedGeometry, initializedShader;
                    std::optional<GpuBuffer::CommittedStateObservation> initializedShaderState;
                };

                std::array<Entry, 2u> entries;
                std::array<std::shared_ptr<const Content>, 1u> typed{actor};
                std::uint64_t visits = 0u;
                for (auto& entry : entries)
                    assert(!Custody::capture(
                        *inventory, custody, typed.data(), typed.size(), entry, visits));
                const std::shared_ptr<const OriginalCommandRecordingSeal> cold;
                const auto inspectRetained = [&]() noexcept {
                    return Custody::validateRetained<Content>(*inventory, custody, entries, cold);
                };
                assert(!inspectRetained()); // Descriptor aliases charge one actor/backing.
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quotas.metadata,
                                                                          quotas.payload,
                                                                          quotas.resource,
                                                                          quotas.lease,
                                                                          sourceQuota},
                    inspectRetained));
                entries[1u].sourceIndex = SIZE_MAX;
                assert(inspectRetained());
                entries[1u].sourceIndex = 0u;
                entries[1u].initializedShader =
                    std::shared_ptr<const Content>(quotas.metadata, actor.get());
                assert(inspectRetained());
                entries[1u].initializedShader = actor;
                entries[1u].initializedShader.reset();
                assert(inspectRetained()); // A stamp cannot outlive its authentic actor.
                entries[1u].initializedShaderState.reset();
                assert(!inspectRetained());
            }
            actor.reset();
            for (const auto& quota :
                {quotas.metadata, quotas.payload, quotas.resource, quotas.lease})
                assert(!quota->liveBytes());
        }
    }
};
} // namespace arp::test
