#include "../fixtures/metal_native_identity_fixture.h"
#include "core/math/half_float.h"
#include "renderer/gi/ddgi_renderer.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_ordinary_binding.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Ordinary native retention tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;

class OrdinaryFixture final {
public:
    explicit OrdinaryFixture(bool arrays, bool dynamic = false, bool storageDynamic = false)
        : arrays(arrays), dynamic(dynamic)
    {
        diagnostic.reserve(256u);
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 1024u * 1024u;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        device = createMetalRhiDevice(desc);
        assert(device && device->capabilities().commandRetention.contractVersion == 0u);
        registry = metalRhiIssuingResourceRegistry(*device);
        assert(registry);
        quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        sourceLayout.name = "OrdinaryInputs";
        outputLayout.name = "OrdinaryOutput";
        PipelineLayoutDesc layoutDesc;
        if (arrays) {
            sourceLayout.bindings = {{7u, DescriptorType::SampledTexture, ShaderStage::Compute, 3u},
                {2u, DescriptorType::SampledTexture, ShaderStage::Compute}};
            outputLayout.bindings = {{0u, DescriptorType::StorageBuffer, ShaderStage::Compute}};
            layoutDesc.descriptorLayouts = {sourceLayout, outputLayout};
            layoutDesc.descriptorSets = {{0u, sourceLayout.name}, {1u, outputLayout.name}};
        } else {
            sourceLayout.bindings = {{0u, DescriptorType::UniformBuffer, ShaderStage::Compute},
                {1u, DescriptorType::UniformBuffer, ShaderStage::Compute},
                {2u, DescriptorType::StorageBuffer, ShaderStage::Compute}};
            layoutDesc.descriptorLayouts = {sourceLayout};
            layoutDesc.descriptorSets = {{0u, sourceLayout.name}};
        }
        if (dynamic) {
            sourceLayout.bindings[0].dynamicOffset = true;
            sourceLayout.bindings[1].dynamicOffset = true;
            sourceLayout.bindings[2].dynamicOffset = storageDynamic;
            std::swap(sourceLayout.bindings[0],
                sourceLayout.bindings[1]); // offsets still sorted by logical binding
            layoutDesc.descriptorLayouts[0] = sourceLayout;
        }
        layout = device->createPipelineLayout(layoutDesc);
        assert(layout);
        const core::pmr::String prefix = arrays ? ARP_COMPUTE_SHADER_DIR "/descriptor_compute"
                                                : ARP_UNIFORM_SHADER_DIR "/ddgi_uniform_consumer";
        package.label = "OrdinaryRetentionFixture";
        package.slangModulePath = prefix + ".slang";
        package.entrypoints = {{package.slangModulePath,
            "computeMain",
            ShaderStage::Compute,
            ShaderSourceLanguage::Slang}};
        package.artifacts = {
            prefix + ".spv", prefix + ".metal", prefix + ".wgsl", prefix + ".reflection.yaml"};
        for (const auto& binding : sourceLayout.bindings)
            package.reflection.descriptorBindings.push_back(
                makeShaderReflectionBinding(0u, binding));
        if (arrays)
            for (const auto& binding : outputLayout.bindings)
                package.reflection.descriptorBindings.push_back(
                    makeShaderReflectionBinding(1u, binding));
        assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
        assert(validateShaderPackage(package).success);
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.label = "OrdinaryRetentionPipeline";
        pipelineDesc.layout = layout.get();
        pipelineDesc.shaderPackage = &package;
        pipelineDesc.computeShader = package.entrypoints[0];
        pipeline = device->createComputePipeline(pipelineDesc);
        assert(pipeline);
        for (auto& output : outputs) {
            output = buffer("OrdinaryOutput",
                outputBytes(),
                ResourceUsage::StorageBuffer,
                ResourceState::ShaderWrite);
            assert(device->writeBuffer(*output, zeros.data(), outputBytes()).success);
        }
        if (arrays)
            initializeTextures();
        else {
            for (unsigned i = 0u; i < values.size(); ++i)
                values[i] = static_cast<float>(i + 1u) / 8.0f;
            for (unsigned i = 0u; i < 2u; ++i) {
                const auto expectedBytes = (i ? 17u : 6u) * 4u * sizeof(float);
                if (!dynamic) {
                    uniforms[i] = buffer("OrdinaryUniform",
                        expectedBytes,
                        ResourceUsage::UniformBuffer,
                        ResourceState::ShaderRead);
                    assert(device
                            ->writeBuffer(
                                *uniforms[i], values.data() + (i ? 6u * 4u : 0u), expectedBytes)
                            .success);
                    continue;
                }
                MetalRhiNativeArgumentBindingPayload logical;
                logical.binding = i;
                logical.type = DescriptorType::UniformBuffer;
                logical.stages = ShaderStage::Compute;
                logical.resourceKind = DescriptorResourceKind::Buffer;
                logical.argumentKind = MetalRhiArgumentKind::Buffer;
                auto native = metalRhiNativePayloadFor(*pipeline);
                MetalRhiPreparedComputeLocation location;
                assert(
                    resolveMetalRhiPreparedComputeLocation(*native, 0u, logical, location).success
                    && location.active && !location.array);
                const MetalRhiBufferBindingFact* fact = nullptr;
                const auto* id = validateMetalRhiBufferOffset(native->computeBufferFacts,
                    native->computeBufferFactsVersion,
                    location.slot,
                    0u,
                    UINT64_MAX,
                    fact);
                if (id)
                    std::fprintf(stderr, "%s\n", id);
                assert(!id && fact && fact->requiredBytes == expectedBytes);
                const auto aligned = core::alignCpuAllocationOffsetUp(
                    expectedBytes, static_cast<std::size_t>(fact->alignment));
                assert(aligned.success);
                offsetA[i] = static_cast<std::uint32_t>(aligned.alignedValue);
                offsetB[i] = offsetA[i] * 2u;
                uniforms[i] = buffer("OrdinaryOffsetUniform",
                    offsetB[i] + expectedBytes,
                    ResourceUsage::UniformBuffer,
                    ResourceState::ShaderRead);
                core::pmr::Vector<std::uint8_t> bytes(
                    uniforms[i]->desc().sizeBytes, 0u, &core::defaultCpuMemoryResource());
                const auto* valuesBegin = values.data() + (i ? 6u * 4u : 0u);
                std::memcpy(bytes.data() + offsetA[i], valuesBegin, expectedBytes);
                for (std::size_t j = 0u; j < expectedBytes / sizeof(float); ++j) {
                    const float changed = valuesBegin[j] * 2.0f;
                    std::memcpy(
                        bytes.data() + offsetB[i] + j * sizeof(float), &changed, sizeof(float));
                }
                assert(device->writeBuffer(*uniforms[i], bytes.data(), bytes.size()).success);
                std::printf(
                    "ordinaryOffsetNativeFact: logical=%u physical=%u alignment=%llu span=%llu A=%u B=%u\n",
                    i,
                    location.slot,
                    static_cast<unsigned long long>(fact->alignment),
                    static_cast<unsigned long long>(fact->requiredBytes),
                    offsetA[i],
                    offsetB[i]);
            }
        }
        arena = device->createDescriptorArena({"OrdinaryArena", arrays ? 3u : 2u, 6u});
        assert(arena);
        for (unsigned i = 0u; i < (arrays ? 3u : 2u); ++i) {
            sets[i] = arena->allocateDescriptorSet(
                {"OrdinarySet", arrays && i ? outputLayout : sourceLayout});
            assert(sets[i]);
            if (arrays && i)
                check(sets[i]->write({0u,
                    DescriptorType::StorageBuffer,
                    outputs[i - 1u]->id(),
                    DescriptorResourceKind::Buffer}));
        }
        writeInputs(false, 0u);
    }

    void check(const ValidationResult& result)
    {
        if (!result.success)
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
        assert(result.success);
    }

    std::size_t outputBytes() const
    {
        return arrays ? 30u * 4u * sizeof(float) : values.size() * sizeof(float);
    }

    core::UniquePtr<GpuBuffer> buffer(
        const char* name, std::uint64_t bytes, ResourceUsage usage, ResourceState state)
    {
        auto result = device->createBuffer(
            {core::toPmrString(name), bytes, usage, MemoryUsage::CpuToGpu, state});
        assert(result);
        return result;
    }

    void initializeTextures()
    {
        auto packed = buffer("OrdinaryTextureUpload",
            3u * kTextureUploadRowPitchAlignment,
            ResourceUsage::CopySrc,
            ResourceState::CopySrc);
        std::array<std::uint8_t, 3u * kTextureUploadRowPitchAlignment> bytes{};
        for (unsigned i = 0u; i < 3u; ++i) {
            for (unsigned channel = 0u; channel < 4u; ++channel) {
                const auto bits = core::floatToHalf(colors[i][channel]);
                std::memcpy(
                    bytes.data() + i * kTextureUploadRowPitchAlignment + channel * sizeof(bits),
                    &bits,
                    sizeof(bits));
            }
            textures[i] = device->createTexture({"OrdinaryInput",
                {1u, 1u, 1u},
                TextureFormat::Rgba16Float,
                TextureUsage::CopyDst | TextureUsage::Sampled,
                MemoryUsage::GpuOnly,
                ResourceState::Undefined});
            assert(textures[i]);
            views[i] = device->createTextureView(
                *textures[i], {"OrdinaryView", TextureFormat::Rgba16Float, TextureUsage::Sampled});
            assert(views[i]);
        }
        assert(device->writeBuffer(*packed, bytes.data(), bytes.size()).success);
        auto command =
            createMetalRhiNativeCommandPayload("OrdinaryTextureInitialize", registry, &diagnostic);
        assert(command);
        assert(command->recordBlitPassBegin(diagnostic));
        for (unsigned i = 0u; i < 3u; ++i) {
            BufferToTextureCopyDesc copy;
            copy.sourceBuffer = packed->id();
            copy.destinationTexture = textures[i]->id();
            copy.bufferOffsetBytes = i * kTextureUploadRowPitchAlignment;
            copy.bytesPerRow = kTextureUploadRowPitchAlignment;
            copy.rowsPerImage = 1u;
            copy.extent = {1u, 1u, 1u};
            assert(command->recordBlitBufferToTexture(copy, diagnostic));
        }
        assert(command->recordBlitPassEnd(diagnostic));
        submit(command);
    }

