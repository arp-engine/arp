#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandResourceLeaseFixture final {
    static constexpr std::uint64_t metadataBytes = 65536u;

public:
    static void verifySamplerLease(rhi::GpuDevice& device)
    {
        rhi::SamplerDesc sampler;
        verifySource<rhi::GpuSampler>([&] {
            return device.createSampler(sampler);
        });
    }

    static void verify(rhi::GpuDevice& device)
    {
        rhi::BufferDesc buffer;
        buffer.sizeBytes = 64u;
        buffer.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        rhi::TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.format = rhi::TextureFormat::Rgba8Unorm;
        texture.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
        verifySource<rhi::GpuBuffer>([&] {
            return device.createBuffer(buffer);
        });
        verifySource<rhi::GpuTexture>([&] {
            return device.createTexture(texture);
        });
        auto viewedDesc = texture;
        viewedDesc.usage = viewedDesc.usage | rhi::TextureUsage::Sampled;
        auto viewedTexture = device.createTexture(viewedDesc);
        assert(viewedTexture);
        rhi::TextureViewDesc view;
        view.format = texture.format;
        view.usage = rhi::TextureUsage::Sampled;
        verifySource<rhi::GpuTextureView>([&] {
            return device.createTextureView(*viewedTexture, view);
        });
        verifySamplerLease(device);
        verifySources(device, buffer, texture);
        std::puts(
            "BINDLESS_RHI_GENERIC_ORIGINAL_RESOURCE_LEASE_OK Buffer-Texture exact-original stable-id full-layout core-quota forged-foreign cold-hot caller-death execution-closed allocation0");
    }

