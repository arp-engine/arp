#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/transfer_readback_input.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class TextureReadbackOwnershipQuotaFixture final {
public:
    static void verify(rhi::GpuDevice& device)
    {
        using Input = rhi::TextureReadbackInputSnapshot;
        using Layout = rhi::TextureReadbackLayout;
        using Request = rhi::TextureReadbackRequestDeclaration;
        using Lease = rhi::TextureReadbackSourceLease;
        constexpr std::uint64_t metadataBytes = 65536u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> quotas{};
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        rhi::TextureDesc physical;
        physical.extent = {2u, 2u, 1u};
        physical.format = rhi::TextureFormat::Rgba8Unorm;
        physical.usage = rhi::TextureUsage::CopySrc;
        physical.mipLevelCount = 2u;
        auto texture = device.createTexture(physical);
        assert(texture);
        const auto* original = texture.get();
        rhi::BufferDesc buffer;
        buffer.sizeBytes = 768u;
        buffer.usage = rhi::ResourceUsage::CopyDst;
        auto destination = device.createBuffer(buffer);
        assert(destination);
        rhi::ReadbackTextureDesc desc;
        desc.sourceResource = texture->id();
        desc.destinationReadbackBuffer = destination->id();
        desc.sizeBytes = 768u;
        desc.extent = physical.extent;
        desc.bytesPerRow = 256u;
        desc.rowsPerImage = 2u;
        desc.sourceSubresources.mipLevelCount = 2u;
        desc.label = "readback";
        desc.mipLayouts = {{0u, {2u, 2u, 1u}, 256u, 2u, 0u}, {1u, {1u, 1u, 1u}, 256u, 1u, 512u}};
        const char* diagnostic = nullptr;
        auto input = Input::prepareOriginal(desc,
            {1u, true, 2u, 8u, metadataBytes, metadataBytes},
            quotas[0],
            quotas[1],
            diagnostic);
        assert(input);
        const auto layoutFactory = [&] {
            return Layout::prepareOriginal(
                input, physical, {1u, true, 2u, 768u, metadataBytes}, quotas[2], diagnostic);
        };
        CpuQuotaFactoryFixture layoutPressure(quotas[2]), requestPressure(quotas[3]),
            leasePressure(quotas[4]);
        const auto layoutPeak = layoutPressure.measure(layoutFactory);
        auto layout = layoutFactory();
        assert(layout);
        const auto requestFactory = [&] {
            return Request::prepareOriginal(layout,
                {17u, 23u, 29u},
                {1u, true, 2u, 8u, 768u, metadataBytes},
                quotas[3],
                diagnostic);
        };
        const auto requestPeak = requestPressure.measure(requestFactory);
        auto request = requestFactory();
        assert(request);
        const rhi::TextureReadbackSourceLeaseBudget leaseBudget{
            1u, true, 2u, 2u, 8u, metadataBytes};
        const auto leaseFactory = [&] {
            return Lease::prepareColdOriginal(texture, request, leaseBudget, quotas[4], diagnostic);
        };
        const auto leasePeak = leasePressure.measure(leaseFactory);
        layoutPressure.verify(layoutPeak, layoutFactory);
        requestPressure.verify(requestPeak, requestFactory);
        leasePressure.verify(leasePeak, leaseFactory);
        assert(texture.get() == original);
        auto receipt = leaseFactory();
        assert(receipt && !receipt->sourceTexture() && receipt->validate()
            && !Lease::validateColdOriginal(receipt, *texture));
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            const auto limit = quotas[i]->limitBytes();
            quotas[i]->setLimitBytes(limit - 1u);
            assert(Lease::validateColdOriginal(receipt, *texture)
                && Lease::adoptOriginal(receipt, texture) && texture.get() == original);
            quotas[i]->setLimitBytes(limit);
            assert(!Lease::validateColdOriginal(receipt, *texture));
        }
        assert(!Lease::adoptOriginal(receipt, texture) && !texture
            && receipt->sourceTexture() == original);
        std::shared_ptr<const Lease> lease = receipt;
        receipt.reset();
        assert(!Lease::validateOriginal(lease, request) && !Layout::validateOriginal(layout)
            && !Request::validateOriginal(request, layout));
        auto forgedLayout = std::shared_ptr<const Layout>(quotas[0], layout.get());
        assert(Layout::validateOriginal(forgedLayout));
        auto forgedRequest = std::shared_ptr<const Request>(quotas[0], request.get());
        assert(Request::validateOriginal(forgedRequest, layout));
        auto forgedLease = std::shared_ptr<const Lease>(quotas[0], lease.get());
        assert(Lease::validateOriginal(forgedLease, request));
        forgedLayout.reset();
        forgedRequest.reset();
        forgedLease.reset();
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            const auto limit = quotas[i]->limitBytes();
            quotas[i]->setLimitBytes(limit - 1u);
            assert(Lease::validateOriginal(lease, request));
            if (i < 3u)
                assert(Layout::validateOriginal(layout));
            if (i < 4u)
                assert(Request::validateOriginal(request, layout));
            quotas[i]->setLimitBytes(limit);
            assert(!Lease::validateOriginal(lease, request));
        }
        {
            auto equalLayout = layoutFactory();
            assert(equalLayout && equalLayout->matches(desc, physical));
            assert(Request::validateOriginal(request, equalLayout));
            auto equalRequest = requestFactory();
            assert(equalRequest && equalRequest->identity() == request->identity());
            assert(Lease::validateOriginal(lease, equalRequest));
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
            if (const auto* id = Lease::validateOriginal(lease, request))
                return id;
            if (const auto* id = Request::validateOriginal(request, layout))
                return id;
            if (const auto* id = Layout::validateOriginal(layout))
                return id;
            if (!layout->matches(desc, physical))
                return "TEST-READBACK-CHAIN-MISMATCH";
            for (std::size_t i = 0u; i < layout->mipCopies().size(); ++i)
                if (const auto* id = Layout::validateOriginalMip(layout, i, layout->mipCopies()[i]))
                    return id;
            auto& frozenMip = const_cast<Layout::Mip&>(layout->mipCopies()[1u]);
            ++frozenMip.rowBytes;
            const auto* mappingDifferent = Lease::validateOriginal(lease, request);
            --frozenMip.rowBytes;
            if (!mappingDifferent
                || std::string_view(mappingDifferent) != "RHI-TEXTURE-READBACK-LAYOUT-MIP-MISMATCH")
                return "TEST-MUTATED-FROZEN-MIP-ACCEPTED";
            auto& observed = const_cast<rhi::TextureDesc&>(original->desc());
            ++observed.extent.width;
            const auto* different = lease->validate();
            --observed.extent.width;
            if (!different)
                return "TEST-MUTATED-SOURCE-ACCEPTED";
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                if (arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend)
                        .executionAvailable
                    || lease->nativeExecutionProven())
                    return "TEST-EXECUTION-OPEN";
            return static_cast<const char*>(nullptr);
        }));
        std::weak_ptr<const Request> observedRequest = request;
        std::weak_ptr<const Layout> observedLayout = layout;
        std::weak_ptr<const Input> observedInput = input;
        request.reset();
        layout.reset();
        input.reset();
        destination.reset();
        desc.label.clear();
        desc.mipLayouts.clear();
        assert(!observedRequest.expired() && !observedLayout.expired() && !observedInput.expired()
            && !Lease::validateOriginal(lease, lease->originalRequest())
            && lease->sourceTexture() == original);
        lease.reset();
        assert(observedRequest.expired() && observedLayout.expired() && observedInput.expired());
        observedRequest.reset();
        observedLayout.reset();
        observedInput.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_OWNERSHIP_QUOTAS_OK %llu %llu %llu original-input-label-layout-request-lease quotas5 immutable-limits canonical-chain shared-mip-validator cold-pressure failed-adoption-preserved single-source caller-drop hot-allocation0 native-unproven\n",
            static_cast<unsigned long long>(layoutPeak),
            static_cast<unsigned long long>(requestPeak),
            static_cast<unsigned long long>(leasePeak));
    }
};
} // namespace arp::test