    void writeInputs(bool reversed, unsigned output)
    {
        auto& set = *sets[arrays ? 0u : output];
        if (arrays) {
            for (unsigned i = 0u; i < 3u; ++i) {
                DescriptorBindingWrite write{7u,
                    DescriptorType::SampledTexture,
                    views[reversed ? 2u - i : i]->id(),
                    DescriptorResourceKind::TextureView};
                write.arrayElement = i;
                check(set.write(write));
            }
            check(set.write({2u,
                DescriptorType::SampledTexture,
                views[1u]->id(),
                DescriptorResourceKind::TextureView}));
        } else {
            check(set.write({0u,
                DescriptorType::UniformBuffer,
                uniforms[0]->id(),
                DescriptorResourceKind::Buffer}));
            check(set.write({1u,
                DescriptorType::UniformBuffer,
                uniforms[1]->id(),
                DescriptorResourceKind::Buffer}));
            check(set.write({2u,
                DescriptorType::StorageBuffer,
                outputs[output]->id(),
                DescriptorResourceKind::Buffer}));
        }
    }

    MetalRhiOrdinaryBindingBudget bindingBudget() const
    {
        return {1u, true, cpuBytes, arrays ? 1u : 0u, arrays ? 4096u : 0u};
    }

    std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding> prepare(
        unsigned index, const std::uint32_t* offsets = nullptr, std::uint32_t count = 0u)
    {
        auto result = prepareMetalRhiOrdinaryComputeBinding(*pipeline,
            *sets[index],
            arrays && index ? 1u : 0u,
            registry,
            bindingBudget(),
            quota,
            diagnostic,
            offsets,
            count);
        if (!result)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(result);
        return result;
    }

    std::shared_ptr<MetalRhiNativeCommandPayload> command(unsigned owners)
    {
        CommandRetentionBudget budget{1u, true, owners, 0u, 0u, 0u, cpuBytes};
        const char* id = nullptr;
        CommandEncoderDesc publicDesc;
        publicDesc.retentionBudget = budget;
        assert(!device->createCommandEncoder(publicDesc));
        auto storage = common::CommandRetentionStorage::prepare(budget, quota, id);
        assert(storage && !id);
        auto result = createMetalRhiNativeCommandPayload(
            "OrdinaryBudgetCommand", registry, &diagnostic, 0u, 0u, 0u, std::move(storage));
        assert(result);
        return result;
    }

    void submit(const std::shared_ptr<MetalRhiNativeCommandPayload>& command)
    {
        assert(command->finish(diagnostic));
        auto completion = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, completion, {}, 0u, {}, 0u, diagnostic));
        completion->wait();
    }

    void dispatch(const std::shared_ptr<MetalRhiNativeCommandPayload>& command)
    {
        assert(command->recordDispatch(arrays ? 6u : 1u, 1u, 1u, diagnostic));
    }

    void verify(
        unsigned index, bool reversed = false, bool untouched = false, float uniformFactor = 1.0f)
    {
        std::array<float, 120u> output{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*outputs[index]),
            output.data(),
            outputBytes(),
            0u,
            outputBytes(),
            diagnostic));
        for (unsigned i = 0u; i < outputBytes() / sizeof(float); ++i) {
            const auto element = i / 4u, channel = i % 4u;
            const float expected = untouched ? 0.0f
                : !arrays                    ? values[i] * uniformFactor
                : channel == 3u              ? colors[1u][0u]
                                : colors[reversed ? 2u - element % 3u : element % 3u][channel];
            assert(output[i] == expected);
        }
    }

    static constexpr std::uint64_t cpuBytes = 65536u;
    const bool arrays, dynamic;
    std::array<std::uint32_t, 2u> offsetA{}, offsetB{};
    const float colors[3][4]{
        {0.25f, 0.5f, 0.75f, 1.0f}, {0.5f, 0.75f, 0.25f, 1.0f}, {0.75f, 0.25f, 0.5f, 1.0f}};
    std::array<float, 23u * 4u> values{};
    std::array<float, 120u> zeros{};
    core::pmr::String diagnostic;
    core::UniquePtr<GpuDevice> device;
    std::shared_ptr<MetalRhiResourceRegistry> registry;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    DescriptorLayoutDesc sourceLayout, outputLayout;
    ShaderPackageDesc package;
    core::UniquePtr<PipelineLayout> layout;
    core::UniquePtr<ComputePipeline> pipeline;
    core::UniquePtr<DescriptorArena> arena;
    std::array<core::UniquePtr<DescriptorSet>, 3u> sets;
    std::array<core::UniquePtr<GpuBuffer>, 2u> uniforms, outputs;
    std::array<core::UniquePtr<GpuTexture>, 3u> textures;
    std::array<core::UniquePtr<GpuTextureView>, 3u> views;
};

class StorageImageFixture final {
public:
    explicit StorageImageFixture(TextureFormat declaredFormat = TextureFormat::Rgba16Float)
        : f(false)
    {
        for (auto& set : f.sets)
            set.reset();
        f.arena.reset();
        f.pipeline.reset();
        f.layout.reset();
        DescriptorLayoutDesc descriptors;
        descriptors.name = "OrdinaryStorageImage";
        DescriptorBindingDesc image{0u, DescriptorType::StorageTexture, ShaderStage::Compute};
        image.storageTextureFormat = declaredFormat;
        descriptors.bindings = {image, {1u, DescriptorType::UniformBuffer, ShaderStage::Compute}};
        PipelineLayoutDesc layout;
        layout.descriptorLayouts = {descriptors};
        layout.descriptorSets = {{0u, descriptors.name}};
        f.layout = f.device->createPipelineLayout(layout);
        assert(f.layout);
        const core::pmr::String prefix = ARP_ATLAS_SHADER_DIR "/ddgi_atlas_fill";
        f.package = {};
        f.package.label = descriptors.name;
        f.package.slangModulePath = prefix + ".slang";
        f.package.entrypoints = {{f.package.slangModulePath,
            "computeMain",
            ShaderStage::Compute,
            ShaderSourceLanguage::Slang}};
        f.package.artifacts = {
            prefix + ".spv", prefix + ".metal", prefix + ".wgsl", prefix + ".reflection.yaml"};
        for (const auto& binding : descriptors.bindings)
            f.package.reflection.descriptorBindings.push_back(
                makeShaderReflectionBinding(0u, binding));
        assert(renderer::hydrateShaderPackageReflectionFromArtifact(f.package)
            && validateShaderPackage(f.package).success);
        ComputePipelineDesc pipeline;
        pipeline.layout = f.layout.get();
        pipeline.shaderPackage = &f.package;
        pipeline.computeShader = f.package.entrypoints[0];
        f.pipeline = f.device->createComputePipeline(pipeline);
        assert(f.pipeline);
        f.arena = f.device->createDescriptorArena({descriptors.name, 2u, 4u});
        assert(f.arena);
        for (unsigned i = 0u; i < 2u; ++i) {
            f.textures[i] = f.device->createTexture({"OrdinaryStorage",
                extent,
                TextureFormat::Rgba16Float,
                TextureUsage::Storage | TextureUsage::CopySrc,
                MemoryUsage::GpuOnly,
                ResourceState::ShaderWrite});
            assert(f.textures[i]);
            f.views[i] = f.device->createTextureView(*f.textures[i],
                {"OrdinaryStorageView",
                    TextureFormat::Rgba16Float,
                    TextureUsage::Storage | TextureUsage::CopySrc});
            assert(f.views[i]);
            f.uniforms[i] = f.buffer("OrdinaryStorageValue",
                sizeof(values[i]),
                ResourceUsage::UniformBuffer,
                ResourceState::ShaderRead);
            assert(
                f.device->writeBuffer(*f.uniforms[i], values[i].data(), sizeof(values[i])).success);
            f.outputs[i] = f.buffer("OrdinaryStorageReadback",
                readbackBytes,
                ResourceUsage::CopyDst,
                ResourceState::CopyDst);
            std::array<std::uint8_t, readbackBytes> sentinel;
            sentinel.fill(0xa5u);
            assert(f.device->writeBuffer(*f.outputs[i], sentinel.data(), sentinel.size()).success);
            f.sets[i] = f.arena->allocateDescriptorSet({descriptors.name, descriptors});
            assert(f.sets[i]);
            f.check(f.sets[i]->write({0u,
                DescriptorType::StorageTexture,
                f.views[i]->id(),
                DescriptorResourceKind::TextureView}));
            f.check(f.sets[i]->write({1u,
                DescriptorType::UniformBuffer,
                f.uniforms[i]->id(),
                DescriptorResourceKind::Buffer}));
        }
    }

    void copy(const std::shared_ptr<MetalRhiNativeCommandPayload>& command, unsigned index)
    {
        TextureToBufferCopyDesc copy;
        copy.sourceTexture = f.views[index]->id();
        copy.destinationBuffer = f.outputs[index]->id();
        copy.bytesPerRow = pitch;
        copy.rowsPerImage = extent.height;
        copy.extent = extent;
        assert(command->recordBlitTextureToBuffer(copy, f.diagnostic));
    }

    void verify(unsigned index)
    {
        std::array<std::uint8_t, readbackBytes> bytes{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*f.outputs[index]),
            bytes.data(),
            bytes.size(),
            0u,
            bytes.size(),
            f.diagnostic));
        for (unsigned y = 0u; y < extent.height; ++y) {
            for (unsigned x = 0u; x < extent.width; ++x)
                assert(std::memcmp(bytes.data() + y * pitch + x * sizeof(halfValues[index]),
                           halfValues[index].data(),
                           sizeof(halfValues[index]))
                    == 0);
            for (unsigned byte = extent.width * sizeof(halfValues[index]); byte < pitch; ++byte)
                assert(bytes[y * pitch + byte] == 0xa5u);
        }
    }

    OrdinaryFixture f;
    static constexpr Extent3D extent{3u, 2u, 1u};
    static constexpr unsigned pitch = kTextureUploadRowPitchAlignment,
                              readbackBytes = pitch * extent.height;
    const std::array<std::array<float, 4>, 2> values{
        {{{.25f, .5f, .75f, 1.f}}, {{.75f, .25f, .5f, 1.f}}}};
    const std::array<std::array<std::uint16_t, 4>, 2> halfValues{
        {{{0x3400, 0x3800, 0x3a00, 0x3c00}}, {{0x3a00, 0x3400, 0x3800, 0x3c00}}}};
};

