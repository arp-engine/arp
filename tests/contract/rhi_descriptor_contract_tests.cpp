#include "../fixtures/depth_stencil_attachment_input_fixture.h"
#include "../fixtures/descriptor_buffer_binding_range_fixture.h"
#include "../fixtures/descriptor_slot_shape_fixture.h"
#include "../fixtures/descriptor_whole_buffer_binding_fixture.h"
#include "../fixtures/original_descriptor_buffer_binding_fixture.h"
#include "../fixtures/render_pass_arguments_fixture.h"
#include "core/cpu_allocator.h"
#include "core/runtime/creation_receipt_lifecycle.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_resources.h"

#include <cassert>
#include <memory>

int main()
{
    arp::test::DescriptorSlotShapeFixture::verify();
    arp::test::DescriptorBufferBindingRangeFixture::verifyContract();
    {
        auto rangeDevice = arp::rhi::createNullDevice();
        assert(rangeDevice);
        arp::test::DescriptorBufferBindingRangeFixture::verifyWriteGate(*rangeDevice);
    }
    arp::test::OriginalDescriptorBufferBindingFixture::verifyContract();
    arp::test::DescriptorWholeBufferBindingFixture::verifyContract();
    arp::test::RenderPassArgumentsFixture::verifyContract();
    using namespace arp::rhi;
    {
        using C = OriginalFiniteDescriptorWriteContract;
        OriginalFiniteDescriptorWriteRequest request{
            1u, true, 2u, static_cast<std::uint32_t>(C::ownerSlots(2u)), 3u, {4096u, 4096u, 4096u}};
        assert(!C::validate(request));
        const auto saved = request;
        request.contractVersion = 2u;
        assert(C::validate(request));
        request = saved;
        request.prototypeOptIn = false;
        assert(C::validate(request));
        request = saved;
        request.maxRetainedOwnerSlots--;
        assert(C::validate(request));
        request = saved;
        request.maxCallbacks = 2u;
        assert(C::validate(request));
        request = saved;
        request.maxCpuBytes[2] = 0u;
        assert(C::validate(request));
        for (const auto backend :
            std::array{Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto closed = C::decide(backend);
            assert(closed.diagnostic && !closed.preparationAvailable && !closed.executionAvailable);
            const auto actual = C::decide(backend, {true, true, true, true, true});
            assert(!actual.executionAvailable
                && actual.preparationAvailable == (backend == Backend::WebGPU));
        }
        for (std::uint32_t absent = 0u; absent < 5u; ++absent) {
            C::Facts facts{absent != 0u, absent != 1u, absent != 2u, absent != 3u, absent != 4u};
            assert(!C::decide(Backend::WebGPU, facts).preparationAvailable);
        }
        using Lifecycle = arp::core::runtime::CreationReceiptLifecycle;
        Lifecycle pending(3u);
        assert(pending.begin());
        for (std::uint32_t i = 0u; i < 3u; ++i) {
            pending.observe(i, true, "scope", []() noexcept -> const char* {
                return nullptr;
            });
            assert((pending.phase() == Lifecycle::Phase::Ready) == (i == 2u));
        }
        Lifecycle rejected(3u);
        assert(rejected.begin());
        rejected.observe(0u, false, "scope", []() noexcept -> const char* {
            return nullptr;
        });
        rejected.observe(1u, true, "scope", []() noexcept -> const char* {
            return nullptr;
        });
        rejected.observe(2u, true, "scope", []() noexcept -> const char* {
            return nullptr;
        });
        assert(rejected.evidence().allReceived() && !rejected.evidence().proven()
            && rejected.phase() == Lifecycle::Phase::Rejected);
    }
    arp::test::DepthStencilAttachmentInputFixture::verifyContract();
    arp::test::DepthStencilAttachmentInputFixture::verifyLoweringSources();

    assert(semantics::validAttachmentLoadOp(LoadOp::Load)
        && semantics::validAttachmentLoadOp(LoadOp::Clear)
        && semantics::validAttachmentLoadOp(LoadOp::DontCare));
    assert(semantics::validAttachmentStoreOp(StoreOp::Store)
        && semantics::validAttachmentStoreOp(StoreOp::DontCare));
    assert(!semantics::validAttachmentLoadOp(static_cast<LoadOp>(255))
        && !semantics::validAttachmentStoreOp(static_cast<StoreOp>(255)));

    assert(
        !validateStorageTextureViewFormat(TextureFormat::Rgba16Float, TextureFormat::Rgba16Float));
    assert(!validateStorageTextureViewFormat(TextureFormat::R32Float, TextureFormat::R32Float));
    assert(std::string_view(
               validateStorageTextureViewFormat(TextureFormat::Unknown, TextureFormat::Rgba16Float))
        == "RHI-DESCRIPTOR-STORAGE-FORMAT-MISSING");
    assert(std::string_view(
               validateStorageTextureViewFormat(TextureFormat::Rgba16Float, TextureFormat::Unknown))
        == "RHI-DESCRIPTOR-STORAGE-FORMAT-MISSING");
    assert(std::string_view(validateStorageTextureViewFormat(
               TextureFormat::Rgba16Float, TextureFormat::R32Float))
        == "RHI-DESCRIPTOR-STORAGE-FORMAT-MISMATCH");

    DescriptorLayoutDesc layout;
    layout.name = "MaterialDescriptors";
    layout.bindings.push_back({0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    layout.bindings.push_back({1, DescriptorType::Sampler, ShaderStage::Fragment});

    assert(validateDescriptorLayout(layout).success);

    auto unnamedLayout = layout;
    unnamedLayout.name.clear();
    const auto unnamedLayoutResult = validateDescriptorLayout(unnamedLayout);
    assert(!unnamedLayoutResult.success);
    assert(unnamedLayoutResult.diagnostics.find("name") != std::pmr::string::npos);

    auto duplicate = layout;
    duplicate.bindings.push_back({1, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    assert(!validateDescriptorLayout(duplicate).success);

    PipelineLayoutDesc pipelineLayout;
    pipelineLayout.descriptorSets.push_back({0, "FrameDescriptors"});
    pipelineLayout.descriptorSets.push_back({1, "MaterialDescriptors"});
    pipelineLayout.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment, 0, 128});

    assert(validatePipelineLayout(pipelineLayout).success);
    assert(pipelineLayout.pushConstantRanges[0].sizeBytes == 128u);

    auto overlappingPushConstants = pipelineLayout;
    overlappingPushConstants.pushConstantRanges.push_back({ShaderStage::Vertex, 64, 32});
    const auto overlappingPushConstantsResult = validatePipelineLayout(overlappingPushConstants);
    assert(!overlappingPushConstantsResult.success);
    assert(
        overlappingPushConstantsResult.diagnostics.find("push constant") != std::pmr::string::npos);
    assert(overlappingPushConstantsResult.diagnostics.find("overlap") != std::pmr::string::npos);

    auto stageSeparatedPushConstants = pipelineLayout;
    stageSeparatedPushConstants.pushConstantRanges.clear();
    stageSeparatedPushConstants.pushConstantRanges.push_back({ShaderStage::Vertex, 0, 64});
    stageSeparatedPushConstants.pushConstantRanges.push_back({ShaderStage::Fragment, 32, 64});
    assert(validatePipelineLayout(stageSeparatedPushConstants).success);

    auto overflowingPushConstants = pipelineLayout;
    overflowingPushConstants.pushConstantRanges.clear();
    overflowingPushConstants.pushConstantRanges.push_back({ShaderStage::Vertex, 0xfffffff0u, 32});
    const auto overflowingPushConstantsResult = validatePipelineLayout(overflowingPushConstants);
    assert(!overflowingPushConstantsResult.success);
    assert(
        overflowingPushConstantsResult.diagnostics.find("push constant") != std::pmr::string::npos);
    assert(overflowingPushConstantsResult.diagnostics.find("overflow") != std::pmr::string::npos);

    auto device = createNullDevice();
    arp::test::DepthStencilAttachmentInputFixture::verifyPublicGate(*device);
    {
        RenderPassDesc invalid;
        invalid.colorAttachments.emplace_back();
        invalid.colorAttachments[0].loadOp = static_cast<LoadOp>(255);
        assert(!semantics::validateRenderPassAttachmentOperations(invalid).success);
        auto command = device->createCommandEncoder({"InvalidAttachmentPolicy"});
        assert(command);
        assert(!command->beginRenderPass(invalid));
        invalid.colorAttachments.clear();
        invalid.hasDepthStencilAttachment = true;
        invalid.depthStencilAttachment.stencilStoreOp = static_cast<StoreOp>(255);
        assert(!semantics::validateRenderPassAttachmentOperations(invalid).success);
    }
    BufferDesc frameBufferDesc;
    frameBufferDesc.label = "FrameConstants";
    frameBufferDesc.sizeBytes = 256;
    frameBufferDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    frameBufferDesc.memory = MemoryUsage::CpuToGpu;
    auto frameBuffer = device->createBuffer(frameBufferDesc);
    assert(frameBuffer);

    TextureDesc materialTextureDesc;
    materialTextureDesc.label = "MaterialBaseColor";
    materialTextureDesc.extent = {32, 32, 1};
    materialTextureDesc.format = TextureFormat::Bgra8Unorm;
    materialTextureDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    materialTextureDesc.memory = MemoryUsage::GpuOnly;
    auto materialTexture = device->createTexture(materialTextureDesc);
    assert(materialTexture);

    auto materialTextureView = device->createTextureView(
        *materialTexture, {"MaterialBaseColorView", TextureFormat::Bgra8Unorm});
    assert(materialTextureView);
    assert(materialTextureView->id().isValid());
    assert(materialTextureView->textureId().value == materialTexture->id().value);

    auto storageBuffer = device->createBuffer({
        "StorageRecords",
        256,
        ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(storageBuffer);

    auto storageTexture = device->createTexture({
        "StorageImage",
        {16, 16, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::Storage | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(storageTexture);
    auto storageTextureView =
        device->createTextureView(*storageTexture, {"StorageImageView", TextureFormat::Bgra8Unorm});
    assert(storageTextureView);

    auto materialSampler = device->createSampler({
        "MaterialSampler",
        SamplerFilter::Linear,
        SamplerFilter::Linear,
        SamplerAddressMode::Repeat,
        SamplerAddressMode::Repeat,
        SamplerAddressMode::ClampToEdge,
    });
    assert(materialSampler);
    assert(materialSampler->id().isValid());

    auto arena = device->createDescriptorArena({"FrameDescriptorArena", 2, 5});
    assert(arena);

    DescriptorLayoutDesc frameSetLayout;
    frameSetLayout.name = "FrameDescriptors";
    frameSetLayout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    frameSetLayout.bindings.push_back({1, DescriptorType::SampledTexture, ShaderStage::Fragment});
    frameSetLayout.bindings.push_back({2, DescriptorType::Sampler, ShaderStage::Fragment});

    DescriptorSetDesc setDesc;
    setDesc.label = "FrameDescriptorSet";
    setDesc.layout = frameSetLayout;
    auto set = arena->allocateDescriptorSet(setDesc);
    {
        auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(65536u);
        const auto* diagnostic =
            device->diagnoseOriginalFiniteDescriptorSource({1u, true, 1u, 65536u},
                *set,
                std::shared_ptr<const ComputePipeline>{},
                0u,
                nullptr,
                0u,
                quota);
        assert(diagnostic
            && std::string_view(diagnostic) == "RHI-ORIGINAL-FINITE-WRITE-NULL-VALIDATOR-ONLY");
        OriginalCommandDescriptorSourceBudget budget{3u,
            true,
            2u,
            2u,
            OriginalCommandDescriptorSetSource::finiteExclusiveRetainedOwnerCount,
            65536u};
        budget.finitePipelineSourceIndex = 0u;
        budget.finiteFootprintContractVersion = 1u;
        budget.finiteFullBoundViewOptIn = true;
        assert(!OriginalCommandDescriptorSetSource::prepareOriginal(
            set, {}, {}, nullptr, 0u, budget, quota, *device, diagnostic));
        assert(diagnostic
            && std::string_view(diagnostic) == "RHI-ORIGINAL-FINITE-WRITE-NULL-VALIDATOR-ONLY");
    }
    assert(set);
    assert(set->allocation().success);
    assert(set->allocation().setIndex == 0u);
    assert(set->isCurrentAllocation());
    assert(set->stats().bindingCount == 3u);
    assert(set->stats().writtenBindings == 0u);
    assert(!set->hasWrittenBinding(0));
    assert(set->stats().backendFallbacksHiddenFromUpperLayer);
    assert(!set->stats().nativeHandleExposedToUpperLayer);

    const auto frameBinding = set->write({
        0,
        DescriptorType::UniformBuffer,
        frameBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(frameBinding.success);
    assert(set->stats().writtenBindings == 1u);
    assert(set->hasWrittenBinding(0));
    const auto textureBinding = set->write({
        1,
        DescriptorType::SampledTexture,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(textureBinding.success);
    const auto samplerBinding = set->write({
        2,
        DescriptorType::Sampler,
        materialSampler->id(),
        DescriptorResourceKind::Sampler,
    });
    assert(samplerBinding.success);
    assert(set->stats().writtenBindings == 3u);
    assert(set->hasWrittenBinding(1));
    assert(set->hasWrittenBinding(2));
    assert(!set->hasWrittenBinding(3));

    {
        using namespace arp;
        PipelineLayoutDesc authored;
        authored.descriptorSets = {{4u, frameSetLayout.name}};
        authored.descriptorLayouts = {frameSetLayout};
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto captured = core::makeUnique<DescriptorSetLayoutContract>(authored);
            assert(captured->valid() && captured->validate(4u, *set).success);
            assert(!captured->validate(0u, *set).success);
            authored.descriptorLayouts.clear();
            authored.descriptorSets.clear();
            assert(captured->validate(4u, *set).success);
            const auto reject = [&](auto mutate) {
                DescriptorLayoutDesc expected{core::pmr::String{&core::defaultCpuMemoryResource()},
                    core::pmr::Vector<DescriptorBindingDesc>{&core::defaultCpuMemoryResource()}};
                expected.name = captured->find(4u)->name;
                expected.bindings = captured->find(4u)->bindings;
                mutate(expected);
                const auto result = validateExactDescriptorSetLayout(expected, *set);
                assert(!result.success
                    && result.diagnostics.find("LAYOUT-CONTRACT-MISMATCH")
                        != result.diagnostics.npos);
            };
            reject([](auto& x) {
                x.name = "Other";
            });
            reject([](auto& x) {
                x.bindings.pop_back();
            });
            reject([](auto& x) {
                x.bindings[0].arrayCount = 2;
            });
            reject([](auto& x) {
                x.bindings[0].binding = 9;
            });
            reject([](auto& x) {
                x.bindings[0].type = DescriptorType::StorageBuffer;
            });
            reject([](auto& x) {
                x.bindings[0].tableBacked = true;
            });
            reject([](auto& x) {
                x.bindings[0].stages = ShaderStage::Vertex | ShaderStage::Fragment;
            });
            reject([](auto& x) {
                x.bindings[0].dynamicOffset = true;
            });
            reject([](auto& x) {
                x.bindings[1].depthTexture = true;
            });
            reject([](auto& x) {
                x.bindings[1].textureViewDimension = TextureViewDimension::TextureCube;
            });
            reject([](auto& x) {
                x.bindings[1].textureViewDimensionConstrained = true;
            });
            reject([](auto& x) {
                x.bindings[1].sampledTextureFilterable = false;
            });
            reject([](auto& x) {
                x.bindings[1].storageTextureFormat = TextureFormat::R32Float;
            });
            reject([](auto& x) {
                x.bindings[1].storageTextureAccess = StorageTextureAccess::ReadOnly;
            });
            reject([](auto& x) {
                x.bindings[2].comparisonSampler = true;
            });
            std::pmr::set_default_resource(previous);
        }
        assert(!quota->liveBytes());
        auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
        bool exhausted = false;
        try {
            core::CpuMemoryQuotaScope scope(tiny);
            auto rejected = core::makeUnique<DescriptorSetLayoutContract>(authored);
        } catch (const std::bad_alloc&) {
            exhausted = true;
        }
        assert(exhausted && !tiny->liveBytes());
    }

    const auto wrongResourceKind = set->write({
        2,
        DescriptorType::Sampler,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!wrongResourceKind.success);
    assert(wrongResourceKind.diagnostics.find("resource kind") != std::pmr::string::npos);

    const auto missingBinding = set->write({
        3,
        DescriptorType::UniformBuffer,
        frameBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(!missingBinding.success);
    assert(missingBinding.diagnostics.find("descriptor binding") != std::pmr::string::npos);

    auto semanticArena = device->createDescriptorArena({"SemanticDescriptorArena", 2, 5});
    assert(semanticArena);
    DescriptorLayoutDesc semanticLayout;
    semanticLayout.name = "SemanticDescriptors";
    semanticLayout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    semanticLayout.bindings.push_back({1, DescriptorType::StorageBuffer, ShaderStage::Compute});
    semanticLayout.bindings.push_back({2, DescriptorType::SampledTexture, ShaderStage::Fragment});
    semanticLayout.bindings.push_back({3, DescriptorType::StorageTexture, ShaderStage::Compute});
    semanticLayout.bindings.push_back(
        {4, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute});
    auto semanticSet = semanticArena->allocateDescriptorSet({
        "SemanticDescriptorSet",
        semanticLayout,
    });
    assert(semanticSet);

    const auto unknownResource = semanticSet->write({
        0,
        DescriptorType::UniformBuffer,
        {999999u},
        DescriptorResourceKind::Buffer,
    });
    assert(!unknownResource.success);
    assert(unknownResource.diagnostics.find("resource") != std::pmr::string::npos);

    const auto storageBufferAsUniform = semanticSet->write({
        0,
        DescriptorType::UniformBuffer,
        storageBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(!storageBufferAsUniform.success);
    assert(storageBufferAsUniform.diagnostics.find("usage") != std::pmr::string::npos);

    const auto uniformBufferAsStorage = semanticSet->write({
        1,
        DescriptorType::StorageBuffer,
        frameBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(!uniformBufferAsStorage.success);
    assert(uniformBufferAsStorage.diagnostics.find("usage") != std::pmr::string::npos);

    const auto storageTextureAsSampled = semanticSet->write({
        2,
        DescriptorType::SampledTexture,
        storageTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!storageTextureAsSampled.success);
    assert(storageTextureAsSampled.diagnostics.find("usage") != std::pmr::string::npos);

    const auto sampledTextureAsStorage = semanticSet->write({
        3,
        DescriptorType::StorageTexture,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!sampledTextureAsStorage.success);
    assert(sampledTextureAsStorage.diagnostics.find("usage") != std::pmr::string::npos);

    arp::core::UniquePtr<GpuTextureView> orphanedTextureView;
    {
        auto temporaryTexture = device->createTexture({
            "TemporaryTextureForView",
            {8, 8, 1},
            TextureFormat::Bgra8Unorm,
            TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined,
        });
        assert(temporaryTexture);
        orphanedTextureView = device->createTextureView(
            *temporaryTexture, {"OrphanedTextureView", TextureFormat::Bgra8Unorm});
        assert(orphanedTextureView);
    }
    const auto orphanedTextureViewWrite = semanticSet->write({
        2,
        DescriptorType::SampledTexture,
        orphanedTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!orphanedTextureViewWrite.success);
    assert(orphanedTextureViewWrite.diagnostics.find("parent") != std::pmr::string::npos);

    const auto storageBufferBinding = semanticSet->write({
        1,
        DescriptorType::StorageBuffer,
        storageBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(storageBufferBinding.success);
    const auto readOnlyStorageBufferBinding = semanticSet->write({
        4,
        DescriptorType::ReadOnlyStorageBuffer,
        storageBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(readOnlyStorageBufferBinding.success);
    const auto storageTextureBinding = semanticSet->write({
        3,
        DescriptorType::StorageTexture,
        storageTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(storageTextureBinding.success);

    auto cubeTexture = device->createTexture({
        "IblCube",
        {1, 1, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::Sampled | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
        1,    // mipLevelCount
        6,    // arrayLayerCount
        1,    // sampleCount
        true, // cubeCompatible
    });
    assert(cubeTexture);
    auto cubeTextureView = device->createTextureView(*cubeTexture,
        {
            "IblCubeView",
            TextureFormat::Rgba16Float,
            TextureUsage::Sampled,
            TextureViewDimension::TextureCube,
            0,
            1,
            0,
            6,
            TextureAspect::Color,
        });
    assert(cubeTextureView);
    DescriptorLayoutDesc cubeLayout;
    cubeLayout.name = "CubeDescriptors";
    cubeLayout.bindings.push_back({0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    cubeLayout.bindings.back().textureViewDimensionConstrained = true;
    cubeLayout.bindings.back().textureViewDimension = TextureViewDimension::TextureCube;
    auto cubeArena = device->createDescriptorArena({"CubeDescriptorArena", 1, 1});
    assert(cubeArena);
    auto cubeSet = cubeArena->allocateDescriptorSet({"CubeDescriptorSet", cubeLayout});
    assert(cubeSet);
    const auto twoDViewForCubeBinding = cubeSet->write({
        0,
        DescriptorType::SampledTexture,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!twoDViewForCubeBinding.success);
    assert(twoDViewForCubeBinding.diagnostics.find("texture view dimension")
        != std::pmr::string::npos);
    const auto cubeViewForCubeBinding = cubeSet->write({
        0,
        DescriptorType::SampledTexture,
        cubeTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(cubeViewForCubeBinding.success);

    DescriptorTableDesc tableDesc;
    tableDesc.label = "MaterialTextureTable";
    tableDesc.type = DescriptorType::SampledTexture;
    tableDesc.stages = ShaderStage::Fragment;
    tableDesc.capacity = 2;

    auto unnamedTableDesc = tableDesc;
    unnamedTableDesc.label.clear();
    const auto unnamedTableAllocation = arena->allocateTable(unnamedTableDesc);
    assert(!unnamedTableAllocation.success);
    assert(unnamedTableAllocation.diagnostics.find("label") != std::pmr::string::npos);
    assert(arena->stats().allocatedSets == 1u);
    assert(arena->stats().allocatedTables == 0u);
    assert(arena->stats().allocatedDescriptors == 3u);

    auto table = arena->allocateDescriptorTable(tableDesc);
    assert(table);
    assert(table->allocation().success);
    assert(table->allocation().tableIndex == 1u);
    assert(table->allocation().descriptorCapacity == 2u);
    assert(table->isCurrentAllocation());
    assert(table->stats().capacity == 2u);
    assert(table->stats().writtenDescriptors == 0u);
    assert(!table->hasWrittenDescriptor(0));
    assert(table->stats().backendFallbacksHiddenFromUpperLayer);
    assert(!table->stats().nativeHandleExposedToUpperLayer);

    const auto tableUnknownResource = table->write({
        0,
        DescriptorType::SampledTexture,
        {999998u},
        DescriptorResourceKind::TextureView,
    });
    assert(!tableUnknownResource.success);
    assert(tableUnknownResource.diagnostics.find("resource") != std::pmr::string::npos);

    const auto tableStorageTextureAsSampled = table->write({
        0,
        DescriptorType::SampledTexture,
        storageTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!tableStorageTextureAsSampled.success);
    assert(tableStorageTextureAsSampled.diagnostics.find("usage") != std::pmr::string::npos);

    const auto tableWrite = table->write({
        0,
        DescriptorType::SampledTexture,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(tableWrite.success);
    assert(table->stats().writtenDescriptors == 1u);
    assert(table->hasWrittenDescriptor(0));
    assert(!table->hasWrittenDescriptor(1));

    const auto tableWrongKind = table->write({
        1,
        DescriptorType::SampledTexture,
        materialSampler->id(),
        DescriptorResourceKind::Sampler,
    });
    assert(!tableWrongKind.success);
    assert(tableWrongKind.diagnostics.find("resource kind") != std::pmr::string::npos);

    const auto tableOutOfRange = table->write({
        2,
        DescriptorType::SampledTexture,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!tableOutOfRange.success);
    assert(tableOutOfRange.diagnostics.find("capacity") != std::pmr::string::npos);

    auto accountingArena = device->createDescriptorArena({
        "DescriptorAccountingArena",
        2,
        0xffffffffu,
    });
    assert(accountingArena);
    DescriptorTableDesc accountingTableDesc;
    accountingTableDesc.label = "AccountingTable";
    accountingTableDesc.type = DescriptorType::SampledTexture;
    accountingTableDesc.stages = ShaderStage::Fragment;
    accountingTableDesc.capacity = 32u;
    const auto accountingTableAllocation = accountingArena->allocateTable(accountingTableDesc);
    assert(accountingTableAllocation.success);
    assert(accountingArena->stats().allocatedDescriptors == 32u);
    auto overflowingAccountingTableDesc = accountingTableDesc;
    overflowingAccountingTableDesc.label = "OverflowingAccountingTable";
    overflowingAccountingTableDesc.capacity = 0xfffffff0u;
    const auto overflowingAccountingTableAllocation =
        accountingArena->allocateTable(overflowingAccountingTableDesc);
    assert(!overflowingAccountingTableAllocation.success);
    assert(overflowingAccountingTableAllocation.diagnostics.find("capacity")
        != std::pmr::string::npos);
    assert(accountingArena->stats().allocatedSets == 1u);
    assert(accountingArena->stats().allocatedTables == 1u);
    assert(accountingArena->stats().allocatedDescriptors == 32u);

    const auto exhaustedTable = arena->allocateDescriptorTable(tableDesc);
    assert(!exhaustedTable);
    assert(arena->stats().allocatedSets == 2u);
    assert(arena->stats().allocatedTables == 1u);
    assert(arena->stats().allocatedDescriptors == 5u);

    arena->reset();
    assert(arena->stats().allocatedSets == 0u);
    assert(arena->stats().allocatedTables == 0u);
    assert(arena->stats().allocatedDescriptors == 0u);
    assert(!set->isCurrentAllocation());
    assert(!table->isCurrentAllocation());
    assert(set->stats().writtenBindings == 0u);
    assert(table->stats().writtenDescriptors == 0u);

    const auto staleSetWrite = set->write({
        0,
        DescriptorType::UniformBuffer,
        frameBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(!staleSetWrite.success);
    assert(staleSetWrite.diagnostics.find("reset") != std::pmr::string::npos);

    const auto staleTableWrite = table->write({
        0,
        DescriptorType::SampledTexture,
        materialTextureView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(!staleTableWrite.success);
    assert(staleTableWrite.diagnostics.find("reset") != std::pmr::string::npos);

    auto recycledSet = arena->allocateDescriptorSet(setDesc);
    assert(recycledSet);
    assert(recycledSet->allocation().success);
    assert(recycledSet->allocation().setIndex == 0u);
    const auto recycledSetWrite = recycledSet->write({
        0,
        DescriptorType::UniformBuffer,
        frameBuffer->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(recycledSetWrite.success);

    // The prepared consumption projection owns just authoritative L2 set/table
    // declarations and reflection bindings under the explicit core quota.
    ShaderReflectionDesc reflection;
    reflection.descriptorBindings = {makeShaderReflectionBinding(0u, frameSetLayout.bindings[0])};
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(4096u);
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        DescriptorConsumptionContract contract(pipelineLayout, reflection);
        std::pmr::set_default_resource(previous);
        assert(quota->liveBytes() && contract.declaresSet(0u) && contract.declaresSet(1u)
            && !contract.declaresTable(0u) && !contract.declaresSet(2u));
        const auto matchAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        assert(contract.matchesBindings(reflection));
        auto& current = reflection.descriptorBindings.front();
        const auto captured = current;
        current.type = DescriptorType::ReadOnlyStorageBuffer;
        assert(!contract.matchesBindings(reflection));
        current = captured;
        current.set += 1u;
        assert(!contract.matchesBindings(reflection));
        current = captured;
        current.binding += 1u;
        assert(!contract.matchesBindings(reflection));
        current = captured;
        current.stages = ShaderStage::None;
        assert(!contract.matchesBindings(reflection));
        current = captured;
        current.arrayCount += 1u;
        assert(!contract.matchesBindings(reflection));
        current = captured;
        current.depthTexture = true;
        assert(!contract.matchesBindings(reflection));
        current = captured;
        current.subpassAttachmentIndex = 0u;
        assert(!contract.matchesBindings(reflection));
        current = captured;
        assert(contract.matchesBindings(reflection)
            && arp::core::defaultCpuAllocator().stats().allocationCount == matchAllocations);
        reflection.descriptorBindings.clear();
        assert(!contract.matchesBindings(reflection));
        BoundDescriptorSlots bound;
        assert(!bound.validateComplete(contract, ShaderStage::Vertex).success);
        assert(bound.bindSet(0u, *recycledSet, nullptr, 0u).success);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        arp::core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        assert(bound.validateComplete(contract, ShaderStage::Vertex).success);
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
        pressure.release();
        assert(recycledSet
                ->write({0u,
                    DescriptorType::UniformBuffer,
                    frameBuffer->id(),
                    DescriptorResourceKind::Buffer})
                .success);
        assert(!bound.validateComplete(contract, ShaderStage::Vertex).success);
        assert(bound.bindSet(0u, *recycledSet, nullptr, 0u).success);
        arena->reset();
        assert(!bound.validateComplete(contract, ShaderStage::Vertex).success);
    }
    assert(!quota->liveBytes());
    return 0;
}