private:
    static void verifySources(
        rhi::GpuDevice& device, const rhi::BufferDesc& buffer, const rhi::TextureDesc& texture)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using B = rhi::OriginalCommandAdmissionBudget;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = 5u;
        budget.maxReferences = 4u;
        budget.maxGraphOwners = 10u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Source)] = metadataBytes;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Graph)] = metadataBytes;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 3u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 1u;
        const rhi::OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 2u, metadataBytes};
        const char* diagnostic = nullptr;
        auto firstBuffer = device.createBuffer(buffer), secondBuffer = device.createBuffer(buffer),
             foreignBuffer = device.createBuffer(buffer);
        auto originalTexture = device.createTexture(texture);
        assert(firstBuffer && secondBuffer && foreignBuffer && originalTexture);
        auto first = rhi::OriginalCommandBufferLease::prepareOriginal(
            firstBuffer, leaseBudget, quota, diagnostic);
        auto second = rhi::OriginalCommandBufferLease::prepareOriginal(
            secondBuffer, leaseBudget, quota, diagnostic);
        auto image = rhi::OriginalCommandTextureLease::prepareOriginal(
            originalTexture, leaseBudget, quota, diagnostic);
        auto foreign = rhi::OriginalCommandBufferLease::prepareOriginal(
            foreignBuffer, leaseBudget, foreignQuota, diagnostic);
        assert(first && second && image && foreign);
        std::array<Sources::Source, 3> inputs{first, second, image};
        const auto baseline = quota->liveBytes();
        const auto factory = [&] {
            return Sources::prepareOriginal(
                inputs.data(), inputs.size(), budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        assert(quota->liveBytes() == baseline);
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
            assert(full.charged() && !factory());
        }
        const auto reject = [&](const char* id) {
            assert(
                !factory() && std::string_view(diagnostic) == id && quota->liveBytes() == baseline);
        };
        budget.maxSourceOwners[0] = 1u;
        reject("RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY");
        budget.maxSourceOwners[0] = 3u;
        budget.maxSourceOwners[1] = 0u;
        reject("RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY");
        budget.maxSourceOwners[1] = 1u;
        inputs[1] = first;
        reject("RHI-ORIGINAL-SOURCES-DUPLICATE");
        inputs[1] = second;
        inputs[1] = foreign;
        reject("RHI-ORIGINAL-SOURCES-SOURCE-QUOTA");
        inputs[1] = second;
        auto forged = std::shared_ptr<const rhi::OriginalCommandBufferLease>(quota, first.get());
        inputs[0] = forged;
        reject("RHI-ORIGINAL-SOURCE-LEASE-OWNER");
        inputs[0] = first;
        forged.reset();
        inputs[0] = std::shared_ptr<const rhi::OriginalCommandBufferLease>{};
        reject("RHI-ORIGINAL-SOURCE-LEASE-OWNER");
        inputs[0] = first;
        assert(!Sources::prepareOriginal(nullptr, inputs.size(), budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-INPUT");
        assert(!Sources::prepareOriginal(
                   inputs.data(), inputs.size(), budget, foreignQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-SOURCE-QUOTA");
        auto sources = factory();
        assert(sources && !sources->nativeExecutionProven() && !Sources::validateOriginal(sources));
        verifyUsage(sources, quota);
        assert(sources->sources().size() == 3u
            && Sources::resource(sources->sources()[0]) == first->resource());
        assert(Sources::domain(sources->sources()[2]) == rhi::CommandReferenceDomain::Texture);
        auto forgedTable = std::shared_ptr<const Sources>(quota, sources.get());
        assert(Sources::validateOriginal(forgedTable));
        forgedTable.reset();
        auto& desc = const_cast<rhi::BufferDesc&>(first->originalObject()->desc());
        ++desc.sizeBytes;
        assert(CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            return Sources::validateOriginal(sources);
        }));
        --desc.sizeBytes;
        std::weak_ptr<const rhi::OriginalCommandBufferLease> observed = first;
        inputs = {};
        first.reset();
        second.reset();
        image.reset();
        foreign.reset();
        assert(!foreignQuota->liveBytes());
        assert(!observed.expired()
            && !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
                   return Sources::validateOriginal(sources);
               }));
        sources.reset();
        assert(observed.expired());
        observed.reset();
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_SOURCES_OK %llu typed-original-table Source-quota domains-independent duplicates-forged-foreign-reject caller-drop hot-exhausted-allocation0 execution-closed\n",
            static_cast<unsigned long long>(peak));
    }

    template <class T, class Create> static void verifySource(Create&& create)
    {
        using Lease = rhi::OriginalCommandResourceLease<T>;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto resourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const auto createOriginal = [&] {
            core::CpuMemoryQuotaScope scope(resourceQuota);
            return create();
        };
        rhi::OriginalCommandResourceLeaseBudget budget{
            1u, true, Lease::retainedOwnerCount, metadataBytes};
        auto source = createOriginal();
        assert(source);
        const auto* ptr = source.get();
        const auto resource = source->id();
        const auto originalCharge = core::inspectCpuAllocationContaining(ptr);
        assert(originalCharge.found && originalCharge.quota == resourceQuota.get());
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            auto receipt = Lease::prepareColdOriginal(source, budget, quota, diagnostic);
            assert(source.get() == ptr);
            if (receipt)
                assert(
                    !receipt->originalObject() && !Lease::validateColdOriginal(receipt, *source));
            return receipt;
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease exhausted(quota, metadataBytes);
            assert(exhausted.charged() && !factory());
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        budget.contractVersion = 0u;
        reject("RHI-ORIGINAL-SOURCE-LEASE-VERSION");
        budget.contractVersion = 1u;
        budget.prototypeOptIn = false;
        reject("RHI-ORIGINAL-SOURCE-LEASE-OPT-IN");
        budget.prototypeOptIn = true;
        --budget.maxRetainedOwners;
        reject("RHI-ORIGINAL-SOURCE-LEASE-OWNER-CAPACITY");
        ++budget.maxRetainedOwners;
        --budget.maxCpuMetadataBytes;
        reject("RHI-ORIGINAL-SOURCE-LEASE-QUOTA");
        ++budget.maxCpuMetadataBytes;
        assert(!Lease::prepareColdOriginal(source, budget, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCE-LEASE-QUOTA");
        core::UniquePtr<T> missing;
        assert(!Lease::prepareColdOriginal(missing, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCE-LEASE-OBJECT-MISSING");
        auto receipt = factory();
        assert(receipt && !receipt->nativeExecutionProven());
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(!arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend)
                    .executionAvailable);
        auto forged = std::shared_ptr<Lease>(quota, receipt.get());
        assert(Lease::adoptOriginal(forged, source) && source.get() == ptr);
        forged.reset();
        auto foreign = createOriginal();
        assert(foreign && foreign->id() != resource);
        assert(Lease::adoptOriginal(receipt, foreign) && foreign && source.get() == ptr);
        foreign.reset();
        quota->setLimitBytes(metadataBytes - 1u);
        assert(Lease::adoptOriginal(receipt, source) && source.get() == ptr);
        quota->setLimitBytes(metadataBytes);
        auto& desc =
            const_cast<std::remove_const_t<std::remove_reference_t<decltype(source->desc())>>&>(
                source->desc());
        const auto saved = desc;
        const auto rejectMutation = [&](auto mutate) {
            mutate(desc);
            const auto* id = CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota, resourceQuota}, [&]() noexcept {
                    if (!Lease::validateColdOriginal(receipt, *source))
                        return "TEST-MUTATION-NOT-REJECTED";
                    return Lease::adoptOriginal(receipt, source);
                });
            assert(id && std::string_view(id) == "RHI-ORIGINAL-SOURCE-LEASE-OBJECT-MISMATCH"
                && source.get() == ptr);
            desc = saved;
        };
        if constexpr (std::is_same_v<T, rhi::GpuBuffer>) {
            rejectMutation([](auto& d) {
                ++d.sizeBytes;
            });
            rejectMutation([](auto& d) {
                d.usage = rhi::ResourceUsage::None;
            });
        } else if constexpr (std::is_same_v<T, rhi::GpuTexture>) {
            rejectMutation([](auto& d) {
                ++d.extent.width;
            });
            rejectMutation([](auto& d) {
                ++d.extent.height;
            });
            rejectMutation([](auto& d) {
                ++d.extent.depth;
            });
            rejectMutation([](auto& d) {
                d.format = rhi::TextureFormat::Unknown;
            });
            rejectMutation([](auto& d) {
                d.usage = rhi::TextureUsage::None;
            });
            rejectMutation([](auto& d) {
                ++d.mipLevelCount;
            });
            rejectMutation([](auto& d) {
                ++d.arrayLayerCount;
            });
            rejectMutation([](auto& d) {
                ++d.sampleCount;
            });
            rejectMutation([](auto& d) {
                d.cubeCompatible = !d.cubeCompatible;
            });
        } else if constexpr (std::is_same_v<T, rhi::GpuTextureView>) {
            rejectMutation([](auto& d) {
                d.format = rhi::TextureFormat::Unknown;
            });
            rejectMutation([](auto& d) {
                d.usage = rhi::TextureUsage::None;
            });
            rejectMutation([](auto& d) {
                d.dimension = rhi::TextureViewDimension::Texture3D;
            });
            rejectMutation([](auto& d) {
                ++d.baseMipLevel;
            });
            rejectMutation([](auto& d) {
                ++d.mipLevelCount;
            });
            rejectMutation([](auto& d) {
                ++d.baseArrayLayer;
            });
            rejectMutation([](auto& d) {
                ++d.arrayLayerCount;
            });
            rejectMutation([](auto& d) {
                d.aspect = rhi::TextureAspect::Depth;
            });
        } else {
            rejectMutation([](auto& d) {
                d.minFilter = rhi::SamplerFilter::Nearest;
            });
            rejectMutation([](auto& d) {
                d.magFilter = rhi::SamplerFilter::Nearest;
            });
            rejectMutation([](auto& d) {
                d.mipFilter = rhi::SamplerMipFilter::Nearest;
            });
            rejectMutation([](auto& d) {
                d.addressU = rhi::SamplerAddressMode::Repeat;
            });
            rejectMutation([](auto& d) {
                d.addressV = rhi::SamplerAddressMode::Repeat;
            });
            rejectMutation([](auto& d) {
                d.addressW = rhi::SamplerAddressMode::Repeat;
            });
            rejectMutation([](auto& d) {
                d.compareOp = rhi::CompareOp::Less;
            });
            rejectMutation([](auto& d) {
                d.maxAnisotropy = 2.0f;
            });
        }
        if constexpr (std::is_same_v<T, rhi::GpuBuffer> || std::is_same_v<T, rhi::GpuTexture>) {
            rejectMutation([](auto& d) {
                d.memory = rhi::MemoryUsage::CpuToGpu;
            });
            rejectMutation([](auto& d) {
                d.initialState = rhi::ResourceState::CopySrc;
            });
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, resourceQuota}, [&]() noexcept {
                return Lease::adoptOriginal(receipt, source);
            }));
        assert(!source && receipt->originalObject() == ptr && receipt->resource() == resource
            && !Lease::validateOriginal(receipt));
        const auto after = core::inspectCpuAllocationContaining(ptr);
        assert(after.found && after.allocationIdentity == originalCharge.allocationIdentity
            && after.quota == originalCharge.quota);
        assert(Lease::adoptOriginal(receipt, source));
        auto changed = createOriginal();
        assert(changed);
        assert(Lease::adoptOriginal(receipt, changed) && changed);
        changed.reset();
        // Revalidation is separate from registry/native/state proof and rejects
        // physical mutation even after the object has been adopted.
        if constexpr (std::is_same_v<T, rhi::GpuBuffer>)
            ++desc.sizeBytes;
        else if constexpr (std::is_same_v<T, rhi::GpuTexture>)
            ++desc.extent.width;
        else if constexpr (std::is_same_v<T, rhi::GpuTextureView>)
            ++desc.baseMipLevel;
        else
            desc.magFilter = rhi::SamplerFilter::Nearest;
        assert(Lease::validateOriginal(receipt));
        desc = saved;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, resourceQuota}, [&]() noexcept {
                return Lease::validateOriginal(receipt);
            }));
        receipt.reset();
        assert(!quota->liveBytes());
        source = createOriginal();
        assert(source);
        ptr = source.get();
        receipt = factory();
        assert(receipt);
        source.reset();
        assert(
            !receipt->originalObject() && receipt->validate()); // Never dereference a dead receipt.
        receipt.reset();
        assert(!quota->liveBytes());
        source = createOriginal();
        assert(source);
        ptr = source.get();
        auto owned = Lease::prepareOriginal(source, budget, quota, diagnostic);
        assert(
            owned && !source && owned->originalObject() == ptr && !Lease::validateOriginal(owned));
        owned.reset();
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_RESOURCE_LEASE_COLD_OK %u %llu exact-less-exhausted original-charge-preserved full-physical-mutation-reject\n",
            static_cast<unsigned>(Lease::domain),
            static_cast<unsigned long long>(peak));
    }

    static void verifyUsage(std::shared_ptr<const rhi::OriginalCommandResourceSources> sources,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota)
    {
        using Usage = rhi::OriginalCommandUsage;
        using K = rhi::CommandUsageKind;
        using Sources = rhi::OriginalCommandResourceSources;
        std::array<rhi::CommandUsage, 5> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::BufferCopy, 0u, 2u},
            {K::TextureToTextureCopy, 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        std::array<Usage::Reference, 4> references{{{0u}, {1u}, {2u}, {2u}}};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Usage::prepareOriginal(sources,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                quota,
                diagnostic);
        };
        const auto sourceBaseline = sourceQuota->liveBytes();
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        // The nested scalar root uses the same Graph quota. Measure both roots
        // before exhausted probes saturate that quota's historical high water.
        {
            auto scalarUsage = factory();
            assert(scalarUsage);
            verifyScalars(scalarUsage, quota, sourceQuota);
        }
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes());
            assert(full.charged() && !factory());
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes()
                && sourceQuota->liveBytes() == sourceBaseline);
        };
        references[0].sourceIndex = 3u;
        reject("RHI-ORIGINAL-USAGE-SOURCE-INDEX");
        references[0].sourceIndex = 0u;
        references[0].sourceIndex = 2u;
        reject("RHI-COMMAND-USAGE-SIGNATURE");
        references[0].sourceIndex = 0u;
        const auto saved = commands;
        commands[1].firstReference = 1u;
        reject("RHI-COMMAND-USAGE-SPAN");
        commands = saved;
        commands[1].referenceCount = UINT32_MAX;
        reject("RHI-COMMAND-USAGE-SPAN");
        commands = saved;
        commands.back().kind = K::EndTransferPass;
        reject("RHI-COMMAND-USAGE-FINISH");
        commands = saved;
        commands[0].kind = K::Finish;
        reject("RHI-COMMAND-USAGE-FINISH");
        commands = saved;
        commands[0].kind = K::BeginComputePass;
        reject("RHI-COMMAND-ORDER-SCOPE");
        commands = saved;
        commands[1].kind = K::LegacySizeOnlyBufferCopy;
        reject("RHI-COMMAND-USAGE-SIGNATURE");
        commands = saved;
        // Unsupported source families cannot be passed off as scalar commands.
        for (const auto kind : {K::Timestamp,
                 K::RetainIndexedPipeline,
                 K::RecordIndirectProducer,
                 K::RecordIndirectProcessor,
                 K::ExecuteRenderBundle,
                 K::BindComputePipeline}) {
            commands[1].kind = kind;
            reject("RHI-COMMAND-USAGE-SIGNATURE");
            commands = saved;
        }
        assert(!Usage::prepareOriginal(sources,
                   commands.data(),
                   commands.size(),
                   references.data(),
                   references.size(),
                   sourceQuota,
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-USAGE-QUOTA-DOMAIN");
        auto forgedSources = std::shared_ptr<const Sources>(quota, sources.get());
        assert(!Usage::prepareOriginal(forgedSources,
            commands.data(),
            commands.size(),
            references.data(),
            references.size(),
            quota,
            diagnostic));
        forgedSources.reset();
        for (unsigned limit = 0u; limit < 3u; ++limit) {
            auto budget = sources->originalBudget();
            if (limit == 0u)
                --budget.maxCommands;
            if (limit == 1u)
                --budget.maxReferences;
            if (limit == 2u)
                budget.maxGraphOwners = 7u;
            auto bounded = Sources::prepareOriginal(sources->sources().data(),
                sources->sources().size(),
                budget,
                sourceQuota,
                diagnostic);
            assert(bounded);
            assert(!Usage::prepareOriginal(bounded,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                quota,
                diagnostic));
            assert(std::string_view(diagnostic)
                == (limit == 2u ? "RHI-ORIGINAL-USAGE-OWNER-CAPACITY"
                                : "RHI-ORIGINAL-USAGE-CAPACITY"));
            bounded.reset();
            assert(sourceQuota->liveBytes() == sourceBaseline && !quota->liveBytes());
        }
        quota->setLimitBytes(metadataBytes - 1u);
        reject("RHI-ORIGINAL-USAGE-QUOTA");
        quota->setLimitBytes(metadataBytes);
        auto usage = factory();
        assert(usage && !Usage::validateOriginal(usage) && !usage->nativeExecutionProven());
        auto forgedUsage = std::shared_ptr<const Usage>(quota, usage.get());
        assert(Usage::validateOriginal(forgedUsage));
        forgedUsage.reset();
        const auto& buffer =
            *std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(sources->sources()[0])
                 ->originalObject();
        const auto& peer =
            *std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(sources->sources()[1])
                 ->originalObject();
        const auto& texture = *std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
            sources->sources()[2])
                                   ->originalObject();
        assert(!usage->compareOriginalReference(0u, buffer)
            && usage->compareOriginalReference(0u, peer));
        assert(usage->compareOriginalReference(0u, texture)
            && !usage->compareOriginalReference(2u, texture)
            && usage->compareOriginalReference(4u, buffer));
        // Caller command/reference array mutation cannot alter the frozen stream.
        commands[1].kind = K::Finish;
        references[0].sourceIndex = 2u;
        assert(
            usage->commands()[1].kind == K::BufferCopy && usage->references()[0].sourceIndex == 0u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, sourceQuota}, [&]() noexcept {
                if (const auto* id = Usage::validateOriginal(usage))
                    return id;
                if (const auto* id = usage->compareOriginalReference(0u, buffer))
                    return id;
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!usage->executionDiagnostic(backend))
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        usage.reset();
        assert(!quota->liveBytes() && sourceQuota->liveBytes() == sourceBaseline);
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_USAGE_OK %llu exact-source-table immutable-command-references shared-span-signature scope independent-quotas forbidden-families-reject execution-closed allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyScalars(std::shared_ptr<const rhi::OriginalCommandUsage> usage,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota)
    {
        using Frozen = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using K = rhi::CommandUsageKind;
        std::array<Store::Input, 5> input{{{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
            {*Scalar::capture(K::BufferCopy, Scalar::None{})},
            {*Scalar::capture(K::TextureToTextureCopy, Scalar::None{})},
            {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
            {*Scalar::capture(K::Finish, Scalar::None{})}}};
        const rhi::CommandVariableArgumentsBudget budget{1u, true, 5u, 0u, 32u, 0u, metadataBytes};
        const char* diagnostic = nullptr;
        const auto usageBaseline = quota->liveBytes();
        auto arguments =
            Store::prepareOriginal(input.data(), input.size(), budget, quota, diagnostic);
        assert(arguments);
        const auto baseline = quota->liveBytes();
        const auto factory = [&] {
            return Frozen::prepareOriginal(usage, arguments, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        assert(quota->liveBytes() == baseline);
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
            assert(full.charged() && !factory());
        }
        auto originalArguments = arguments;
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
        };
        arguments = std::shared_ptr<const Store>(sourceQuota, originalArguments.get());
        reject("RHI-ORIGINAL-SCALARS-ARGUMENTS-OWNER");
        arguments = originalArguments;
        auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        arguments =
            Store::prepareOriginal(input.data(), input.size(), budget, foreignQuota, diagnostic);
        assert(arguments);
        reject("RHI-ORIGINAL-SCALARS-GRAPH-QUOTA");
        arguments = originalArguments;
        assert(!foreignQuota->liveBytes());
        arguments =
            Store::prepareOriginal(input.data(), input.size() - 1u, budget, quota, diagnostic);
        assert(arguments);
        reject("RHI-ORIGINAL-SCALARS-COUNT");
        arguments = originalArguments;
        input[1].scalar = *Scalar::capture(K::RetainTexture, Scalar::None{});
        arguments = Store::prepareOriginal(input.data(), input.size(), budget, quota, diagnostic);
        assert(arguments);
        reject("RHI-ORIGINAL-SCALARS-ORDER");
        arguments = originalArguments;
        input[1].scalar = *Scalar::capture(K::BufferCopy, Scalar::None{});
        // The actual original store must fit the full graph's variable bounds,
        // even when separately prepared with a larger permitted local limit.
        input[0].text = "label exceeds disabled full graph text budget";
        auto larger = budget;
        larger.maxTextBytes = input[0].text.size();
        arguments = Store::prepareOriginal(input.data(), input.size(), larger, quota, diagnostic);
        assert(arguments);
        reject("RHI-COMMAND-ARGUMENTS-CAPACITY");
        arguments = originalArguments;
        input[0].text = {};
        assert(quota->liveBytes() == baseline);
        {
            auto boundedBudget = usage->originalSources()->originalBudget();
            boundedBudget.maxGraphOwners = usage->retainedOwnerCount() + 1u;
            using Sources = rhi::OriginalCommandResourceSources;
            const auto& original = usage->originalSources()->sources();
            auto boundedSources = Sources::prepareOriginal(
                original.data(), original.size(), boundedBudget, sourceQuota, diagnostic);
            assert(boundedSources);
            auto boundedUsage = rhi::OriginalCommandUsage::prepareOriginal(boundedSources,
                usage->commands().data(),
                usage->commands().size(),
                usage->references().data(),
                usage->references().size(),
                quota,
                diagnostic);
            assert(boundedUsage);
            assert(!Frozen::prepareOriginal(boundedUsage, arguments, quota, diagnostic)
                && std::string_view(diagnostic) == "RHI-ORIGINAL-SCALARS-OWNER-CAPACITY");
        }
        assert(quota->liveBytes() == baseline);
        auto frozen = factory();
        assert(frozen && !Frozen::validateOriginal(frozen) && !frozen->nativeExecutionProven());
        assert(frozen->retainedOwnerCount() == usage->retainedOwnerCount() + 2u
            && frozen->originalUsage() == usage && frozen->originalArguments() == arguments);
        auto forged = std::shared_ptr<const Frozen>(sourceQuota, frozen.get());
        assert(Frozen::validateOriginal(forged));
        forged.reset();
        const auto originalBaseline = quota->liveBytes();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, sourceQuota}, [&]() noexcept {
                if (const auto* id = Frozen::validateOriginal(frozen))
                    return id;
                for (std::size_t i = 0u; i < input.size(); ++i)
                    if (const auto* id = frozen->compareOriginalArguments(i, input[i]))
                        return id;
                if (!frozen->compareOriginalArguments(input.size(), input[0]))
                    return "TEST-INVALID-INDEX-ACCEPTED";
                input[1].scalar = *Scalar::capture(K::RetainTexture, Scalar::None{});
                const auto* rejection = frozen->compareOriginalArguments(1u, input[1]);
                input[1].scalar = *Scalar::capture(K::BufferCopy, Scalar::None{});
                if (!rejection)
                    return "TEST-ALTERED-INPUT-ACCEPTED";
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!frozen->executionDiagnostic(backend))
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        assert(quota->liveBytes() == originalBaseline);
        std::weak_ptr<const Store> observed = arguments;
        arguments.reset();
        originalArguments.reset();
        usage.reset();
        assert(!observed.expired() && !Frozen::validateOriginal(frozen));
        frozen.reset();
        assert(observed.expired());
        observed.reset();
        assert(quota->liveBytes() == usageBaseline);
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_SCALARS_OK %llu exact-usage-arguments Graph-quota shared-kind-binding actual-variable-bounds forged-foreign-count-order-reject hot-exhausted-allocation0 execution-closed\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