struct TracePushConstants {
    std::array<float, 4u> counts{0.0f, 0.0f, 1.0f, 1.0f};
    std::array<float, 4u> origin{0.0f, 5.0f, 0.25f, 0.0f};
    std::array<float, 4u> direction{0.0f, -1.0f, 0.0f, 16.0f};
};

static_assert(sizeof(TracePushConstants) == 48u);

class TraceFixture final {
public:
    TraceFixture() : f(false)
    {
        for (auto& set : f.sets)
            set.reset();
        f.arena.reset();
        f.pipeline.reset();
        f.layout.reset();
        for (auto& uniform : f.uniforms)
            uniform.reset();
        DescriptorLayoutDesc descriptors;
        descriptors.name = "OrdinaryTrace";
        descriptors.bindings = {{6u, DescriptorType::StorageBuffer, ShaderStage::Compute}};
        for (const auto binding : bindings)
            descriptors.bindings.push_back(
                {binding, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute});
        PipelineLayoutDesc layout;
        layout.descriptorLayouts = {descriptors};
        layout.descriptorSets = {{0u, descriptors.name}};
        layout.pushConstantRanges = {{ShaderStage::Compute, 0u, sizeof(TracePushConstants)}};
        f.layout = f.device->createPipelineLayout(layout);
        assert(f.layout);
        const core::pmr::String prefix = ARP_TRACE_SHADER_DIR "/ddgi_bvh_trace_consumer";
        f.package = {};
        f.package.label = "OrdinaryTrace";
        f.package.slangModulePath = prefix + ".slang";
        f.package.entrypoints = {{f.package.slangModulePath,
            "computeMain",
            ShaderStage::Compute,
            ShaderSourceLanguage::Slang}};
        f.package.artifacts = {
            prefix + ".spv", prefix + ".metal", prefix + ".wgsl", prefix + ".reflection.yaml"};
        for (const auto& binding : descriptors.bindings)
            f.package.reflection.descriptorBindings.push_back(
                makeShaderReflectionBinding(0u, binding));
        assert(renderer::hydrateShaderPackageReflectionFromArtifact(f.package));
        assert(f.package.reflection.pushConstantByteCount == sizeof(TracePushConstants));
        ComputePipelineDesc pipeline;
        pipeline.layout = f.layout.get();
        pipeline.shaderPackage = &f.package;
        pipeline.computeShader = f.package.entrypoints[0];
        f.pipeline = f.device->createComputePipeline(pipeline);
        assert(f.pipeline);
        // A one-triangle native DDGI tree, using the production GPU ABI structs.
        // leftChild UINT32_MAX selects a leaf; rightChild0 selects triangle0.
        renderer::GpuBvhNodeData node{{-1.0f, 0.0f, 0.0f}, UINT32_MAX, {1.0f, 0.0f, 1.0f}, 0u};
        renderer::GpuBvhLeafData leaf{};
        leaf.v0[0] = -1.0f;
        leaf.v1[0] = 1.0f;
        leaf.v2[2] = 1.0f;
        leaf.normal[1] = 1.0f;
        leaf.materialIdx = 3u;
        renderer::GpuBvhEmissiveData emissive{{0.5f, 0.0f, 0.0f, 0.0f}};
        renderer::DdgiSceneBvhInstance instance;
        instance.stableId = 1u;
        const auto identity = core::Mat4::identity();
        std::copy(identity.m.begin(), identity.m.end(), instance.worldFromLocal);
        std::copy(identity.m.begin(), identity.m.end(), instance.localFromWorld);
        const std::array<const void*, 4u> data{{&node, &leaf, &emissive, &instance}};
        const std::array<std::size_t, 4u> sizes{
            {sizeof(node), sizeof(leaf), sizeof(emissive), sizeof(instance)}};
        for (unsigned i = 0u; i < sources.size(); ++i) {
            sources[i] = f.buffer("OrdinaryTraceInput",
                sizes[i],
                ResourceUsage::StorageBuffer,
                ResourceState::ShaderRead);
            assert(f.device->writeBuffer(*sources[i], data[i], sizes[i]).success);
        }
        f.arena = f.device->createDescriptorArena({"OrdinaryTraceArena", 2u, 16u});
        assert(f.arena);
        for (unsigned output = 0u; output < 2u; ++output) {
            f.sets[output] = f.arena->allocateDescriptorSet({"OrdinaryTraceSet", descriptors});
            assert(f.sets[output]);
            f.check(f.sets[output]->write({6u,
                DescriptorType::StorageBuffer,
                f.outputs[output]->id(),
                DescriptorResourceKind::Buffer}));
            const std::array<unsigned, 7u> sourceIndex{{0u, 1u, 2u, 0u, 1u, 2u, 3u}};
            for (unsigned i = 0u; i < bindings.size(); ++i)
                f.check(f.sets[output]->write({bindings[i],
                    DescriptorType::ReadOnlyStorageBuffer,
                    sources[sourceIndex[i]]->id(),
                    DescriptorResourceKind::Buffer}));
        }
    }

    rhi::PushConstantsDesc desc(const TracePushConstants& value) const
    {
        return {ShaderStage::Compute, 0u, sizeof(value), &value};
    }

    std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush> push(const TracePushConstants& value)
    {
        auto result = prepareMetalRhiOrdinaryComputePush(
            *f.pipeline, desc(value), f.bindingBudget(), f.quota, f.diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", f.diagnostic.c_str());
        assert(result);
        return result;
    }

    void verify(unsigned output, float distance, bool untouched = false)
    {
        std::array<float, 92u> actual{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*f.outputs[output]),
            actual.data(),
            sizeof(actual),
            0u,
            sizeof(actual),
            f.diagnostic));
        const std::array<float, 8u> expected{{distance, 3.0f, 1.0f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f}};
        for (unsigned i = 0u; i < actual.size(); ++i)
            assert(actual[i] == (untouched || i >= expected.size() ? 0.0f : expected[i]));
    }

    OrdinaryFixture f;
    std::array<core::UniquePtr<GpuBuffer>, 4u> sources;
    static constexpr std::array<std::uint32_t, 7u> bindings{{10u, 11u, 22u, 24u, 25u, 26u, 27u}};
};

void verifyExecution(bool arrays, bool dropCommand)
{
    OrdinaryFixture f(arrays);
    auto first = f.prepare(0u), outputA = arrays ? f.prepare(1u) : nullptr;
    auto command = f.command(arrays ? 35u : 24u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    if (arrays) {
        assert(!command->recordDispatch(6u, 1u, 1u, f.diagnostic)
            && !command->stats()->nativeDispatchEncoded);
        assert(command->recordOrdinaryPreparedComputeBinding(outputA, f.diagnostic));
    }
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - f.quota->liveBytes());
    for (unsigned repeat = 0u; repeat < 32u; ++repeat)
        assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    f.dispatch(command);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    pressure.release();
    f.writeInputs(true, 1u);
    if (arrays)
        assert(!command->recordDispatch(
            6u, 1u, 1u, f.diagnostic)); // revision requires a fresh snapshot/rebind
    auto second = f.prepare(arrays ? 0u : 1u), outputB = arrays ? f.prepare(2u) : nullptr;
    assert(command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic));
    if (arrays)
        assert(command->recordOrdinaryPreparedComputeBinding(outputB, f.diagnostic));
    f.dispatch(command);
    assert(command->retentionStorage()->resourceOwnerCount() == (arrays ? 35u : 24u));
    assert(command->stats()->nativeArgumentBufferAllocationCount == 0u);
    assert(command->recordComputePassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<MetalRhiNativePipelinePayload> pipeline = metalRhiNativePayloadFor(*f.pipeline);
    std::weak_ptr<MetalRhiNativeBufferPayload> uniform =
        arrays ? nullptr : metalRhiNativePayloadFor(*f.uniforms[0]);
    std::weak_ptr<MetalRhiNativeTextureViewPayload> view =
        arrays ? metalRhiNativePayloadFor(*f.views[0]) : nullptr;
    first.reset();
    second.reset();
    outputA.reset();
    outputB.reset();
    for (auto& set : f.sets)
        set.reset();
    f.arena.reset();
    f.pipeline.reset();
    for (auto& buffer : f.uniforms)
        buffer.reset();
    for (auto& v : f.views)
        v.reset();
    for (auto& t : f.textures)
        t.reset();
    assert(!pipeline.expired() && (arrays ? !view.expired() : !uniform.expired()));
    auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryDelay");
    assert(gate && gate->nativeRuntimeBacked);
    auto completion = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, completion, gate, 1u, {}, 0u, f.diagnostic));
    assert(!completion->isComplete() && !pipeline.expired());
    command->abandonUnsubmitted();
    assert(!pipeline.expired());
    if (dropCommand)
        command.reset();
    signalMetalRhiNativeSharedEvent(gate, 1u);
    completion->wait();
    assert(pipeline.expired() && view.expired() && uniform.expired());
    f.verify(0u);
    f.verify(1u, true);
    std::printf(
        "ordinaryCompute: arrays=%d dropped=%d exact A/B output; rebind32/dispatch allocation0 at exhausted quota; exact flattened owners retire at actual completion\n",
        arrays,
        dropCommand);
}

void expectFailure(bool rejected, const core::pmr::String& diagnostic, const char* id)
{
    if (!rejected || diagnostic.find(id) == core::pmr::String::npos)
        std::fprintf(stderr, "expected %s: %s\n", id, diagnostic.c_str());
    assert(rejected && diagnostic.find(id) != core::pmr::String::npos);
}

