#include "core/math/half_float.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_features.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/frontend/command_indirect_commands.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    const auto mode = argc == 2 ? std::string_view(argv[1]) : std::string_view{};
    const bool compute = mode == "--compute" || mode == "--compute-in-flight"
        || mode == "--depth-compute" || mode == "--depth-compute-in-flight";
    const bool indexedIndirect = mode == "--indexed-indirect"
        || mode == "--indexed-indirect-in-flight" || mode == "--depth-indexed-indirect"
        || mode == "--depth-indexed-indirect-in-flight";
    const bool depthTable = mode == "--depth-table" || mode == "--depth-table-in-flight"
        || mode == "--depth-compute" || mode == "--depth-compute-in-flight"
        || mode == "--depth-indexed-indirect" || mode == "--depth-indexed-indirect-in-flight";
    const bool inFlight = mode == "--in-flight" || mode == "--depth-table-in-flight"
        || mode == "--compute-in-flight" || mode == "--depth-compute-in-flight"
        || mode == "--indexed-indirect-in-flight" || mode == "--depth-indexed-indirect-in-flight";
    assert(argc == 1 || (argc == 2 && (depthTable || inFlight || compute || indexedIndirect)));
    const auto check = [](const auto& result) {
        if (!result.success)
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
        assert(result.success);
    };
    constexpr std::uint32_t width = 30u, height = 4u;
    const std::uint32_t outputPixelBytes = compute ? 16u : 8u;
    const std::uint32_t pitch = compute ? width * outputPixelBytes : 256u;
    const unsigned readbackCount = indexedIndirect ? 3u : 2u;
    constexpr std::array<std::uint32_t, 6> indices{{0u, 0u, 0u, 0u, 1u, 2u}};
    constexpr std::array<DrawIndexedIndirectCommand, 2> indirectRecords{
        {{0u, 1u, 3u, 0, 0u}, {3u, 1u, 3u, 0, 0u}}};
    const auto stages = compute ? ShaderStage::Compute : ShaderStage::Fragment;
    const auto nativeFeatures = probeMetalRhiNativeFeatures();
    assert(nativeFeatures.argumentBuffers && nativeFeatures.memorylessTransientTextures);
    if (depthTable)
        assert(nativeFeatures.sampledDepthTexture);
    const auto sourceFormat = depthTable ? TextureFormat::D32Float : TextureFormat::Rgba16Float;
    MetalRhiDeviceDesc deviceDesc;
    deviceDesc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
    deviceDesc.capabilityContract.queriedFromNativeDevice = true;
    deviceDesc.capabilityContract.queriedExtendedFeatures = true;
    deviceDesc.uploadStagingPageSizeBytes = 4096u;
    deviceDesc.uploadStagingMaxPageCount = 1u;
    const unsigned sourceCount =
        depthTable ? 4u : 3u; // one replacement depth source for post-bind parent death
    deviceDesc.memoryBudgetBytes = sourceCount * (depthTable ? 4u : 8u)
        + width * height * outputPixelBytes + readbackCount * pitch * height
        + (indexedIndirect ? sizeof(indices) + sizeof(indirectRecords) : 0u);
    auto device = createMetalRhiDevice(deviceDesc);
    assert(device);
    DescriptorLayoutDesc setLayout;
    setLayout.name = "ArrayElements";
    setLayout.bindings = {{7u, DescriptorType::SampledTexture, stages, 3u},
        {2u, DescriptorType::SampledTexture, stages}};
    PipelineLayoutDesc layoutDesc;
    DescriptorTableDesc tableDesc{"DepthTable", DescriptorType::SampledTexture, stages, 3u};
    tableDesc.depthTexture = true;
    tableDesc.textureViewDimensionConstrained = true;
    if (depthTable) {
        setLayout.bindings.resize(1u);
        setLayout.bindings[0].binding = 0u;
        setLayout.bindings[0].depthTexture = true;
        setLayout.bindings[0].textureViewDimensionConstrained = true;
        layoutDesc.descriptorTables.push_back({0u,
            tableDesc.label,
            tableDesc.type,
            tableDesc.stages,
            tableDesc.capacity,
            true,
            TextureViewDimension::Texture2D,
            true});
    } else {
        layoutDesc.descriptorLayouts.push_back(setLayout);
        layoutDesc.descriptorSets.push_back({0u, setLayout.name});
    }
    DescriptorLayoutDesc outputLayout;
    if (compute) {
        outputLayout.name = "ComputeOutput";
        outputLayout.bindings.push_back({0u, DescriptorType::StorageBuffer, stages});
        layoutDesc.descriptorLayouts.push_back(outputLayout);
        layoutDesc.descriptorSets.push_back({1u, outputLayout.name});
    }
    auto layout = device->createPipelineLayout(layoutDesc);
    assert(layout);
    const core::pmr::String prefix = compute
        ? (depthTable ? ARP_DEPTH_COMPUTE_SHADER_DIR "/descriptor_depth_compute"
                      : ARP_COMPUTE_SHADER_DIR "/descriptor_compute")
        : (depthTable ? ARP_DEPTH_SHADER_DIR "/descriptor_depth"
                      : ARP_ARRAY_SHADER_DIR "/descriptor_array");
    ShaderPackageDesc package;
    package.label = "DescriptorArrayFixture";
    package.slangModulePath = prefix + ".slang";
    if (compute)
        package.entrypoints = {
            {package.slangModulePath, "computeMain", stages, ShaderSourceLanguage::Slang}};
    else
        package.entrypoints = {{package.slangModulePath,
                                   "vertexMain",
                                   ShaderStage::Vertex,
                                   ShaderSourceLanguage::Slang},
            {package.slangModulePath, "fragmentMain", stages, ShaderSourceLanguage::Slang}};
    package.artifacts.spirvPath = prefix + ".spv";
    package.artifacts.mslPath = prefix + ".metal";
    package.artifacts.wgslPath = prefix + ".wgsl";
    package.artifacts.reflectionPath = prefix + ".reflection.yaml";
    for (const auto& binding : setLayout.bindings)
        package.reflection.descriptorBindings.push_back(makeShaderReflectionBinding(0u, binding));
    if (compute)
        package.reflection.descriptorBindings.push_back(
            makeShaderReflectionBinding(1u, outputLayout.bindings[0]));
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
    check(validateShaderPackage(package));
    core::UniquePtr<RenderPipeline> pipeline;
    core::UniquePtr<ComputePipeline> computePipeline;
    if (compute) {
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.label = "ComputeArrayFixture";
        pipelineDesc.layout = layout.get();
        pipelineDesc.shaderPackage = &package;
        pipelineDesc.computeShader = package.entrypoints[0];
        check(device->validateComputePipeline(pipelineDesc));
        computePipeline = device->createComputePipeline(pipelineDesc);
        assert(computePipeline);
    } else {
        RenderPipelineDesc pipelineDesc;
        pipelineDesc.label = "ArrayFixture";
        pipelineDesc.layout = layout.get();
        pipelineDesc.shaderPackage = &package;
        pipelineDesc.vertexShader = package.entrypoints[0];
        pipelineDesc.fragmentShader = package.entrypoints[1];
        pipelineDesc.colorFormat = TextureFormat::Rgba16Float;
        pipelineDesc.cullMode = CullMode::None;
        pipelineDesc.hasViewport = true;
        pipelineDesc.viewport = {0, 0, width, height, 0, 1};
        pipelineDesc.hasScissor = true;
        pipelineDesc.scissor = {0, 0, width, height};
        pipeline = device->createRenderPipeline(pipelineDesc);
        assert(pipeline);
    }
    constexpr float colors[3][4]{
        {0.25f, 0.5f, 0.75f, 1.0f}, {0.5f, 0.75f, 0.25f, 1.0f}, {0.75f, 0.25f, 0.5f, 1.0f}};
    std::array<core::UniquePtr<GpuTexture>, 4> sources;
    std::array<core::UniquePtr<GpuTextureView>, 4> views;
    core::UniquePtr<GpuTexture> target;
    core::UniquePtr<GpuBuffer> computeOutput;
    core::UniquePtr<GpuBuffer> indexBuffer, indirectBuffer;
    if (indexedIndirect) {
        indexBuffer = device->createBuffer({"ArrayIndices",
            sizeof(indices),
            ResourceUsage::IndexBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        indirectBuffer = device->createBuffer({"ArrayIndirect",
            sizeof(indirectRecords),
            ResourceUsage::IndirectBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(indexBuffer && indirectBuffer);
        check(device->writeBuffer(*indexBuffer, indices.data(), sizeof(indices)));
        check(
            device->writeBuffer(*indirectBuffer, indirectRecords.data(), sizeof(indirectRecords)));
    }
    if (compute) {
        computeOutput = device->createBuffer({"ComputeArrayOutput",
            width * height * outputPixelBytes,
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined});
        assert(computeOutput);
    } else {
        target = device->createTexture({"ArrayOutput",
            {width, height, 1u},
            TextureFormat::Rgba16Float,
            TextureUsage::RenderAttachment | TextureUsage::CopySrc,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined});
        assert(target);
    }
    auto readback = device->createBuffer({"ArrayReadback",
        readbackCount * pitch * height,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(readback);
    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Color};
    {
        auto initialize = device->createCommandEncoder({"ArrayInitialize"});
        assert(initialize);
        for (unsigned i = 0; i < sourceCount; ++i) {
            sources[i] = device->createTexture({"ArraySource",
                {1u, 1u, 1u},
                sourceFormat,
                TextureUsage::Sampled
                    | (depthTable ? TextureUsage::CopyDst : TextureUsage::RenderAttachment),
                MemoryUsage::GpuOnly,
                ResourceState::Undefined});
            assert(sources[i]);
            TextureViewDesc viewDesc{"ArrayView", sourceFormat, TextureUsage::Sampled};
            viewDesc.aspect = depthTable ? TextureAspect::Depth : TextureAspect::Color;
            views[i] = device->createTextureView(*sources[i], viewDesc);
            assert(views[i]);
            if (depthTable) {
                const TextureSubresourceRange depthRange{0u, 1u, 0u, 1u, TextureAspect::Depth};
                check(initialize->transition(
                    *sources[i], depthRange, ResourceState::Undefined, ResourceState::CopyDst));
                auto transfer = initialize->beginTransferPass({"DepthInitialize"});
                assert(transfer);
                std::array<std::uint8_t, kTextureUploadRowPitchAlignment> bytes{};
                std::memcpy(bytes.data(), &colors[i % 3u][0], sizeof(float));
                UploadTextureDesc upload;
                upload.destinationTexture = sources[i]->id();
                upload.extent = {1u, 1u, 1u};
                upload.format = sourceFormat;
                upload.subresources = depthRange;
                upload.bytesPerRow = bytes.size();
                upload.rowsPerImage = 1u;
                upload.pixelData = bytes.data();
                upload.pixelDataSize = bytes.size();
                upload.frameSerial = 1u;
                upload.resourceGeneration = 1u;
                check(transfer->uploadTexture(upload));
                check(transfer->end());
                check(initialize->transition(
                    *sources[i], depthRange, ResourceState::CopyDst, ResourceState::ShaderRead));
                continue;
            }
            check(initialize->transition(
                *sources[i], range, ResourceState::Undefined, ResourceState::ColorAttachment));
            RenderPassDesc passDesc;
            RenderPassColorAttachmentDesc color;
            color.target = sources[i]->id();
            color.format = TextureFormat::Rgba16Float;
            color.loadOp = LoadOp::Clear;
            color.storeOp = StoreOp::Store;
            color.clearColor = {colors[i][0], colors[i][1], colors[i][2], colors[i][3]};
            passDesc.colorAttachments.push_back(color);
            auto pass = initialize->beginRenderPass(passDesc);
            assert(pass);
            check(pass->end());
            check(initialize->transition(
                *sources[i], range, ResourceState::ColorAttachment, ResourceState::ShaderRead));
        }
        auto commands = initialize->finish();
        assert(commands);
        check(device->queue().submit(*commands));
        device->waitIdle();
    }
    auto arena = device->createDescriptorArena({"ArrayFixtureArena",
        (depthTable ? 2u : 1u) + (compute ? 1u : 0u),
        (depthTable ? 6u : 4u) + (compute ? 1u : 0u)});
    assert(arena);
    core::UniquePtr<DescriptorSet> set, outputSet;
    core::UniquePtr<DescriptorTable> table, wrongShapeTable;
    if (depthTable) {
        table = arena->allocateDescriptorTable(tableDesc);
        assert(table);
        tableDesc.depthTexture = false;
        wrongShapeTable = arena->allocateDescriptorTable(tableDesc);
        assert(wrongShapeTable);
        for (unsigned i = 0u; i < 3u; ++i)
            check(wrongShapeTable->write({i,
                DescriptorType::SampledTexture,
                views[i]->id(),
                DescriptorResourceKind::TextureView}));
    } else {
        set = arena->allocateDescriptorSet({"ArrayFixtureSet", setLayout});
        assert(set);
    }
    if (compute) {
        outputSet = arena->allocateDescriptorSet({"ComputeOutput", outputLayout});
        assert(outputSet);
        check(outputSet->write({0u,
            DescriptorType::StorageBuffer,
            computeOutput->id(),
            DescriptorResourceKind::Buffer}));
    }
    const auto writeElement = [&](unsigned element, GpuResourceId resource) {
        if (depthTable)
            return table->write({element,
                DescriptorType::SampledTexture,
                resource,
                DescriptorResourceKind::TextureView});
        DescriptorBindingWrite write{
            7u, DescriptorType::SampledTexture, resource, DescriptorResourceKind::TextureView};
        write.arrayElement = element;
        return set->write(write);
    };
    const auto revisionOf = [&]() {
        return depthTable ? table->contentRevision() : set->contentRevision();
    };
    const auto bind = [&](auto& pass) {
        return depthTable ? pass.bindDescriptorTable(0u, *table) : pass.bindDescriptorSet(0u, *set);
    };
    check(writeElement(2u, views[2]->id()));
    auto record = device->createCommandEncoder({"ArrayAB"});
    assert(record);
    const auto beginGraphics = [&](bool first) {
        check(record->transition(*target,
            range,
            first ? ResourceState::Undefined : ResourceState::CopySrc,
            ResourceState::ColorAttachment));
        RenderPassDesc passDesc;
        RenderPassColorAttachmentDesc color;
        color.target = target->id();
        color.format = TextureFormat::Rgba16Float;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        color.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        passDesc.colorAttachments.push_back(color);
        auto pass = record->beginRenderPass(passDesc);
        assert(pass);
        check(pass->setPipeline(*pipeline));
        return pass;
    };
    const auto recordBindings = [&](auto& pass, unsigned iteration, const auto& consume) {
        if (!iteration)
            assert(!bind(pass).success);
        for (unsigned element = 0u; element < 3u; ++element) {
            check(writeElement(element, views[iteration ? 2u - element : element]->id()));
        }
        if (!depthTable)
            check(set->write({2u,
                DescriptorType::SampledTexture,
                views[1]->id(),
                DescriptorResourceKind::TextureView}));
        auto bound = bind(pass);
        check(bound);
        // The second bind is the same immutable descriptor snapshot. It must
        // reuse the command-local argument buffer without changing what the
        // following draw or dispatch reads.
        bound = bind(pass);
        check(bound);
        if (depthTable) {
            const auto rejected = pass.bindDescriptorTable(0u, *wrongShapeTable);
            assert(
                !rejected.success && rejected.recordedCommandCount == bound.recordedCommandCount);
        }
        const auto revision = revisionOf();
        assert(!writeElement(3u, views[0]->id()).success && revisionOf() == revision);
        if (compute || indexedIndirect) {
            check(writeElement(0u, views[1]->id()));
            const auto stale = consume();
            assert(!stale.success && stale.recordedCommandCount == bound.recordedCommandCount);
            check(writeElement(0u, views[iteration ? 2u : 0u]->id()));
            bound = bind(pass);
            check(bound);
        }
        if (depthTable && iteration == 1u) {
            const auto retainedBytes = device->memoryStats().allocatedBytes;
            sources[0].reset();
            assert(device->memoryStats().allocatedBytes == retainedBytes);
            const auto rejected = consume();
            assert(
                !rejected.success && rejected.recordedCommandCount == bound.recordedCommandCount);
            assert(!table->validateDescriptor(2u).success);
            check(writeElement(2u, views[3]->id()));
            check(bind(pass));
        }
        check(consume());
        check(pass.end());
    };
    for (unsigned iteration = 0u; iteration < 2u; ++iteration) {
        if (compute) {
            check(record->transition(*computeOutput,
                iteration ? ResourceState::CopySrc : ResourceState::Undefined,
                ResourceState::ShaderWrite));
            auto pass = record->beginComputePass({"ArrayCompute"});
            assert(pass);
            check(pass->setPipeline(*computePipeline));
            check(pass->bindDescriptorSet(1u, *outputSet));
            recordBindings(*pass, iteration, [&] {
                return pass->dispatch(6u, height, 1u);
            });
            check(record->transition(
                *computeOutput, ResourceState::ShaderWrite, ResourceState::CopySrc));
        } else {
            auto pass = beginGraphics(iteration == 0u);
            if (indexedIndirect) {
                const auto missingIndex = pass->drawIndexedIndirect(*indirectBuffer,
                    sizeof(DrawIndexedIndirectCommand),
                    1u,
                    sizeof(DrawIndexedIndirectCommand));
                assert(!missingIndex.success
                    && missingIndex.diagnostics == "RHI-METAL-INDIRECT-DRAW-REQUIRES-INDEX-BUFFER");
                const auto indexBound = pass->bindIndexBuffer(*indexBuffer, IndexFormat::Uint32);
                check(indexBound);
                const auto rejectArguments = [&](const GpuBuffer& args,
                                                 std::uint64_t offset,
                                                 std::uint32_t count,
                                                 std::uint32_t stride) {
                    const auto rejected = pass->drawIndexedIndirect(args, offset, count, stride);
                    assert(!rejected.success
                        && rejected.recordedCommandCount == indexBound.recordedCommandCount);
                };
                rejectArguments(*readback, 0u, 1u, sizeof(DrawIndexedIndirectCommand));
                rejectArguments(*indirectBuffer, 1u, 1u, sizeof(DrawIndexedIndirectCommand));
                rejectArguments(*indirectBuffer,
                    sizeof(indirectRecords),
                    1u,
                    sizeof(DrawIndexedIndirectCommand));
                rejectArguments(*indirectBuffer,
                    sizeof(DrawIndexedIndirectCommand),
                    2u,
                    sizeof(DrawIndexedIndirectCommand));
                rejectArguments(*indirectBuffer, 0u, 0u, sizeof(DrawIndexedIndirectCommand));
                rejectArguments(*indirectBuffer, 0u, 1u, sizeof(DrawIndexedIndirectCommand) - 4u);
            }
            recordBindings(*pass, iteration, [&] {
                return indexedIndirect ? pass->drawIndexedIndirect(*indirectBuffer,
                                             sizeof(DrawIndexedIndirectCommand),
                                             1u,
                                             sizeof(DrawIndexedIndirectCommand))
                                       : pass->draw(3u, 1u, 0u, 0u);
            });
            check(record->transition(
                *target, range, ResourceState::ColorAttachment, ResourceState::CopySrc));
        }
        auto copy = record->beginTransferPass({"ArrayCopy"});
        assert(copy);
        if (compute)
            check(copy->copyBufferToBuffer({computeOutput->id(),
                readback->id(),
                0u,
                iteration * pitch * height,
                pitch * height}));
        else
            check(copy->copyTextureToBuffer({target->id(),
                readback->id(),
                range,
                iteration * pitch * height,
                pitch,
                height,
                target->desc().extent}));
        check(copy->end());
    }
    if (indexedIndirect) {
        auto pass = beginGraphics(false);
        check(pass->bindIndexBuffer(*indexBuffer, IndexFormat::Uint32));
        check(bind(*pass));
        // Offset zero selects a legal zero-index record; the other offset
        // above selects the real triangle after a degenerate index prefix.
        check(
            pass->drawIndexedIndirect(*indirectBuffer, 0u, 1u, sizeof(DrawIndexedIndirectCommand)));
        check(pass->end());
        check(record->transition(
            *target, range, ResourceState::ColorAttachment, ResourceState::CopySrc));
        auto copy = record->beginTransferPass({"ArrayZeroIndirectCopy"});
        assert(copy);
        check(copy->copyTextureToBuffer({target->id(),
            readback->id(),
            range,
            2u * pitch * height,
            pitch,
            height,
            target->desc().extent}));
        check(copy->end());
    }
    auto commands = record->finish();
    assert(commands);
    const auto commandStats = inspectMetalRhiNativeCommandStats(*commands);
    assert(commandStats.nativeArgumentBufferAllocationCount > 0u);
    assert(commandStats.nativeArgumentBufferReuseCount > 0u);
    core::UniquePtr<GpuTimelineSemaphore> gate;
    core::UniquePtr<GpuFence> fence;
    bool inFlightMutationValid = true;
    if (inFlight) {
        gate = device->createTimelineSemaphore(0u);
        fence = device->createFence();
        assert(gate && fence);
        check(device->queue().submit(*commands, gate.get(), 1u, nullptr, 0u, fence.get()));
        // QueueSubmitStartOrder/1 proves this nonempty submit gate prevents
        // work execution. Keep both old recordings pending while writing C.
        try {
            inFlightMutationValid = !fence->isSignaled();
            const auto retainedBytes = device->memoryStats().allocatedBytes;
            for (unsigned element = 0u; element < 3u; ++element) {
                const auto before = revisionOf();
                const auto changed = writeElement(element, views[1]->id());
                inFlightMutationValid =
                    changed.success && revisionOf() > before && inFlightMutationValid;
            }
            const auto beforeRejectedWrite = revisionOf();
            const auto rejected = writeElement(3u, views[1]->id());
            inFlightMutationValid = !rejected.success && revisionOf() == beforeRejectedWrite
                && !fence->isSignaled() && device->memoryStats().allocatedBytes == retainedBytes
                && inFlightMutationValid;
        } catch (...) {
            gate->signal(1u);
            throw;
        }
    }
    set.reset();
    table.reset();
    outputSet.reset();
    wrongShapeTable.reset();
    arena.reset();
    for (auto& view : views)
        view.reset();
    for (auto& source : sources)
        source.reset();
    if (indexedIndirect) {
        const auto retainedBytes = device->memoryStats().allocatedBytes;
        indexBuffer.reset();
        indirectBuffer.reset();
        assert(device->memoryStats().allocatedBytes == retainedBytes);
    }
    if (inFlight)
        gate->signal(1u);
    else
        check(device->queue().submit(*commands));
    device->waitIdle();
    assert(inFlightMutationValid && (!inFlight || fence->isSignaled()));
    core::pmr::Vector<std::uint8_t> bytes(readbackCount * pitch * height);
    core::pmr::String diagnostic;
    assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
        bytes.data(),
        bytes.size(),
        0u,
        bytes.size(),
        diagnostic));
    for (unsigned iteration = 0u; iteration < 2u; ++iteration)
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
                for (unsigned channel = 0u; channel < 4u; ++channel) {
                    const auto element = iteration ? 2u - x % 3u : x % 3u;
                    const float expected = depthTable
                        ? (channel == 3u ? 1.0f : colors[element][0])
                        : (channel == 3u ? colors[1][0] : colors[element][channel]);
                    float actual;
                    if (compute) {
                        std::memcpy(&actual,
                            bytes.data() + iteration * pitch * height + y * pitch
                                + x * outputPixelBytes + channel * sizeof(float),
                            sizeof(float));
                    } else {
                        std::uint16_t bits;
                        std::memcpy(&bits,
                            bytes.data() + iteration * pitch * height + y * pitch + x * 8u
                                + channel * 2u,
                            2u);
                        actual = core::halfToFloat(bits);
                    }
                    assert(std::isfinite(actual)
                        && (compute ? actual == expected : std::abs(actual - expected) <= 0.001f));
                }
    if (indexedIndirect) {
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
                for (unsigned channel = 0u; channel < 4u; ++channel) {
                    std::uint16_t bits;
                    std::memcpy(&bits,
                        bytes.data() + 2u * pitch * height + y * pitch + x * 8u + channel * 2u,
                        2u);
                    assert(core::halfToFloat(bits) == 0.0f);
                }
    }
    commands.reset();
    record.reset();
    target.reset();
    computeOutput.reset();
    readback.reset();
    assert(device->memoryStats().allocatedBytes == 0u);
    if (indexedIndirect)
        std::printf(
            "DescriptorRecordedStorage/1 native Metal indexed indirect %s stale-draw rejection, A/B pixels, zero-work pixels and index/argument retirement (in-flight=%d): PASS\n",
            depthTable ? "depth table" : "argument-buffer array",
            inFlight);
    if (compute)
        std::printf(
            "DescriptorRecordedStorage/1 native Metal compute %s stale-dispatch rejection, A/B output and retirement (in-flight=%d): PASS\n",
            depthTable ? "depth table" : "argument-buffer array",
            inFlight);
    if (!compute && inFlight)
        std::printf(
            "DescriptorRecordedStorage/1 native Metal %s in-flight C rewrite preserves A/B pixels and retirement: PASS\n",
            depthTable ? "depth table" : "argument-buffer array");
    if (!compute)
        std::puts(depthTable
                ? "DescriptorTableTextureShape/1 native Metal dynamic depth table, shape rejection, A/B pixels and retirement: PASS"
                : "DescriptorArrayElements/1 native Metal dynamic 3-element/scalar pixels, A/B snapshots and retirement: PASS");
}
