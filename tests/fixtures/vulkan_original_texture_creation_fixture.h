#pragma once
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/original_undefined_texture_upload_plan_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class VulkanOriginalTextureCreationFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void expect(const char* id, const char* wanted)
    {
        if (!id || std::string_view(id) != wanted)
            std::fprintf(stderr,
                "Vulkan ContentCreation3 actual=%s expected=%s\n",
                id ? id : "<none>",
                wanted);
        assert(id && std::string_view(id) == wanted);
    }

public:
    static void verifyAttachments(const rhi_vulkan::VulkanRhiDeviceDesc& input)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        tests::VulkanHeadlessRuntime native(true, input.runtime->getDeviceProcAddr());
        assert(native.runtime());
        auto desc = input;
        desc.runtime = native.runtime();
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> q;
        for (const auto format : {TextureFormat::Rgba8Unorm,
                 TextureFormat::Rgba16Float,
                 TextureFormat::D32Float,
                 TextureFormat::Depth24Stencil8}) {
            // Exhausted probes deliberately raise the historical peak to the
            // limit. Each format gets independent measurement history.
            for (auto& owner : q) {
                assert(!owner || !owner->liveBytes());
                owner = core::makeShared<core::CpuMemoryQuota>(bytes);
            }
            auto device = createVulkanRhiDevice(desc);
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, q[0]));
            assert(!device->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, bytes}, q[1]));
            constexpr auto owners = OriginalResourceRegistrationContract::fixedOwnerCount
                + 2u * OriginalResourceRegistrationContract::perResourceOwnerCount;
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 0u, 1u, 1u, owners, bytes}, q[2]));
            const char* id = nullptr;
            TextureDesc target;
            target.extent = {8u, 8u, 1u};
            target.mipLevelCount = 3u;
            target.arrayLayerCount = 2u;
            target.format = format;
            target.usage =
                TextureUsage::RenderAttachment | TextureUsage::Sampled | TextureUsage::CopySrc;
            target.initialState = ResourceState::Undefined;
            const auto required = device->queryOriginalContentTextureAllocation(target, id, 4u);
            assert(required && !id);
            std::uint64_t texels = 0u;
            assert(!semantics::TextureUploadDestinationValidation::footprint(target, texels));
            const auto cells = format == TextureFormat::Depth24Stencil8 ? 12u : 6u;
            OriginalTextureContentCreationBudget budget{
                4u, true, 1u, 1u, texels, 4u, bytes, cells, required};
            const auto gpuBaseline = device->memoryStats().allocatedBytes;
            const auto make = [&] {
                core::CpuMemoryQuotaScope resource(q[3]);
                return device->createOriginalContentTexture(target, budget, q[4], id);
            };
            for (unsigned failure = 0u; failure < 3u; ++failure) {
                auto small = budget;
                if (failure == 0u)
                    --small.maxNativeAllocationBytes;
                if (failure == 1u)
                    --small.maxTexelBytes;
                if (failure == 2u)
                    --small.maxContentCells;
                core::CpuMemoryQuotaScope resource(q[3]);
                assert(!device->createOriginalContentTexture(target, small, q[4], id));
                expect(id,
                    failure == 0u       ? "RHI-VULKAN-CONTENT-CREATION-NATIVE-CAPACITY"
                        : failure == 1u ? "RHI-CANONICAL-CONTENT-CREATION-BYTE-CAPACITY"
                                        : "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
                assert(!q[3]->liveBytes() && !q[4]->liveBytes()
                    && device->memoryStats().allocatedBytes == gpuBaseline);
            }
            for (const auto index : {0u, 1u, 2u, 3u}) {
                core::CpuMemoryQuotaScope resource(q[3]);
                assert(!device->createOriginalContentTexture(target, budget, q[index], id));
                expect(id, "RHI-CANONICAL-CONTENT-CREATION-QUOTA-DOMAIN");
            }
            for (const auto index : {2u, 3u, 4u}) {
                CpuQuotaFactoryFixture pressure(q[index]);
                const auto peak = pressure.measure(make);
                pressure.verify(peak, make);
                assert(device->memoryStats().allocatedBytes == gpuBaseline);
            }
            auto texture = make();
            assert(texture && !id && texture->originalContentExclusive());
            assert(texture->state() == ResourceState::Undefined
                && texture->allocation().estimatedSizeBytes == required
                && !texture->observeOriginalContent()); // /4 never exposes a whole-upload stamp.
            const auto geometry = semantics::OriginalTextureStateRange::capture(target, cells, id);
            assert(geometry && !id && geometry->cellCount() == cells);
            for (std::uint64_t cell = 0u; cell < cells; ++cell) {
                TextureSubresourceRange range;
                assert(geometry->cellRange(cell, range));
                const auto observed = texture->observeOriginalContent(range);
                assert(observed && !observed->diagnostic());
                assert(
                    observed->state() == common::OriginalTextureContentControl::State::Uninitialized
                    && !observed->contentGeneration() && !observed->nativeExecutionProven());
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                    return texture->validateOriginalContent(*observed);
                }));
            }
            TextureViewDesc viewDesc;
            viewDesc.format = format;
            viewDesc.dimension = TextureViewDimension::Texture2DArray;
            viewDesc.usage = TextureUsage::RenderAttachment;
            viewDesc.arrayLayerCount = 2u;
            viewDesc.aspect =
                isDepthTextureFormat(format) ? TextureAspect::Depth : TextureAspect::Color;
            auto view = device->createTextureView(*texture, viewDesc);
            assert(view);
            const auto payload = vulkanRhiNativePayload(*texture),
                       viewPayload = vulkanRhiNativePayload(*view);
            assert(payload && viewPayload && payload->backendVulkanOriginalContentExclusive()
                && viewPayload->backendVulkanOriginalContentExclusive());
            {
                VulkanRhiAllocator allocator(
                    {"exclusive attachment refusal", desc.memoryBudgetBytes}, native.runtime());
                VulkanRhiNativeResourceAllocator resources(native.runtime(), &allocator);
                core::pmr::String diagnostic;
                auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
                assert(command);
                RenderPassDesc pass;
                core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>> colors;
                if (isDepthTextureFormat(format)) {
                    pass.hasDepthStencilAttachment = true;
                    pass.depthStencilAttachment.target = view->id();
                    pass.depthStencilAttachment.format = format;
                    pass.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
                    pass.depthStencilAttachment.depthStoreOp = StoreOp::Store;
                } else {
                    RenderPassColorAttachmentDesc color;
                    color.target = view->id();
                    color.format = format;
                    color.loadOp = LoadOp::Clear;
                    color.storeOp = StoreOp::Store;
                    pass.colorAttachments.push_back(color);
                    colors.push_back(viewPayload);
                }
                assert(!command->recordRenderPassBegin(pass,
                    colors,
                    isDepthTextureFormat(format) ? viewPayload.get() : nullptr,
                    diagnostic));
                expect(diagnostic.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            }
            const auto stamp = texture->observeOriginalContent({0u, 1u, 0u, 1u, viewDesc.aspect});
            assert(stamp);
            device.reset();
            expect(stamp->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            view.reset();
            texture.reset();
            std::printf(
                "BINDLESS_VULKAN_ORIGINAL_ATTACHMENT_CREATION4 format=%u cells=%u logical=%llu native=%llu Undefined uninitialized exclusive\n",
                static_cast<unsigned>(format),
                cells,
                static_cast<unsigned long long>(texels),
                static_cast<unsigned long long>(required));
        }
        for (const auto& owner : q)
            assert(!owner->liveBytes());
    }

    static void verify(const rhi_vulkan::VulkanRhiDeviceDesc& input)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        tests::VulkanHeadlessRuntime native(true, input.runtime->getDeviceProcAddr());
        assert(native.runtime());
        auto desc = input;
        desc.runtime = native.runtime();
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto device = createVulkanRhiDevice(desc);
        assert(device);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, q[0]));
        assert(!device->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, bytes}, q[1]));
        constexpr auto registrationOwners = OriginalResourceRegistrationContract::fixedOwnerCount
            + 2u * OriginalResourceRegistrationContract::perResourceOwnerCount;
        assert(!device->prepareOriginalResourceRegistration(
            {1u, true, 0u, 1u, 1u, registrationOwners, bytes}, q[2]));
        const char* id = nullptr;
        TextureDesc textureDesc;
        textureDesc.extent = {2u, 2u, 1u};
        textureDesc.mipLevelCount = 2u;
        textureDesc.format = TextureFormat::Rgba8Unorm;
        textureDesc.usage = TextureUsage::CopyDst | TextureUsage::CopySrc | TextureUsage::Sampled;
        textureDesc.initialState = ResourceState::Undefined;
        const auto required = device->queryOriginalContentTextureAllocation(textureDesc, id);
        assert(required && !id);
        OriginalTextureContentCreationBudget budget{3u, true, 1u, 1u, 20u, 4u, bytes, 0u, required};
        const auto gpuBaseline = device->memoryStats().allocatedBytes;
        const auto make = [&]() {
            core::CpuMemoryQuotaScope scope(q[3]);
            return device->createOriginalContentTexture(textureDesc, budget, q[4], id);
        };
        {
            auto tooSmall = budget;
            --tooSmall.maxNativeAllocationBytes;
            core::CpuMemoryQuotaScope scope(q[3]);
            assert(!device->createOriginalContentTexture(textureDesc, tooSmall, q[4], id));
            expect(id, "RHI-VULKAN-CONTENT-CREATION-NATIVE-CAPACITY");
            assert(!q[4]->liveBytes() && !q[3]->liveBytes()
                && device->memoryStats().allocatedBytes == gpuBaseline);
        }
        for (const auto index : {0u, 1u, 2u, 3u}) {
            core::CpuMemoryQuotaScope scope(q[3]);
            assert(!device->createOriginalContentTexture(textureDesc, budget, q[index], id));
            expect(id, "RHI-CANONICAL-CONTENT-CREATION-QUOTA-DOMAIN");
        }
        for (const auto index : {2u, 3u, 4u}) {
            CpuQuotaFactoryFixture pressure(q[index]);
            const auto peak = pressure.measure(make);
            pressure.verify(peak, make);
            assert(device->memoryStats().allocatedBytes == gpuBaseline);
        }
        {
            // A live registry retains its measured bucket capacity; texture and
            // content retirement must return to that existing device baseline.
            const auto resourceBaseline = q[3]->liveBytes(), contentBaseline = q[4]->liveBytes();
            auto texture = make();
            assert(texture && !id);
            OriginalUndefinedTextureUploadPlanFixture::verifyRoot(
                *device, std::move(texture), q[4]);
            assert(q[3]->liveBytes() == resourceBaseline && q[4]->liveBytes() == contentBaseline
                && device->memoryStats().allocatedBytes == gpuBaseline);
        }
        {
            auto texture = make();
            assert(texture && !id && texture->originalContentExclusive());
            assert(texture->desc().initialState == ResourceState::Undefined
                && texture->state() == ResourceState::Undefined);
            assert(texture->allocation().estimatedSizeBytes == required);
            auto observation = texture->observeOriginalContent();
            assert(observation && !observation->diagnostic());
            assert(
                observation->state() == common::OriginalTextureContentControl::State::Uninitialized
                && !observation->contentGeneration() && !observation->nativeExecutionProven());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                return texture->validateOriginalContent(*observation);
            }));
            const ResourceBarrierDesc barrier{texture->id(),
                defaultTextureSubresourceRange(textureDesc),
                ResourceState::Undefined,
                ResourceState::CopyDst};
            auto legacy = transitionVulkanRhiTexture(*texture, barrier);
            assert(!legacy.success);
            expect(legacy.diagnostics.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            assert(texture->state() == ResourceState::Undefined && !observation->diagnostic());
            const auto payload = vulkanRhiNativePayload(*texture);
            assert(payload && payload->backendVulkanOriginalContentExclusive());
            {
                VulkanRhiAllocator allocator(
                    {"exclusive raw rejection", desc.memoryBudgetBytes}, native.runtime());
                VulkanRhiNativeResourceAllocator resources(native.runtime(), &allocator);
                core::pmr::String diagnostic;
                auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
                assert(command);
                const VulkanRhiNativeResourcePayload ordinary;
                assert(!command->recordImageBarrier(*payload, barrier, diagnostic));
                expect(diagnostic.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                assert(!command->recordImageBarrier(*payload,
                    barrier.textureSubresources,
                    barrier.before,
                    barrier.after,
                    diagnostic));
                expect(diagnostic.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                assert(!command->recordUploadStagingTextureCopy(
                    VK_NULL_HANDLE, 0u, *payload, {}, diagnostic));
                expect(diagnostic.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                assert(!command->recordBufferToImageCopy(ordinary, *payload, {}, diagnostic));
                expect(diagnostic.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                assert(!command->recordImageToBufferCopy(*payload, ordinary, {}, diagnostic));
                expect(diagnostic.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                for (const auto reverse : {false, true}) {
                    assert(!command->recordImageToImageCopy(reverse ? ordinary : *payload,
                        reverse ? *payload : ordinary,
                        {},
                        diagnostic));
                    expect(diagnostic.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                }
                assert(!resources.stats().nativeImageBarrierRecordCount
                    && !resources.stats().nativeBufferToImageCopyRecordCount
                    && !resources.stats().nativeImageToBufferCopyRecordCount
                    && !resources.stats().nativeImageToImageCopyRecordCount);
            }
            auto encoder = device->createCommandEncoder({"content-exclusive rejection"});
            assert(encoder);
            auto rejected = encoder->transition(*texture, barrier);
            assert(!rejected.success);
            expect(rejected.diagnostics.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            encoder.reset();
            assert(texture->state() == ResourceState::Undefined);
            TextureViewDesc viewDesc;
            viewDesc.format = textureDesc.format;
            viewDesc.usage = TextureUsage::Sampled;
            auto view = device->createTextureView(*texture, viewDesc);
            assert(view);
            const auto viewPayload = vulkanRhiNativePayload(*view);
            assert(viewPayload && viewPayload->backendVulkanOriginalContentExclusive());
            {
                auto arena =
                    device->createDescriptorArena({"exclusive descriptor refusal", 1u, 1u});
                assert(arena);
                DescriptorLayoutDesc layout;
                layout.name = "exclusive sampled texture";
                layout.bindings = {{0u, DescriptorType::SampledTexture, ShaderStage::Compute}};
                auto set = arena->allocateDescriptorSet({"exclusive set", layout});
                assert(set);
                auto write = set->write({0u,
                    DescriptorType::SampledTexture,
                    view->id(),
                    DescriptorResourceKind::TextureView,
                    ResourceState::ShaderRead});
                assert(!write.success);
                expect(write.diagnostics.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                assert(!set->stats().writtenBindings);
                set.reset();
                arena->reset();
                auto table = arena->allocateDescriptorTable(
                    {"exclusive table", DescriptorType::SampledTexture, ShaderStage::Compute, 1u});
                assert(table);
                write = table->write({0u,
                    DescriptorType::SampledTexture,
                    view->id(),
                    DescriptorResourceKind::TextureView});
                assert(!write.success);
                expect(write.diagnostics.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                assert(!table->stats().writtenDescriptors);
            }
            view.reset();
            // Logical shutdown invalidates metadata without fabricating content.
            device.reset();
            expect(observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            observation.reset();
            texture.reset();
        }
        for (const auto& owner : q)
            assert(!owner->liveBytes());
        {
            auto limitedDesc = desc;
            limitedDesc.memoryBudgetBytes = required - 1u;
            auto limited = createVulkanRhiDevice(limitedDesc);
            assert(limited);
            assert(!limited->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, q[0]));
            assert(
                !limited->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, bytes}, q[1]));
            constexpr auto limitedOwners = OriginalResourceRegistrationContract::fixedOwnerCount
                + OriginalResourceRegistrationContract::perResourceOwnerCount;
            assert(!limited->prepareOriginalResourceRegistration(
                {1u, true, 0u, 1u, 0u, limitedOwners, bytes}, q[2]));
            core::CpuMemoryQuotaScope scope(q[3]);
            assert(!limited->createOriginalContentTexture(textureDesc, budget, q[4], id));
            expect(id, "RHI-VULKAN-CONTENT-CREATION-DEVICE-CAPACITY");
            assert(
                !limited->memoryStats().allocatedBytes && !q[3]->liveBytes() && !q[4]->liveBytes());
            limited.reset();
        }
        for (const auto& owner : q)
            assert(!owner->liveBytes());
        std::printf(
            "BINDLESS_VULKAN_ORIGINAL_TEXTURE_CREATION3 1 logical=20 native=%llu Undefined uninitialized exclusive quota-return\n",
            static_cast<unsigned long long>(required));
    }
};
} // namespace arp::test