void verifyPreparationBudgets(bool arrays)
{
    OrdinaryFixture f(arrays);
    auto budget = f.bindingBudget();
    auto prepare = [&] {
        return prepareMetalRhiOrdinaryComputeBinding(
            *f.pipeline, *f.sets[0], 0u, f.registry, budget, f.quota, f.diagnostic);
    };
    const auto baseline = f.quota->liveBytes();
    budget.optIn = false;
    expectFailure(!prepare(), f.diagnostic, "BUDGET-INVALID");
    budget = f.bindingBudget();
    budget.contractVersion = 0u;
    expectFailure(!prepare(), f.diagnostic, "BUDGET-INVALID");
    budget = f.bindingBudget();
    budget.maxCpuMetadataBytes--;
    expectFailure(!prepare(), f.diagnostic, "CPU-QUOTA-MISMATCH");
    budget = f.bindingBudget();
    if (arrays) {
        budget.maxArgumentBuffers = budget.maxArgumentBufferBytes = 0u;
        expectFailure(!prepare(), f.diagnostic, "ARGUMENT-COUNT-BUDGET");
        budget = f.bindingBudget();
        budget.maxArgumentBufferBytes = 1u;
        expectFailure(!prepare(), f.diagnostic, "ARGUMENT-BYTE-BUDGET");
        budget = f.bindingBudget();
    }
    assert(f.quota->liveBytes() == baseline);
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - baseline);
        expectFailure(!prepare(), f.diagnostic, "PREPARATION-ALLOCATION-FAILED");
        assert(f.quota->liveBytes() == f.quota->limitBytes());
    }
    // Preparation must ignore even a hostile process PMR default. The actual
    // nested metadata, snapshots and function helper use explicit core resources.
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto binding = prepare();
    std::pmr::set_default_resource(previous);
    assert(binding && f.quota->liveBytes() > baseline);
    const auto completeCharge = f.quota->liveBytes() - baseline;
    if (arrays) {
        budget.maxArgumentBufferBytes = binding->function().argumentBytes - 1u;
        expectFailure(!prepare(), f.diagnostic, "ARGUMENT-BYTE-BUDGET");
        budget.maxArgumentBufferBytes = binding->function().argumentBytes;
        assert(prepare());
    } else
        assert(!binding->function().argumentBytes && binding->function().arrays.empty());
    binding.reset();
    assert(f.quota->liveBytes() == baseline);
    budget = f.bindingBudget();
    {
        core::CpuMemoryQuotaLease pressure(
            f.quota, f.quota->limitBytes() - baseline - completeCharge + 1u);
        expectFailure(!prepare(), f.diagnostic, "PREPARATION-ALLOCATION-FAILED");
        assert(f.quota->liveBytes() == f.quota->limitBytes() - completeCharge + 1u);
    }
    assert(f.quota->liveBytes() == baseline);
    auto foreign = core::makeShared<MetalRhiResourceRegistry>();
    expectFailure(!prepareMetalRhiOrdinaryComputeBinding(
                      *f.pipeline, *f.sets[0], 0u, foreign, budget, f.quota, f.diagnostic),
        f.diagnostic,
        "REGISTRY-MISMATCH");
    std::printf(
        "ordinaryPreparation: arrays=%d actual count/allocated bytes/core quota, partial cleanup, hostile PMR default\n",
        arrays);
}

void verifyOwnerAdmission(bool arrays)
{
    OrdinaryFixture f(arrays);
    auto first = f.prepare(0u);
    for (const unsigned capacity : {0u, 1u, arrays ? 18u : 17u}) {
        auto command = f.command(capacity);
        assert(command->recordComputePassBegin(f.diagnostic));
        expectFailure(!command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic),
            f.diagnostic,
            "OWNER-CAPACITY");
        assert(!command->retentionStorage()->resourceOwnerCount()
            && !command->stats()->nativeComputePipelineBound
            && !command->stats()->nativeDescriptorArgumentBound);
        assert(!command->recordDispatch(1u, 1u, 1u, f.diagnostic));
        command->abandonUnsubmitted();
    }
    auto command = f.command(arrays ? 25u : 18u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    if (arrays)
        assert(command->recordOrdinaryPreparedComputeBinding(f.prepare(1u), f.diagnostic));
    const auto count = command->retentionStorage()->resourceOwnerCount();
    if (!arrays)
        f.writeInputs(false, 1u);
    auto other = f.prepare(arrays ? 2u : 1u);
    expectFailure(!command->recordOrdinaryPreparedComputeBinding(other, f.diagnostic),
        f.diagnostic,
        "OWNER-CAPACITY");
    assert(command->retentionStorage()->resourceOwnerCount() == count
        && !command->recordingPoisoned());
    f.dispatch(command);
    assert(command->recordComputePassEnd(f.diagnostic));
    f.submit(command);
    f.verify(0u);
    f.verify(1u, false, true);
    std::printf(
        "ordinaryAdmission: arrays=%d zero/one/late count rejection atomically preserves legal A binding\n",
        arrays);
}

void verifyRecordingIdentityAndFailureCleanup()
{
    OrdinaryFixture f(false);
    auto first = f.prepare(0u);
    auto command = f.command(32u);
    assert(command->recordComputePassBegin(f.diagnostic));
    auto otherQuota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
    auto other = prepareMetalRhiOrdinaryComputeBinding(
        *f.pipeline, *f.sets[0], 0u, f.registry, f.bindingBudget(), otherQuota, f.diagnostic);
    assert(other);
    expectFailure(!command->recordOrdinaryPreparedComputeBinding(other, f.diagnostic),
        f.diagnostic,
        "CPU-QUOTA-MISMATCH");
    assert(!command->retentionStorage()->resourceOwnerCount()
        && !command->stats()->nativeComputePipelineBound);
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    expectFailure(
        !command->recordPushConstants(ShaderStage::Compute, 0u, 4u, f.values.data(), f.diagnostic),
        f.diagnostic,
        "OPERATION-UNIMPLEMENTED");
    expectFailure(
        !command->recordDispatchIndirect(metalRhiNativePayloadFor(*f.outputs[1]), 0u, f.diagnostic),
        f.diagnostic,
        "OPERATION-UNIMPLEMENTED");
    assert(
        !command->recordComputePipelineBind(metalRhiNativePayloadFor(*f.pipeline), f.diagnostic));
    f.dispatch(command);
    assert(command->recordComputePassEnd(f.diagnostic));
    f.submit(command);
    f.verify(0u);
    other.reset();
    assert(!otherQuota->liveBytes());
    {
        auto wrongRegistry = core::makeShared<MetalRhiResourceRegistry>();
        const char* id = nullptr;
        CommandRetentionBudget budget{1u, true, 16u, 0u, 0u, 0u, f.cpuBytes};
        auto storage = common::CommandRetentionStorage::prepare(budget, f.quota, id);
        auto foreignCommand = createMetalRhiNativeCommandPayload("ForeignOrdinaryRegistry",
            wrongRegistry,
            &f.diagnostic,
            0u,
            0u,
            0u,
            std::move(storage));
        assert(foreignCommand);
        assert(foreignCommand->recordComputePassBegin(f.diagnostic));
        expectFailure(!foreignCommand->recordOrdinaryPreparedComputeBinding(first, f.diagnostic),
            f.diagnostic,
            "RECORDING-MISMATCH");
        assert(!foreignCommand->retentionStorage()->resourceOwnerCount());
        foreignCommand->abandonUnsubmitted();
    }
    {
        auto exhausted = f.command(19u);
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - f.quota->liveBytes());
        expectFailure(!exhausted->recordComputePassBegin(f.diagnostic),
            f.diagnostic,
            "PREPARATION-ALLOCATION-FAILED");
        assert(exhausted->recordingPoisoned() && !exhausted->finish(f.diagnostic));
    }
    {
        auto waitFailure = f.command(19u);
        assert(waitFailure->recordComputePassBegin(f.diagnostic));
        assert(waitFailure->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
        f.dispatch(waitFailure);
        assert(
            waitFailure->recordComputePassEnd(f.diagnostic) && waitFailure->finish(f.diagnostic));
        std::weak_ptr<const MetalRhiOrdinaryPreparedComputeBinding> observed = first;
        first.reset();
        auto completion = waitFailure->preparedCompletionState();
        auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryWaitFailure");
        assert(gate);
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - f.quota->liveBytes());
        MetalRhiNativeQueueStats queue;
        expectFailure(!waitFailure->submit(queue, completion, gate, 1u, {}, 0u, f.diagnostic),
            f.diagnostic,
            "PREPARATION-ALLOCATION-FAILED");
        assert(completion->isComplete() && !queue.nativeQueueSubmitCount && observed.expired());
    }
    OrdinaryFixture dynamic(false, true);
    expectFailure(!prepareMetalRhiOrdinaryComputeBinding(*dynamic.pipeline,
                      *dynamic.sets[0],
                      0u,
                      dynamic.registry,
                      dynamic.bindingBudget(),
                      dynamic.quota,
                      dynamic.diagnostic),
        dynamic.diagnostic,
        "OFFSET-COUNT");
}

void verifyPreviouslyBoundSetsAndPipelineSwitch()
{
    OrdinaryFixture f(true);
    auto first = f.prepare(0u), output = f.prepare(1u);
    auto command = f.command(35u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(output, f.diagnostic));
    f.registry->unregisterResource(f.outputs[0]->id());
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    expectFailure(
        !command->recordDispatch(6u, 1u, 1u, f.diagnostic), f.diagnostic, "RESOURCE-NOT-LIVE");
    f.registry->registerBuffer(*f.outputs[0]);
    ComputePipelineDesc desc;
    desc.layout = f.layout.get();
    desc.shaderPackage = &f.package;
    desc.computeShader = f.package.entrypoints[0];
    auto pipeline = f.device->createComputePipeline(desc);
    assert(pipeline);
    auto next = prepareMetalRhiOrdinaryComputeBinding(
        *pipeline, *f.sets[0], 0u, f.registry, f.bindingBudget(), f.quota, f.diagnostic);
    assert(next && command->recordOrdinaryPreparedComputeBinding(next, f.diagnostic));
    assert(!command->recordDispatch(6u, 1u, 1u, f.diagnostic)); // the old pipeline's set1 was reset
    auto nextOutput = prepareMetalRhiOrdinaryComputeBinding(
        *pipeline, *f.sets[1], 1u, f.registry, f.bindingBudget(), f.quota, f.diagnostic);
    assert(nextOutput && command->recordOrdinaryPreparedComputeBinding(nextOutput, f.diagnostic));
    assert(command->retentionStorage()->resourceOwnerCount() == 35u);
    f.dispatch(command);
    assert(command->recordComputePassEnd(f.diagnostic));
    f.submit(command);
    f.verify(0u);
}

enum class InvalidBinding {
    Rewrite,
    Arena,
    Descriptor,
    Resource,
    Parent,
    Seal,
    Registry,
    Quota,
    Pipeline
};

void verifyInvalidBinding(InvalidBinding failure)
{
    const bool arrays = failure == InvalidBinding::Parent || failure == InvalidBinding::Resource;
    OrdinaryFixture f(arrays);
    auto first = f.prepare(0u);
    auto command = f.command(32u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    if (arrays)
        assert(command->recordOrdinaryPreparedComputeBinding(f.prepare(1u), f.diagnostic));
    f.dispatch(command);
    const auto count = command->retentionStorage()->resourceOwnerCount();
    const char* id = nullptr;
    std::shared_ptr<void> state;
    switch (failure) {
    case InvalidBinding::Rewrite:
        f.writeInputs(false, 0u);
        id = "CONTENT-CHANGED";
        break;
    case InvalidBinding::Arena:
        f.arena->reset();
        id = "ALLOCATION-STALE";
        break;
    case InvalidBinding::Descriptor:
        f.sets[0].reset();
        id = "OBJECT-NOT-LIVE";
        break;
    case InvalidBinding::Resource:
        f.views[2].reset();
        id = "RESOURCE-NOT-LIVE";
        break;
    case InvalidBinding::Parent:
        f.textures[2].reset();
        id = "PARENT";
        break;
    case InvalidBinding::Seal:
        f.outputs[0]->sealWrites();
        id = "GPU-WRITES-SEALED";
        break;
    case InvalidBinding::Registry:
        f.registry->unregisterResource(f.uniforms[1]->id());
        id = "RESOURCE-NOT-LIVE";
        break;
    case InvalidBinding::Quota:
        f.quota->setLimitBytes(f.cpuBytes - 1u);
        id = "CPU-QUOTA-MISMATCH";
        break;
    case InvalidBinding::Pipeline:
        state = std::move(first->function().pipeline->pipelineState);
        id = "OWNER-PROJECTION-MISMATCH";
        break;
    }
    expectFailure(
        !command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic), f.diagnostic, id);
    assert(command->retentionStorage()->resourceOwnerCount() == count
        && !command->recordingPoisoned());
    assert(!command->recordDispatch(arrays ? 6u : 1u, 1u, 1u, f.diagnostic));
    if (failure == InvalidBinding::Quota)
        f.quota->setLimitBytes(f.cpuBytes);
    if (failure == InvalidBinding::Pipeline)
        first->function().pipeline->pipelineState = std::move(state);
    assert(command->recordComputePassEnd(f.diagnostic));
    f.submit(command);
    f.verify(0u);
}

void verifyStorageImageExecution(bool dropCommand)
{
    StorageImageFixture image;
    auto& f = image.f;
    auto first = f.prepare(0u), second = f.prepare(1u);
    auto command = f.command(30u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - f.quota->liveBytes());
        for (unsigned repeat = 0u; repeat < 32u; ++repeat) {
            assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
            f.dispatch(command);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic));
    f.dispatch(command);
    assert(command->retentionStorage()->resourceOwnerCount() == 24u);
    assert(
        command->recordComputePassEnd(f.diagnostic) && command->recordBlitPassBegin(f.diagnostic));
    image.copy(command, 0u);
    image.copy(command, 1u);
    assert(command->retentionStorage()->resourceOwnerCount() == 30u);
    assert(command->recordBlitPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::array<std::weak_ptr<MetalRhiNativeTextureViewPayload>, 2> views;
    std::array<std::weak_ptr<void>, 2> allocationsHeld;
    std::weak_ptr<const MetalRhiOrdinaryPreparedComputeBinding> prepared = first;
    for (unsigned i = 0u; i < 2u; ++i) {
        views[i] = metalRhiNativePayloadFor(*f.views[i]);
        allocationsHeld[i] = metalRhiNativePayloadFor(*f.views[i])->resource.allocationReleaseToken;
        f.sets[i].reset();
        f.views[i].reset();
        f.textures[i].reset();
        f.uniforms[i].reset();
    }
    first.reset();
    second.reset();
    f.arena.reset();
    f.pipeline.reset();
    auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryStorageDelay");
    assert(gate);
    auto completion = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, completion, gate, 1u, {}, 0u, f.diagnostic)
        && !completion->isComplete());
    for (unsigned i = 0u; i < 2u; ++i)
        assert(!views[i].expired() && !allocationsHeld[i].expired());
    assert(!prepared.expired());
    if (dropCommand)
        command.reset();
    signalMetalRhiNativeSharedEvent(gate, 1u);
    completion->wait();
    for (unsigned i = 0u; i < 2u; ++i) {
        assert(views[i].expired() && allocationsHeld[i].expired());
        image.verify(i);
    }
    assert(prepared.expired());
    std::printf(
        "ordinaryStorageImage: dropped=%d exact 3x2 RGBA16F A/B and padding; owner24/30; rebind32/dispatch allocation0; actual delayed view/allocation retirement\n",
        dropCommand);
}

void verifyStorageImageRejectionAndCancellation()
{
    {
        StorageImageFixture image(TextureFormat::Unknown);
        auto& f = image.f;
        expectFailure(
            !prepareMetalRhiOrdinaryComputeBinding(
                *f.pipeline, *f.sets[0], 0u, f.registry, f.bindingBudget(), f.quota, f.diagnostic),
            f.diagnostic,
            "STORAGE-FORMAT-MISSING");
        assert(!f.quota->liveBytes());
    }
    {
        StorageImageFixture image;
        auto& f = image.f;
        auto wrong = f.device->createTexture({"WrongStorageFormat",
            StorageImageFixture::extent,
            TextureFormat::R32Float,
            TextureUsage::Storage,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderWrite});
        assert(wrong);
        auto view = f.device->createTextureView(
            *wrong, {"WrongStorageFormatView", TextureFormat::R32Float, TextureUsage::Storage});
        assert(view);
        f.check(f.sets[1]->write(
            {0u, DescriptorType::StorageTexture, view->id(), DescriptorResourceKind::TextureView}));
        expectFailure(
            !prepareMetalRhiOrdinaryComputeBinding(
                *f.pipeline, *f.sets[1], 0u, f.registry, f.bindingBudget(), f.quota, f.diagnostic),
            f.diagnostic,
            "STORAGE-FORMAT-MISMATCH");
        assert(!f.quota->liveBytes());
        auto first = f.prepare(0u);
        auto command = f.command(15u);
        assert(command->recordComputePassBegin(f.diagnostic));
        assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
        auto payload = metalRhiNativePayloadFor(*f.views[0]);
        const auto format = payload->format;
        payload->format = TextureFormat::R32Float;
        expectFailure(!command->recordDispatch(1u, 1u, 1u, f.diagnostic),
            f.diagnostic,
            "STORAGE-FORMAT-MISMATCH");
        assert(!command->stats()->nativeDispatchEncoded
            && command->retentionStorage()->resourceOwnerCount() == 15u);
        payload->format = format;
        command->abandonUnsubmitted();
        StorageImageFixture foreign;
        const auto foreignWrite = f.sets[0]->write({0u,
            DescriptorType::StorageTexture,
            foreign.f.views[0]->id(),
            DescriptorResourceKind::TextureView});
        expectFailure(!foreignWrite.success, foreignWrite.diagnostics, "RESOURCE-NOT-LIVE");
    }
    for (const unsigned owners : {0u, 1u, 14u}) {
        StorageImageFixture image;
        auto& f = image.f;
        auto first = f.prepare(0u);
        auto command = f.command(owners);
        assert(command->recordComputePassBegin(f.diagnostic));
        expectFailure(!command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic),
            f.diagnostic,
            "OWNER-CAPACITY");
        assert(!command->retentionStorage()->resourceOwnerCount()
            && !command->stats()->nativeDescriptorArgumentBound);
        command->abandonUnsubmitted();
    }
    for (const unsigned invalid : {0u, 1u, 2u, 3u}) {
        StorageImageFixture image;
        auto& f = image.f;
        auto first = f.prepare(0u), second = f.prepare(1u);
        auto command = f.command(15u);
        assert(command->recordComputePassBegin(f.diagnostic));
        assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
        expectFailure(!command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic),
            f.diagnostic,
            "OWNER-CAPACITY");
        if (invalid == 0u)
            f.views[1].reset();
        else if (invalid == 1u)
            f.textures[1].reset();
        else if (invalid == 2u)
            f.check(f.sets[1]->write({1u,
                DescriptorType::UniformBuffer,
                f.uniforms[0]->id(),
                DescriptorResourceKind::Buffer}));
        else {
            auto native = metalRhiNativePayloadFor(*f.pipeline);
            const auto mapping = native->metalBindingSpace.front();
            native->metalBindingSpace.front().type = DescriptorType::SampledTexture;
            assert(!command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic));
            native->metalBindingSpace.front() = mapping;
        }
        if (invalid != 3u)
            expectFailure(!command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic),
                f.diagnostic,
                invalid == 0u       ? "RESOURCE-NOT-LIVE"
                    : invalid == 1u ? "PARENT-NOT-LIVE"
                                    : "CONTENT-CHANGED");
        assert(command->retentionStorage()->resourceOwnerCount() == 15u);
        f.dispatch(command);
        assert(command->recordComputePassEnd(f.diagnostic));
        f.submit(command);
        auto copy = createMetalRhiNativeCommandPayload(
            "OrdinaryStorageLegalReadback", f.registry, &f.diagnostic);
        assert(copy);
        assert(copy->recordBlitPassBegin(f.diagnostic));
        image.copy(copy, 0u);
        assert(copy->recordBlitPassEnd(f.diagnostic));
        f.submit(copy);
        image.verify(0u);
    }
    for (const unsigned invalid : {0u, 1u, 2u}) {
        StorageImageFixture image;
        auto& f = image.f;
        auto first = f.prepare(0u);
        auto command = f.command(15u);
        assert(command->recordComputePassBegin(f.diagnostic));
        assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
        if (invalid == 0u)
            f.views[0].reset();
        else if (invalid == 1u)
            f.textures[0].reset();
        else
            f.check(f.sets[0]->write({1u,
                DescriptorType::UniformBuffer,
                f.uniforms[1]->id(),
                DescriptorResourceKind::Buffer}));
        expectFailure(!command->recordDispatch(1u, 1u, 1u, f.diagnostic),
            f.diagnostic,
            invalid == 0u       ? "RESOURCE-NOT-LIVE"
                : invalid == 1u ? "PARENT-NOT-LIVE"
                                : "CONTENT-CHANGED");
        assert(!command->stats()->nativeDispatchEncoded
            && command->retentionStorage()->resourceOwnerCount() == 15u);
        command->abandonUnsubmitted();
    }
    for (const bool sealed : {false, true}) {
        StorageImageFixture image;
        auto& f = image.f;
        auto first = f.prepare(0u);
        auto command = f.command(15u);
        assert(command->recordComputePassBegin(f.diagnostic));
        assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
        if (sealed) {
            assert(command->recordComputePassEnd(f.diagnostic));
            assert(command->finish(f.diagnostic));
        }
        std::weak_ptr<MetalRhiNativeTextureViewPayload> view =
            metalRhiNativePayloadFor(*f.views[0]);
        std::weak_ptr<void> allocation =
            metalRhiNativePayloadFor(*f.views[0])->resource.allocationReleaseToken;
        first.reset();
        f.sets[0].reset();
        f.views[0].reset();
        f.textures[0].reset();
        assert(!view.expired() && !allocation.expired());
        command->abandonUnsubmitted();
        assert(view.expired() && allocation.expired()
            && !command->retentionStorage()->resourceOwnerCount());
    }
    std::puts(
        "ordinaryStorageImageAdmission: owner0/1/one-less, rejected B preserves legal A, stale view/parent/revision/mapping, open/sealed cancellation pass");
}

void verifyPreparedPushExecution(bool dropCommand)
{
    TraceFixture t;
    auto& f = t.f;
    TracePushConstants a, b;
    b.origin[1] = 7.0f;
    auto first = f.prepare(0u), second = f.prepare(1u);
    auto pushA = t.push(a), pushB = t.push(b);
    a = {};
    b = {}; // prepared storage must outlive and ignore caller source changes
    auto command = f.command(32u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    expectFailure(
        !command->recordDispatch(1u, 1u, 1u, f.diagnostic), f.diagnostic, "ORDINARY-PUSH-REQUIRED");
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, pushA));
    expectFailure(!command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, pushB),
        f.diagnostic,
        "PC-E013");
    assert(command->recordOrdinaryPreparedComputeBinding(
        first, f.diagnostic)); // descriptor-only rebind preserves pending bytes
    f.dispatch(command);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - f.quota->liveBytes());
        for (unsigned repeat = 0u; repeat < 32u; ++repeat) {
            assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, pushA));
            f.dispatch(command);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic, pushB));
    f.dispatch(command);
    assert(command->retentionStorage()->resourceOwnerCount() == 32u
        && command->stats()->nativeArgumentBufferAllocationCount == 0u);
    assert(command->recordComputePassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedComputePush> observedA = pushA, observedB = pushB;
    first.reset();
    second.reset();
    pushA.reset();
    pushB.reset();
    for (auto& set : f.sets)
        set.reset();
    f.arena.reset();
    f.pipeline.reset();
    for (auto& source : t.sources)
        source.reset();
    assert(!observedA.expired() && !observedB.expired());
    auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryPushDelay");
    assert(gate);
    auto completion = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, completion, gate, 1u, {}, 0u, f.diagnostic));
    assert(!completion->isComplete());
    if (dropCommand)
        command.reset();
    signalMetalRhiNativeSharedEvent(gate, 1u);
    completion->wait();
    assert(observedA.expired() && observedB.expired());
    t.verify(0u, 5.0f);
    t.verify(1u, 7.0f);
    std::printf(
        "ordinaryPush: dropped=%d exact native ray A=5 B=7 material=3 normalY=1 emissive=.5, owner32, rebind32/dispatch allocation0, actual delayed packet retirement\n",
        dropCommand);
}

void verifyPushPreparationNegatives()
{
    TraceFixture t;
    auto& f = t.f;
    TracePushConstants value;
    auto desc = t.desc(value);
    auto prepare = [&] {
        return prepareMetalRhiOrdinaryComputePush(
            *f.pipeline, desc, f.bindingBudget(), f.quota, f.diagnostic);
    };
    const auto baseline = f.quota->liveBytes();
    desc.stages = ShaderStage::None;
    expectFailure(!prepare(), f.diagnostic, "PC-E001");
    desc = t.desc(value);
    desc.stages = ShaderStage::Fragment;
    expectFailure(!prepare(), f.diagnostic, "PC-E006");
    desc = t.desc(value);
    desc.offsetBytes = 4u;
    expectFailure(!prepare(), f.diagnostic, "PC-E002");
    desc = t.desc(value);
    desc.sizeBytes = 0u;
    expectFailure(!prepare(), f.diagnostic, "PC-E003");
    desc = t.desc(value);
    desc.sizeBytes += 16u;
    expectFailure(!prepare(), f.diagnostic, "PC-E009");
    desc = t.desc(value);
    desc.sizeBytes = kL2AtomicPushConstantLimitBytes + 1u;
    expectFailure(!prepare(), f.diagnostic, "PC-E012");
    desc = t.desc(value);
    desc.data = nullptr;
    expectFailure(!prepare(), f.diagnostic, "PC-E004");
    desc = t.desc(value);
    assert(f.quota->liveBytes() == baseline);
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto packet = prepare();
    std::pmr::set_default_resource(previous);
    assert(packet);
    const auto charge = f.quota->liveBytes() - baseline;
    assert(charge > sizeof(value));
    packet.reset();
    assert(f.quota->liveBytes() == baseline);
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - baseline);
        expectFailure(!prepare(), f.diagnostic, "PREPARATION-ALLOCATION-FAILED");
        assert(f.quota->liveBytes() == f.quota->limitBytes());
    }
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - baseline - charge + 1u);
        expectFailure(!prepare(), f.diagnostic, "PREPARATION-ALLOCATION-FAILED");
        assert(f.quota->liveBytes() == f.quota->limitBytes() - charge + 1u);
    }
    auto native = metalRhiNativePayloadFor(*f.pipeline);
    const auto slot = native->pushConstantMetalBufferIndex;
    native->pushConstantMetalBufferIndex = UINT32_MAX;
    expectFailure(!prepare(), f.diagnostic, "PC-E020");
    native->pushConstantMetalBufferIndex = slot;
    auto& reflection = const_cast<ShaderReflectionDesc&>(f.pipeline->shaderReflection());
    const auto reflectionVersion = reflection.pushConstantContractVersion,
               stages = reflection.pushConstantStages;
    reflection.pushConstantContractVersion = "InvalidPushContract";
    expectFailure(!prepare(), f.diagnostic, "PC-E015");
    reflection.pushConstantContractVersion = reflectionVersion;
    reflection.pushConstantStages = "VS";
    expectFailure(!prepare(), f.diagnostic, "PC-E016");
    reflection.pushConstantStages = stages;
    reflection.pushConstantByteCount -= 16u;
    expectFailure(!prepare(), f.diagnostic, "PC-E009");
    reflection.pushConstantByteCount += 16u;
    const auto version = native->computeBufferFactsVersion;
    native->computeBufferFactsVersion = 0u;
    expectFailure(!prepare(), f.diagnostic, "FACTS-MISSING");
    native->computeBufferFactsVersion = version;
    const auto it = std::find_if(native->computeBufferFacts.begin(),
        native->computeBufferFacts.end(),
        [&](const auto& fact) {
            return fact.slot == slot;
        });
    assert(it != native->computeBufferFacts.end());
    const auto factIndex = static_cast<std::size_t>(it - native->computeBufferFacts.begin());
    auto fact = *it;
    native->computeBufferFacts.push_back(fact);
    expectFailure(!prepare(), f.diagnostic, "FACTS-DUPLICATE");
    native->computeBufferFacts.pop_back();
    native->computeBufferFacts[factIndex].requiredBytes -= 16u;
    expectFailure(!prepare(), f.diagnostic, "NATIVE-SIZE-MISMATCH");
    native->computeBufferFacts[factIndex] = fact;
    auto function = std::move(native->computeFunction);
    expectFailure(!prepare(), f.diagnostic, "PIPELINE-INVALID");
    native->computeFunction = std::move(function);
    assert(f.quota->liveBytes() == baseline);
    OrdinaryFixture noPush(false);
    expectFailure(!prepareMetalRhiOrdinaryComputePush(*noPush.pipeline,
                      t.desc(value),
                      noPush.bindingBudget(),
                      noPush.quota,
                      noPush.diagnostic),
        noPush.diagnostic,
        "PC-E007");
    std::printf(
        "ordinaryPushPreparation: malformed full blocks, actual slot/facts, exact CPU quota and late byte-copy cleanup, hostile PMR default pass\n");
}

void verifyPushAtomicAdmissionAndLifecycle()
{
    TraceFixture t;
    auto& f = t.f;
    TracePushConstants a, b;
    b.origin[1] = 7.0f;
    auto first = f.prepare(0u), second = f.prepare(1u);
    auto pushA = t.push(a), pushB = t.push(b);
    for (const unsigned capacity : {0u, 1u, 24u}) {
        auto command = f.command(capacity);
        assert(command->recordComputePassBegin(f.diagnostic));
        expectFailure(!command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, pushA),
            f.diagnostic,
            "OWNER-CAPACITY");
        assert(!command->retentionStorage()->resourceOwnerCount()
            && !command->stats()->nativeComputePipelineBound
            && !command->stats()->nativeDescriptorArgumentBound
            && !command->stats()->nativePushConstantSetBytesWritten);
        command->abandonUnsubmitted();
    }
    auto command = f.command(25u);
    assert(command->recordComputePassBegin(f.diagnostic));
    auto pipelineNative = pushA->pipeline();
    const auto mapped = pipelineNative->metalBindingSpace.front().metalSlot;
    pipelineNative->metalBindingSpace.front().metalSlot = pushA->slot();
    assert(!command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, pushA));
    assert(!command->retentionStorage()->resourceOwnerCount()
        && !command->stats()->nativePushConstantSetBytesWritten);
    pipelineNative->metalBindingSpace.front().metalSlot = mapped;
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, pushA));
    assert(!command->recordDispatch(0u, 1u, 1u, f.diagnostic));
    expectFailure(!command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic, pushB),
        f.diagnostic,
        "PC-E013");
    f.dispatch(command);
    expectFailure(!command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic, pushB),
        f.diagnostic,
        "OWNER-CAPACITY");
    assert(
        command->retentionStorage()->resourceOwnerCount() == 25u && !command->recordingPoisoned());
    auto otherQuota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
    auto foreign = prepareMetalRhiOrdinaryComputePush(
        *f.pipeline, t.desc(b), f.bindingBudget(), otherQuota, f.diagnostic);
    assert(foreign);
    expectFailure(!command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, foreign),
        f.diagnostic,
        "CPU-QUOTA-MISMATCH");
    auto native = pushA->pipeline();
    const auto slot = native->pushConstantMetalBufferIndex;
    native->pushConstantMetalBufferIndex = UINT32_MAX;
    assert(!command->recordDispatch(1u, 1u, 1u, f.diagnostic));
    native->pushConstantMetalBufferIndex = slot;
    f.quota->setLimitBytes(f.cpuBytes - 1u);
    assert(!command->recordDispatch(1u, 1u, 1u, f.diagnostic));
    f.quota->setLimitBytes(f.cpuBytes);
    f.dispatch(command);
    assert(command->recordComputePassEnd(f.diagnostic));
    f.submit(command);
    t.verify(0u, 5.0f);
    t.verify(1u, 0.0f, true);
    foreign.reset();
    assert(!otherQuota->liveBytes());
    // A pending update cannot be silently discarded by switching pipelines.
    ComputePipelineDesc desc;
    desc.layout = f.layout.get();
    desc.shaderPackage = &f.package;
    desc.computeShader = f.package.entrypoints[0];
    auto pipeline = f.device->createComputePipeline(desc);
    assert(pipeline);
    auto otherBinding = prepareMetalRhiOrdinaryComputeBinding(
        *pipeline, *f.sets[0], 0u, f.registry, f.bindingBudget(), f.quota, f.diagnostic);
    assert(otherBinding);
    auto otherPush = prepareMetalRhiOrdinaryComputePush(
        *pipeline, t.desc(a), f.bindingBudget(), f.quota, f.diagnostic);
    assert(otherPush);
    auto switched = f.command(32u);
    assert(switched->recordComputePassBegin(f.diagnostic));
    assert(switched->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, pushA));
    expectFailure(!switched->recordOrdinaryPreparedComputeBinding(otherBinding, f.diagnostic),
        f.diagnostic,
        "PC-E013");
    f.dispatch(switched);
    expectFailure(!switched->recordOrdinaryPreparedComputeBinding(first, f.diagnostic, otherPush),
        f.diagnostic,
        "BINDING-MISMATCH");
    assert(switched->recordOrdinaryPreparedComputeBinding(otherBinding, f.diagnostic));
    expectFailure(!switched->recordDispatch(1u, 1u, 1u, f.diagnostic),
        f.diagnostic,
        "ORDINARY-PUSH-REQUIRED");
    assert(switched->recordOrdinaryPreparedComputeBinding(otherBinding, f.diagnostic, otherPush));
    f.dispatch(switched);
    assert(switched->retentionStorage()->resourceOwnerCount() == 32u);
    assert(switched->recordComputePassEnd(f.diagnostic));
    f.submit(switched);
    t.verify(0u, 5.0f);
    std::printf(
        "ordinaryPushAdmission: exact combined owner25/32, atomic capacity0/1/one-less refusal, pending and foreign packet gates, prior legal A survives, pipeline switch invalidates bytes\n");
}

void verifyPushCancellation(bool sealed, bool waitFailure = false)
{
    TraceFixture t;
    auto& f = t.f;
    TracePushConstants value;
    auto binding = f.prepare(0u);
    auto push = t.push(value);
    auto command = f.command(25u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(binding, f.diagnostic, push));
    std::weak_ptr<const MetalRhiOrdinaryPreparedComputePush> observed = push;
    binding.reset();
    push.reset();
    if (sealed || waitFailure)
        assert(command->recordComputePassEnd(f.diagnostic) && command->finish(f.diagnostic));
    if (waitFailure) {
        auto completion = command->preparedCompletionState();
        auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryPushWaitFailure");
        assert(gate);
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - f.quota->liveBytes());
        MetalRhiNativeQueueStats queue;
        expectFailure(!command->submit(queue, completion, gate, 1u, {}, 0u, f.diagnostic),
            f.diagnostic,
            "PREPARATION-ALLOCATION-FAILED");
        assert(completion->isComplete() && !queue.nativeQueueSubmitCount && observed.expired());
    } else {
        assert(!observed.expired());
        command->abandonUnsubmitted();
        assert(observed.expired());
    }
    command.reset();
    observed.reset();
    assert(!f.quota->liveBytes());
    t.verify(0u, 0.0f, true);
}

void verifyDynamicUniformOffsets(bool dropCommand)
{
    OrdinaryFixture f(false, true);
    auto input = f.offsetA;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto first = f.prepare(0u, input.data(), input.size());
    std::pmr::set_default_resource(previous);
    input = {UINT32_MAX, UINT32_MAX}; // immutable preparation copy
    auto command = f.command(24u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.quota->limitBytes() - f.quota->liveBytes());
        for (unsigned repeat = 0u; repeat < 32u; ++repeat)
            assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
        f.dispatch(command);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    f.writeInputs(false, 1u);
    auto second = f.prepare(1u, f.offsetB.data(), f.offsetB.size());
    assert(command->recordOrdinaryPreparedComputeBinding(second, f.diagnostic));
    f.dispatch(command);
    assert(command->retentionStorage()->resourceOwnerCount() == 24u);
    assert(command->recordComputePassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedComputeBinding> observed = first;
    first.reset();
    second.reset();
    for (auto& set : f.sets)
        set.reset();
    f.arena.reset();
    f.pipeline.reset();
    for (auto& uniform : f.uniforms)
        uniform.reset();
    assert(!observed.expired());
    auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryOffsetDelay");
    assert(gate);
    auto completion = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, completion, gate, 1u, {}, 0u, f.diagnostic));
    assert(!completion->isComplete());
    if (dropCommand)
        command.reset();
    signalMetalRhiNativeSharedEvent(gate, 1u);
    completion->wait();
    assert(observed.expired());
    f.verify(0u);
    f.verify(1u, false, false, 2.0f);
    std::printf(
        "ordinaryOffset: dropped=%d native nonzero A/B byte offsets, immutable inputs, rebind32/dispatch allocation0, exact delayed retirement\n",
        dropCommand);
}

void verifyDynamicOffsetNegatives()
{
    OrdinaryFixture f(false, true);
    auto offsets = f.offsetA;
    auto prepare = [&](const std::uint32_t* input, std::uint32_t count) {
        return prepareMetalRhiOrdinaryComputeBinding(*f.pipeline,
            *f.sets[0],
            0u,
            f.registry,
            f.bindingBudget(),
            f.quota,
            f.diagnostic,
            input,
            count);
    };
    expectFailure(!prepare(nullptr, 2u), f.diagnostic, "OFFSET-COUNT");
    expectFailure(!prepare(offsets.data(), 1u), f.diagnostic, "OFFSET-COUNT");
    expectFailure(!prepare(offsets.data(), 3u), f.diagnostic, "OFFSET-COUNT");
    auto legal = prepare(offsets.data(), 2u);
    assert(legal);
    const auto charged = f.quota->liveBytes();
    auto command = f.command(18u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(legal, f.diagnostic));
    const auto withCommand = f.quota->liveBytes();
    offsets[1]++;
    expectFailure(!prepare(offsets.data(), 2u), f.diagnostic, "OFFSET-ALIGNMENT");
    offsets = f.offsetA;
    offsets[1] = f.offsetB[1] + sizeof(float) * 4u;
    expectFailure(!prepare(offsets.data(), 2u), f.diagnostic, "OFFSET-RANGE");
    offsets = f.offsetA;
    auto native = metalRhiNativePayloadFor(*f.pipeline);
    const auto version = native->computeBufferFactsVersion;
    native->computeBufferFactsVersion = 0u;
    expectFailure(!prepare(offsets.data(), 2u), f.diagnostic, "FACTS-MISSING");
    assert(!command->recordDispatch(1u, 1u, 1u, f.diagnostic));
    native->computeBufferFactsVersion = version;
    const auto selected = std::find_if(native->computeBufferFacts.begin(),
        native->computeBufferFacts.end(),
        [&](const auto& fact) {
            return fact.slot == legal->function().direct[1].slot;
        });
    assert(selected != native->computeBufferFacts.end());
    const auto factIndex = static_cast<std::size_t>(selected - native->computeBufferFacts.begin());
    auto fact = *selected;
    native->computeBufferFacts.push_back(fact);
    expectFailure(!prepare(offsets.data(), 2u), f.diagnostic, "FACTS-DUPLICATE");
    native->computeBufferFacts.pop_back();
    native->computeBufferFacts[factIndex].requiredBytes = 0u;
    expectFailure(!prepare(offsets.data(), 2u), f.diagnostic, "FACTS-MISSING");
    native->computeBufferFacts[factIndex] = fact;
    assert(f.quota->liveBytes() == withCommand && charged < withCommand);
    f.dispatch(command);
    assert(command->recordComputePassEnd(f.diagnostic));
    f.submit(command);
    f.verify(0u);
    // L2 itself excludes offset arrays, before any private native preparation.
    auto invalid = f.sourceLayout;
    invalid.bindings[0].arrayCount = 2u;
    assert(!validateDescriptorLayout(invalid).success);
    OrdinaryFixture storage(false, true, true);
    std::array<std::uint32_t, 3u> storageOffsets{storage.offsetA[0], storage.offsetA[1], 0u};
    expectFailure(!prepareMetalRhiOrdinaryComputeBinding(*storage.pipeline,
                      *storage.sets[0],
                      0u,
                      storage.registry,
                      storage.bindingBudget(),
                      storage.quota,
                      storage.diagnostic,
                      storageOffsets.data(),
                      storageOffsets.size()),
        storage.diagnostic,
        "OFFSET-DOMAIN-UNIMPLEMENTED");
    assert(!storage.quota->liveBytes());
}

void verifyCancellation(bool sealed, bool dynamic = false)
{
    OrdinaryFixture f(!dynamic, dynamic);
    auto first = f.prepare(0u, dynamic ? f.offsetA.data() : nullptr, dynamic ? 2u : 0u);
    auto command = f.command(19u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(first, f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedComputeBinding> prepared = first;
    std::weak_ptr<const void> argument = dynamic
        ? std::shared_ptr<const void>(first->function().descriptors)
        : std::shared_ptr<const void>(first->function().arrays[0].buffer);
    first.reset();
    if (sealed)
        assert(command->recordComputePassEnd(f.diagnostic) && command->finish(f.diagnostic));
    assert(!prepared.expired() && !argument.expired());
    command->abandonUnsubmitted();
    assert(prepared.expired() && argument.expired()
        && !command->retentionStorage()->resourceOwnerCount());
    assert(!command->recordComputePassBegin(f.diagnostic));
    command.reset();
    prepared.reset();
    argument.reset();
    assert(!f.quota->liveBytes());
}

void verifyNativeIdentities(bool arrays, bool dropped)
{
    OrdinaryFixture f(arrays);
    auto input = f.prepare(0u), output = arrays ? f.prepare(1u) : nullptr;
    auto command = f.command(128u);
    assert(command->recordComputePassBegin(f.diagnostic));
    assert(command->recordOrdinaryPreparedComputeBinding(input, f.diagnostic));
    if (output)
        assert(command->recordOrdinaryPreparedComputeBinding(output, f.diagnostic));
    const auto owners = command->retentionStorage()->resourceOwnerCount();
    assert(owners == (arrays ? 25u : 18u));
    // Check explicit native roles, independently of the production visitor.
    for (const auto& selected : {input, output}) {
        if (!selected)
            continue;
        core::BoundedRetainer<void> members(selected->childOwnerCount());
        assert(members.retainMapped(selected->childOwnerCount(), [&](std::size_t i) noexcept {
            return selected->childOwnerAt(i);
        }));
        const auto& function = selected->function();
        const auto& pipeline = *function.pipeline;
        for (const auto& owner : std::array<std::shared_ptr<const void>, 7>{function.pipeline,
                 pipeline.nativeDevice,
                 pipeline.shaderLibrary,
                 pipeline.computeFunction,
                 pipeline.pipelineState,
                 function.descriptors,
                 f.registry})
            assert(owner && members.contains(owner));
        if (function.descriptors->nativeDevice)
            assert(members.contains(function.descriptors->nativeDevice));
        if (function.descriptors->nativeObject)
            assert(members.contains(function.descriptors->nativeObject));
        for (const auto& binding : function.descriptors->bindings) {
            assert(members.contains(binding.payloadOwner()));
            const auto& resource =
                binding.buffer ? binding.buffer->resource : binding.textureView->resource;
            assert(
                members.contains(resource.nativeDevice) && members.contains(resource.nativeObject));
            if (resource.allocationReleaseToken)
                assert(members.contains(resource.allocationReleaseToken));
        }
        for (const auto& argument : function.arrays)
            assert(members.contains(argument.buffer));
    }
    std::uint64_t replacementBytes =
        std::max(input->function().argumentBytes, static_cast<std::uint64_t>(f.outputBytes()));
    for (const auto& buffer : f.uniforms)
        if (buffer)
            replacementBytes = std::max(replacementBytes, buffer->desc().sizeBytes);
    assert(replacementBytes);
    auto replacementBuffer = f.device->createBuffer({"IdentityReplacement",
        replacementBytes,
        ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacementBuffer);
    auto replacementTexture = f.device->createTexture({"IdentityTexture",
        {1u, 1u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(replacementTexture);
    auto replacementView = f.device->createTextureView(
        *replacementTexture, {"IdentityView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
    assert(replacementView);
    const auto bufferNative = metalRhiNativePayloadFor(*replacementBuffer);
    const auto viewNative = metalRhiNativePayloadFor(*replacementView);
    const auto reject = [&](const auto& selected, const char* expected) {
        expectFailure(!command->recordOrdinaryPreparedComputeBinding(selected, f.diagnostic),
            f.diagnostic,
            expected);
        expectFailure(!command->recordDispatch(arrays ? 6u : 1u, 1u, 1u, f.diagnostic),
            f.diagnostic,
            expected);
        assert(!command->stats()->nativeDispatchEncoded && !command->recordingPoisoned()
            && command->retentionStorage()->resourceOwnerCount() == owners);
    };
    auto pipeline = input->function().pipeline;
    for (auto* field : {&pipeline->shaderLibrary,
             &pipeline->fragmentShaderLibrary,
             &pipeline->vertexFunction,
             &pipeline->fragmentFunction,
             &pipeline->computeFunction,
             &pipeline->pipelineState,
             &pipeline->depthStencilState})
        test::MetalNativeIdentityMutationProbe::verifyOwner(
            *field, bufferNative->resource.nativeObject, [&] {
                reject(input, "OWNER-PROJECTION-MISMATCH");
            });
    // The native device gate may reject before the owner projection gate.
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        pipeline->nativeDevice, bufferNative->resource.nativeObject, [&] {
            assert(!command->recordOrdinaryPreparedComputeBinding(input, f.diagnostic));
            assert(!command->recordDispatch(arrays ? 6u : 1u, 1u, 1u, f.diagnostic));
            assert(!command->stats()->nativeDispatchEncoded
                && command->retentionStorage()->resourceOwnerCount() == owners);
        });
    for (const auto& selected : {input, output})
        if (selected) {
            const auto group = std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(
                selected->function().descriptors);
            for (auto* field : {&group->nativeDevice, &group->nativeObject})
                test::MetalNativeIdentityMutationProbe::verifyOwner(
                    *field, bufferNative->resource.nativeObject, [&] {
                        reject(selected, "OWNER-PROJECTION-MISMATCH");
                    });
        }
    for (const auto& selected : {input, output}) {
        if (!selected)
            continue;
        for (const auto& binding : selected->function().descriptors->bindings) {
            auto& resource =
                binding.buffer ? binding.buffer->resource : binding.textureView->resource;
            const auto& other = binding.buffer ? bufferNative->resource : viewNative->resource;
            test::MetalNativeIdentityMutationProbe::verify(resource, other, [&] {
                reject(selected, "RESOURCE-NATIVE-IDENTITY-MISMATCH");
            });
        }
    }
    if (arrays) {
        auto& function = const_cast<MetalRhiNativePreparedComputeBindings&>(input->function());
        assert(function.arrays.size() == 1u);
        auto& argument = function.arrays[0];
        const auto saved = argument.buffer;
        for (const auto& object : {bufferNative->resource.nativeObject,
                 std::shared_ptr<void>(bufferNative->resource.nativeObject, saved.get())}) {
            argument.buffer = object;
            reject(input, "OWNER-PROJECTION-MISMATCH");
            argument.buffer = saved;
        }
    }
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0; i < 32u; ++i)
            assert(command->recordOrdinaryPreparedComputeBinding(input, f.diagnostic));
        f.dispatch(command);
        assert(command->recordComputePassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    assert(command->finish(f.diagnostic));
    // An internal edit after encoding cannot release the original input early.
    core::pmr::Vector<std::weak_ptr<void>> oldObjects{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<const void*> edited{&core::defaultCpuMemoryResource()};
    for (auto* field :
        {&pipeline->shaderLibrary, &pipeline->computeFunction, &pipeline->pipelineState}) {
        oldObjects.push_back(*field);
        *field = bufferNative->resource.nativeObject;
    }
    pipeline.reset();
    for (const auto& binding : input->function().descriptors->bindings) {
        if (binding.type != DescriptorType::UniformBuffer
            && binding.type != DescriptorType::SampledTexture)
            continue;
        auto& resource = binding.buffer ? binding.buffer->resource : binding.textureView->resource;
        if (std::find(edited.begin(), edited.end(), &resource) != edited.end())
            continue;
        edited.push_back(&resource);
        oldObjects.push_back(resource.nativeObject);
        resource.nativeObject = binding.buffer ? bufferNative->resource.nativeObject
                                               : viewNative->resource.nativeObject;
    }
    input.reset();
    output.reset();
    for (auto& set : f.sets)
        set.reset();
    f.arena.reset();
    f.pipeline.reset();
    for (auto& buffer : f.uniforms)
        buffer.reset();
    for (auto& view : f.views)
        view.reset();
    for (auto& texture : f.textures)
        texture.reset();
    for (const auto& object : oldObjects)
        assert(!object.expired());
    auto gate = createMetalRhiNativeSharedEventPayload("IdentityDelayedCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    if (dropped)
        command.reset();
    assert(!done->isComplete());
    for (const auto& object : oldObjects)
        assert(!object.expired());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    for (const auto& object : oldObjects)
        assert(object.expired());
    f.verify(0u);
    std::printf(
        "ordinary Compute native identity: arrays=%d dropped=%d owners=%zu; direct/array objects, ownership aliases, storage and original post-encode retirement pass\n",
        arrays,
        dropped,
        owners);
}

} // namespace

int main()
{
    for (const bool arrays : {false, true}) {
        verifyPreparationBudgets(arrays);
        verifyOwnerAdmission(arrays);
        for (const bool dropped : {false, true}) {
            verifyExecution(arrays, dropped);
            verifyNativeIdentities(arrays, dropped);
        }
    }
    for (const auto invalid : {InvalidBinding::Rewrite,
             InvalidBinding::Arena,
             InvalidBinding::Descriptor,
             InvalidBinding::Resource,
             InvalidBinding::Parent,
             InvalidBinding::Seal,
             InvalidBinding::Registry,
             InvalidBinding::Quota,
             InvalidBinding::Pipeline})
        verifyInvalidBinding(invalid);
    for (const bool sealed : {false, true}) {
        verifyCancellation(sealed);
        verifyCancellation(sealed, true);
    }
    verifyRecordingIdentityAndFailureCleanup();
    verifyPreviouslyBoundSetsAndPipelineSwitch();
    verifyDynamicOffsetNegatives();
    for (const bool dropped : {false, true}) {
        verifyDynamicUniformOffsets(dropped);
        verifyPreparedPushExecution(dropped);
    }
    verifyPushPreparationNegatives();
    verifyPushAtomicAdmissionAndLifecycle();
    verifyPushCancellation(false);
    verifyPushCancellation(true);
    verifyPushCancellation(true, true);
    for (const bool dropped : {false, true})
        verifyStorageImageExecution(dropped);
    verifyStorageImageRejectionAndCancellation();
}
