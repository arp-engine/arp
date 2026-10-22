#include "../fixtures/canonical_graphics_color_contract_fixture.h"
#include "../fixtures/completion_admission_transaction_fixture.h"
#include "../fixtures/original_bindless_graphics_content_contract_fixture.h"
#include "../fixtures/original_mixed_entry_flow_fixture.h"
#include "../fixtures/original_storage_content_sequence_fixture.h"
#include "../fixtures/original_storage_execution_contract_fixture.h"
#include "../fixtures/original_storage_texture_creation_fixture.h"
#include "../fixtures/transfer_dependency_lowering_fixture.h"
#include "core/math/unsigned_range.h"
#include "core/ownership/bounded_retainer.h"
#include "core/runtime/completion_retention_pool.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_indirect_packet.h"
#include "rhi/frontend/bindless_recording_validation.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/bindless_snapshot_metadata.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/descriptor_completed_texture.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_completed_image_lowering.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/bindless_root_program_lowering.h"
#include "rhi/semantics/bindless_storage_image_lowering.h"
#include "rhi/semantics/indirect_instance_lowering.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

#include <cassert>
#include <cstring>
#include <iostream>

#ifdef NDEBUG
#error Bindless contract tests require assertions
#endif

using namespace arp::rhi;

// Synthetic capability inputs exercise the planner, not native execution.
struct Fixture {
    BindlessCapabilities caps;
    BindlessRequirements request;
    BindlessShaderArtifact artifact;

    explicit Fixture(BindlessExecution mode = BindlessExecution::Emulated)
    {
        caps.deviceIdentity = 7;
        caps.execution = mode;
        caps.limitsQueried = true;
        caps.executablePayload = mode != BindlessExecution::Validator;
        assert(!caps.indirectFirstInstance);
        caps.faultReadbackContractVersion = mode == BindlessExecution::Validator ? 0u : 1u;
        caps.maxBindGroups = 4;
        caps.maxBindingsPerGroup = 16;
        caps.maxUniformBufferBindingSize = 64u * 1024u;
        caps.maxStorageBufferBindingSize = 128u * 1024u * 1024u;
        caps.stageLimits[1].sampledTextures = 4;
        caps.support[1][0] = {BindlessOperation::SampleGrad, true};
        request.optIn = true;
        request.allowEmulated = true;
        request.deviceIdentity = 7;
        request.shaderPackageIdentity = 11;
        request.budget = {2, 4, 256, 4};
        BindlessTableRequirement table;
        table.tableId = 1;
        table.resourceCount = 2;
        table.binding.type = DescriptorType::SampledTexture;
        table.binding.stages = ShaderStage::Fragment;
        table.operations = BindlessOperation::SampleGrad;
        table.nonUniform = true;
        request.tables.push_back(table);
        artifact.execution = mode;
        artifact.faultReportingContractVersion = mode == BindlessExecution::Validator ? 0u : 1u;
        artifact.shaderPackageIdentity = 11;
        artifact.physicalLayout.name = "bindless-contract-fixture";
        artifact.tables.push_back({1, 3, 2, table.operations, table.nonUniform});
        artifact.snapshotBytes = 128;
        artifact.switchCases = mode == BindlessExecution::Emulated ? 2 : 0;
        auto binding = table.binding;
        binding.binding = 3;
        binding.arrayCount = mode == BindlessExecution::Emulated ? 1 : 2;
        artifact.physicalLayout.bindings.push_back(binding);
        if (mode == BindlessExecution::Emulated) {
            binding.binding = 4;
            artifact.physicalLayout.bindings.push_back(binding);
        }
        if (mode != BindlessExecution::Validator) {
            DescriptorBindingDesc latch;
            latch.binding = 14;
            latch.type = DescriptorType::StorageBuffer;
            latch.stages = ShaderStage::Fragment;
            artifact.physicalLayout.bindings.push_back(latch);
            artifact.faultLatch = {1, 14, ShaderStage::Fragment};
            caps.stageLimits[1].storageBuffers = 1;
        }
    }

    BindlessLoweringPlan plan() const { return planBindless(caps, request, artifact); }

    void useBuffer(DescriptorType type = DescriptorType::ReadOnlyStorageBuffer)
    {
        auto& table = request.tables[0];
        table.binding.type = type;
        table.operations = type == DescriptorType::StorageBuffer
            ? BindlessOperation::Load | BindlessOperation::Store
            : BindlessOperation::Load;
        table.bufferLayout = {123u, 4u, 4u, 4u}; // explicit test uint layout identity
        if (type == DescriptorType::UniformBuffer)
            table.bufferLayout = {456u, 16u, 16u, 16u};
        artifact.tables[0].operations = table.operations;
        artifact.tables[0].bufferLayout = table.bufferLayout;
        for (auto& binding : artifact.physicalLayout.bindings)
            if (binding.binding != artifact.faultLatch.binding
                || !artifact.faultLatch.contractVersion)
                binding.type = type;
        caps.stageLimits[1].storageBuffers = artifact.faultLatch.contractVersion ? 3 : 2;
        caps.stageLimits[1].uniformBuffers = 2;
        caps.support[1][bindlessResourceClass(type)] = {table.operations, true};
    }

    void reject(const char* id) const
    {
        const auto p = plan();
        assert(!p.success && !p.executable);
        assert(p.execution == BindlessExecution::DiagnosticFailure);
        if (std::strcmp(p.diagnosticId, id) != 0)
            std::cerr << "expected " << id << ", got " << p.diagnosticId << '\n';
        assert(std::strcmp(p.diagnosticId, id) == 0);
    }
};

class BindlessContractFixture {
public:
    static void verifyOwnershipAndProducerContracts();
    static void verifyReflectionAndLowering();
    static void verifyNullPacketAndFaultContracts();
    static void verifyNullSnapshotContracts();
};

static void verifyCompletePipelineInterface()
{
    using Contract = arp::rhi::semantics::BindlessPipelineInterfaceContract;
    {
        DescriptorBindingDesc lhs, rhs;
        lhs.type = DescriptorType::StorageTexture;
        rhs.type = DescriptorType::SampledTexture;
        TextureSubresourceRange a{1u, 1u, 0u, 1u, TextureAspect::Color}, b = a;
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto* id = Contract::textureAliasDiagnostic(backend, true, lhs, a, rhs, b);
            assert(bool(id) == (backend == Backend::WebGPU));
            if (id)
                assert(
                    std::string_view(id) == "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-TEXTURE-ALIAS");
        }
        const auto conflict = [&] {
            return Contract::textureAliasDiagnostic(Backend::WebGPU, true, lhs, a, rhs, b);
        };
        assert(!Contract::textureAliasDiagnostic(Backend::WebGPU, false, lhs, a, rhs, b));
        b.baseMipLevel = 2u;
        assert(!conflict());
        a.mipLevelCount = 2u;
        assert(conflict());
        b.baseArrayLayer = 1u;
        assert(!conflict());
        a.arrayLayerCount = 2u;
        assert(conflict());
        b.aspect = TextureAspect::Depth;
        assert(!conflict());
        b.aspect = a.aspect;
        lhs.storageTextureAccess = StorageTextureAccess::ReadOnly;
        assert(!conflict());
        rhs.type = DescriptorType::StorageTexture;
        assert(conflict());
        rhs.storageTextureAccess = StorageTextureAccess::ReadOnly;
        assert(!conflict());
        lhs.storageTextureAccess = StorageTextureAccess::ReadWrite;
        assert(conflict());
        lhs.type = DescriptorType::Sampler;
        assert(!conflict());
        lhs.type = rhs.type = DescriptorType::SampledTexture;
        assert(!conflict());
        // Overflow-safe interval comparison does not wrap a large mip range.
        lhs.type = DescriptorType::StorageTexture;
        a.baseMipLevel = UINT32_MAX - 1u;
        a.mipLevelCount = 2u;
        b.baseMipLevel = UINT32_MAX;
        assert(conflict());
    }
    {
        DescriptorBindingWrite lhs, rhs;
        lhs.resource = rhs.resource = GpuResourceId{1u};
        lhs.type = DescriptorType::StorageBuffer;
        rhs.type = DescriptorType::ReadOnlyStorageBuffer;
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto* id = Contract::bufferAliasDiagnostic(backend, lhs, 16u, rhs, 16u);
            assert(bool(id) == (backend == Backend::WebGPU));
            if (id)
                assert(
                    std::string_view(id) == "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-BUFFER-ALIAS");
        }
        rhs.resource = GpuResourceId{2u};
        assert(!Contract::bufferAliasDiagnostic(Backend::WebGPU, lhs, 16u, rhs, 16u));
        rhs.resource = lhs.resource;
        lhs.type = DescriptorType::ReadOnlyStorageBuffer;
        assert(!Contract::bufferAliasDiagnostic(Backend::WebGPU, lhs, 16u, rhs, 16u));
        lhs.type = DescriptorType::StorageBuffer;
        lhs.bufferBindingRange = {2u, 0u, 8u};
        rhs.bufferBindingRange = {2u, 8u, 8u};
        assert(!Contract::bufferAliasDiagnostic(Backend::WebGPU, lhs, 16u, rhs, 16u));
        rhs.bufferBindingRange = {2u, 7u, 8u};
        assert(Contract::bufferAliasDiagnostic(Backend::WebGPU, lhs, 16u, rhs, 16u));
        rhs.type = DescriptorType::UniformBuffer;
        assert(Contract::bufferAliasDiagnostic(Backend::WebGPU, lhs, 16u, rhs, 16u));
    }
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto lowering = Contract::lowering(backend);
        const bool executable = backend != Backend::Null;
        assert(lowering.interfaceInspectionAvailable && lowering.executionAvailable == executable
            && bool(lowering.diagnostic) == !executable);
        assert(lowering.validatorOnly == (backend == Backend::Null));
    }
    Fixture f(BindlessExecution::Validator);
    assert(!Contract::standaloneSnapshotDiagnostic(f.artifact));
    f.artifact.pipelineInterfaceContractVersion = 1u;
    BindlessPipelineDescriptorDomain frame;
    frame.set = 2u;
    frame.layout.name = "frame";
    DescriptorBindingDesc uniform;
    // Binding3 also exists in the root: identities are (set,binding).
    uniform.binding = 3u;
    uniform.stages = ShaderStage::Vertex | ShaderStage::Fragment;
    uniform.type = DescriptorType::UniformBuffer;
    frame.layout.bindings.push_back(uniform);
    {
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        BindlessPipelineDescriptorDomain owned(frame);
        BindlessPipelineDescriptorDomain assigned;
        assigned = owned;
        std::pmr::set_default_resource(previous);
        assert(owned.layout.bindings.size() == 1u && assigned.layout.bindings.size() == 1u
            && owned.layout.bindings.get_allocator().resource()
                == &arp::core::defaultCpuMemoryResource());
    }
    f.artifact.additionalDomains.push_back(frame);
    assert(std::string_view(Contract::standaloneSnapshotDiagnostic(f.artifact))
        == "RHI-BINDLESS-PIPELINE-DOMAINS-ORIGINAL-ROOT-REQUIRED");
    f.caps.stageLimits[0].uniformBuffers = f.caps.stageLimits[1].uniformBuffers = 1u;
    BindlessPipelineInterfaceSummary summary;
    const auto inspect = [&] {
        return inspectBindlessPipelineInterface(f.caps, f.artifact, summary);
    };
    assert(!inspect() && summary.bindGroupCount == 2u && summary.descriptorCount == 3u);
    assert(summary.additionalPerStage[0].uniformBuffers == 1u
        && summary.additionalPerStage[1].uniformBuffers == 1u);
    assert(f.plan().success && !f.plan().executable && f.plan().descriptorCount == 2u);
    const auto identity = f.plan().identity;
    f.artifact.additionalDomains[0].layout.bindings[0].binding = 4u;
    assert(f.plan().success && f.plan().identity != identity);
    const auto reject = [&](const char* expected) {
        summary.bindGroupCount = 999u;
        summary.descriptorCount = 999u;
        const auto* id = inspect();
        assert(id && std::string_view(id) == expected);
        assert(!summary.bindGroupCount && !summary.descriptorCount);
        for (const auto& stage : summary.additionalPerStage)
            assert(!stage.sampledTextures && !stage.samplers && !stage.storageTextures
                && !stage.uniformBuffers && !stage.storageBuffers);
    };
    f.artifact.pipelineInterfaceContractVersion = 2u;
    reject("RHI-BINDLESS-PIPELINE-INTERFACE-VERSION");
    f.artifact.pipelineInterfaceContractVersion = 0u;
    f.reject("RHI-BINDLESS-PIPELINE-INTERFACE-VERSION");
    f.artifact.pipelineInterfaceContractVersion = 1u;
    f.artifact.additionalDomains[0].set = f.artifact.set;
    reject("RHI-BINDLESS-PIPELINE-DOMAIN-COLLISION");
    f.artifact.additionalDomains[0].set = 4u;
    reject("RHI-BINDLESS-PIPELINE-DOMAIN-INVALID");
    f.artifact.additionalDomains[0].set = 2u;
    f.artifact.additionalDomains.push_back(f.artifact.additionalDomains[0]);
    reject("RHI-BINDLESS-PIPELINE-DOMAIN-COLLISION");
    f.artifact.additionalDomains.pop_back();
    f.artifact.otherBindGroups = 1u;
    reject("RHI-BINDLESS-PIPELINE-DOMAINS-MISMATCH");
    f.artifact.otherBindGroups = 0u;
    f.artifact.reservedPerStage[1].uniformBuffers = 1u;
    reject("RHI-BINDLESS-PIPELINE-RESERVATIONS-MISMATCH");
    f.artifact.reservedPerStage[1] = {};
    auto& bindings = f.artifact.additionalDomains[0].layout.bindings;
    bindings.push_back(bindings[0]);
    f.caps.stageLimits[0].uniformBuffers = f.caps.stageLimits[1].uniformBuffers = 2u;
    reject("RHI-BINDLESS-PHYSICAL-BINDING-COLLISION");
    bindings.pop_back();
    f.caps.stageLimits[1].uniformBuffers = 0u;
    reject("RHI-BINDLESS-STAGE-LIMIT");
    f.caps.stageLimits[1].uniformBuffers = 1u;
    bindings[0].arrayCount = UINT32_MAX;
    reject("RHI-BINDLESS-STAGE-LIMIT");
    bindings[0].arrayCount = 1u;
    bindings[0].dynamicOffset = true;
    reject("RHI-BINDLESS-PIPELINE-BINDING-SHAPE");
    bindings[0].dynamicOffset = false;
    bindings[0].stages = ShaderStage::None;
    reject("RHI-BINDLESS-PIPELINE-BINDING-INVALID");
    bindings[0] = uniform;
    // Cube IBL, depth image and independent comparison sampler are representable
    // ABI, without claiming their offline package or backend execution proof.
    DescriptorBindingDesc image;
    image.binding = 4u;
    image.stages = ShaderStage::Fragment;
    image.textureViewDimensionConstrained = true;
    image.textureViewDimension = TextureViewDimension::TextureCube;
    bindings.push_back(image);
    image.binding = 5u;
    image.textureViewDimension = TextureViewDimension::Texture2D;
    image.depthTexture = true;
    bindings.push_back(image);
    DescriptorBindingDesc sampler;
    sampler.binding = 6u;
    sampler.type = DescriptorType::Sampler;
    sampler.stages = ShaderStage::Fragment;
    sampler.comparisonSampler = true;
    bindings.push_back(sampler);
    f.caps.stageLimits[1].sampledTextures = 4u;
    f.caps.stageLimits[1].samplers = 1u;
    assert(!inspect() && summary.descriptorCount == 6u);
    f.caps.combinedSampledStorageTextureLimits[1] = 3u;
    reject("RHI-BINDLESS-STAGE-LIMIT");
    f.caps.combinedSampledStorageTextureLimits[1].reset();
    const auto cubeIdentity = f.plan().identity;
    bindings[1].textureViewDimension = TextureViewDimension::Texture2DArray;
    assert(f.plan().success && f.plan().identity != cubeIdentity);
    for (const auto mode : {BindlessExecution::Native,
             BindlessExecution::Emulated,
             BindlessExecution::DiagnosticFailure}) {
        f.caps.execution = mode;
        f.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
    }
    // Inspection is cold/allocation-free even under an exhausted caller quota.
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        arp::core::CpuMemoryQuotaLease pressure(quota, 1u);
        const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i)
            assert(!inspect());
        assert(before == arp::core::defaultCpuAllocator().stats().allocationCount);
    }
    assert(!quota->liveBytes());
}

void BindlessContractFixture::verifyOwnershipAndProducerContracts()
{
    {
        // Ownership mechanics only: this is not a device-issued Graphics binding.
        using namespace arp::core;

        struct RetainedLeaseFixture {
            std::shared_ptr<const std::uint32_t> storage;
            std::shared_ptr<AtomicCapacityLease> lease;
        };

        auto budget = makeShared<AtomicCapacityBudget>(1u);
        auto storage = makeShared<std::uint32_t>(7u);
        auto owner = makeShared<RetainedLeaseFixture>();
        owner->storage = storage;
        owner->lease = makeShared<AtomicCapacityLease>(budget, 1u);
        assert(owner->lease->charged() && budget->used() == 1u);
        std::weak_ptr<RetainedLeaseFixture> observedOwner = owner;
        std::weak_ptr<const std::uint32_t> observedStorage = storage;
        BoundedRetainer<void> recording(1u), completion;
        std::shared_ptr<const void> alias(owner, storage.get());
        auto other = makeShared<std::uint32_t>(8u);
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        assert(recording.retain(alias) && recording.contains(storage) && recording.retain(storage));
        assert(recording.values().size() == 1u && !recording.canRetain(other)
            && !recording.retain(other));
        assert(!recording.canRetain({}));
        completion = std::move(recording);
        owner.reset();
        storage.reset();
        alias.reset();
        assert(!observedOwner.expired() && !observedStorage.expired() && budget->used() == 1u);
        completion.clear();
        completion.clear();
        assert(observedOwner.expired() && observedStorage.expired() && budget->used() == 0u);
        assert(defaultCpuAllocator().stats().allocationCount == allocations);
    }
    {
        const std::array<BindlessFaultValidationStatus, 7> states{
            BindlessFaultValidationStatus::Pending,
            BindlessFaultValidationStatus::Clean,
            BindlessFaultValidationStatus::Faulted,
            BindlessFaultValidationStatus::ReadbackFailed,
            BindlessFaultValidationStatus::Abandoned,
            BindlessFaultValidationStatus::NotExecuted,
            BindlessFaultValidationStatus::Unavailable};
        for (const auto consumer : states)
            for (const auto producer : states) {
                const auto merged =
                    mergeBindlessFaultReports({7u, consumer, 0u}, {7u, producer, 0u});
                assert(merged.validate().success
                    == (consumer == BindlessFaultValidationStatus::Clean
                        && producer == BindlessFaultValidationStatus::Clean));
            }
        assert(!mergeBindlessFaultReports({7u, BindlessFaultValidationStatus::Clean, 0u},
            {8u, BindlessFaultValidationStatus::Clean, 0u})
                .validate()
                .success);
        const auto fault = mergeBindlessFaultReports({7u, BindlessFaultValidationStatus::Clean, 0u},
            {7u, BindlessFaultValidationStatus::Faulted, 1u});
        assert(fault.status == BindlessFaultValidationStatus::Faulted && fault.faultBits == 1u
            && !fault.validate().success);
        auto device = createNullDevice();
        const auto* extension = static_cast<const BindlessCrossExtension*>(
            device->queryInterface(kExtensionBindlessCross));
        arp::core::pmr::String diagnostic;
        assert(extension
            && !extension->createIndirectProcessorPool(*device, {1u, 4096u, 4096u}, diagnostic)
            && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-EXECUTION-UNAVAILABLE");
    }
    {
        auto device = createNullDevice();
        auto buffer = device->createBuffer({"monotonic buffer protection",
            16u,
            ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        const std::array<std::uint32_t, 4> words{};
        assert(buffer && device->writeBuffer(*buffer, words.data(), sizeof(words)).success);
        buffer->sealHostWrites();
        buffer->sealHostWrites();
        assert(buffer->hostWritesSealed() && !buffer->gpuWritesSealed()
            && !buffer->gpuTransferWritesSealed());
        assert(device->writeBuffer(*buffer, words.data(), sizeof(words)).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
        DescriptorLayoutDesc layout;
        layout.name = "host sealed shader writer";
        layout.bindings = {{3u, DescriptorType::StorageBuffer, ShaderStage::Fragment}};
        auto arena = device->createDescriptorArena({"host sealed writer", 1u, 1u});
        assert(arena);
        auto set = arena->allocateDescriptorSet({"writer", layout});
        assert(set);
        assert(
            set->write({3u,
                           DescriptorType::StorageBuffer,
                           buffer->id(),
                           DescriptorResourceKind::Buffer})
                .success);
        assert(validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements)
                .success);
        buffer->sealWrites();
        buffer->sealWrites();
        assert(buffer->hostWritesSealed() && buffer->gpuWritesSealed()
            && buffer->gpuTransferWritesSealed());
        assert(device->writeBuffer(*buffer, words.data(), sizeof(words)).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
        buffer->sealHostWrites();
        assert(buffer->gpuWritesSealed() && buffer->gpuTransferWritesSealed());
        assert(!validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements)
                .success);
    }
    {
        using Cold = CanonicalZeroBaseIndirectColdBindingContract;
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto lowering = Cold::lowering(backend);
            assert(!lowering.executable && lowering.diagnosticId);
            assert(lowering.validatorOnly == (backend == Backend::Null));
            assert(lowering.prototypeColdBindings == (backend == Backend::Metal));
        }
        BindlessShaderArtifact artifact;
        artifact.snapshotMetadata = {1u, 0u, Cold::metadataWordCount, ShaderStage::Compute};
        for (std::uint32_t i = 0u; i < Cold::tableCount; ++i) {
            BindlessTableMapping table;
            table.tableId = i + 1u;
            table.capacity = 1u;
            table.metadataWordOffset = 4u + i * 5u;
            table.bufferLayout.elementStrideBytes =
                i == 3u ? sizeof(ZeroBaseIndirectProducerParams) : sizeof(std::uint32_t);
            artifact.tables.push_back(table);
        }
        Cold::Resources resources{{{1u, {100u}, 32u},
            {2u, {101u}, sizeof(DrawIndexedIndirectCommand)},
            {3u, {102u}, sizeof(IndirectInstanceBase)},
            {4u, {103u}, sizeof(ZeroBaseIndirectProducerParams)}}};
        Cold::Words words;
        Cold::Bytes bytes;
        const auto build = [&] {
            return Cold::buildMetadata(artifact, 0x100000002ull, resources, words, bytes);
        };
        assert(!build() && words[2] == 2u && words[3] == 1u);
        assert(words[8] == 8u && words[13] == 5u && words[18] == 4u && words[23] == 1u);
        assert(bytes[8u] == 2u && bytes[12u] == 1u && bytes[32u] == 8u);
        const auto saved = resources;
        resources[0].sizeBytes = 31u;
        assert(build());
        resources = saved;
        resources[0].sizeBytes = (std::uint64_t{UINT32_MAX} + 1u) * sizeof(std::uint32_t);
        assert(build());
        resources = saved;
        resources[0].sizeBytes = std::uint64_t{UINT32_MAX} * sizeof(std::uint32_t);
        assert(!build() && words[8] == UINT32_MAX);
        resources = saved;
        resources[0].resource = {};
        assert(build());
        resources = saved;
        resources[0].resource = resources[1].resource;
        assert(build());
        resources = saved;
        resources[0].tableId = resources[1].tableId;
        assert(build());
        resources = saved;
        resources[0].tableId = 99u;
        assert(build());
        resources = saved;
        assert(Cold::buildMetadata(artifact, 0u, resources, words, bytes));
        // Reordered authored tables still use explicit IDs rather than ordinals.
        std::swap(artifact.tables[0], artifact.tables[3]);
        assert(!build() && words[8] == 8u && words[23] == 1u);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!build());
            resources[0].sizeBytes = 31u;
            assert(build());
            resources = saved;
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == arp::core::defaultCpuAllocator().stats().allocationCount);
    }
    {
        class Source final : public BindlessSnapshotMetadataSource {
        public:
            std::array<BindlessTableMapping, 2> tables{};
            std::array<std::uint32_t, 2> active{{1u, 1u}};
            bool readable = true;
            std::uint32_t elements = 5u;

            std::size_t tableCount() const noexcept override { return tables.size(); }

            bool readTable(std::size_t i, BindlessMetadataTableSource& out) const override
            {
                if (i >= tables.size())
                    return false;
                out = {&tables[i], active[i]};
                return true;
            }

            bool readElementCount(std::size_t i, std::uint32_t, std::uint32_t& out) const override
            {
                out = i == 0u ? elements : 0u;
                return readable;
            }
        } source;

        source.tables[0].tableId = 7u;
        source.tables[0].capacity = 2u;
        source.tables[0].metadataWordOffset = 4u;
        source.tables[0].bufferLayout.elementStrideBytes = sizeof(std::uint32_t);
        source.tables[1].tableId = 9u;
        source.tables[1].capacity = 1u;
        source.tables[1].metadataWordOffset = 10u;
        BindlessSnapshotMetadataLayout layout{1u, 0u, 15u, ShaderStage::Compute};
        std::array<std::uint32_t, 15> words{};
        const auto build = [&] {
            return buildBindlessSnapshotMetadata(
                layout, 0x100000002ull, source, words.data(), words.size());
        };
        assert(build().success && words[0] == 1u && words[1] == 2u && words[2] == 2u
            && words[3] == 1u);
        assert(words[4] == 7u && words[5] == 1u && words[6] == 2u && words[7] == 0u
            && words[8] == 5u && words[9] == 0u && words[10] == 9u && words[14] == 0u);
        const auto correct = words;
        source.tables[1].metadataWordOffset = 9u;
        assert(build().diagnostics == "RHI-BINDLESS-METADATA-BUILD-OVERLAP" && words == correct);
        source.tables[1].metadataWordOffset = 10u;
        source.tables[1].tableId = 7u;
        assert(build().diagnostics == "RHI-BINDLESS-METADATA-BUILD-OVERLAP");
        source.tables[1].tableId = 9u;
        source.active[0] = 3u;
        assert(build().diagnostics == "RHI-BINDLESS-METADATA-BUILD-TABLE");
        source.active[0] = 1u;
        source.elements = 0u;
        assert(build().diagnostics == "RHI-BINDLESS-ACCESS-METADATA-MISSING");
        source.elements = 5u;
        source.readable = false;
        assert(!build().success);
        source.readable = true;
        layout.wordCount--;
        assert(build().diagnostics == "RHI-BINDLESS-METADATA-BUILD-INVALID");
        layout.wordCount++;
        source.tables[1].metadataWordOffset = UINT32_MAX;
        assert(build().diagnostics == "RHI-BINDLESS-METADATA-BUILD-TABLE");
        source.tables[1].metadataWordOffset = 10u;
        assert(
            !buildBindlessSnapshotMetadata(layout, 0u, source, words.data(), words.size()).success);
        assert(!buildBindlessSnapshotMetadata(layout, 1u, source, nullptr, words.size()).success);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 32u; ++i)
            assert(build().success);
        assert(allocations == arp::core::defaultCpuAllocator().stats().allocationCount);
    }
    {
        auto device = createNullDevice();
        arp::core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        BindlessFaultDomainDesc desc;
        desc.label = "OwnedFaultDomain";
        assert(!BindlessOwnedFaultDomain::create(*device, desc, diagnostic)
            && diagnostic == "RHI-BINDLESS-FAULT-DOMAIN-DESCRIPTOR");
        desc.maxResidentBytes = 1u;
        const auto before = device->memoryStats().allocatedBytes;
        assert(!BindlessOwnedFaultDomain::create(*device, desc, diagnostic)
            && diagnostic == "RHI-BINDLESS-FAULT-DOMAIN-BUDGET");
        assert(device->memoryStats().allocatedBytes == before);
        desc.maxResidentBytes = 65536u;
        auto domain = BindlessOwnedFaultDomain::create(*device, desc, diagnostic);
        assert(domain && domain->validate().success && domain->generation()
            && domain->reportedResidentBytes() <= desc.maxResidentBytes);
        assert(domain->faultLatchPriorState() == ResourceState::Undefined);
        const std::uint32_t zero = 0u;
        assert(device->writeBuffer(*domain->faultLatchBuffer(), &zero, sizeof(zero)).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
        assert(domain->faultLatchBuffer()->gpuTransferWritesSealed()
            && !domain->faultLatchBuffer()->gpuWritesSealed());
        assert(domain->faultReadbackBuffer()->hostWritesSealed());
        auto unrelated = arp::core::makeShared<std::uint32_t>(0u);
        auto alias = std::shared_ptr<const BindlessFaultDomain>(unrelated, domain.get());
        assert(!BindlessFaultSubmissionLease::acquire(alias, diagnostic).valid()
            && diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH");
        alias.reset();
        unrelated.reset();
        std::weak_ptr<BindlessOwnedFaultDomain> observed = domain;
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        auto lease = BindlessFaultSubmissionLease::acquire(domain, diagnostic);
        assert(lease.valid());
        assert(!BindlessFaultSubmissionLease::acquire(domain, diagnostic).valid()
            && diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
        BindlessFaultSubmissionLease moved(std::move(lease));
        assert(!lease.valid() && moved.valid());
        moved.release();
        moved.release();
        assert(domain->faultLatchPriorState() == ResourceState::Undefined);
        lease = BindlessFaultSubmissionLease::acquire(domain, diagnostic);
        assert(lease.valid());
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
        // Null is a lifetime/transition validator, never native completion proof.
        lease.markFaultReadbackCompleted();
        assert(domain->faultLatchPriorState() == ResourceState::CopySrc);
        domain.reset();
        assert(!observed.expired());
        lease.release();
        assert(observed.expired() && device->memoryStats().allocatedBytes == before);
        desc.contractVersion = 2u;
        assert(!BindlessOwnedFaultDomain::create(*device, desc, diagnostic)
            && diagnostic == "RHI-BINDLESS-FAULT-DOMAIN-DESCRIPTOR");
        assert(!BindlessFaultSubmissionLease::acquire({}, diagnostic).valid());
    }
    {
        using Pool = arp::core::runtime::CompletionRetentionPool<std::uint32_t>;
        Pool pool(2u);
        auto first = arp::core::makeUnique<std::uint32_t>(11u);
        auto second = arp::core::makeUnique<std::uint32_t>(22u);
        auto excess = arp::core::makeUnique<std::uint32_t>(33u);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        const auto one = pool.reserve(first), two = pool.reserve(second);
        assert(one && two && !first && !second && pool.pending() == 2u);
        const auto& inspection = pool;
        assert(inspection.canCancel(one) && inspection.canCancel(two));
        assert(
            !inspection.canCancel({}) && !inspection.canCancel({one.index, one.generation + 1u}));
        assert(!inspection.canCancel({pool.capacity(), one.generation}));
        assert(pool.containsMatching([](const auto& value) noexcept {
            return value == 11u;
        }));
        assert(!pool.containsMatching([](const auto& value) noexcept {
            return value == 33u;
        }));
        assert(!pool.reserve(excess) && excess && pool.pending() == 2u);
        assert(!pool.resolve(one, true) && pool.submit(one) && !pool.submit(one));
        assert(!inspection.canCancel(one) && inspection.canCancel(two));
        assert(!pool.cancel(one) && pool.cancel(two) && pool.pending() == 1u);
        assert(!pool.containsMatching([](const auto& value) noexcept {
            return value == 22u;
        }));
        const auto reused = pool.reserve(excess);
        assert(reused && reused.index == two.index && reused.generation != two.generation);
        assert(!inspection.canCancel(two) && inspection.canCancel(reused));
        assert(!pool.get(two) && !pool.cancel(two) && !pool.resolve(two, true));
        assert(pool.submit(reused) && pool.resolve(one, false));
        assert(!pool.accepting() && pool.failed() && !pool.closed() && pool.pending() == 2u);
        assert(!inspection.canCancel(one) && !inspection.canCancel(reused));
        assert(pool.get(one) && *pool.get(one) == 11u);
        assert(pool.containsMatching([](const auto& value) noexcept {
            return value == 11u;
        }));
        assert(pool.resolve(reused, true) && pool.pending() == 1u && !pool.accepting());
        std::uint32_t invalidated = 0u;
        pool.invalidate([&](std::uint32_t& value) noexcept {
            assert(pool.closed() && !pool.accepting() && value == 11u);
            ++invalidated;
            value = 0u;
        });
        assert(invalidated == 1u);
        pool.invalidate([&](std::uint32_t&) noexcept {
            ++invalidated;
        });
        assert(invalidated == 1u);
        assert(pool.closed() && !pool.accepting() && !pool.pending() && !pool.get(one));
        assert(!inspection.canCancel(one) && !inspection.canCancel(reused));
        assert(!pool.containsMatching([](const auto&) noexcept {
            return true;
        }));
        assert(!pool.resolve(one, true) && !pool.cancel(one) && !pool.submit(one));
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
        Pool empty(0u);
        auto candidate = arp::core::makeUnique<std::uint32_t>(44u);
        assert(!empty.reserve(candidate) && candidate);
        Pool cancelled(1u);
        const auto cancelledTicket = cancelled.reserve(candidate);
        assert(cancelledTicket && cancelled.cancel(cancelledTicket) && !cancelled.pending());
        auto afterFailure = arp::core::makeUnique<std::uint32_t>(55u);
        assert(!pool.reserve(afterFailure) && afterFailure);
    }
    {
        using semantics::IndirectInstanceBaseLowering;
        using semantics::IndirectInstanceDiagnostic;
        constexpr DrawIndexedIndirectCommand logical{3u, 2u, 7u, -4, 5u};
        constexpr auto encoded = IndirectInstanceBaseLowering::encode(logical, 7u);
        static_assert(encoded.valid() && encoded.physical.firstInstance == 0u);
        static_assert(encoded.physical.indexCount == 3u && encoded.physical.instanceCount == 2u
            && encoded.physical.firstIndex == 7u && encoded.physical.vertexOffset == -4);
        static_assert(
            IndirectInstanceBaseLowering::resolve(encoded.mapping, 1u).logicalInstance == 6u);
        assert(IndirectInstanceBaseLowering::resolve(encoded.mapping, 2u).diagnostic
            == IndirectInstanceDiagnostic::InstanceInvalid);
        for (const auto capacity : {0u, 4u, 6u}) {
            const auto rejected = IndirectInstanceBaseLowering::encode(logical, capacity);
            assert(!rejected.valid() && rejected.physical.instanceCount == 0u
                && rejected.physical.firstInstance == 0u && rejected.mapping.contractVersion == 0u);
        }
        constexpr auto overflow =
            IndirectInstanceBaseLowering::encode({3u, 2u, 0u, 0, UINT32_MAX - 1u}, UINT32_MAX);
        static_assert(!overflow.valid() && overflow.physical.instanceCount == 0u);
        constexpr auto last =
            IndirectInstanceBaseLowering::encode({3u, 1u, 0u, 0, UINT32_MAX - 1u}, UINT32_MAX);
        static_assert(
            last.valid() && IndirectInstanceBaseLowering::resolve(last.mapping, 0u).valid());
        constexpr auto invisible = IndirectInstanceBaseLowering::encode({3u, 0u, 0u, 0, 7u}, 7u);
        static_assert(invisible.valid()
            && !IndirectInstanceBaseLowering::resolve(invisible.mapping, 0u).valid());
        auto stale = encoded.mapping;
        stale.contractVersion = 2u;
        assert(IndirectInstanceBaseLowering::resolve(stale, 0u).diagnostic
            == IndirectInstanceDiagnostic::MappingInvalid);
        stale = encoded.mapping;
        stale.instanceCapacity = 6u;
        assert(!IndirectInstanceBaseLowering::resolve(stale, 0u).valid());
        assert(
            IndirectInstanceBaseLowering::resolve(encoded.mapping, 1u, 7u).logicalInstance == 6u);
        assert(!IndirectInstanceBaseLowering::resolve(encoded.mapping, 0u, 8u).valid());
        assert(!IndirectInstanceBaseLowering::resolve(encoded.mapping, 0u, 0u).valid());
        Fixture compilerAbi;
        const auto prior = compilerAbi.plan();
        assert(prior.success);
        compilerAbi.artifact.indirectInstanceBaseContractVersion = 1u;
        const auto current = compilerAbi.plan();
        assert(current.success && current.identity != prior.identity);
        assert(!compilerAbi.caps.indirectFirstInstance);
        compilerAbi.artifact.indirectInstanceBaseContractVersion = 2u;
        compilerAbi.reject("RHI-BINDLESS-INDIRECT-INSTANCE-BASE-VERSION");
        Fixture noFault(BindlessExecution::Validator);
        noFault.artifact.indirectInstanceBaseContractVersion = 1u;
        noFault.reject("RHI-BINDLESS-INDIRECT-INSTANCE-BASE-FAULT-LATCH-MISSING");
    }
    {
        Fixture consumer;
        consumer.artifact.indirectInstanceBaseContractVersion = 1u;
        consumer.artifact.indirectInstanceConsumer = {1u, 0u, 1u, 7u};
        consumer.caps.indirectInstanceConsumerContractVersion = 1u;
        DescriptorBindingDesc mapping;
        mapping.binding = 0u;
        mapping.type = DescriptorType::ReadOnlyStorageBuffer;
        mapping.stages = ShaderStage::Vertex;
        consumer.artifact.physicalLayout.bindings.push_back(mapping);
        consumer.caps.stageLimits[0].storageBuffers = 1u;
        consumer.request.budget.maxUpdatesPerSnapshot = 5u;
        const auto original = consumer.plan();
        assert(original.success);
        consumer.artifact.indirectInstanceConsumer.instanceCapacity = 8u;
        assert(consumer.plan().identity != original.identity);
        consumer.artifact.indirectInstanceConsumer.recordIndex = 2u;
        assert(consumer.plan().identity != original.identity);
        consumer.artifact.indirectInstanceConsumer.binding = 3u;
        consumer.reject("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
        consumer.artifact.indirectInstanceConsumer.binding = 0u;
        consumer.artifact.physicalLayout.bindings.back().stages = ShaderStage::Fragment;
        consumer.caps.stageLimits[1].storageBuffers = 2u;
        consumer.reject("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
        consumer.artifact.physicalLayout.bindings.back().stages = ShaderStage::Vertex;
        consumer.artifact.indirectInstanceConsumer.instanceCapacity = 0u;
        consumer.reject("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
        consumer.artifact.indirectInstanceConsumer.instanceCapacity = 8u;
        consumer.artifact.indirectInstanceConsumer.contractVersion = 2u;
        consumer.caps.indirectInstanceConsumerContractVersion = 2u;
        consumer.reject("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
        consumer.artifact.indirectInstanceConsumer.contractVersion = 1u;
        consumer.caps.indirectInstanceConsumerContractVersion = 1u;
        consumer.artifact.indirectInstanceBaseContractVersion = 0u;
        consumer.reject("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
        consumer.artifact.indirectInstanceBaseContractVersion = 1u;
        consumer.caps.indirectInstanceConsumerContractVersion = 0u;
        consumer.reject("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-UNAVAILABLE");
        consumer.caps.indirectInstanceConsumerContractVersion = 1u;
        consumer.caps.maxStorageBufferBindingSize = 32u;
        consumer.reject("RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-ABI");
    }
    {
        for (const auto mode : {BindlessExecution::Validator,
                 BindlessExecution::Native,
                 BindlessExecution::Emulated}) {
            Fixture producer(mode);
            producer.artifact.indirectInstanceBaseContractVersion = 1u;
            if (mode == BindlessExecution::Validator)
                producer.artifact.faultReportingContractVersion = 1u;
            producer.artifact.zeroBaseIndirectProducerContractVersion = 1u;
            producer.reject("RHI-BINDLESS-ZERO-BASE-INDIRECT-PRODUCER-UNAVAILABLE");
            producer.caps.zeroBaseIndirectProducerContractVersion = 1u;
            // A synthetic planner input checks gates; it never certifies a package or execution.
            producer.artifact.zeroBaseIndirectProducerContractVersion = 2u;
            producer.reject("RHI-BINDLESS-ZERO-BASE-INDIRECT-PRODUCER-VERSION");
            producer.artifact.zeroBaseIndirectProducerContractVersion = 1u;
            producer.artifact.indirectInstanceBaseContractVersion = 0u;
            producer.reject("RHI-BINDLESS-ZERO-BASE-INDIRECT-PRODUCER-ABI");
            producer.artifact.indirectInstanceBaseContractVersion = 1u;
            producer.artifact.indirectInstanceConsumer.contractVersion = 1u;
            producer.reject("RHI-BINDLESS-ZERO-BASE-INDIRECT-PRODUCER-ABI");
        }
        Fixture identity;
        const auto previous = identity.plan();
        identity.caps.zeroBaseIndirectProducerContractVersion = 1u;
        assert(identity.plan().success && identity.plan().identity != previous.identity);
    }
    for (auto status : {BindlessFaultValidationStatus::Pending,
             BindlessFaultValidationStatus::Faulted,
             BindlessFaultValidationStatus::ReadbackFailed,
             BindlessFaultValidationStatus::Abandoned,
             BindlessFaultValidationStatus::NotExecuted,
             BindlessFaultValidationStatus::Unavailable})
        assert(!validateBindlessFaultStatus(status).success);
    assert(validateBindlessFaultStatus(BindlessFaultValidationStatus::Clean).success);
    assert(validateBindlessFaultStatus(BindlessFaultValidationStatus::Faulted).diagnostics
        == "RHI-BINDLESS-GPU-INDEX-FAULT");
    assert((BindlessFaultReport{7u, BindlessFaultValidationStatus::Clean, 0u}.validate().success));
    assert(
        (BindlessFaultReport{7u, BindlessFaultValidationStatus::Faulted, 1u}.validate().diagnostics
            == "RHI-BINDLESS-GPU-INDEX-FAULT"));
    assert((BindlessFaultReport{7u, BindlessFaultValidationStatus::Clean, 1u}.validate().diagnostics
        == "RHI-BINDLESS-GPU-FAULT-REPORT-INCONSISTENT"));
    assert((BindlessFaultReport{0u, BindlessFaultValidationStatus::Clean, 0u}.validate().diagnostics
        == "RHI-BINDLESS-GPU-FAULT-REPORT-GENERATION-INVALID"));
}

static void verifyDepthSnapshotPreparationContract()
{
    // Native color8 reuses ContentPlan8/lifecycle8, and cannot inherit buffer7
    // or callback execution authority. These are synthetic planner facts only.
    {
        Fixture f(BindlessExecution::Native);
        f.request.originalExecution = {8u, true};
        const auto ops =
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel;
        f.request.tables.front().binding.stages = ShaderStage::Compute;
        f.request.tables.front().operations = ops;
        f.artifact.tables.front().operations = ops;
        f.artifact.activePrefixContractVersion = 2u;
        f.artifact.tables.front().metadataWordOffset = 4u;
        f.artifact.snapshotMetadata = {1u, 13u, 10u, ShaderStage::Compute};
        f.artifact.faultLatch.stages = ShaderStage::Compute;
        for (auto& b : f.artifact.physicalLayout.bindings)
            b.stages = ShaderStage::Compute;
        DescriptorBindingDesc metadata;
        metadata.binding = 13u;
        metadata.type = DescriptorType::ReadOnlyStorageBuffer;
        metadata.stages = ShaderStage::Compute;
        f.artifact.physicalLayout.bindings.push_back(metadata);
        f.caps.stageLimits[2].sampledTextures = 2u;
        f.caps.stageLimits[2].storageBuffers = 2u;
        f.caps.support[2][0] = {ops, true};
        f.caps.originalNativeBufferComputeExecutionContractVersion = 7u;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        f.caps.originalNativeColorComputeExecutionContractVersion = 8u;
        const auto plan = f.plan();
        assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 8u));
        OriginalBindlessRootFenceExecutionBudget b{{8u, true},
            {1u, true, 8u, 32u, 128u, 65536u},
            {8u, true, 1u, 0u, 0u, 128u, 256u, 0u, 65536u, 65536u, 0u},
            {4u, true, 1u, 0u, 0u},
            65536u,
            256u};
        assert(!b.validate());
        assert(!OriginalBindlessRootNativeExecutionContract::validateProfile(
            b.intent, plan, b.lifecycle));
        auto bad = b;
        bad.lifecycle.contractVersion = 6u;
        assert(bad.validate());
        bad = b;
        bad.capacity.contractVersion = 2u;
        assert(bad.validate());
        bad = b;
        bad.closure.contractVersion = 9u;
        assert(bad.validate());
        OriginalBindlessRootExecutionBudget callback;
        callback.intent = {8u, true};
        assert(arp::core::StringView(callback.validate())
            == "RHI-ORIGINAL-ROOT-EXECUTION-FENCE-BUDGET-REQUIRED");
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = semantics::OriginalBindlessExecutionLowering::lowering(backend, 8u);
            assert(
                d.executionAvailable == (backend == Backend::Metal || backend == Backend::Vulkan));
            assert(d.completionAvailable == d.executionAvailable
                && bool(d.diagnostic) != d.executionAvailable);
        }
        f.caps.originalNativeColorComputeExecutionContractVersion = 7u;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        f.caps.originalNativeColorComputeExecutionContractVersion = 8u;
        f.caps.limitsQueried = false;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        f.caps.limitsQueried = true;
        f.request.originalExecution.prototypeOptIn = false;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-OPT-IN");
        f.request.originalExecution.prototypeOptIn = true;
        f.request.tables.front().operations = BindlessOperation::SampleGrad;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        f.request.tables.front().operations = ops;
        f.artifact.physicalLayout.bindings.front().depthTexture = true;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        f.artifact.physicalLayout.bindings.front().depthTexture = false;
        f.artifact.physicalLayout.bindings.front().textureViewDimension =
            TextureViewDimension::TextureCube;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        f.artifact.physicalLayout.bindings.front().textureViewDimension =
            TextureViewDimension::Texture2DArray;
        f.request.tables.front().binding.textureViewDimension =
            TextureViewDimension::Texture2DArray;
        assert(f.plan().success);
    }
    // Native cold /6 is independent from WebGPU /4-/5 and every executor.
    // A synthetic plan can never promote this to a workload selection.
    {
        Fixture f(BindlessExecution::Native);
        f.request.originalPreparation = {6u, true};
        f.caps.originalNativeDepthRootPreparationContractVersion = 6u;
        const auto ops =
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel;
        auto& table = f.request.tables.front();
        table.binding.stages = ShaderStage::Compute;
        table.binding.depthTexture = true;
        table.operations = ops;
        f.artifact.tables.front().operations = ops;
        f.artifact.activePrefixContractVersion = 2u;
        f.artifact.tables.front().metadataWordOffset = 4u;
        f.artifact.snapshotMetadata = {1u, 13u, 10u, ShaderStage::Compute};
        f.artifact.faultLatch.stages = ShaderStage::Compute;
        for (auto& b : f.artifact.physicalLayout.bindings) {
            b.stages = ShaderStage::Compute;
            if (b.type == DescriptorType::SampledTexture)
                b.depthTexture = true;
        }
        DescriptorBindingDesc metadata;
        metadata.binding = 13u;
        metadata.type = DescriptorType::ReadOnlyStorageBuffer;
        metadata.stages = ShaderStage::Compute;
        f.artifact.physicalLayout.bindings.push_back(metadata);
        f.caps.stageLimits[2].sampledTextures = 2u;
        f.caps.stageLimits[2].storageBuffers = 2u;
        f.caps.support[2][0] = {ops, true};
        const auto plan = f.plan();
        assert(semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 6u));
        assert(!plan.executable && plan.execution == BindlessExecution::Native);
        const auto packageIdentity = f.request.shaderPackageIdentity;
        f.request.shaderPackageIdentity = 0u;
        for (const auto policy : {BindlessPolicy::Required, BindlessPolicy::Auto}) {
            const auto choice =
                semantics::selectBindlessVariant(policy, f.caps, f.request, &f.artifact, 1u);
            assert(choice.path
                == (policy == BindlessPolicy::Required ? BindlessSelectionPath::Rejected
                                                       : BindlessSelectionPath::Traditional));
            assert(arp::core::StringView(choice.diagnosticId)
                == "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE");
        }
        f.request.shaderPackageIdentity = packageIdentity;
        f.caps.originalNativeDepthRootPreparationContractVersion = 5u;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        f.caps.originalNativeDepthRootPreparationContractVersion = 6u;
        assert(f.plan().identity == plan.identity);
        f.caps.originalDepthRootPreparationContractVersion = 5u;
        assert(f.plan().identity != plan.identity);
        f.request.originalPreparation = {};
        f.reject("RHI-BINDLESS-SAMPLED-DEPTH-EXECUTION-UNAVAILABLE");
        f.request.originalExecution = {6u, true};
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        f.caps.originalNativeDepthComputeExecutionContractVersion = 6u;
        const auto nativeExecution = f.plan();
        assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(nativeExecution, 6u));
        assert(nativeExecution.identity != plan.identity);
        f.caps.originalNativeDepthComputeExecutionContractVersion = 5u;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        f.caps.originalNativeDepthComputeExecutionContractVersion = 6u;
        f.request.originalExecution.prototypeOptIn = false;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-OPT-IN");
        f.request.originalPreparation = {6u, true};
        f.request.originalExecution = {5u, true};
        f.reject("RHI-BINDLESS-ORIGINAL-PROFILE-CONFLICT");
        f.request.originalExecution = {};
        f.artifact.snapshotMetadata.stages = ShaderStage::Fragment;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
        const auto prep = semantics::OriginalBindlessPreparationLowering::lowering(backend, 6u);
        assert(prep.preparationAvailable == native && !prep.executionAvailable
            && bool(prep.diagnostic) != native);
        const auto read = OriginalCompletedDepthReadInputContract::lowering(backend, 2u);
        assert(read.nativeInputValidationAvailable == native && !read.executionAvailable
            && bool(read.diagnostic) != native);
        const auto closure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 9u);
        assert(closure.nativeCorrespondenceAvailable == native && !closure.executionAvailable
            && bool(closure.diagnostic) != native);
        const auto execution = semantics::OriginalBindlessExecutionLowering::lowering(backend, 6u);
        assert(execution.executionAvailable == native && execution.completionAvailable == native
            && bool(execution.diagnostic) != native);
        const auto life = semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 14u);
        assert(life.sealedIssuerAvailable == native && life.executionAvailable == native
            && bool(life.diagnostic) != native);
    }
    {
        OriginalBindlessRootNativeClosureBudget b{9u, true, 8u, 16u, 128u, 65536u};
        b.completedDepthInputs = {
            2u, true, 1u, 16u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        assert(!OriginalBindlessRootNativeClosureContract::validateBudget(b));
        b.completedDepthInputs.contractVersion = 1u;
        assert(arp::core::StringView(OriginalBindlessRootNativeClosureContract::validateBudget(b))
            == "RHI-BINDLESS-ROOT-DEPTH-INPUT-PROFILE");
        b.completedDepthInputs.contractVersion = 2u;
        b.contractVersion = 8u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(b));
        b.contractVersion = 1u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(b));
    }

    struct DepthPreparationFixture : Fixture {
        explicit DepthPreparationFixture(ShaderStage stage = ShaderStage::Compute)
        {
            request.originalPreparation = {4u, true};
            caps.execution = BindlessExecution::DiagnosticFailure;
            caps.executablePayload = false;
            caps.originalDepthSnapshotPreparationContractVersion = 4u;
            caps.faultReadbackContractVersion = 0u;
            const auto operations =
                BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel;
            request.tables[0].binding.stages = stage;
            request.tables[0].binding.depthTexture = true;
            request.tables[0].operations = operations;
            artifact.tables[0].operations = operations;
            artifact.activePrefixContractVersion = 2u;
            artifact.tables[0].metadataWordOffset = 4u;
            artifact.snapshotMetadata = {1u, 13u, 10u, stage};
            artifact.faultLatch.stages = stage;
            for (auto& binding : artifact.physicalLayout.bindings) {
                binding.stages = stage;
                if (binding.type == DescriptorType::SampledTexture)
                    binding.depthTexture = true;
            }
            DescriptorBindingDesc metadata;
            metadata.binding = 13u;
            metadata.type = DescriptorType::ReadOnlyStorageBuffer;
            metadata.stages = stage;
            artifact.physicalLayout.bindings.push_back(metadata);
            const auto index = stage == ShaderStage::Compute ? 2u : 1u;
            caps.stageLimits[index].sampledTextures = 4u;
            caps.stageLimits[index].storageBuffers = 2u;
            caps.support[index][0] = {operations, true};
        }
    };

    for (const auto stage : {ShaderStage::Compute, ShaderStage::Fragment}) {
        DepthPreparationFixture f(stage);
        const auto plan = f.plan();
        assert(semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 4u));
        assert(!plan.executable && !plan.originalExecutionContractVersion);
        f.caps.originalDepthSnapshotPreparationContractVersion = 3u;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        f.caps.originalDepthSnapshotPreparationContractVersion = 4u;
        f.request.originalPreparation = {5u, true};
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        f.caps.originalDepthRootPreparationContractVersion = 5u;
        for (auto& binding : f.artifact.physicalLayout.bindings)
            if (binding.type == DescriptorType::Sampler)
                binding.sampledTextureFilterable = false;
        const auto rootPlan = f.plan();
        assert(semantics::OriginalBindlessPreparationLowering::hasPlan(rootPlan, 5u));
        assert(OriginalBindlessRootNativeExecutionContract::hasPreparedPlan(rootPlan, 5u));
        assert(!OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(rootPlan, 5u));
        assert(rootPlan.identity != plan.identity && !rootPlan.executable);
        f.caps.originalDepthRootPreparationContractVersion = 4u;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        f.caps.originalDepthRootPreparationContractVersion = 5u;
        f.caps.originalGraphicsPreparationContractVersion = 2u;
        assert(f.plan().identity != plan.identity);
        f.request.shaderPackageIdentity = 0u;
        for (const auto policy : {BindlessPolicy::Required, BindlessPolicy::Auto}) {
            const auto choice =
                semantics::selectBindlessVariant(policy, f.caps, f.request, &f.artifact, 1u);
            assert(choice.path
                == (policy == BindlessPolicy::Required ? BindlessSelectionPath::Rejected
                                                       : BindlessSelectionPath::Traditional));
            assert(arp::core::StringView(choice.diagnosticId)
                == "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE");
        }
    }

    struct DepthExecutionFixture : DepthPreparationFixture {
        explicit DepthExecutionFixture(ShaderStage stage = ShaderStage::Compute)
            : DepthPreparationFixture(stage)
        {
            request.originalPreparation = {};
            request.originalExecution = {5u, true};
            caps.originalDepthComputeExecutionContractVersion = 5u;
            caps.faultReadbackContractVersion = 1u;
            for (auto& binding : artifact.physicalLayout.bindings)
                if (binding.type == DescriptorType::Sampler)
                    binding.sampledTextureFilterable = false;
        }
    };

    {
        DepthExecutionFixture f;
        const auto plan = f.plan();
        assert(plan.success && plan.executable && plan.originalExecutionContractVersion == 5u);
        assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 5u));
        assert(!OriginalBindlessRootNativeExecutionContract::validateProfile(
            {5u, true}, plan, {13u, true, 1u, 12u, 12u, 128u, 128u, 128u, 65536u, 65536u, 65536u}));
        assert(!f.caps.executablePayload);
        f.caps.originalDepthComputeExecutionContractVersion = 0u;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
    }
    for (const auto stage : {ShaderStage::Vertex,
             ShaderStage::Fragment,
             ShaderStage::Compute | ShaderStage::Fragment}) {
        DepthExecutionFixture f(stage);
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
    for (const auto dimension :
        {TextureViewDimension::Texture2DArray, TextureViewDimension::TextureCube}) {
        DepthExecutionFixture f;
        f.artifact.physicalLayout.bindings[0].textureViewDimension = dimension;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
    {
        DepthExecutionFixture f;
        f.request.tables[0].operations = BindlessOperation::SampleGrad;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
    {
        DepthExecutionFixture f;
        f.artifact.physicalLayout.bindings[0].depthTexture = false;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
    {
        DepthExecutionFixture f;
        auto& b = f.artifact.physicalLayout.bindings.back();
        b.type = DescriptorType::Sampler;
        b.sampledTextureFilterable = true;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto execution = semantics::OriginalBindlessExecutionLowering::lowering(backend, 5u);
        assert(execution.executionAvailable == (backend == Backend::WebGPU)
            && execution.completionAvailable == (backend == Backend::WebGPU));
        const auto lifecycle =
            semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 13u);
        assert(lifecycle.executionAvailable == (backend == Backend::WebGPU));
        assert(!semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 8u)
                .executionAvailable);
        assert(!semantics::OriginalBindlessRootProgramLowering::lowering(backend, 3u)
                .executionAvailable);
        const auto decision = semantics::OriginalBindlessPreparationLowering::lowering(backend, 4u);
        assert(decision.preparationAvailable == (backend == Backend::WebGPU)
            && !decision.executionAvailable);
        const auto rootDecision =
            semantics::OriginalBindlessPreparationLowering::lowering(backend, 5u);
        assert(rootDecision.preparationAvailable == (backend == Backend::WebGPU)
            && !rootDecision.executionAvailable);
        const auto closure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 7u);
        assert(closure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
            && closure.validatorOnly == (backend == Backend::Null) && !closure.executionAvailable);
    }
    {
        DepthPreparationFixture f;
        f.request.originalPreparation.prototypeOptIn = false;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-OPT-IN");
    }
    {
        DepthPreparationFixture f;
        f.caps.limitsQueried = false;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
    }
    {
        DepthPreparationFixture f;
        f.request.originalPreparation = {3u, true};
        f.caps.originalSampledGraphicsPreparationContractVersion = 3u;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.artifact.physicalLayout.bindings[0].depthTexture = false;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    for (const auto dimension :
        {TextureViewDimension::Texture2DArray, TextureViewDimension::TextureCube}) {
        DepthPreparationFixture f;
        f.artifact.physicalLayout.bindings[0].textureViewDimension = dimension;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.artifact.physicalLayout.bindings[0].arrayCount = 2u;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.artifact.snapshotMetadata.stages = ShaderStage::Vertex;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.artifact.snapshotMetadata.stages = ShaderStage::Compute | ShaderStage::Fragment;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.request.tables[0].operations = BindlessOperation::SampleGrad;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.artifact.physicalLayout.bindings[0].type = DescriptorType::StorageTexture;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.artifact.physicalLayout.bindings[0].type = DescriptorType::Sampler;
        f.artifact.physicalLayout.bindings[0].comparisonSampler = true;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.artifact.reservedPerStage[2].storageBuffers = 1u;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.caps.stageLimits[2].sampledTextures = 1u;
        f.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
    {
        DepthPreparationFixture f;
        f.request.budget.maxResidentBytes = 1u;
        f.reject("RHI-BINDLESS-SNAPSHOT-BUDGET");
    }
    {
        DepthPreparationFixture f;
        f.artifact.indirectInstanceBaseContractVersion = 1u;
        f.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        DepthPreparationFixture f;
        f.request.originalPreparation = {};
        f.caps.execution = BindlessExecution::Emulated;
        f.caps.executablePayload = true;
        f.reject("RHI-BINDLESS-SAMPLED-DEPTH-EXECUTION-UNAVAILABLE");
    }
    {
        DepthPreparationFixture f(ShaderStage::Fragment);
        f.request.originalPreparation = {};
        f.request.originalExecution = {3u, true};
        f.caps.originalSampledFragmentGraphicsExecutionContractVersion = 3u;
        f.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
}

static void verifySampledGraphicsPreparationContract()
{
    {
        using Domain = BindlessStorageImageDomain;
        using Lowering = semantics::BindlessStorageImageDeclarationLowering;
        assert(Domain::declaration(TextureFormat::R32Float).floatLanes == 1u);
        assert(!Domain::declaration(TextureFormat::Rgba16Float).floatLanes);
        assert(Domain::declaration(TextureFormat::Rgba16Float, 1u).floatLanes == 4u);
        assert(Domain::declaration(TextureFormat::Rgba16Float, 1u).spirvImageFormat == 2u);
        for (const auto format : {TextureFormat::R32Float, TextureFormat::Rgba16Float}) {
            const auto access = Domain::declaration(format, 1u).access;
            assert(Domain::accepts(
                format, TextureViewDimension::Texture2D, TextureAspect::Color, 1u, access, 1u));
            assert(!Domain::accepts(format,
                TextureViewDimension::Texture2DArray,
                TextureAspect::Color,
                1u,
                StorageTextureAccess::ReadWrite,
                1u));
            assert(!Domain::accepts(format,
                TextureViewDimension::Texture2D,
                TextureAspect::Depth,
                1u,
                StorageTextureAccess::ReadWrite,
                1u));
            assert(!Domain::accepts(format,
                TextureViewDimension::Texture2D,
                TextureAspect::Color,
                4u,
                StorageTextureAccess::ReadWrite,
                1u));
            assert(!Domain::accepts(format,
                TextureViewDimension::Texture2D,
                TextureAspect::Color,
                1u,
                access == StorageTextureAccess::ReadWrite ? StorageTextureAccess::WriteOnly
                                                          : StorageTextureAccess::ReadWrite,
                1u));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto d = Lowering::lowering(backend, format, 1u);
                assert(d.target && !d.diagnostic && !d.grantsExecution);
                assert(d.requiresStorageFormatSupport == (backend != Backend::Null));
                assert(Lowering::lowering(backend, format, 2u).diagnostic);
            }
        }
        assert(!Domain::declaration(TextureFormat::Rgba8Unorm, 1u).floatLanes);
        assert(!Domain::declaration(TextureFormat::D32Float, 1u).floatLanes);
    }
    using Completed = OriginalBindlessCompletedImageDomain;
    using Scope = OriginalBindlessCompletedImageScope;
    BindlessLoweringPlan completedPlan;
    completedPlan.success = completedPlan.executable = true;
    completedPlan.execution = BindlessExecution::Native;
    assert(Completed::acceptsPlan(completedPlan, Scope::SampledGraphics1));
    assert(!Completed::acceptsPlan(completedPlan, Scope::ComputeColor8));
    completedPlan.originalExecutionContractVersion = 8u;
    assert(Completed::acceptsPlan(completedPlan, Scope::ComputeColor8));
    assert(!Completed::acceptsPlan(completedPlan, Scope::SampledGraphics1));
    for (const auto scope : {Scope::ComputeColor8, Scope::SampledGraphics1}) {
        auto denied = completedPlan;
        denied.originalExecutionContractVersion = scope == Scope::ComputeColor8 ? 8u : 0u;
        denied.executable = false;
        assert(!Completed::acceptsPlan(denied, scope));
        denied.executable = true;
        denied.originalPreparationContractVersion = 3u;
        assert(!Completed::acceptsPlan(denied, scope));
        denied.originalPreparationContractVersion = 0u;
        denied.execution = BindlessExecution::Emulated;
        assert(!Completed::acceptsPlan(denied, scope));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d =
                semantics::OriginalBindlessCompletedImageLowering::lowering(backend, scope);
            const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
            assert(d.preparationAvailable == native && bool(d.diagnostic) == !native);
            assert(semantics::OriginalBindlessCompletedImageLowering::lowering(backend, scope, 2u)
                    .diagnostic);
        }
    }
    assert(Completed::acceptsStages(
        Scope::SampledGraphics1, DescriptorType::SampledTexture, ShaderStage::Fragment));
    for (const auto stage : {ShaderStage::None,
             ShaderStage::Vertex,
             ShaderStage::Compute,
             ShaderStage::Vertex | ShaderStage::Fragment})
        assert(!Completed::acceptsStages(
            Scope::SampledGraphics1, DescriptorType::SampledTexture, stage));
    for (const auto dimension :
        {TextureViewDimension::Texture2D, TextureViewDimension::Texture2DArray})
        assert(Completed::acceptsTexture(
            TextureFormat::Rgba8Unorm, dimension, TextureAspect::Color, 1u));
    assert(!Completed::acceptsTexture(
        TextureFormat::R32Float, TextureViewDimension::Texture2D, TextureAspect::Color, 1u));
    assert(!Completed::acceptsTexture(
        TextureFormat::D32Float, TextureViewDimension::Texture2D, TextureAspect::Depth, 1u));
    assert(!Completed::acceptsTexture(
        TextureFormat::Rgba8Unorm, TextureViewDimension::TextureCube, TextureAspect::Color, 1u));
    assert(!Completed::acceptsTexture(
        TextureFormat::Rgba8Unorm, TextureViewDimension::Texture2D, TextureAspect::Color, 4u));
    assert(Completed::sampledOperations(Scope::SampledGraphics1)
        == (BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel
            | BindlessOperation::SampleGrad));

    struct SampledPreparationFixture : Fixture {
        SampledPreparationFixture()
        {
            request.originalPreparation = {3u, true};
            caps.execution = BindlessExecution::DiagnosticFailure;
            caps.executablePayload = false;
            caps.originalSampledGraphicsPreparationContractVersion = 3u;
            caps.faultReadbackContractVersion = 0u;
            artifact.snapshotMetadata = {1u, 13u, 10u, ShaderStage::Fragment};
            artifact.activePrefixContractVersion = 2u;
            artifact.tables[0].metadataWordOffset = 4u;
            DescriptorBindingDesc metadata;
            metadata.binding = 13u;
            metadata.type = DescriptorType::ReadOnlyStorageBuffer;
            metadata.stages = ShaderStage::Fragment;
            artifact.physicalLayout.bindings.push_back(metadata);
            caps.stageLimits[1].storageBuffers = 2u;
        }
    };

    SampledPreparationFixture sampled;
    assert(semantics::OriginalBindlessPreparationLowering::hasPlan(sampled.plan(), 3u));
    assert(!sampled.plan().executable && sampled.plan().originalPreparationContractVersion == 3u);
    const auto sampledFingerprint = sampled.plan().identity;
    {
        SampledPreparationFixture composed;
        composed.artifact.pipelineInterfaceContractVersion = 1u;
        composed.caps.ordinaryDescriptorDomains[3u] = composed.caps.ordinaryDescriptorDomains[4u] =
            composed.caps.ordinaryDescriptorDomains[5u] = true;
        composed.caps.stageLimits[0].uniformBuffers = composed.caps.stageLimits[1].uniformBuffers =
            4u;
        composed.caps.stageLimits[0].storageBuffers = composed.caps.stageLimits[1].storageBuffers =
            8u;
        BindlessPipelineDescriptorDomain domain;
        domain.set = 1u;
        domain.layout.name = "SceneGlobals";
        DescriptorBindingDesc binding;
        binding.binding = 3u;
        binding.type = DescriptorType::UniformBuffer;
        binding.stages = ShaderStage::Vertex | ShaderStage::Fragment;
        domain.layout.bindings.push_back(binding);
        composed.artifact.additionalDomains.push_back(domain);
        assert(composed.plan().success && !composed.plan().executable);
        composed.request.originalPreparation = {};
        composed.request.originalExecution = {3u, true};
        composed.caps.originalSampledFragmentGraphicsExecutionContractVersion = 3u;
        composed.caps.faultReadbackContractVersion = 1u;
        assert(composed.plan().success && composed.plan().executable);
        auto& ordinary = composed.artifact.additionalDomains[0].layout.bindings[0];
        ordinary.type = DescriptorType::StorageBuffer;
        composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        ordinary.stages = ShaderStage::Fragment;
        assert(composed.plan().success);
        ordinary.stages = ShaderStage::Compute;
        composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        ordinary.stages = ShaderStage::Fragment;
        ordinary.type = DescriptorType::SampledTexture;
        composed.caps.ordinaryDescriptorDomains[0u] = true;
        composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        ordinary.type = DescriptorType::ReadOnlyStorageBuffer;
        composed.request.originalExecution = {4u, true};
        composed.caps.originalArrayFragmentGraphicsExecutionContractVersion = 4u;
        composed.request.tables[0].binding.textureViewDimension =
            TextureViewDimension::Texture2DArray;
        for (auto& physical : composed.artifact.physicalLayout.bindings)
            if (physical.type == DescriptorType::SampledTexture)
                physical.textureViewDimension = TextureViewDimension::Texture2DArray;
        assert(composed.plan().success && composed.plan().executable);
        ordinary.arrayCount = 2u;
        composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        ordinary.arrayCount = 1u;
        ordinary.dynamicOffset = true;
        composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        ordinary.dynamicOffset = false;
        // Preparation4 is a depth snapshot profile, not Array execution4.
        assert(!semantics::BindlessPipelineInterfaceContract::originalEmulatedBufferDomains(
            composed.artifact, 4u, true));
    }
    sampled.caps.originalGraphicsPreparationContractVersion = 2u;
    assert(sampled.plan().identity != sampledFingerprint);
    {
        SampledPreparationFixture bad;
        bad.caps.originalSampledGraphicsPreparationContractVersion = 0u;
        bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
    }
    {
        SampledPreparationFixture bad;
        bad.request.originalPreparation = {2u, true};
        bad.caps.originalGraphicsPreparationContractVersion = 2u;
        bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        SampledPreparationFixture bad;
        bad.request.originalPreparation = {};
        bad.request.originalExecution = {2u, true};
        bad.caps.originalFragmentGraphicsExecutionContractVersion = 2u;
        bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
    {
        SampledPreparationFixture bad;
        bad.artifact.physicalLayout.bindings[0].depthTexture = true;
        bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        SampledPreparationFixture bad;
        bad.artifact.physicalLayout.bindings[0].textureViewDimension =
            TextureViewDimension::TextureCube;
        bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        SampledPreparationFixture bad;
        bad.artifact.physicalLayout.bindings[0].sampledTextureFilterable = false;
        bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        SampledPreparationFixture bad;
        bad.artifact.physicalLayout.bindings[0].type = DescriptorType::StorageTexture;
        bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        SampledPreparationFixture bad;
        bad.caps.stageLimits[1].sampledTextures = 1u;
        bad.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
    {
        SampledPreparationFixture bad;
        bad.artifact.physicalLayout.bindings[0].stages = ShaderStage::Vertex;
        bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
    }
    {
        SampledPreparationFixture live;
        live.request.originalPreparation = {};
        live.request.originalExecution = {3u, true};
        live.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        live.caps.originalSampledFragmentGraphicsExecutionContractVersion = 3u;
        live.caps.faultReadbackContractVersion = 1u;
        const auto plan = live.plan();
        assert(plan.success && plan.executable && plan.originalExecutionContractVersion == 3u
            && !plan.originalPreparationContractVersion);
        assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 3u));
        assert(!OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(sampled.plan(), 3u));
        assert(!OriginalBindlessRootNativeExecutionContract::validateProfile(
            {3u, true}, plan, {12u, true, 1u, 12u, 12u, 128u, 128u, 128u, 65536u, 65536u, 65536u}));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto execution =
                semantics::OriginalBindlessExecutionLowering::lowering(backend, 3u);
            const auto closure =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 5u);
            assert(execution.executionAvailable == (backend == Backend::WebGPU));
            assert(execution.completionAvailable == execution.executionAvailable);
            assert(closure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
                && !closure.executionAvailable);
        }
        live.artifact.physicalLayout.bindings[0].textureViewDimension =
            TextureViewDimension::Texture2DArray;
        live.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        live.request.originalExecution = {4u, true};
        live.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        live.caps.originalArrayFragmentGraphicsExecutionContractVersion = 4u;
        live.request.tables[0].binding.textureViewDimension = TextureViewDimension::Texture2DArray;
        for (auto& binding : live.artifact.physicalLayout.bindings)
            if (binding.type == DescriptorType::SampledTexture)
                binding.textureViewDimension = TextureViewDimension::Texture2DArray;
        const auto arrayPlan = live.plan();
        assert(arrayPlan.success && arrayPlan.executable
            && arrayPlan.originalExecutionContractVersion == 4u);
        assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(arrayPlan, 4u));
        assert(!OriginalBindlessRootNativeExecutionContract::validateProfile({4u, true},
            arrayPlan,
            {12u, true, 1u, 12u, 12u, 128u, 128u, 128u, 65536u, 65536u, 65536u}));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto execution =
                semantics::OriginalBindlessExecutionLowering::lowering(backend, 4u);
            const auto closure =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 6u);
            assert(execution.executionAvailable == (backend == Backend::WebGPU));
            assert(execution.completionAvailable == execution.executionAvailable);
            assert(closure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
                && !closure.executionAvailable);
        }
        live.artifact.physicalLayout.bindings[0].textureViewDimension =
            TextureViewDimension::TextureCube;
        live.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
    }
    sampled.request.shaderPackageIdentity = 0u;
    for (const auto policy : {BindlessPolicy::Required, BindlessPolicy::Auto}) {
        const auto choice =
            selectBindlessVariant(policy, sampled.caps, sampled.request, &sampled.artifact, 1u);
        assert(choice.path
            == (policy == BindlessPolicy::Required ? BindlessSelectionPath::Rejected
                                                   : BindlessSelectionPath::Traditional));
        assert(arp::core::StringView(choice.diagnosticId)
            == "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE");
    }
}

void BindlessContractFixture::verifyReflectionAndLowering()
{
    arp::test::OriginalMixedEntryFlowFixture::verifyContract();
    {
        struct PreparationFixture : Fixture {
            PreparationFixture()
            {
                useBuffer();
                request.originalPreparation = {1u, true};
                caps.execution = BindlessExecution::DiagnosticFailure;
                caps.executablePayload = false;
                caps.faultReadbackContractVersion = 0u;
                caps.originalPreparationContractVersion = 1u;
                request.tables[0].binding.stages = ShaderStage::Compute;
                caps.stageLimits[2].storageBuffers = 4u;
                caps.support[2][4] = {BindlessOperation::Load, true};
                artifact.snapshotMetadata = {1u, 13u, 10u, ShaderStage::Compute};
                artifact.tables[0].metadataWordOffset = 4u;
                artifact.activePrefixContractVersion = 2u;
                artifact.faultLatch.stages = ShaderStage::Compute;
                for (auto& binding : artifact.physicalLayout.bindings)
                    binding.stages = ShaderStage::Compute;
                DescriptorBindingDesc metadata;
                metadata.binding = 13u;
                metadata.type = DescriptorType::ReadOnlyStorageBuffer;
                metadata.stages = ShaderStage::Compute;
                artifact.physicalLayout.bindings.push_back(metadata);
            }
        };

        PreparationFixture f;
        const auto prepared = f.plan();
        assert(prepared.success && !prepared.executable
            && prepared.execution == BindlessExecution::Emulated
            && prepared.originalPreparationContractVersion == 1u);
        auto altered = prepared;
        altered.originalPreparationContractVersion = 0u;
        assert(!bindlessPlansMatch(prepared, altered));
        f.request.originalPreparation = {};
        f.reject("RHI-BINDLESS-EXECUTION-UNAVAILABLE");
        {
            PreparationFixture bad;
            bad.request.originalPreparation.contractVersion = 13u;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-VERSION");
        }
        {
            PreparationFixture bad;
            bad.request.originalPreparation.contractVersion = 9u;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        }
        {
            PreparationFixture bad;
            bad.request.originalPreparation.contractVersion = 2u;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        }
        {
            PreparationFixture bad;
            bad.request.originalPreparation.prototypeOptIn = false;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-OPT-IN");
        }
        {
            PreparationFixture bad;
            bad.caps.originalPreparationContractVersion = 0u;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        }
        {
            PreparationFixture bad;
            bad.artifact.physicalLayout.bindings[0].stages = ShaderStage::Fragment;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
        }
        {
            PreparationFixture bad;
            bad.artifact.physicalLayout.bindings[0].type = DescriptorType::SampledTexture;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
        }
        {
            PreparationFixture bad;
            bad.caps.stageLimits[2].storageBuffers = 3u;
            bad.reject("RHI-BINDLESS-STAGE-LIMIT");
        }
        PreparationFixture selection;
        selection.request.shaderPackageIdentity = 0u;
        const auto selected = selectBindlessVariant(
            BindlessPolicy::Required, selection.caps, selection.request, &selection.artifact, 1u);
        assert(selected.path == BindlessSelectionPath::Rejected);
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto lowering = semantics::OriginalBindlessPreparationLowering::lowering(backend);
            assert(!lowering.executionAvailable);
            assert(lowering.preparationAvailable == (backend == Backend::WebGPU));
            assert(!lowering.validatorOnly);
            assert((lowering.diagnostic == nullptr) == lowering.preparationAvailable);
            const auto graphics =
                semantics::OriginalBindlessPreparationLowering::lowering(backend, 2u);
            assert(graphics.preparationAvailable == (backend == Backend::WebGPU)
                && !graphics.executionAvailable);
            const auto sampled =
                semantics::OriginalBindlessPreparationLowering::lowering(backend, 3u);
            assert(sampled.preparationAvailable == (backend == Backend::WebGPU)
                && !sampled.executionAvailable && !sampled.validatorOnly);
            assert(!semantics::OriginalBindlessPreparationLowering::lowering(backend, 13u)
                    .preparationAvailable);
            const auto live = semantics::OriginalBindlessExecutionLowering::lowering(backend);
            assert(live.executionAvailable == (backend == Backend::WebGPU)
                && live.completionAvailable == (backend == Backend::WebGPU)
                && bool(live.diagnostic) == (backend != Backend::WebGPU));
            for (const auto version : {15u, 16u}) {
                const auto produced =
                    semantics::OriginalBindlessExecutionLowering::lowering(backend, version);
                assert(!produced.executionAvailable && !produced.completionAvailable
                    && produced.diagnostic);
            }
            for (const auto version : {0u, 17u, UINT32_MAX})
                assert(arp::core::StringView(
                           semantics::OriginalBindlessExecutionLowering::lowering(backend, version)
                               .diagnostic)
                    == "RHI-BINDLESS-ORIGINAL-EXECUTION-VERSION");
        }

        struct GraphicsPreparationFixture : PreparationFixture {
            GraphicsPreparationFixture()
            {
                request.originalPreparation = {2u, true};
                caps.originalPreparationContractVersion = 0u;
                caps.originalGraphicsPreparationContractVersion = 2u;
                request.tables[0].binding.stages = ShaderStage::Fragment;
                artifact.snapshotMetadata.stages = ShaderStage::Fragment;
                artifact.faultLatch.stages = ShaderStage::Fragment;
                for (auto& binding : artifact.physicalLayout.bindings)
                    binding.stages = ShaderStage::Fragment;
                caps.stageLimits[1].storageBuffers = 4u;
                caps.support[1][4] = {BindlessOperation::Load, true};
            }
        };

        GraphicsPreparationFixture graphics;
        const auto graphicsPlan = graphics.plan();
        assert(semantics::OriginalBindlessPreparationLowering::hasPlan(graphicsPlan, 2u)
            && !semantics::OriginalBindlessPreparationLowering::hasPlan(graphicsPlan, 1u));
        assert(!bindlessPlansMatch(graphicsPlan, prepared));
        {
            GraphicsPreparationFixture bad;
            bad.caps.originalGraphicsPreparationContractVersion = 0u;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        }
        {
            GraphicsPreparationFixture bad;
            bad.request.originalPreparation = {1u, true};
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE");
        }
        {
            GraphicsPreparationFixture bad;
            bad.artifact.faultLatch.stages = ShaderStage::Vertex;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
        }
        {
            GraphicsPreparationFixture bad;
            bad.artifact.physicalLayout.bindings[0].stages = ShaderStage::Compute;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
        }
        {
            GraphicsPreparationFixture bad;
            bad.artifact.physicalLayout.bindings[0].arrayCount = 2u;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
        }
        {
            GraphicsPreparationFixture bad;
            bad.artifact.physicalLayout.bindings[0].stages = ShaderStage::Vertex;
            bad.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
        }
        {
            GraphicsPreparationFixture bad;
            bad.caps.stageLimits[1].storageBuffers = 3u;
            bad.reject("RHI-BINDLESS-STAGE-LIMIT");
        }
        graphics.request.shaderPackageIdentity = 0u;
        for (const auto policy : {BindlessPolicy::Required, BindlessPolicy::Auto}) {
            const auto choice = selectBindlessVariant(
                policy, graphics.caps, graphics.request, &graphics.artifact, 1u);
            assert(choice.path
                == (policy == BindlessPolicy::Required ? BindlessSelectionPath::Rejected
                                                       : BindlessSelectionPath::Traditional));
            assert(arp::core::StringView(choice.diagnosticId)
                == "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE");
        }
        // Explicit Graphics2 uses its own queried capability and Fragment
        // ABI. Neither Compute1 nor cold preparation2 can grant execution.
        GraphicsPreparationFixture liveGraphics;
        liveGraphics.request.originalPreparation = {};
        liveGraphics.request.originalExecution = {2u, true};
        liveGraphics.caps.originalFragmentGraphicsExecutionContractVersion = 2u;
        liveGraphics.caps.faultReadbackContractVersion = 1u;
        const auto liveGraphicsPlan = liveGraphics.plan();
        assert(liveGraphicsPlan.success && liveGraphicsPlan.executable
            && liveGraphicsPlan.originalExecutionContractVersion == 2u);
        assert(!bindlessPlansMatch(liveGraphicsPlan, graphicsPlan));
        liveGraphics.caps.originalFragmentGraphicsExecutionContractVersion = 0u;
        liveGraphics.caps.originalExecutionContractVersion = 1u;
        liveGraphics.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        liveGraphics.caps.originalFragmentGraphicsExecutionContractVersion = 2u;
        liveGraphics.artifact.snapshotMetadata.stages = ShaderStage::Compute;
        liveGraphics.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        {
            GraphicsPreparationFixture composed;
            composed.request.originalPreparation = {};
            composed.request.originalExecution = {2u, true};
            composed.caps.originalFragmentGraphicsExecutionContractVersion = 2u;
            composed.caps.faultReadbackContractVersion = 1u;
            composed.caps.ordinaryDescriptorDomains[3u] =
                composed.caps.ordinaryDescriptorDomains[4u] =
                    composed.caps.ordinaryDescriptorDomains[5u] = true;
            composed.caps.stageLimits[0].uniformBuffers =
                composed.caps.stageLimits[1].uniformBuffers = 4u;
            composed.caps.stageLimits[0].storageBuffers =
                composed.caps.stageLimits[1].storageBuffers = 8u;
            composed.artifact.pipelineInterfaceContractVersion = 1u;
            BindlessPipelineDescriptorDomain domain;
            domain.set = 1u;
            domain.layout.name = "Frame";
            DescriptorBindingDesc binding;
            binding.binding = 3u;
            binding.type = DescriptorType::UniformBuffer;
            binding.stages = ShaderStage::Vertex | ShaderStage::Fragment;
            domain.layout.bindings.push_back(binding);
            composed.artifact.additionalDomains.push_back(domain);
            assert(composed.plan().success);
            auto& selected = composed.artifact.additionalDomains[0].layout.bindings[0];
            for (const auto stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
                selected.stages = stage;
                assert(composed.plan().success);
            }
            selected.stages = ShaderStage::Compute;
            composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            selected.stages = ShaderStage::Vertex;
            selected.type = DescriptorType::ReadOnlyStorageBuffer;
            assert(composed.plan().success);
            selected.type = DescriptorType::StorageBuffer;
            composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            selected.stages = ShaderStage::Fragment;
            assert(composed.plan().success);
            selected.dynamicOffset = true;
            composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            selected.dynamicOffset = false;
            selected.arrayCount = 2u;
            composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            selected.arrayCount = 1u;
            composed.caps.ordinaryDescriptorDomains[0u] = true;
            selected.type = DescriptorType::SampledTexture;
            composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }

        {
            GraphicsPreparationFixture ordinary;
            ordinary.request.originalPreparation = {11u, true};
            ordinary.caps.originalOrdinaryColorGraphicsPreparationContractVersion = 11u;
            ordinary.artifact.pipelineInterfaceContractVersion = 1u;
            BindlessPipelineDescriptorDomain domain;
            domain.set = 1u;
            domain.layout.name = "OrdinaryColor";
            DescriptorBindingDesc image;
            image.type = DescriptorType::SampledTexture;
            image.stages = ShaderStage::Vertex | ShaderStage::Fragment;
            image.textureViewDimensionConstrained = true;
            image.textureViewDimension = TextureViewDimension::TextureCube;
            domain.layout.bindings.push_back(image);
            ordinary.artifact.additionalDomains.push_back(domain);
            ordinary.caps.stageLimits[0].sampledTextures =
                ordinary.caps.stageLimits[1].sampledTextures = 1u;
            const auto plan = ordinary.plan();
            if (!plan.success)
                std::cerr << "ordinary color preparation: " << plan.diagnosticId << '\n';
            assert(plan.success && !plan.executable
                && plan.originalPreparationContractVersion == 11u
                && !plan.originalExecutionContractVersion);
            ordinary.caps.originalOrdinaryColorGraphicsPreparationContractVersion = 0u;
            ordinary.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
            ordinary.caps.originalOrdinaryColorGraphicsPreparationContractVersion = 11u;
            ordinary.request.originalPreparation = {2u, true};
            ordinary.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
            ordinary.request.originalPreparation = {11u, true};
            ordinary.caps.stageLimits[0].sampledTextures = 0u;
            assert(!ordinary.plan().success);

            ordinary.caps.stageLimits[0].sampledTextures = 1u;
            ordinary.request.originalPreparation = {};
            ordinary.request.originalExecution = {13u, true};
            ordinary.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
            ordinary.caps.originalOrdinaryColorGraphicsExecutionContractVersion = 13u;
            ordinary.caps.faultReadbackContractVersion = 1u;
            const auto executable = ordinary.plan();
            assert(executable.success && executable.executable
                && executable.originalExecutionContractVersion == 13u
                && !executable.originalPreparationContractVersion);
            assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(executable, 13u));
            assert(!OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 13u));
            ordinary.request.originalExecution.prototypeOptIn = false;
            ordinary.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
            ordinary.request.originalExecution = {13u, true};
            ordinary.caps.faultReadbackContractVersion = 0u;
            ordinary.reject("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE");
            ordinary.caps.faultReadbackContractVersion = 1u;
            ordinary.request.originalExecution = {2u, true};
            ordinary.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
        }

        {
            GraphicsPreparationFixture ordinary;
            ordinary.request.originalPreparation = {12u, true};
            ordinary.caps.originalOrdinaryDepthGraphicsPreparationContractVersion = 12u;
            ordinary.artifact.pipelineInterfaceContractVersion = 1u;
            BindlessPipelineDescriptorDomain domain;
            domain.set = 1u;
            domain.layout.name = "OrdinaryDepth";
            DescriptorBindingDesc depth;
            depth.type = DescriptorType::SampledTexture;
            depth.stages = ShaderStage::Fragment;
            depth.depthTexture = true;
            depth.textureViewDimensionConstrained = true;
            depth.textureViewDimension = TextureViewDimension::Texture2D;
            domain.layout.bindings.push_back(depth);
            DescriptorBindingDesc comparison;
            comparison.binding = 1u;
            comparison.type = DescriptorType::Sampler;
            comparison.stages = ShaderStage::Fragment;
            comparison.comparisonSampler = true;
            domain.layout.bindings.push_back(comparison);
            ordinary.artifact.additionalDomains.push_back(domain);
            ordinary.caps.stageLimits[1].sampledTextures = 1u;
            ordinary.caps.stageLimits[1].samplers = 1u;
            const auto plan = ordinary.plan();
            assert(plan.success && !plan.executable
                && plan.originalPreparationContractVersion == 12u
                && !plan.originalExecutionContractVersion);
            ordinary.artifact.additionalDomains.clear();
            ordinary.reject("RHI-BINDLESS-ORIGINAL-PREPARATION-SCOPE");
            ordinary.artifact.additionalDomains.push_back(domain);
            ordinary.caps.originalOrdinaryDepthGraphicsPreparationContractVersion = 0u;
            ordinary.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
            ordinary.caps.originalOrdinaryDepthGraphicsPreparationContractVersion = 12u;
            ordinary.request.originalPreparation.prototypeOptIn = false;
            assert(!ordinary.plan().success);
            ordinary.request.originalPreparation = {12u, true};
            ordinary.caps.stageLimits[1].sampledTextures = 0u;
            assert(!ordinary.plan().success);
            ordinary.caps.stageLimits[1].sampledTextures = 1u;
            ordinary.caps.stageLimits[1].samplers = 0u;
            assert(!ordinary.plan().success);
            ordinary.caps.stageLimits[1].samplers = 1u;
            for (const auto old : {2u, 3u, 11u}) {
                ordinary.request.originalPreparation = {old, true};
                assert(!ordinary.plan().success);
            }
            ordinary.request.originalPreparation = {};
            ordinary.request.originalExecution = {13u, true};
            ordinary.caps.originalOrdinaryColorGraphicsExecutionContractVersion = 13u;
            ordinary.caps.faultReadbackContractVersion = 1u;
            assert(!ordinary.plan().success);
        }

        struct ExecutionFixture : PreparationFixture {
            ExecutionFixture()
            {
                request.originalPreparation = {};
                request.originalExecution = {1u, true};
                // Synthetic planner facts only. No backend currently publishes
                // this capability or issues a Root execution authorization.
                caps.originalExecutionContractVersion = 1u;
                caps.faultReadbackContractVersion = 1u;
            }
        };

        {
            ExecutionFixture native;
            native.caps.execution = native.artifact.execution = BindlessExecution::Native;
            native.artifact.physicalLayout.bindings.front().arrayCount = 2u;
            native.artifact.physicalLayout.bindings.erase(
                native.artifact.physicalLayout.bindings.begin() + 1u);
            native.request.originalExecution = {7u, true};
            native.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
            native.caps.originalNativeBufferComputeExecutionContractVersion = 7u;
            assert(
                OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(native.plan(), 7u));
            assert(OriginalBindlessRootNativeExecutionContract::lifecycleVersion(7u) == 6u);
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto lowering =
                    semantics::OriginalBindlessExecutionLowering::lowering(backend, 7u);
                assert(lowering.executionAvailable == (backend == Backend::Metal));
                assert(lowering.completionAvailable == lowering.executionAvailable);
                assert(bool(lowering.diagnostic) == !lowering.executionAvailable);
            }
            native.caps.originalNativeBufferComputeExecutionContractVersion = 6u;
            native.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
            native.caps.originalNativeBufferComputeExecutionContractVersion = 7u;
            native.request.originalExecution.prototypeOptIn = false;
            native.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-OPT-IN");
            native.request.originalExecution.prototypeOptIn = true;
            native.artifact.snapshotMetadata.stages = ShaderStage::Fragment;
            native.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        ExecutionFixture execution;
        {
            ExecutionFixture composed;
            composed.caps.ordinaryDescriptorDomains[3u] = true;
            composed.caps.stageLimits[2].uniformBuffers = 4u;
            composed.artifact.pipelineInterfaceContractVersion = 1u;
            BindlessPipelineDescriptorDomain domain;
            domain.set = 1u;
            domain.layout.name = "Frame";
            DescriptorBindingDesc binding;
            binding.binding = 3u;
            binding.type = DescriptorType::UniformBuffer;
            binding.stages = ShaderStage::Compute;
            domain.layout.bindings.push_back(binding);
            composed.artifact.additionalDomains.push_back(domain);
            assert(composed.plan().success);
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto d = semantics::BindlessPipelineInterfaceContract::lowering(backend);
                assert(d.interfaceInspectionAvailable
                    && d.executionAvailable == (backend != Backend::Null));
            }
            for (const auto stage : {ShaderStage::Vertex,
                     ShaderStage::Fragment,
                     ShaderStage::Compute | ShaderStage::Fragment}) {
                composed.artifact.additionalDomains[0].layout.bindings[0].stages = stage;
                composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            }
            composed.artifact.additionalDomains[0].layout.bindings[0].stages = ShaderStage::Compute;
            composed.artifact.additionalDomains[0].layout.bindings[0].dynamicOffset = true;
            composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            composed.artifact.additionalDomains[0].layout.bindings[0].dynamicOffset = false;
            composed.artifact.additionalDomains[0].layout.bindings[0].arrayCount = 2u;
            composed.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            composed.artifact.additionalDomains[0].layout.bindings[0].arrayCount = 1u;
            composed.caps.ordinaryDescriptorDomains[3u] = false;
            composed.reject("RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
        }
        const auto planned = execution.plan();
        assert(planned.success && planned.executable
            && planned.originalExecutionContractVersion == 1u
            && !planned.originalPreparationContractVersion
            && planned.execution == BindlessExecution::Emulated
            && arp::core::StringView(planned.telemetryLabel) == "OriginalExecution");
        assert(!bindlessPlansMatch(planned, prepared) && planned.identity != prepared.identity);
        auto alteredExecution = planned;
        alteredExecution.originalExecutionContractVersion = 0u;
        assert(!bindlessPlansMatch(planned, alteredExecution));
        // General Emulated eligibility is a distinct identity and cannot reuse
        // a Root snapshot or authorization, even with the same package/layout.
        auto general = execution;
        general.request.originalExecution = {};
        general.caps.execution = BindlessExecution::Emulated;
        general.caps.executablePayload = true;
        assert(general.plan().success && general.plan().identity != planned.identity);
        assert(!bindlessPlansMatch(general.plan(), planned));
        {
            ExecutionFixture bad;
            bad.request.originalPreparation = {1u, true};
            bad.reject("RHI-BINDLESS-ORIGINAL-PROFILE-CONFLICT");
        }
        {
            ExecutionFixture bad;
            bad.request.originalExecution.contractVersion = 14u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        }
        for (const auto version : {15u, 16u}) {
            ExecutionFixture bad;
            bad.request.originalExecution.contractVersion = version;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        }
        {
            ExecutionFixture bad;
            bad.request.originalExecution.contractVersion = 17u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-VERSION");
        }
        {
            ExecutionFixture bad;
            bad.request.originalExecution.contractVersion = 0u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-VERSION");
        }
        {
            ExecutionFixture bad;
            bad.request.originalExecution.prototypeOptIn = false;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-OPT-IN");
        }
        {
            ExecutionFixture bad;
            bad.request.optIn = false;
            bad.reject("RHI-BINDLESS-OPT-IN-REQUIRED");
        }
        for (const auto version : {0u, 2u}) {
            ExecutionFixture bad;
            bad.caps.originalExecutionContractVersion = version;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        }
        {
            ExecutionFixture bad;
            bad.caps.limitsQueried = false;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        }
        {
            ExecutionFixture bad;
            bad.caps.faultReadbackContractVersion = 0u;
            bad.reject("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE");
        }
        {
            ExecutionFixture bad;
            bad.request.allowEmulated = false;
            bad.reject("RHI-BINDLESS-EMULATED-NOT-ALLOWED");
        }
        {
            ExecutionFixture bad;
            bad.artifact.otherBindGroups = 1u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.execution = BindlessExecution::Native;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.snapshotMetadata = {};
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.faultReportingContractVersion = 0u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.faultLatch.stages = ShaderStage::Fragment;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.indirectInstanceBaseContractVersion = 1u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.reservedPerStage[0].uniformBuffers = 1u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.physicalLayout.bindings[0].stages = ShaderStage::Vertex;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.physicalLayout.bindings[0].dynamicOffset = true;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.physicalLayout.bindings[0].arrayCount = 2u;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.physicalLayout.bindings[0].tableBacked = true;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.artifact.physicalLayout.bindings[0].type = DescriptorType::SampledTexture;
            bad.reject("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
        }
        {
            ExecutionFixture bad;
            bad.caps.stageLimits[2].storageBuffers = 3u;
            bad.reject("RHI-BINDLESS-STAGE-LIMIT");
        }
        {
            ExecutionFixture bad;
            --bad.request.budget.maxSwitchCases;
            --bad.request.budget.maxSwitchCases;
            --bad.request.budget.maxSwitchCases;
            bad.reject("RHI-BINDLESS-SWITCH-BUDGET");
        }
        auto selectionRequest = execution.request;
        selectionRequest.shaderPackageIdentity = 0u;
        assert(
            selectBindlessVariant(
                BindlessPolicy::Required, execution.caps, selectionRequest, &execution.artifact, 1u)
                .path
            == BindlessSelectionPath::Indexed);
        auto unavailableCaps = execution.caps;
        unavailableCaps.originalExecutionContractVersion = 0u;
        for (const auto policy : {BindlessPolicy::Required, BindlessPolicy::Auto}) {
            const auto selectedExecution = selectBindlessVariant(
                policy, unavailableCaps, selectionRequest, &execution.artifact, 1u);
            assert(selectedExecution.path
                == (policy == BindlessPolicy::Required ? BindlessSelectionPath::Rejected
                                                       : BindlessSelectionPath::Traditional));
            assert(arp::core::StringView(selectedExecution.diagnosticId)
                == "RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        }
    }
    {
        const auto parsed = arp::shader::ShaderReflectionCache::loadFromYaml(
            "moduleName: BindlessFixture\nsourcePath: bindless.slang\n"
            "entryPoints:\n  - name: computeMain\n    stage: Compute\n"
            "reflectionContractVersion: ShaderReflection/3\n"
            "sourceSha256: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
            "bindlessContractVersion: BindlessCross/1\nbindlessExecution: Emulated\n"
            "bindlessLoweringSha256: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n");
        assert(parsed.success);
        auto reflection = parsed.module.reflection;
        assert(reflection.bindlessExecution == "Emulated");
        assert(arp::shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(reflection)
                .success);
        reflection.bindlessLoweringSha256 = "bad";
        assert(
            !arp::shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(reflection)
                .success);
    }
    {
        ShaderPackageDesc package;
        const auto ordinaryHash = stableShaderPackageHash(package);
        package.reflection.bindlessContractVersion = "BindlessCross/1";
        package.reflection.bindlessExecution = "Emulated";
        package.reflection.bindlessLoweringSha256 = "typed-variant-digest";
        const auto emulatedHash = stableShaderPackageHash(package);
        assert(emulatedHash != ordinaryHash);
        package.reflection.bindlessExecution = "Native";
        assert(stableShaderPackageHash(package) != emulatedHash);
        const auto gate = validateShaderPackage(package);
        assert(
            !gate.success && gate.diagnostics == "RHI-BINDLESS-PIPELINE-AUTHORIZATION-UNAVAILABLE");
    }
    for (auto mode :
        {BindlessExecution::Validator, BindlessExecution::Native, BindlessExecution::Emulated}) {
        Fixture f(mode);
        assert(f.plan().success);
        assert(f.plan().executable == (mode != BindlessExecution::Validator));
    }
    {
        Fixture f;
        f.request.optIn = false;
        f.reject("RHI-BINDLESS-OPT-IN-REQUIRED");
    }
    {
        Fixture f;
        f.request.allowEmulated = false;
        f.reject("RHI-BINDLESS-EMULATED-NOT-ALLOWED");
    }
    for (auto mode : {BindlessExecution::Native, BindlessExecution::Emulated}) {
        Fixture f(mode);
        f.artifact.faultReportingContractVersion = 0;
        f.reject("RHI-BINDLESS-FAULT-REPORTING-REQUIRED");
        f.artifact.faultReportingContractVersion = 2;
        f.reject("RHI-BINDLESS-FAULT-REPORTING-VERSION");
        f.artifact.faultReportingContractVersion = 1;
        f.caps.faultReadbackContractVersion = 0;
        f.reject("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE");
        f.caps.faultReadbackContractVersion = 2;
        f.reject("RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE");
        f.caps.faultReadbackContractVersion = 1;
        f.artifact.snapshotBytes = 7u;
        f.reject("RHI-BINDLESS-FAULT-READBACK-BUDGET");
        f.artifact.snapshotBytes = 128u;
        f.artifact.faultLatch = {};
        f.reject("RHI-BINDLESS-FAULT-LATCH-REQUIRED");
    }
    {
        Fixture f;
        const auto identity = f.plan().identity;
        assert(f.plan().success);
        f.artifact.faultLatch.binding = 15;
        f.reject("RHI-BINDLESS-FAULT-LATCH-ABI");
        f.artifact.faultLatch.binding = 14;
        f.artifact.faultLatch.stages = ShaderStage::Vertex;
        f.reject("RHI-BINDLESS-FAULT-LATCH-ABI");
        f.artifact.faultLatch.stages = ShaderStage::Fragment;
        f.artifact.faultLatch.contractVersion = 0;
        f.reject("RHI-BINDLESS-FAULT-LATCH-ABI");
        f.artifact.faultLatch.contractVersion = 1;
        f.artifact.faultLatch.binding = 14;
        assert(f.plan().success && f.plan().identity == identity);
    }
    {
        Fixture f;
        f.request.tables[0].binding.stages = ShaderStage::Vertex;
        f.request.tables[0].operations = BindlessOperation::Load;
        f.artifact.tables[0].operations = BindlessOperation::Load;
        for (auto& binding : f.artifact.physicalLayout.bindings)
            if (binding.binding != f.artifact.faultLatch.binding)
                binding.stages = ShaderStage::Vertex;
        f.caps.stageLimits[0].sampledTextures = 4;
        f.caps.support[0][0] = {BindlessOperation::Load, true};
        f.reject("RHI-BINDLESS-VERTEX-FAULT-PROVENANCE-REQUIRED");
        f.caps.execution = BindlessExecution::Validator;
        f.reject("RHI-BINDLESS-FAULT-LATCH-STAGES");
        f.artifact.faultLatch = {};
        f.artifact.faultReportingContractVersion = 0;
        f.artifact.physicalLayout.bindings.pop_back();
        assert(f.plan().success && !f.plan().executable);
    }
    {
        Fixture f(BindlessExecution::Native);
        f.request.tables[0].binding.stages = ShaderStage::Vertex;
        f.request.tables[0].operations = BindlessOperation::Load;
        f.artifact.tables[0].operations = BindlessOperation::Load;
        f.artifact.physicalLayout.bindings[0].stages = ShaderStage::Vertex;
        f.artifact.physicalLayout.bindings[1].stages = ShaderStage::Vertex;
        f.artifact.faultLatch.stages = ShaderStage::Vertex;
        f.caps.stageLimits[0].sampledTextures = 4;
        f.caps.stageLimits[0].storageBuffers = 1;
        f.caps.support[0][0] = {BindlessOperation::Load, true};
        assert(f.plan().success && f.plan().executable);
    }
    // A compiler depth declaration cannot inherit executable color/buffer
    // evidence. These are synthetic capability states, not native validation.
    for (const auto mode : {BindlessExecution::Validator,
             BindlessExecution::Native,
             BindlessExecution::Emulated,
             BindlessExecution::DiagnosticFailure}) {
        Fixture f(mode);
        f.request.tables[0].binding.depthTexture = true;
        f.artifact.physicalLayout.bindings[0].depthTexture = true;
        f.reject("RHI-BINDLESS-SAMPLED-DEPTH-EXECUTION-UNAVAILABLE");
    }
    {
        Fixture f;
        f.caps.executablePayload = false;
        f.reject("RHI-BINDLESS-EXECUTION-UNAVAILABLE");
    }
    {
        Fixture f;
        f.caps.limitsQueried = false;
        f.reject("RHI-BINDLESS-EXECUTION-UNAVAILABLE");
    }
    {
        Fixture f;
        f.request.deviceIdentity = 8;
        f.reject("RHI-BINDLESS-DEVICE-MISMATCH");
    }
    {
        Fixture f;
        f.artifact.shaderPackageIdentity = 12;
        f.reject("RHI-BINDLESS-SHADER-MISMATCH");
    }
    {
        Fixture f;
        f.artifact.execution = BindlessExecution::Native;
        f.reject("RHI-BINDLESS-SHADER-MISMATCH");
    }
    {
        Fixture f;
        f.artifact.contractVersion = 0;
        f.reject("RHI-BINDLESS-CONTRACT-VERSION");
    }
    {
        Fixture f;
        f.request.budget.maxResidentBytes = 255;
        f.reject("RHI-BINDLESS-SNAPSHOT-BUDGET");
    }
    {
        Fixture f;
        f.request.budget.maxSwitchCases = 1;
        f.reject("RHI-BINDLESS-SWITCH-BUDGET");
    }
    {
        Fixture f;
        f.artifact.reservedPerStage[1].sampledTextures = 3;
        f.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
    {
        Fixture f;
        f.artifact.otherBindGroups = 4;
        f.reject("RHI-BINDLESS-LAYOUT-INVALID");
    }
    {
        Fixture f;
        f.request.budget.maxUpdatesPerSnapshot = 1;
        f.reject("RHI-BINDLESS-UPDATE-BUDGET");
    }
    {
        Fixture f;
        f.request.tables[0].resourceCount = 3;
        f.reject("RHI-BINDLESS-TABLE-CAPACITY");
    }
    for (auto mode :
        {BindlessExecution::Validator, BindlessExecution::Native, BindlessExecution::Emulated}) {
        Fixture f(mode);
        f.useBuffer();
        assert(f.plan().success);
        f.caps.maxStorageBufferBindingSize = 0;
        f.reject(mode == BindlessExecution::Validator ? "RHI-BINDLESS-BUFFER-SIZE-LIMIT"
                                                      : "RHI-BINDLESS-FAULT-LATCH-ABI");
        f.caps.maxStorageBufferBindingSize = 16;
        f.artifact.tables[0].bufferLayout.identity++;
        f.reject("RHI-BINDLESS-BUFFER-LAYOUT-MISMATCH");
        f.artifact.tables[0].bufferLayout = f.request.tables[0].bufferLayout;
        const auto identity = f.plan().identity;
        f.artifact.tables[0].bufferLayout.identity++;
        f.request.tables[0].bufferLayout.identity++;
        assert(f.plan().success && f.plan().identity != identity);
        f.artifact.tables[0].bufferLayout.elementAlignmentBytes = 3;
        f.request.tables[0].bufferLayout.elementAlignmentBytes = 3;
        f.reject("RHI-BINDLESS-BUFFER-LAYOUT-INVALID");
    }
    {
        Fixture f;
        f.request.tables[0].resourceCount = 1;
        f.reject("RHI-BINDLESS-ACTIVE-PREFIX-ABI");
        f.artifact.activePrefixContractVersion = 1;
        assert(f.plan().success);
        const auto identity = f.plan().identity;
        f.request.tables[0].resourceCount = 2;
        assert(f.plan().success && f.plan().identity != identity);
    }
    {
        Fixture f;
        f.caps.support[1][0].nonUniform = false;
        f.reject("RHI-BINDLESS-OPERATION-UNSUPPORTED");
    }
    {
        Fixture f;
        f.request.tables[0].operations = BindlessOperation::Store;
        f.reject("RHI-BINDLESS-OPERATION-UNSUPPORTED");
    }
    {
        Fixture f;
        f.artifact.tables[0].operations = BindlessOperation::Load;
        f.reject("RHI-BINDLESS-SHADER-OPERATIONS-MISMATCH");
    }
    {
        Fixture f;
        f.artifact.tables[0].nonUniform = false;
        f.reject("RHI-BINDLESS-SHADER-OPERATIONS-MISMATCH");
    }
    {
        Fixture f;
        f.artifact.physicalLayout.bindings[1].type = DescriptorType::Sampler;
        f.caps.stageLimits[1].samplers = 1;
        f.reject("RHI-BINDLESS-MAPPING-MISMATCH");
    }
    {
        Fixture f;
        f.artifact.physicalLayout.bindings[1].binding = 3;
        f.reject("RHI-BINDLESS-PHYSICAL-BINDING-COLLISION");
    }
    {
        Fixture f;
        f.artifact.physicalLayout.bindings[1].arrayCount = 2;
        f.reject("RHI-BINDLESS-MAPPING-MISMATCH");
    }
    {
        Fixture f;
        f.artifact.physicalLayout.bindings[1].binding = 16;
        f.reject("RHI-BINDLESS-LAYOUT-INVALID");
    }
    // Ordinary reservations must consume the same per-stage limits.
    {
        Fixture f;
        f.caps.combinedSampledStorageTextureLimits[1] = 0u;
        f.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
    {
        Fixture f;
        f.caps.combinedSampledStorageTextureLimits[1] = 2u;
        assert(f.plan().success);
        f.artifact.reservedPerStage[1].storageTextures = 1u;
        f.caps.stageLimits[1].storageTextures = 1u;
        f.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
    {
        Fixture f;
        f.caps.combinedSampledStorageTextureLimits[1] = 2u;
        DescriptorBindingDesc image;
        image.binding = 10u;
        image.stages = ShaderStage::Fragment;
        image.type = DescriptorType::StorageTexture;
        f.artifact.physicalLayout.bindings.push_back(image);
        f.caps.stageLimits[1].storageTextures = 1u;
        f.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
    {
        Fixture f;
        auto reserved = f.artifact.physicalLayout.bindings[0];
        reserved.binding = 9;
        reserved.arrayCount = 3;
        f.artifact.physicalLayout.bindings.push_back(reserved);
        f.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
    // RO + RW storage buffers share the limit, they are not separate budgets.
    {
        Fixture f;
        DescriptorBindingDesc b;
        b.binding = 10;
        b.stages = ShaderStage::Fragment;
        b.type = DescriptorType::ReadOnlyStorageBuffer;
        f.artifact.physicalLayout.bindings.push_back(b);
        b.binding = 11;
        b.type = DescriptorType::StorageBuffer;
        f.artifact.physicalLayout.bindings.push_back(b);
        f.caps.stageLimits[1].storageBuffers = 1;
        f.reject("RHI-BINDLESS-STAGE-LIMIT");
    }
}

void BindlessContractFixture::verifyNullPacketAndFaultContracts()
{
    // A real Null descriptor domain validates resource liveness and immutable
    // publication. This is not evidence for native completion/retirement.
    auto device = createNullDevice();
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    for (const auto& combined : extension->capabilities().combinedSampledStorageTextureLimits)
        assert(!combined);
    assert(extension && !extension->capabilities().executablePayload);
    assert(extension->capabilities().indirectInstanceConsumerContractVersion == 1u);
    assert(extension->capabilities().indirectPacketStorageContractVersion == 1u);
    assert(extension->capabilities().zeroBaseIndirectProducerContractVersion == 0u);
    {
        Fixture consumer(BindlessExecution::Validator);
        consumer.request.deviceIdentity = extension->capabilities().deviceIdentity;
        consumer.request.budget = {1u, 8u, 512u, 4u};
        consumer.artifact.snapshotBytes = 512u;
        consumer.artifact.indirectInstanceBaseContractVersion = 1u;
        consumer.artifact.faultReportingContractVersion = 1u;
        consumer.artifact.faultLatch = {1u, 14u, ShaderStage::Fragment};
        consumer.artifact.physicalLayout.bindings.push_back(
            {14u, DescriptorType::StorageBuffer, ShaderStage::Fragment, 1u});
        consumer.artifact.indirectInstanceConsumer = {1u, 0u, 1u, 7u};
        consumer.artifact.physicalLayout.bindings.push_back(
            {0u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Vertex, 1u});
        arp::core::pmr::String diagnostic;
        BindlessSnapshotPool pool(*device, consumer.request.budget);
        auto snapshot = pool.prepareShared(consumer.request, consumer.artifact, diagnostic);
        assert(snapshot);
        for (const auto bytes : {16u, 28u, 36u}) {
            auto buffer = device->createBuffer({"rejected mapping extent",
                bytes,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(buffer
                && snapshot->writeOrdinaryBuffer(0u, *buffer).diagnostics
                    == "RHI-BINDLESS-INDIRECT-INSTANCE-CONSUMER-RANGE");
        }
        auto mapping = device->createBuffer({"valid mapping extent",
            32u,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(mapping && snapshot->writeOrdinaryBuffer(0u, *mapping).success);
        constexpr std::uint64_t payloadBytes = sizeof(DrawIndexedIndirectCommand)
            + sizeof(IndirectInstanceBase) + sizeof(ZeroBaseIndirectProducerParams);
        consumer.artifact.indirectInstanceConsumer.recordIndex = 0u;
        ZeroBaseIndirectProducerParams parameters{5u, 7u, 0u, 1u};
        BindlessIndirectPacketPool packets(*device, {1u, payloadBytes, payloadBytes});
        auto packet =
            packets.prepareShared(consumer.request, consumer.artifact, parameters, diagnostic);
        assert(packet && diagnostic.empty() && packet->validate().success);
        assert(packet->reportedBufferBytes() == payloadBytes && packets.livePackets() == 1u
            && packets.chargedBytes() == payloadBytes);
        assert(packet->generation()
            && packet->consumerPlan().identity
                == extension->plan(consumer.request, consumer.artifact).identity);
        assert(packet->parameters().sourceWordOffset == 5u
            && packet->parameters().instanceCapacity == 7u);
        {
            using Storage = BindlessIndirectPacketStorageContract;
            Storage::Buffers buffers{
                packet->physicalArgs(), packet->instanceMappings(), packet->producerParameters()};
            Storage::Facts facts;
            assert(!Storage::verify(buffers, parameters, payloadBytes, payloadBytes, facts)
                && facts.contractVersion == Storage::version
                && facts.reportedBufferBytes == payloadBytes);
            const auto proven = facts;
            Storage::ParameterBytes encoded;
            const auto checkParameters = [&](ZeroBaseIndirectProducerParams values) {
                assert(!Storage::encodeParameters(values, encoded));
                const arp::core::UnsignedElementSpan words(encoded.data(),
                    encoded.size(),
                    arp::core::UnsignedElementSpan::Width::Uint32,
                    arp::core::UnsignedElementSpan::ByteOrder::LittleEndian);
                const std::array<std::uint32_t, 4> expected{values.sourceWordOffset,
                    values.instanceCapacity,
                    values.reserved,
                    values.contractVersion};
                for (std::size_t i = 0u; i < expected.size(); ++i) {
                    std::uint32_t value = 0u;
                    assert(words.read(i, value) && value == expected[i]);
                }
            };
            checkParameters(parameters);
            checkParameters({UINT32_MAX, UINT32_MAX, 0u, 1u});
            auto malformed = parameters;
            malformed.reserved = 1u;
            assert(Storage::encodeParameters(malformed, encoded)
                == std::string_view("RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS"));
            for (auto byte : encoded)
                assert(!byte);
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto decision = Storage::coldLowering(backend);
                assert(!decision.consumerIssued
                    && decision.validatorOnly == (backend == Backend::Null)
                    && decision.prototypeStorage == (backend == Backend::Metal)
                    && decision.diagnosticId);
            }
            const auto rejectStorage = [&](const Storage::Buffers& changed,
                                           const ZeroBaseIndirectProducerParams& params,
                                           std::uint64_t reported,
                                           std::uint64_t ceiling,
                                           const char* expected) {
                facts = proven;
                assert(Storage::verify(changed, params, reported, ceiling, facts)
                    == std::string_view(expected));
                assert(!facts.contractVersion && !facts.reportedBufferBytes);
                for (auto resource : facts.resources)
                    assert(!resource.isValid());
                assert(packets.livePackets() == 1u && packets.chargedBytes() == payloadBytes);
            };
            for (std::size_t role = 0u; role < buffers.size(); ++role) {
                auto missing = buffers;
                missing[role] = nullptr;
                rejectStorage(missing,
                    parameters,
                    payloadBytes,
                    payloadBytes,
                    "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-IDENTITY");
                auto& desc = const_cast<BufferDesc&>(buffers[role]->desc());
                const auto saved = desc;
                for (unsigned field = 0u; field < 4u; ++field) {
                    if (field == 0u)
                        ++desc.sizeBytes;
                    if (field == 1u)
                        desc.usage = ResourceUsage::CopyDst;
                    if (field == 2u)
                        desc.memory = MemoryUsage::GpuToCpu;
                    if (field == 3u)
                        desc.initialState = ResourceState::Undefined;
                    rejectStorage(buffers,
                        parameters,
                        payloadBytes,
                        payloadBytes,
                        "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-DOMAIN");
                    assert(packet->validate().diagnostics
                        == "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-DOMAIN");
                    desc = saved;
                }
                auto& allocation = const_cast<ResourceAllocation&>(buffers[role]->allocation());
                const auto savedBytes = allocation.estimatedSizeBytes;
                allocation.estimatedSizeBytes = saved.sizeBytes - 1u;
                rejectStorage(buffers,
                    parameters,
                    payloadBytes,
                    payloadBytes,
                    "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING");
                allocation.estimatedSizeBytes = UINT64_MAX;
                rejectStorage(buffers,
                    parameters,
                    UINT64_MAX,
                    UINT64_MAX,
                    "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING");
                allocation.estimatedSizeBytes = savedBytes;
                auto unsealed = device->createBuffer(saved);
                assert(unsealed);
                auto changed = buffers;
                changed[role] = unsealed.get();
                rejectStorage(changed,
                    parameters,
                    payloadBytes,
                    payloadBytes,
                    "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-WRITES-NOT-SEALED");
                unsealed->sealHostWrites();
                rejectStorage(changed,
                    parameters,
                    payloadBytes,
                    payloadBytes,
                    "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-WRITES-NOT-SEALED");
                unsealed->sealWrites();
                assert(!Storage::verify(changed, parameters, payloadBytes, payloadBytes, facts));
                // Equal logical storage never establishes actual packet ownership.
                assert(facts.resources[role] != proven.resources[role]);
            }
            auto aliases = buffers;
            aliases[1] = aliases[0];
            rejectStorage(aliases,
                parameters,
                payloadBytes,
                payloadBytes,
                "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-IDENTITY");
            auto invalidParams = parameters;
            invalidParams.contractVersion = 0u;
            rejectStorage(buffers,
                invalidParams,
                payloadBytes,
                payloadBytes,
                "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS");
            invalidParams = parameters;
            invalidParams.reserved = 1u;
            rejectStorage(buffers,
                invalidParams,
                payloadBytes,
                payloadBytes,
                "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS");
            invalidParams = parameters;
            invalidParams.instanceCapacity = 0u;
            rejectStorage(buffers,
                invalidParams,
                payloadBytes,
                payloadBytes,
                "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS");
            rejectStorage(buffers,
                parameters,
                payloadBytes - 1u,
                payloadBytes,
                "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING");
            rejectStorage(buffers,
                parameters,
                payloadBytes,
                payloadBytes - 1u,
                "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING");
            auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
            const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
            {
                arp::core::CpuMemoryQuotaScope scope(quota);
                arp::core::CpuMemoryQuotaLease exhausted(quota, 1u);
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                for (unsigned i = 0u; i < 100u; ++i) {
                    assert(
                        !Storage::verify(buffers, parameters, payloadBytes, payloadBytes, facts));
                    assert(packet->validate().success);
                    rejectStorage(aliases,
                        parameters,
                        payloadBytes,
                        payloadBytes,
                        "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-IDENTITY");
                }
                std::pmr::set_default_resource(previous);
            }
            assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations
                && !quota->liveBytes());
            assert(packet->validate().success);
            std::cout
                << "BindlessIndirectPacketStorage/1: exact three logical roles, seals, identity/accounting failures cleared, allocation0 PASS; no native authority\n";
        }
        const std::uint32_t zero = 0u;
        for (auto* output :
            {packet->physicalArgs(), packet->instanceMappings(), packet->producerParameters()}) {
            assert(output->hostWritesSealed() && output->gpuWritesSealed()
                && output->gpuTransferWritesSealed());
            assert(device->writeBuffer(*output, &zero, sizeof(zero)).diagnostics
                == "RHI-BUFFER-HOST-WRITES-SEALED");
            BufferCopyDesc copy;
            copy.sourceBuffer = mapping->id();
            copy.destinationBuffer = output->id();
            copy.sizeBytes = sizeof(zero);
            auto encoder = device->createCommandEncoder({});
            auto transfer = encoder->beginTransferPass({});
            assert(
                transfer->copyBufferToBuffer(copy).diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
        }
        auto writableArena = device->createDescriptorArena({"packet write rejection", 1u, 1u});
        DescriptorLayoutDesc writableLayout;
        writableLayout.name = "packet write rejection";
        writableLayout.bindings.push_back(
            {0u, DescriptorType::StorageBuffer, ShaderStage::Compute, 1u});
        auto writableSet =
            writableArena->allocateDescriptorSet({"packet write rejection", writableLayout});
        assert(writableSet);
        DescriptorBindingWrite write;
        write.binding = 0u;
        write.type = DescriptorType::StorageBuffer;
        write.resourceKind = DescriptorResourceKind::Buffer;
        for (const auto* output : {packet->physicalArgs(), packet->instanceMappings()}) {
            write.resource = output->id();
            assert(writableSet->write(write).diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
        }
        auto readonlySnapshot = pool.prepareShared(consumer.request, consumer.artifact, diagnostic);
        // The original snapshot occupies this single-slot pool until released.
        assert(!readonlySnapshot && diagnostic == "RHI-BINDLESS-SNAPSHOT-POOL-FULL");
        snapshot.reset();
        readonlySnapshot = pool.prepareShared(consumer.request, consumer.artifact, diagnostic);
        assert(readonlySnapshot
            && readonlySnapshot->writeOrdinaryBuffer(0u, *packet->instanceMappings()).success);
        const auto snapshotGeneration = readonlySnapshot->generation();
        assert(
            packet->validateConsumer(*readonlySnapshot, packet->consumerPlan(), snapshotGeneration)
                .diagnostics
            == "RHI-BINDLESS-SNAPSHOT-NOT-SEALED");
        auto sampled = device->createTexture({"packet consumer sampled",
            {2u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        assert(sampled);
        TextureViewDesc sampledViewDesc;
        sampledViewDesc.label = "packet consumer sampled";
        sampledViewDesc.format = TextureFormat::Rgba8Unorm;
        sampledViewDesc.usage = TextureUsage::Sampled;
        auto sampledView = device->createTextureView(*sampled, sampledViewDesc);
        assert(sampled && sampledView);
        const auto complete = [&](BindlessSnapshot& target) {
            for (std::uint32_t index = 0u; index < 2u; ++index)
                assert(target
                        .writeTexture(
                            target.handle(1u, index), *sampledView, ResourceState::ShaderRead)
                        .success);
            assert(target.seal(*device).success);
        };
        complete(*readonlySnapshot);
        assert(
            packet->validateConsumer(*readonlySnapshot, packet->consumerPlan(), snapshotGeneration)
                .success);
        BindlessSnapshotPool wrongPool(*device, consumer.request.budget);
        auto unrelatedMapping = device->createBuffer({"same-sized unrelated mapping",
            sizeof(IndirectInstanceBase),
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(unrelatedMapping);
        auto wrongMapping =
            wrongPool.prepareShared(consumer.request, consumer.artifact, diagnostic);
        assert(wrongMapping && wrongMapping->writeOrdinaryBuffer(0u, *unrelatedMapping).success);
        complete(*wrongMapping);
        assert(bindlessPlansMatch(wrongMapping->plan(), packet->consumerPlan()));
        assert(packet
                   ->validateConsumer(
                       *wrongMapping, packet->consumerPlan(), wrongMapping->generation())
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-CONSUMER-MISMATCH");
        assert(!BindlessIndirectPacketRecordingLease::acquire(packet,
            wrongMapping,
            packet->consumerPlan(),
            wrongMapping->generation(),
            17u,
            diagnostic)
                .valid());
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-CONSUMER-MISMATCH");
        assert(!BindlessIndirectPacketRecordingLease::acquire(packet,
            readonlySnapshot,
            packet->consumerPlan(),
            snapshotGeneration + 1u,
            17u,
            diagnostic)
                .valid());
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-PLAN-MISMATCH");
        assert(!BindlessIndirectPacketRecordingLease::acquire(
            packet, readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 0u, diagnostic)
                .valid());
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-RECORDING-INVALID");
        assert(!BindlessIndirectPacketRecordingLease::acquire(
            {}, readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 17u, diagnostic)
                .valid());
        assert(!BindlessIndirectPacketRecordingLease::acquire(
            packet, {}, packet->consumerPlan(), snapshotGeneration, 17u, diagnostic)
                .valid());
        auto recording = BindlessIndirectPacketRecordingLease::acquire(
            packet, readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 17u, diagnostic);
        assert(recording.valid() && diagnostic.empty());
        assert(!BindlessIndirectPacketRecordingLease::acquire(
            packet, readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 17u, diagnostic)
                .valid());
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT");
        assert(!BindlessIndirectPacketRecordingLease::acquire(
            packet, readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 18u, diagnostic)
                .valid());
        assert(recording
                .validateForRecord(
                    *packet, *readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 17u)
                .success);
        assert(recording
                   .validateForRecord(
                       *packet, *readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 18u)
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-RECORDING-MISMATCH");
        assert(
            recording
                .validateForRecord(
                    *packet, *wrongMapping, packet->consumerPlan(), wrongMapping->generation(), 17u)
                .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-RECORDING-MISMATCH");
        wrongMapping.reset();
        auto alternateSnapshot =
            wrongPool.prepareShared(consumer.request, consumer.artifact, diagnostic);
        assert(alternateSnapshot
            && alternateSnapshot->writeOrdinaryBuffer(0u, *packet->instanceMappings()).success);
        complete(*alternateSnapshot);
        assert(packet
                ->validateConsumer(
                    *alternateSnapshot, packet->consumerPlan(), alternateSnapshot->generation())
                .success);
        assert(recording
                   .validateForRecord(*packet,
                       *alternateSnapshot,
                       packet->consumerPlan(),
                       alternateSnapshot->generation(),
                       17u)
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-RECORDING-MISMATCH");
        assert(!BindlessIndirectPacketRecordingLease::acquire(packet,
            alternateSnapshot,
            packet->consumerPlan(),
            alternateSnapshot->generation(),
            18u,
            diagnostic)
                .valid());
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT");
        auto changedPlan = packet->consumerPlan();
        ++changedPlan.budget.maxSnapshots;
        assert(
            recording
                .validateForRecord(*packet, *readonlySnapshot, changedPlan, snapshotGeneration, 17u)
                .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-PLAN-MISMATCH");
        auto source = device->createBuffer({"packet logical args",
            40u,
            ResourceUsage::StorageBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        assert(source);
        auto producerAccess = packet->producerAccesses(*source);
        const auto declarations = [&] {
            return BindlessAccessArray(producerAccess.data(), producerAccess.size());
        };
        assert(packet->validateProducerAccess(*source, declarations()).success);
        for (std::size_t index = 0u; index < producerAccess.size(); ++index) {
            const auto original = producerAccess[index];
            producerAccess[index].bufferSize--;
            assert(packet->validateProducerAccess(*source, declarations()).diagnostics
                == "RHI-BINDLESS-INDIRECT-PRODUCER-ACCESS-INCOMPLETE");
            producerAccess[index] = original;
            producerAccess[index].pipelineStages =
                index < 2u ? PipelineStage::FragmentShader : PipelineStage::ComputeShader;
            assert(!packet->validateProducerAccess(*source, declarations()).success);
            producerAccess[index] = original;
            producerAccess[index].access = ResourceAccess::ShaderWrite;
            assert(!packet->validateProducerAccess(*source, declarations()).success);
            producerAccess[index] = original;
            producerAccess[index].state = ResourceState::Undefined;
            assert(!packet->validateProducerAccess(*source, declarations()).success);
            producerAccess[index] = original;
            producerAccess[index].kind = BindlessAccessKind::Texture;
            assert(!packet->validateProducerAccess(*source, declarations()).success);
            producerAccess[index] = original;
            producerAccess[index].resource = {};
            assert(!packet->validateProducerAccess(*source, declarations()).success);
            producerAccess[index] = original;
            if (index < 2u) {
                producerAccess[index].stages = ShaderStage::Fragment;
                assert(!packet->validateProducerAccess(*source, declarations()).success);
                producerAccess[index] = original;
            }
        }
        producerAccess[0].bufferOffset = 20u;
        producerAccess[0].bufferSize = 20u;
        assert(!packet->validateProducerAccess(*source, declarations()).success);
        producerAccess = packet->producerAccesses(*source);
        assert(packet->validateProducerAccess(*source, BindlessAccessArray(nullptr, 1u)).diagnostics
            == "RHI-BINDLESS-ACCESS-SET-INVALID");
        for (const auto bytes : {36u, 41u}) {
            auto invalidSource = device->createBuffer({"packet source range",
                bytes,
                ResourceUsage::StorageBuffer,
                MemoryUsage::GpuOnly,
                ResourceState::ShaderRead});
            assert(invalidSource
                && packet->validateProducerAccess(*invalidSource, declarations()).diagnostics
                    == "RHI-BINDLESS-INDIRECT-PRODUCER-SOURCE-INVALID");
        }
        auto invalidSource = device->createBuffer({"packet source usage",
            40u,
            ResourceUsage::VertexBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        assert(invalidSource
            && packet->validateProducerAccess(*invalidSource, declarations()).diagnostics
                == "RHI-BINDLESS-INDIRECT-PRODUCER-SOURCE-INVALID");
        const auto recordingAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned repeat = 0; repeat < 64u; ++repeat) {
            assert(recording
                    .validateForRecord(
                        *packet, *readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 17u)
                    .success);
            assert(packet->validateProducerAccess(*source, declarations()).success);
        }
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == recordingAllocations);
        auto retainedRecording = std::move(recording);
        assert(retainedRecording.valid() && !recording.valid());
        retainedRecording = {};
        const auto leaseAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned repeat = 0u; repeat < 64u; ++repeat) {
            auto cancellation = BindlessIndirectPacketRecordingLease::acquire(packet,
                readonlySnapshot,
                packet->consumerPlan(),
                snapshotGeneration,
                18u,
                diagnostic);
            assert(cancellation.valid() && diagnostic.empty());
        }
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == leaseAllocations);
        recording = BindlessIndirectPacketRecordingLease::acquire(
            packet, readonlySnapshot, packet->consumerPlan(), snapshotGeneration, 18u, diagnostic);
        assert(recording.valid());
        recording = std::move(recording);
        assert(recording.valid());
        DescriptorLayoutDesc readonlyLayout;
        readonlyLayout.name = "packet readonly mapping";
        readonlyLayout.bindings.push_back(
            {0u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Vertex, 1u});
        auto readonlyArena = device->createDescriptorArena({readonlyLayout.name, 1u, 1u});
        auto readonlySet =
            readonlyArena->allocateDescriptorSet({readonlyLayout.name, readonlyLayout});
        write.resource = packet->instanceMappings()->id();
        write.type = DescriptorType::ReadOnlyStorageBuffer;
        assert(readonlySet && readonlySet->write(write).success
            && readonlySet->validateBindingElement(0u, 0u).success);
        assert(!packets.prepareShared(consumer.request, consumer.artifact, parameters, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-POOL-FULL");
        std::weak_ptr<BindlessIndirectPacket> observed = packet;
        auto retained = packet;
        std::weak_ptr<BindlessSnapshot> observedSnapshot = readonlySnapshot;
        packet.reset();
        assert(!observed.expired() && packets.livePackets() == 1u);
        retained.reset();
        readonlySnapshot.reset();
        assert(!observed.expired() && !observedSnapshot.expired() && packets.livePackets() == 1u
            && pool.liveSnapshots() == 1u);
        recording = {};
        assert(observed.expired() && packets.livePackets() == 0u && packets.chargedBytes() == 0u);
        assert(observedSnapshot.expired() && pool.liveSnapshots() == 0u);
        assert(!alternateSnapshot->validate().success);
        assert(!readonlySet->validateBindingElement(0u, 0u).success);
        {
            auto overflowingParameters = parameters;
            overflowingParameters.sourceWordOffset = UINT32_MAX;
            auto overflowing = packets.prepareShared(
                consumer.request, consumer.artifact, overflowingParameters, diagnostic);
            assert(overflowing
                && overflowing->validateProducerAccess(*source, declarations()).diagnostics
                    == "RHI-BINDLESS-INDIRECT-PRODUCER-SOURCE-INVALID");
        }
        for (auto malformed : {ZeroBaseIndirectProducerParams{0u, 6u, 0u, 1u},
                 ZeroBaseIndirectProducerParams{0u, 7u, 1u, 1u},
                 ZeroBaseIndirectProducerParams{0u, 7u, 0u, 2u}}) {
            assert(
                !packets.prepareShared(consumer.request, consumer.artifact, malformed, diagnostic));
            assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS");
        }
        consumer.artifact.indirectInstanceConsumer.recordIndex = 1u;
        assert(!packets.prepareShared(consumer.request, consumer.artifact, parameters, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS");
        consumer.artifact.indirectInstanceConsumer.recordIndex = 0u;
        BindlessIndirectPacketPool insufficient(*device, {1u, payloadBytes - 1u, payloadBytes});
        assert(!insufficient.prepareShared(
            consumer.request, consumer.artifact, parameters, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-FOOTPRINT-EXCEEDED"
            && insufficient.livePackets() == 0u && insufficient.chargedBytes() == 0u);
        BindlessIndirectPacketPool resident(*device, {2u, payloadBytes, payloadBytes});
        auto first =
            resident.prepareShared(consumer.request, consumer.artifact, parameters, diagnostic);
        assert(first
            && !resident.prepareShared(
                consumer.request, consumer.artifact, parameters, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-RESIDENT-BUDGET"
            && resident.livePackets() == 1u);
        const auto nativeBytes = device->memoryStats().allocatedBytes;
        consumer.request.optIn = false;
        assert(!packets.prepareShared(consumer.request, consumer.artifact, parameters, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-OPT-IN-REQUIRED"
            && device->memoryStats().allocatedBytes == nativeBytes);
        consumer.request.optIn = true;
        consumer.request.deviceIdentity = 0u;
        assert(!packets.prepareShared(consumer.request, consumer.artifact, parameters, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-DEVICE-MISMATCH");
        consumer.request.deviceIdentity = extension->capabilities().deviceIdentity;
        for (const auto budget : {BindlessIndirectPacketBudget{0u, payloadBytes, payloadBytes},
                 BindlessIndirectPacketBudget{1u, 0u, payloadBytes},
                 BindlessIndirectPacketBudget{1u, payloadBytes, 0u},
                 BindlessIndirectPacketBudget{1u, payloadBytes, payloadBytes - 1u}}) {
            BindlessIndirectPacketPool malformedPool(*device, budget);
            assert(!malformedPool.prepareShared(
                consumer.request, consumer.artifact, parameters, diagnostic));
            assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-BUDGET");
        }
        std::shared_ptr<BindlessIndirectPacket> survivor;
        {
            BindlessIndirectPacketPool temporary(*device, {1u, payloadBytes, payloadBytes});
            survivor = temporary.prepareShared(
                consumer.request, consumer.artifact, parameters, diagnostic);
        }
        assert(survivor && survivor->validate().success);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned repeat = 0; repeat < 64u; ++repeat)
            assert(survivor->validate().success);
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto temporaryDevice = createNullDevice();
        auto* temporaryExtension = static_cast<BindlessCrossExtension*>(
            temporaryDevice->queryInterface(kExtensionBindlessCross));
        consumer.request.deviceIdentity = temporaryExtension->capabilities().deviceIdentity;
        {
            BindlessIndirectPacketPool temporary(
                *temporaryDevice, {1u, payloadBytes, payloadBytes});
            survivor = temporary.prepareShared(
                consumer.request, consumer.artifact, parameters, diagnostic);
        }
        assert(survivor && survivor->validate().success);
        auto temporaryTexture = temporaryDevice->createTexture({"epoch consumer sampled",
            {2u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled});
        assert(temporaryTexture);
        auto temporaryView = temporaryDevice->createTextureView(*temporaryTexture, sampledViewDesc);
        assert(temporaryView);
        std::shared_ptr<BindlessSnapshot> epochSnapshot;
        {
            BindlessSnapshotPool temporary(*temporaryDevice, consumer.request.budget);
            epochSnapshot =
                temporary.prepareShared(consumer.request, consumer.artifact, diagnostic);
            assert(epochSnapshot
                && epochSnapshot->writeOrdinaryBuffer(0u, *survivor->instanceMappings()).success);
            for (std::uint32_t index = 0u; index < 2u; ++index)
                assert(epochSnapshot
                        ->writeTexture(epochSnapshot->handle(1u, index),
                            *temporaryView,
                            ResourceState::ShaderRead)
                        .success);
            assert(epochSnapshot->seal(*temporaryDevice).success);
        }
        assert(first
                   ->validateConsumer(
                       *epochSnapshot, first->consumerPlan(), epochSnapshot->generation())
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-PLAN-MISMATCH");
        auto epochRecording = BindlessIndirectPacketRecordingLease::acquire(survivor,
            epochSnapshot,
            survivor->consumerPlan(),
            epochSnapshot->generation(),
            19u,
            diagnostic);
        assert(epochRecording.valid());
        temporaryDevice.reset();
        assert(survivor->validate().diagnostics == "RHI-BINDLESS-INDIRECT-PACKET-STALE");
        assert(epochRecording
                   .validateForRecord(*survivor,
                       *epochSnapshot,
                       survivor->consumerPlan(),
                       epochSnapshot->generation(),
                       19u)
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-STALE");
        assert(!BindlessIndirectPacketRecordingLease::acquire(survivor,
            epochSnapshot,
            survivor->consumerPlan(),
            epochSnapshot->generation(),
            20u,
            diagnostic)
                .valid());
        assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-STALE");
    }

    struct UnissuedExtension final : BindlessCrossExtension {
        BindlessCapabilities caps;

        const BindlessCapabilities& capabilities() const noexcept override { return caps; }

        ValidationResult probe(const BindlessLoweringPlan& plan) const
        {
            return validateSnapshotCreation(plan);
        }

        ValidationResult packetProbe(const BindlessLoweringPlan& plan) const
        {
            return validateIndirectPacketCreation(plan);
        }
    } unissued;

    assert(unissued.probe({}).diagnostics == "RHI-BINDLESS-SNAPSHOT-EXECUTION-UNAVAILABLE");
    assert(
        unissued.packetProbe({}).diagnostics == "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-UNAVAILABLE");
    {
        Fixture guarded(BindlessExecution::Validator);
        guarded.request.deviceIdentity = extension->capabilities().deviceIdentity;
        guarded.artifact.faultLatch = {1, 14, ShaderStage::Fragment};
        guarded.artifact.physicalLayout.bindings.push_back(
            {14, DescriptorType::StorageBuffer, ShaderStage::Fragment, 1});
        auto wrongStages = guarded.artifact;
        wrongStages.faultLatch.stages = ShaderStage::Compute;
        wrongStages.physicalLayout.bindings.back().stages = ShaderStage::Compute;
        assert(extension->plan(guarded.request, wrongStages).diagnosticId
            == std::string_view("RHI-BINDLESS-FAULT-LATCH-STAGES"));
        auto shortBudget = guarded.artifact;
        shortBudget.snapshotBytes = sizeof(std::uint32_t) - 1u;
        assert(extension->plan(guarded.request, shortBudget).diagnosticId
            == std::string_view("RHI-BINDLESS-FAULT-LATCH-BUDGET"));
        BindlessSnapshotPool guardedPool(*device, guarded.request.budget);
        arp::core::pmr::String guardedDiagnostic;
        auto guardedSnapshot =
            guardedPool.prepareShared(guarded.request, guarded.artifact, guardedDiagnostic);
        assert(guardedSnapshot && !guardedSnapshot->faultLatchBuffer());
        assert(!guardedSnapshot->faultLatchAccess().resource.isValid());
        assert(guardedSnapshot->reportedInternalBufferBytes() == sizeof(std::uint32_t));
        {
            arp::render_graph::FrameGraph graph(*device);
            bool rejected = false;
            try {
                graph.importSnapshotFaultLatch(guardedSnapshot);
            } catch (const std::runtime_error& error) {
                rejected =
                    std::string_view(error.what()) == "RG-BINDLESS-FAULT-LATCH-IMPORT-INVALID";
            }
            assert(rejected);
            rejected = false;
            try {
                graph.importSnapshotFaultReadback(guardedSnapshot);
            } catch (const std::runtime_error& error) {
                rejected =
                    std::string_view(error.what()) == "RG-BINDLESS-FAULT-READBACK-IMPORT-INVALID";
            }
            assert(rejected);
        }
        auto texture = device->createTexture(
            {"guarded-sampled", {2, 2, 1}, TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
        TextureViewDesc viewDesc;
        viewDesc.format = TextureFormat::Rgba8Unorm;
        viewDesc.usage = TextureUsage::Sampled;
        auto view = device->createTextureView(*texture, viewDesc);
        assert(view);
        const OriginalCompletedTextureDescriptorRequest depthPublication{
            3u, true, 1u, 1u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        const auto refuseUnissuedDepth = [&] {
            const auto refused =
                guardedSnapshot->writeOriginalCompletedTexture(guardedSnapshot->handle(1u, 0u),
                    *device,
                    std::shared_ptr<const OriginalCompletedDepthTextureSource>{},
                    depthPublication);
            assert(!refused.success && refused.diagnostics == "RHI-COMPLETED-DEPTH-SOURCE-OWNER");
        };
        refuseUnissuedDepth();
        assert(guardedSnapshot
                ->writeTexture(guardedSnapshot->handle(1, 0), *view, ResourceState::ShaderRead)
                .success);
        assert(guardedSnapshot
                ->writeTexture(guardedSnapshot->handle(1, 1), *view, ResourceState::ShaderRead)
                .success);
        DescriptorBindingWrite forged;
        forged.binding = 14;
        forged.type = DescriptorType::StorageBuffer;
        forged.resourceKind = DescriptorResourceKind::Buffer;
        forged.resource = allocateGpuResourceId();
        assert(guardedSnapshot->writeOrdinary(forged).diagnostics
            == "RHI-BINDLESS-FAULT-LATCH-RESERVED");
        assert(guardedSnapshot->seal(*device).success && guardedSnapshot->faultLatchBuffer());
        const auto* sealedSet = guardedSnapshot->descriptorSet();
        assert(sealedSet);
        const auto sealedRevision = sealedSet->contentRevision();
        refuseUnissuedDepth();
        assert(guardedSnapshot->descriptorSet() == sealedSet
            && sealedSet->contentRevision() == sealedRevision);
        assert(guardedSnapshot->faultLatchPriorState() == ResourceState::Undefined);
        assert(guardedSnapshot->faultLatchBuffer()->desc().memory == MemoryUsage::GpuOnly);
        assert(guardedSnapshot->faultLatchBuffer()->hostWritesSealed());
        assert(guardedSnapshot->faultLatchBuffer()->gpuTransferWritesSealed());
        assert(!guardedSnapshot->faultLatchBuffer()->gpuWritesSealed());
        const std::uint32_t zeroFault = 0;
        assert(
            device->writeBuffer(*guardedSnapshot->faultLatchBuffer(), &zeroFault, sizeof(zeroFault))
                .diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
        auto faultCopySource = device->createBuffer({"fault-copy-source",
            sizeof(zeroFault),
            ResourceUsage::CopySrc,
            MemoryUsage::CpuToGpu,
            ResourceState::CopySrc});
        assert(faultCopySource);
        auto faultCopyEncoder = device->createCommandEncoder({});
        auto faultCopyPass = faultCopyEncoder->beginTransferPass({});
        BufferCopyDesc faultCopy;
        faultCopy.sourceBuffer = faultCopySource->id();
        faultCopy.destinationBuffer = guardedSnapshot->faultLatchBuffer()->id();
        faultCopy.sizeBytes = sizeof(zeroFault);
        assert(faultCopyPass->copyBufferToBuffer(faultCopy).diagnostics
            == "RHI-BUFFER-GPU-WRITES-SEALED");
        {
            arp::render_graph::FrameGraph graph(*device);
            const auto latch = graph.importSnapshotFaultLatch(guardedSnapshot);
            assert(graph.importSnapshotFaultLatch(guardedSnapshot).id == latch.id);
            assert(graph
                       .importBufferRef(std::shared_ptr<GpuBuffer>(
                           guardedSnapshot, guardedSnapshot->faultLatchBuffer()))
                       .id
                == latch.id);
            assert(graph.buffer(latch).id() == guardedSnapshot->faultLatchBuffer()->id());
            graph.addPass("GuardedFaultLatch")
                .kind(arp::render_graph::PassKind::Graphics)
                .externalSideEffects()
                .access(latch,
                    {ResourceState::ShaderWrite,
                        PipelineStage::FragmentShader,
                        ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite},
                    0,
                    sizeof(std::uint32_t));
            const auto compiled = graph.compile();
            bool declared = false;
            for (const auto& passUses : compiled.passResourceUses())
                for (const auto& use : passUses)
                    declared = declared
                        || (use.handleId == latch.id
                            && use.kind == arp::render_graph::ResourceKind::Buffer
                            && use.state == ResourceState::ShaderWrite
                            && use.stages == PipelineStage::FragmentShader
                            && use.resourceAccess
                                == (ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite)
                            && use.bufferSize == sizeof(std::uint32_t));
            assert(declared);
        }
        const auto latchAccess = guardedSnapshot->faultLatchAccess();
        assert(latchAccess.resource == guardedSnapshot->faultLatchBuffer()->id()
            && latchAccess.access == (ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite)
            && latchAccess.bufferSize == sizeof(std::uint32_t));
        BindlessResourceAccess declarations[2];
        declarations[0].resource = texture->id();
        declarations[0].kind = BindlessAccessKind::Texture;
        declarations[0].stages = ShaderStage::Fragment;
        declarations[0].pipelineStages = PipelineStage::FragmentShader;
        declarations[0].access = ResourceAccess::ShaderRead;
        declarations[0].state = ResourceState::ShaderRead;
        declarations[0].textureRange = {0, 1, 0, 1, TextureAspect::Color};
        declarations[1] = latchAccess;
        assert(guardedSnapshot
                   ->validateForRecord(
                       guardedSnapshot->plan(), guardedSnapshot->generation(), declarations, 1)
                   .diagnostics
            == "RHI-BINDLESS-ACCESS-SET-INCOMPLETE");
        assert(guardedSnapshot
                ->validateForRecord(
                    guardedSnapshot->plan(), guardedSnapshot->generation(), declarations, 2)
                .success);
        guardedDiagnostic.reserve(96);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        auto firstUse = BindlessFaultSubmissionLease::acquire(guardedSnapshot, guardedDiagnostic);
        assert(firstUse.valid() && guardedDiagnostic.empty());
        assert(firstUse.faultLatchBuffer() == guardedSnapshot->faultLatchBuffer());
        assert(!BindlessFaultSubmissionLease::acquire(guardedSnapshot, guardedDiagnostic).valid());
        assert(guardedDiagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::weak_ptr<BindlessSnapshot> observed = guardedSnapshot;
        guardedSnapshot.reset();
        assert(!observed.expired() && guardedPool.liveSnapshots() == 1u);
        auto retained = observed.lock();
        firstUse.release();
        auto nextUse = BindlessFaultSubmissionLease::acquire(retained, guardedDiagnostic);
        assert(nextUse.valid() && guardedDiagnostic.empty());
        auto movedUse = std::move(nextUse);
        assert(movedUse.valid() && !nextUse.valid()
            && movedUse.faultLatchBuffer() == retained->faultLatchBuffer()
            && !nextUse.faultLatchBuffer());
        movedUse.release();
        retained.reset();
        assert(observed.expired() && !guardedPool.liveSnapshots());
    }
}

void BindlessContractFixture::verifyNullSnapshotContracts()
{
    auto device = createNullDevice();
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    Fixture snapshotFixture(BindlessExecution::Validator);
    auto& f = snapshotFixture;
    f.request.deviceIdentity = extension->capabilities().deviceIdentity;
    f.useBuffer();
    arp::core::pmr::String diagnostic{&arp::core::defaultCpuMemoryResource()};
    BindlessSnapshotPool pool(*device, f.request.budget);
    auto snapshot = pool.prepare(f.request, f.artifact, diagnostic);
    assert(snapshot && diagnostic.empty());
    assert(pool.liveSnapshots() == 1 && pool.chargedBytes() == 128);
    auto candidate = pool.prepare(f.request, f.artifact, diagnostic);
    assert(candidate && pool.liveSnapshots() == 2);
    assert(!pool.prepare(f.request, f.artifact, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-SNAPSHOT-POOL-FULL");
    candidate.reset();
    assert(pool.liveSnapshots() == 1 && pool.chargedBytes() == 128);
    assert(!snapshot->seal(*device).success && !snapshot->descriptorSet());
    auto bufferA = device->createBuffer({"bindless-a",
        16,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    auto bufferB = device->createBuffer({"bindless-b",
        16,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(bufferA && bufferB);
    auto h0 = snapshot->handle(1, 0);
    auto h1 = snapshot->handle(1, 1);
    auto badSize = device->createBuffer({"partial-element",
        15,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    auto badUsage = device->createBuffer({"wrong-buffer-usage",
        16,
        ResourceUsage::VertexBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(badSize && badUsage);
    assert(!snapshot->writeBuffer(h0, *badSize).success);
    assert(!snapshot->writeBuffer(h0, *badUsage).success);
    assert(snapshot->bufferElementCount(h0) == 0u);
    assert(h0.generation && !snapshot->handle(1, 2).generation);
    auto foreignDevice = createNullDevice();
    auto foreignBuffer = foreignDevice->createBuffer({"foreign",
        16,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(foreignBuffer && !snapshot->writeBuffer(h0, *foreignBuffer).success);
    DescriptorBindingWrite w;
    w.type = DescriptorType::ReadOnlyStorageBuffer;
    w.resourceKind = DescriptorResourceKind::Buffer;
    w.resource = bufferA->id();
    auto stale = h0;
    ++stale.generation;
    assert(!snapshot->write(stale, w).success);
    assert(snapshot->writeBuffer(h0, *bufferA).success);
    assert(snapshot->bufferElementCount(h0) == 4u);
    assert(!snapshot->seal(*device).success);
    w.resource = bufferB->id();
    assert(snapshot->writeBuffer(h1, *bufferB).success);
    assert(snapshot->seal(*foreignDevice).diagnostics == "RHI-BINDLESS-SEAL-DEVICE-MISMATCH");
    assert(snapshot->seal(*device).success && snapshot->descriptorSet());
    BindlessResourceAccess access[2];
    for (auto& declaration : access) {
        declaration.stages = ShaderStage::Fragment;
        declaration.pipelineStages = PipelineStage::FragmentShader;
        declaration.state = ResourceState::ShaderRead;
        declaration.access = ResourceAccess::ShaderRead;
        declaration.bufferSize = 16;
    }
    access[0].resource = bufferA->id();
    access[1].resource = bufferB->id();
    assert(
        snapshot->validateForRecord(snapshot->plan(), snapshot->generation(), access, 2).success);
    assert(
        !snapshot->validateForRecord(snapshot->plan(), snapshot->generation(), access, 1).success);
    assert(!snapshot->validateForRecord(snapshot->plan(), snapshot->generation() + 1, access, 2)
            .success);
    auto wrongPlan = snapshot->plan();
    ++wrongPlan.shaderPackageIdentity;
    assert(!snapshot->validateForRecord(wrongPlan, snapshot->generation(), access, 2).success);
    using PlanMutation = void (*)(BindlessLoweringPlan&);
    for (const PlanMutation mutate : {PlanMutation{[](BindlessLoweringPlan& p) {
                                          p.success = false;
                                      }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.identity;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.contractVersion;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 p.execution = BindlessExecution::Native;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 p.bindingModel = BindlessBindingModel::ScalarExpansion;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 p.executable = !p.executable;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.deviceIdentity;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.shaderPackageIdentity;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.snapshotBytes;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.descriptorCount;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.budget.maxSnapshots;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.budget.maxUpdatesPerSnapshot;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.budget.maxResidentBytes;
             }},
             PlanMutation{[](BindlessLoweringPlan& p) {
                 ++p.budget.maxSwitchCases;
             }}}) {
        auto changed = snapshot->plan();
        mutate(changed);
        assert(!bindlessPlansMatch(snapshot->plan(), changed));
        assert(snapshot->validateForRecord(changed, snapshot->generation(), access, 2).diagnostics
            == "RHI-BINDLESS-RECORD-PLAN-MISMATCH");
    }
    assert(bindlessPlansMatch(snapshot->plan(), snapshot->plan()));
    assert(!bindlessPlansMatch({}, {}));
    auto changedIntent = f.request;
    changedIntent.budget.maxUpdatesPerSnapshot++;
    const auto changedPlan = extension->plan(changedIntent, f.artifact);
    assert(changedPlan.success && changedPlan.identity != snapshot->plan().identity);
    assert(!snapshot->validateForRecord(changedPlan, snapshot->generation(), access, 2).success);
    access[1].bufferSize = 15;
    assert(
        !snapshot->validateForRecord(snapshot->plan(), snapshot->generation(), access, 2).success);
    access[1].bufferSize = 16;
    assert(!snapshot->write(h1, w).success);
    assert(!snapshot->writeOrdinary(w).success);
    bufferB.reset();
    assert(!snapshot->validate().success && !snapshot->descriptorSet());
    // A caller cannot fabricate Native support and turn the validator factory
    // into an unproven execution path.
    f.caps.execution = BindlessExecution::Native;
    f.caps.executablePayload = true;
    f.artifact.execution = BindlessExecution::Native;
    {
        auto validated = pool.prepare(f.request, f.artifact, diagnostic);
        assert(validated && validated->plan().execution == BindlessExecution::Validator
            && !validated->plan().executable);
    }
    f.artifact.execution = BindlessExecution::Validator;
    auto anotherDevice = createNullDevice();
    BindlessSnapshotPool anotherPool(*anotherDevice, f.request.budget);
    assert(!anotherPool.prepare(f.request, f.artifact, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-DEVICE-MISMATCH");
    assert(!anotherPool.liveSnapshots() && !anotherPool.chargedBytes());
    candidate = pool.prepare(f.request, f.artifact, diagnostic);
    assert(candidate);
    const OriginalCompletedTextureDescriptorRequest depthRequest{
        3u, true, 1u, 1u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
    const auto depthRefusal = candidate->writeOriginalCompletedTexture(candidate->handle(1, 0),
        *device,
        std::shared_ptr<const OriginalCompletedDepthTextureSource>{},
        depthRequest);
    assert(!depthRefusal.success
        && depthRefusal.diagnostics == "RHI-COMPLETED-TEXTURE-DESCRIPTOR-TABLE");
    const auto staleDepth =
        candidate->writeOriginalCompletedTexture({candidate->generation(), 999u, 0u},
            *device,
            std::shared_ptr<const OriginalCompletedDepthTextureSource>{},
            depthRequest);
    assert(!staleDepth.success && staleDepth.diagnostics == "RHI-BINDLESS-HANDLE-STALE");
    w.resource = bufferA->id();
    for (unsigned i = 0; i < f.request.budget.maxUpdatesPerSnapshot; ++i)
        assert(candidate->write(candidate->handle(1, 0), w).success);
    assert(!candidate->write(candidate->handle(1, 1), w).success);

    {
        auto partial = f.request;
        partial.tables[0].resourceCount = 1;
        auto artifact = f.artifact;
        artifact.activePrefixContractVersion = 1;
        BindlessSnapshotPool partialPool(*device, partial.budget);
        auto padded = partialPool.prepare(partial, artifact, diagnostic);
        assert(padded && padded->activeCount(1) == 1 && padded->activeCount(99) == 0);
        assert(!padded->handle(1, 1).generation);
        assert(!padded->initializeInactiveSlots(padded->handle(1, 0)).success);
        assert(padded->writeBuffer(padded->handle(1, 0), *bufferA).success);
        assert(!padded->seal(*device).success);
        assert(padded->initializeInactiveSlots(padded->handle(1, 0)).success);
        assert(padded->seal(*device).success);
        assert(!padded->initializeInactiveSlots(padded->handle(1, 0)).success);
        assert(padded->validateForRecord(padded->plan(), padded->generation(), access, 1).success);
        auto budgeted = partialPool.prepare(partial, artifact, diagnostic);
        for (unsigned i = 0; i < partial.budget.maxUpdatesPerSnapshot; ++i)
            assert(budgeted->writeBuffer(budgeted->handle(1, 0), *bufferA).success);
        assert(!budgeted->initializeInactiveSlots(budgeted->handle(1, 0)).success);
        assert(!budgeted->seal(*device).success);
    }

    {
        Fixture writable(BindlessExecution::Validator);
        writable.request.deviceIdentity = extension->capabilities().deviceIdentity;
        writable.useBuffer(DescriptorType::StorageBuffer);
        BindlessSnapshotPool writerPool(*device, writable.request.budget);
        auto writer = writerPool.prepare(writable.request, writable.artifact, diagnostic);
        assert(writer);
        assert(writer->writeBuffer(writer->handle(1, 0), *bufferA).success);
        assert(writer->writeBuffer(writer->handle(1, 1), *bufferA).success);
        assert(writer->seal(*device).success);
        access[0].state = ResourceState::ShaderWrite;
        assert(!writer->validateForRecord(writer->plan(), writer->generation(), access, 1).success);
        access[0].access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        assert(writer->validateForRecord(writer->plan(), writer->generation(), access, 1).success);
    }

    {
        Fixture uniform(BindlessExecution::Validator);
        uniform.request.deviceIdentity = extension->capabilities().deviceIdentity;
        uniform.useBuffer(DescriptorType::UniformBuffer);
        BindlessSnapshotPool uniformPool(*device, uniform.request.budget);
        auto uniforms = uniformPool.prepare(uniform.request, uniform.artifact, diagnostic);
        assert(uniforms);
        auto oversized = device->createBuffer({"uniform-over-device-limit",
            65552u,
            ResourceUsage::UniformBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        auto undersized = device->createBuffer({"uniform-under-shader-minimum",
            4u,
            ResourceUsage::UniformBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        auto valid = device->createBuffer({"uniform-valid-range",
            16u,
            ResourceUsage::UniformBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        assert(oversized && undersized && valid);
        const auto first = uniforms->handle(1, 0);
        assert(!uniforms->writeBuffer(first, *oversized).success);
        assert(!uniforms->writeBuffer(first, *undersized).success);
        assert(uniforms->writeBuffer(first, *valid).success);
        assert(uniforms->bufferElementCount(first) == 1u);
        assert(uniforms->writeBuffer(uniforms->handle(1, 1), *valid).success);
        assert(uniforms->seal(*device).success);
    }
    static_assert(arp::core::containsUnsignedRange<std::uint64_t>(4, 8, 6, 6));
    static_assert(!arp::core::containsUnsignedRange<std::uint64_t>(4, 8, 6, 7));
    static_assert(!arp::core::containsUnsignedRange<std::uint64_t>(4, UINT64_MAX, 6, 1));
    static_assert(!arp::core::containsUnsignedRange<std::uint64_t>(0, 10, 1, 0));

    {
        auto indirect = device->createBuffer({"bindless-indirect-args",
            64u,
            ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined});
        auto nonIndirect = device->createBuffer({"bindless-non-indirect",
            64u,
            ResourceUsage::StorageBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined});
        assert(indirect && nonIndirect);
        BindlessResourceAccess declaration;
        declaration.resource = indirect->id();
        declaration.kind = BindlessAccessKind::Buffer;
        declaration.pipelineStages = PipelineStage::DrawIndirect;
        declaration.access = ResourceAccess::IndirectCommandRead;
        declaration.state = ResourceState::ShaderRead;
        declaration.bufferSize = 40u;
        const auto declared = [&] {
            return BindlessAccessArray(&declaration, 1u);
        };
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 20u, declared()).success);
        BindlessIndirectDeclarations retained(1u);
        assert(retained.capture(declared()).success);
        const auto allocationCount = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0; i < 32u; ++i)
            assert(retained.capture(declared()).success);
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocationCount);
        declaration.resource = nonIndirect->id();
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 20u, retained).success);
        declaration.resource = indirect->id();
        BindlessIndirectDeclarations empty(0u);
        assert(empty.capture(declared()).diagnostics == "RHI-BINDLESS-INDIRECT-ACCESS-BUDGET");
        assert(empty.size() == 0u);
        BindlessResourceAccess two[2] = {declaration, declaration};
        assert(retained.capture(BindlessAccessArray(two, 2u)).diagnostics
            == "RHI-BINDLESS-INDIRECT-ACCESS-BUDGET");
        assert(retained.size() == 0u);
        assert(retained.capture(BindlessAccessArray(nullptr, 1u)).diagnostics
            == "RHI-BINDLESS-ACCESS-SET-INVALID");
        assert(retained.capture(declared()).success);
        BindlessRetainedDeclarations graphicsIntent(1u, BindlessAccessRetention::GraphicsPass);
        assert(graphicsIntent.capture(declared()).success && graphicsIntent.matches(declared()));
        assert(
            graphicsIntent.capture(graphicsIntent).success && graphicsIntent.matches(declared()));
        const auto original = declaration;
        for (unsigned field = 0u; field < 11u; ++field) {
            declaration = original;
            switch (field) {
            case 0u:
                declaration.resource = nonIndirect->id();
                break;
            case 1u:
                declaration.kind = BindlessAccessKind::Texture;
                break;
            case 2u:
                declaration.stages = ShaderStage::Fragment;
                break;
            case 3u:
                declaration.pipelineStages = PipelineStage::FragmentShader;
                break;
            case 4u:
                declaration.access = ResourceAccess::ShaderRead;
                break;
            case 5u:
                declaration.state = ResourceState::CopySrc;
                break;
            case 6u:
                ++declaration.bufferOffset;
                break;
            case 7u:
                ++declaration.bufferSize;
                break;
            case 8u:
                ++declaration.textureRange.baseMipLevel;
                break;
            case 9u:
                ++declaration.textureRange.arrayLayerCount;
                break;
            case 10u:
                declaration.textureRange.aspect = TextureAspect::Depth;
                break;
            }
            assert(!graphicsIntent.matches(declared()));
        }
        declaration = original;
        const auto graphicsAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 32u; ++i)
            assert(
                graphicsIntent.capture(declared()).success && graphicsIntent.matches(declared()));
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == graphicsAllocations);
        assert(graphicsIntent.capture(BindlessAccessArray(two, 2u)).diagnostics
            == "RHI-BINDLESS-GRAPHICS-PASS-ACCESS-BUDGET");
        assert(graphicsIntent.size() == 0u);

        class UnreadableAccessSet final : public BindlessAccessDeclarations {
        public:
            explicit UnreadableAccessSet(BindlessResourceAccess value) : value_(value) {}

            bool valid() const noexcept override { return true; }

            std::size_t size() const noexcept override { return 2u; }

            bool read(std::size_t index, BindlessResourceAccess& out) const override
            {
                if (index != 0u)
                    return false;
                out = value_;
                return true;
            }

        private:
            BindlessResourceAccess value_;
        } unreadable(declaration);

        assert(graphicsIntent.capture(unreadable).diagnostics == "RHI-BINDLESS-ACCESS-SET-INVALID");
        assert(graphicsIntent.size() == 0u);
        assert(validateBindlessIndirectDrawAccess(*nonIndirect, 0u, 2u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 0u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 16u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 21u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
        assert(validateBindlessIndirectDrawAccess(*indirect, 2u, 1u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
        assert(validateBindlessIndirectDrawAccess(*indirect, 48u, 2u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
        assert(validateBindlessIndirectDrawAccess(*indirect, UINT64_MAX, 1u, 20u, declared())
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
        assert(validateBindlessIndirectDrawAccess(
                   *indirect, 0u, 1u, 20u, BindlessAccessArray(nullptr, 1u))
                   .diagnostics
            == "RHI-BINDLESS-ACCESS-SET-INVALID");
        declaration.pipelineStages = PipelineStage::FragmentShader;
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE");
        declaration.pipelineStages = PipelineStage::DrawIndirect;
        declaration.access = ResourceAccess::ShaderRead;
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE");
        declaration.access = ResourceAccess::IndirectCommandRead;
        declaration.bufferSize = 39u;
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE");
        declaration.bufferSize = 40u;
        declaration.resource = nonIndirect->id();
        assert(validateBindlessIndirectDrawAccess(*indirect, 0u, 2u, 20u, declared()).diagnostics
            == "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE");
    }

    {
        Fixture textures(BindlessExecution::Validator);
        textures.request.deviceIdentity = extension->capabilities().deviceIdentity;
        auto& requirement = textures.request.tables[0];
        requirement.binding.type = DescriptorType::StorageTexture;
        requirement.binding.storageTextureFormat = TextureFormat::R32Float;
        requirement.binding.storageTextureAccess = StorageTextureAccess::ReadWrite;
        requirement.operations = BindlessOperation::Load | BindlessOperation::Store;
        textures.artifact.tables[0].operations = requirement.operations;
        auto& layout = textures.artifact.physicalLayout.bindings[0];
        layout.type = requirement.binding.type;
        layout.storageTextureFormat = TextureFormat::R32Float;
        layout.storageTextureAccess = StorageTextureAccess::ReadWrite;
        auto wrongAccess = textures.request;
        wrongAccess.tables[0].binding.storageTextureAccess = StorageTextureAccess::WriteOnly;
        assert(extension->plan(wrongAccess, textures.artifact).diagnosticId
            == std::string_view("RHI-BINDLESS-STORAGE-ACCESS-MISMATCH"));
        auto invalidLayout = textures.artifact.physicalLayout;
        invalidLayout.bindings[0].storageTextureAccess = static_cast<StorageTextureAccess>(255);
        assert(!validateDescriptorLayout(invalidLayout).success);
        BindlessSnapshotPool texturePool(*device, textures.request.budget);
        auto textureSnapshot = texturePool.prepare(textures.request, textures.artifact, diagnostic);
        assert(textureSnapshot);
        TextureDesc desc;
        desc.label = "bindless-mips";
        desc.extent = {4, 4, 1};
        desc.format = TextureFormat::R32Float;
        desc.usage = TextureUsage::Storage;
        desc.mipLevelCount = 2;
        auto texture = device->createTexture(desc);
        assert(texture);
        TextureViewDesc viewDesc;
        viewDesc.label = "bindless-mip1";
        viewDesc.format = desc.format;
        viewDesc.usage = TextureUsage::Storage;
        viewDesc.baseMipLevel = 1;
        auto view = device->createTextureView(*texture, viewDesc);
        assert(view);
        auto handle = textureSnapshot->handle(1, 0);
        assert(!textureSnapshot->writeTexture(handle, *view, ResourceState::ShaderRead).success);
        assert(textureSnapshot->writeTexture(handle, *view, ResourceState::ShaderWrite).success);
        assert(textureSnapshot
                ->writeTexture(textureSnapshot->handle(1, 1), *view, ResourceState::ShaderWrite)
                .success);
        assert(textureSnapshot->seal(*device).success);
        BindlessResourceAccess declaration;
        declaration.resource = view->id();
        declaration.kind = BindlessAccessKind::Texture;
        declaration.state = ResourceState::ShaderWrite;
        declaration.stages = ShaderStage::Fragment;
        declaration.pipelineStages = PipelineStage::FragmentShader;
        declaration.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        assert(!textureSnapshot
                ->validateForRecord(
                    textureSnapshot->plan(), textureSnapshot->generation(), &declaration, 1)
                .success);
        declaration.resource = texture->id();
        assert(!textureSnapshot
                ->validateForRecord(
                    textureSnapshot->plan(), textureSnapshot->generation(), &declaration, 1)
                .success);
        declaration.textureRange.baseMipLevel = 1;
        assert(textureSnapshot
                ->validateForRecord(
                    textureSnapshot->plan(), textureSnapshot->generation(), &declaration, 1)
                .success);
        auto wrongFormatSnapshot =
            texturePool.prepare(textures.request, textures.artifact, diagnostic);
        assert(wrongFormatSnapshot);
        desc.format = TextureFormat::Rgba8Unorm;
        auto wrongTexture = device->createTexture(desc);
        assert(wrongTexture);
        viewDesc.format = desc.format;
        auto wrongView = device->createTextureView(*wrongTexture, viewDesc);
        assert(wrongView);
        const auto rejected = wrongFormatSnapshot->writeTexture(
            wrongFormatSnapshot->handle(1, 0), *wrongView, ResourceState::ShaderWrite);
        assert(!rejected.success && rejected.diagnostics == "RHI-BINDLESS-STORAGE-FORMAT-MISMATCH");
        texture.reset();
        assert(!textureSnapshot->validate().success);
    }

    {
        Fixture selection;
        selection.request.shaderPackageIdentity = 0;
        BindlessShaderArtifact candidates[] = {selection.artifact, selection.artifact};
        candidates[0].shaderPackageIdentity = 17;
        candidates[1].shaderPackageIdentity = 13;
        auto choose = [&](BindlessPolicy policy) {
            return selectBindlessVariant(policy, selection.caps, selection.request, candidates, 2);
        };
        auto selected = choose(BindlessPolicy::Required);
        assert(selected.path == BindlessSelectionPath::Indexed && selected.artifactIndex == 1);
        assert(selected.plan.shaderPackageIdentity == 13);
        candidates[0].shaderPackageIdentity = 5;
        candidates[0].tables[0].capacity = 3;
        candidates[0].activePrefixContractVersion = 1;
        candidates[0].switchCases = 3;
        auto extra = candidates[0].physicalLayout.bindings[0];
        extra.binding = 5;
        candidates[0].physicalLayout.bindings.push_back(extra);
        selected = choose(BindlessPolicy::Required);
        assert(selected.path == BindlessSelectionPath::Indexed && selected.artifactIndex == 1);
        selection.caps.limitsQueried = false;
        assert(choose(BindlessPolicy::Auto).path == BindlessSelectionPath::Traditional);
        assert(choose(BindlessPolicy::Required).path == BindlessSelectionPath::Rejected);
        assert(choose(BindlessPolicy::Disabled).path == BindlessSelectionPath::Traditional);
        selection.caps.limitsQueried = true;
        selection.request.shaderPackageIdentity = 42;
        assert(choose(BindlessPolicy::Auto).path == BindlessSelectionPath::Rejected);
    }
    {
        // A backend without indexed discovery (currently WebGL1) cannot
        // borrow execution authority from an offline Native/Emulated package.
        // Traditional is only a selection result; its workload still needs
        // independent graphics/compute/resource admission before execution.
        DiagnosticBindlessCrossExtension unavailable;
        for (const auto mode : {BindlessExecution::Native, BindlessExecution::Emulated}) {
            Fixture selection(mode);
            selection.request.shaderPackageIdentity = 0u;
            selection.request.deviceIdentity = unavailable.capabilities().deviceIdentity;
            for (const auto policy :
                {BindlessPolicy::Required, BindlessPolicy::Auto, BindlessPolicy::Disabled}) {
                const auto selected = selectBindlessVariant(
                    policy, unavailable.capabilities(), selection.request, &selection.artifact, 1u);
                assert(selected.path
                    == (policy == BindlessPolicy::Required ? BindlessSelectionPath::Rejected
                                                           : BindlessSelectionPath::Traditional));
                assert(!selected.plan.success && !selected.plan.executable);
                assert(arp::core::StringView(selected.diagnosticId)
                    == (policy == BindlessPolicy::Disabled ? "RHI-BINDLESS-DISABLED"
                                                           : "RHI-BINDLESS-EXECUTION-UNAVAILABLE"));
            }
        }
    }
}

static void verifyInitializedGraphicsProfileContract()
{
    using Contract = semantics::BindlessInitializedGraphicsContract;
    const Contract::Profile profile{2u, 77u, true};
    assert(Contract::select(profile, profile).recognized);
    assert(!Contract::select({}, {}).recognized && !Contract::select({}, {}).diagnostic);
    assert(Contract::select(profile, {}).diagnostic);
    assert(Contract::select(profile, {2u, 78u, true}).diagnostic);
    assert(Contract::select(profile, {1u, 77u, true}).diagnostic);
    assert(Contract::select(profile, {2u, 77u, false}).diagnostic);
    for (auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const bool native = backend == Backend::Vulkan || backend == Backend::Metal;
        const bool emulated = backend == Backend::WebGPU;
        const auto* id = Contract::lowering(backend, 2u, native ? 3u : 5u, true, true, emulated);
        assert(bool(id) == (!native && !emulated));
        assert(Contract::lowering(backend, 1u, 3u, true, true, emulated));
        assert(Contract::lowering(backend, 2u, 3u, false, true, emulated));
    }
}

static void verifyBindlessPureDecisionAllocations()
{
    Fixture fixture;
    fixture.request.shaderPackageIdentity = 0u;
    auto resolved = fixture.request;
    resolved.shaderPackageIdentity = fixture.artifact.shaderPackageIdentity;
    const auto expected = semantics::planBindless(fixture.caps, resolved, fixture.artifact);
    assert(expected.success);
    for (auto policy : {BindlessPolicy::Auto, BindlessPolicy::Required, BindlessPolicy::Disabled}) {
        const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
        const auto empty =
            semantics::selectBindlessVariant(policy, fixture.caps, fixture.request, nullptr, 0u);
        const auto selected = semantics::selectBindlessVariant(
            policy, fixture.caps, fixture.request, &fixture.artifact, 1u);
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
        assert(empty.path
            == (policy == BindlessPolicy::Required ? BindlessSelectionPath::Rejected
                                                   : BindlessSelectionPath::Traditional));
        if (policy != BindlessPolicy::Disabled)
            assert(bindlessPlansMatch(expected, selected.plan));
    }
    fixture.request.budget.maxResidentBytes = 0u;
    const auto failureBefore = arp::core::defaultCpuAllocator().stats().allocationCount;
    const auto unavailable = semantics::selectBindlessVariant(
        BindlessPolicy::Auto, fixture.caps, fixture.request, &fixture.artifact, 1u);
    assert(unavailable.path == BindlessSelectionPath::Traditional
        && arp::core::defaultCpuAllocator().stats().allocationCount == failureBefore);
    PipelineValidationReport report;
    report.success = true;
    report.colorTargetCount = 1u;
    report.colorFormats.push_back(TextureFormat::Rgba8Unorm);
    RenderPassDesc pass;
    RenderPassColorAttachmentDesc attachment;
    attachment.target = allocateGpuResourceId();
    attachment.format = TextureFormat::Rgba8Unorm;
    pass.colorAttachments.push_back(attachment);
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 128u; ++i)
        assert(validateBindlessRenderPass(report, pass).success);
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
    report.sampleCount = 2u;
    assert(!validateBindlessRenderPass(report, pass).success);
    report.sampleCount = 1u;
    report.colorFormats[0] = TextureFormat::Rgba16Float;
    assert(!validateBindlessRenderPass(report, pass).success);
    report.success = false;
    assert(!validateBindlessRenderPass(report, pass).success);
}

int main()
{
    verifyInitializedGraphicsProfileContract();
    verifyBindlessPureDecisionAllocations();
    arp::test::OriginalStorageContentPlanFixture::verifyContract();
    arp::test::OriginalStorageContentSequenceFixture::verify();
    arp::test::OriginalStorageExecutionContractFixture::verify();
    {
        auto nullDevice = arp::rhi::createNullDevice();
        arp::test::OriginalStorageTextureCreationFixture::verifyContract(*nullDevice);
    }
    using namespace arp::rhi;
    using DepthSource = OriginalCompletedDepthTextureSource;
    using DepthRead = OriginalCompletedDepthReadInputContract;
    using DepthPlan = OriginalCompletedDepthRootReadPlanContract;
    OriginalTextureContentPlanBudget depthPlan{11u, true, 1u, 0u, 2u, 4u, 60u, 4096u};
    assert(!DepthPlan::validateIntent(depthPlan));
    auto nativeDepthPlan = depthPlan;
    nativeDepthPlan.contractVersion = 12u;
    assert(!DepthPlan::validateIntent(nativeDepthPlan));
    static_assert(DepthPlan::controlVersion(11u) == 2u && DepthPlan::controlVersion(12u) == 4u);
    static_assert(DepthPlan::rootVersion(11u) == 5u && DepthPlan::rootVersion(12u) == 6u);
    static_assert(!DepthPlan::isVersion(13u) && !DepthPlan::controlVersion(13u)
        && !DepthPlan::rootVersion(13u));
    static_assert(!DepthPlan::validateReadOnlyControl(11u, 2u, true, 0u));
    static_assert(!DepthPlan::validateReadOnlyControl(12u, 4u, true, 0u));
    static_assert(DepthPlan::validateReadOnlyControl(12u, 4u, true, 1u));
    static_assert(DepthPlan::validateReadOnlyControl(12u, 2u, true, 0u));
    static_assert(DepthPlan::validateReadOnlyControl(11u, 4u, true, 0u));
    static_assert(DepthPlan::validateReadOnlyControl(12u, 4u, false, 0u));
    static_assert(DepthPlan::validateReadOnlyControl(13u, 4u, true, 0u));
    assert(DepthPlan::validatePendingProfile(1u, 13u, 12u, true, true));
    assert(!DepthPlan::validatePendingProfile(1u, 13u, 11u, true, true));
    assert(DepthPlan::validatePendingProfile(2u, 13u, 11u, true, true));
    assert(DepthPlan::validatePendingProfile(1u, 12u, 11u, true, true));
    assert(DepthPlan::validatePendingProfile(1u, 13u, 10u, true, true));
    assert(DepthPlan::validatePendingProfile(1u, 13u, 11u, false, true));
    assert(DepthPlan::validatePendingProfile(1u, 13u, 11u, true, false));
    for (const auto initial :
        {ResourceState::DepthAttachment, ResourceState::CopySrc, ResourceState::ShaderRead}) {
        assert(!DepthPlan::validatePendingStates(
            initial, initial, ResourceState::ShaderRead, ResourceState::ShaderRead, 7u, 8u, 1u));
        assert(DepthPlan::validatePendingStates(
            initial, initial, ResourceState::ShaderRead, ResourceState::ShaderRead, 7u, 9u, 1u));
        assert(DepthPlan::validatePendingStates(initial,
            initial,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            UINT64_MAX,
            1u,
            1u));
        assert(DepthPlan::validatePendingStates(
            initial, initial, ResourceState::ShaderRead, ResourceState::CopySrc, 7u, 8u, 1u));
    }
    assert(!DepthPlan::validatePendingStates(ResourceState::ShaderRead,
        ResourceState::ShaderRead,
        ResourceState::ShaderRead,
        ResourceState::ShaderRead,
        7u,
        7u,
        0u));
    assert(DepthPlan::validatePendingStates(ResourceState::DepthAttachment,
        ResourceState::DepthAttachment,
        ResourceState::ShaderRead,
        ResourceState::ShaderRead,
        7u,
        7u,
        0u));
    OriginalCommandNativeLifecycleBudget depthLifecycle;
    depthLifecycle.contractVersion = 13u;
    assert(!OriginalCommandNativeLifecycleContract::validateContentProfile(depthLifecycle, 11u));
    assert(OriginalCommandNativeLifecycleContract::validateContentProfile(depthLifecycle, 0u));
    assert(OriginalCommandNativeLifecycleContract::validateContentProfile(depthLifecycle, 9u));
    assert(
        !OriginalGeneralSubmissionCompletionContract::validateProfile(7u, false, false, 11u, true));
    assert(
        OriginalGeneralSubmissionCompletionContract::validateProfile(7u, false, false, 11u, false));
    assert(
        OriginalGeneralSubmissionCompletionContract::validateProfile(7u, true, false, 11u, true));
    assert(
        OriginalGeneralSubmissionCompletionContract::validateProfile(7u, false, false, 9u, true));
    auto invalidDepthPlan = depthPlan;
    invalidDepthPlan.contractVersion = 2u;
    assert(DepthPlan::validateIntent(invalidDepthPlan));
    invalidDepthPlan = depthPlan;
    invalidDepthPlan.prototypeOptIn = false;
    assert(DepthPlan::validateIntent(invalidDepthPlan));
    invalidDepthPlan = depthPlan;
    invalidDepthPlan.maxContentCells = 0u;
    assert(DepthPlan::validateIntent(invalidDepthPlan));
    invalidDepthPlan = depthPlan;
    invalidDepthPlan.maxUploadCommands = 1u;
    assert(DepthPlan::validateIntent(invalidDepthPlan));
    invalidDepthPlan = depthPlan;
    invalidDepthPlan.maxReadbackCommands = 1u;
    assert(DepthPlan::validateIntent(invalidDepthPlan));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = DepthPlan::lowering(backend);
        assert(
            decision.contractVersion == 11u && !decision.executionAvailable && decision.diagnostic);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.validationAvailable
            == (backend == Backend::Null || backend == Backend::WebGPU));
        const auto nativeDecision = DepthPlan::lowering(backend, 12u);
        assert(nativeDecision.contractVersion == 12u && !nativeDecision.executionAvailable
            && nativeDecision.diagnostic);
        assert(nativeDecision.validatorOnly == (backend == Backend::Null));
        assert(nativeDecision.validationAvailable
            == (backend == Backend::Null || backend == Backend::Vulkan
                || backend == Backend::Metal));
    }
    for (std::uint32_t version = 1u; version <= 12u; ++version) {
        OriginalCommandNativeLifecycleBudget lifecycle;
        lifecycle.contractVersion = version;
        assert(OriginalCommandNativeLifecycleContract::validateContentProfile(lifecycle, 11u));
        assert(OriginalCommandNativeLifecycleContract::validateContentProfile(lifecycle, 12u));
    }
    assert(OriginalCommandNativeLifecycleContract::validateContentProfile(depthLifecycle, 12u));
    SamplerDesc depthSampler;
    assert(validateNonFilteringSamplerBinding(false, depthSampler));
    assert(!validateNonFilteringSamplerBinding(true, depthSampler));
    depthSampler.minFilter = depthSampler.magFilter = SamplerFilter::Nearest;
    assert(validateNonFilteringSamplerBinding(false, depthSampler));
    depthSampler.mipFilter = SamplerMipFilter::Nearest;
    assert(!validateNonFilteringSamplerBinding(false, depthSampler));
    depthSampler.maxAnisotropy = 2.0f;
    assert(validateNonFilteringSamplerBinding(false, depthSampler));
    const DepthRead::Budget depthReadBudget{
        1u, true, 2u, 20u, DepthSource::completeRetainedOwnerCount};
    OriginalBindlessRootNativeClosureBudget depthClosure{7u, true, 1u, 1u, 1u, 4096u};
    assert(OriginalBindlessRootNativeClosureContract::validateBudget(depthClosure));
    depthClosure.completedDepthInputs = depthReadBudget;
    assert(!OriginalBindlessRootNativeClosureContract::validateBudget(depthClosure));
    auto invalidDepthClosure = depthClosure;
    invalidDepthClosure.contractVersion = 1u;
    assert(arp::core::StringView(
               OriginalBindlessRootNativeClosureContract::validateBudget(invalidDepthClosure))
        == "RHI-BINDLESS-ROOT-DEPTH-INPUT-SCHEMA");
    invalidDepthClosure = depthClosure;
    invalidDepthClosure.maxAttachmentViews = 1u;
    assert(OriginalBindlessRootNativeClosureContract::validateBudget(invalidDepthClosure));
    invalidDepthClosure = depthClosure;
    invalidDepthClosure.completedDepthInputs.prototypeOptIn = false;
    assert(OriginalBindlessRootNativeClosureContract::validateBudget(invalidDepthClosure));
    assert(!DepthRead::validateIntent(depthReadBudget));
    for (const auto stage : {PipelineStage::ComputeShader, PipelineStage::FragmentShader})
        assert(!DepthRead::validateAccess(
            ResourceState::ShaderRead, stage, ResourceAccess::ShaderRead));
    for (const auto stage : {PipelineStage::None,
             PipelineStage::VertexShader,
             PipelineStage::Transfer,
             PipelineStage::ComputeShader | PipelineStage::FragmentShader})
        assert(DepthRead::validateAccess(
            ResourceState::ShaderRead, stage, ResourceAccess::ShaderRead));
    assert(DepthRead::validateAccess(
        ResourceState::CopySrc, PipelineStage::ComputeShader, ResourceAccess::ShaderRead));
    assert(DepthRead::validateAccess(ResourceState::ShaderRead,
        PipelineStage::ComputeShader,
        ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite));
    auto invalidDepthRead = depthReadBudget;
    invalidDepthRead.prototypeOptIn = false;
    assert(DepthRead::validateIntent(invalidDepthRead));
    invalidDepthRead = depthReadBudget;
    invalidDepthRead.contractVersion = 4u;
    assert(DepthRead::validateIntent(invalidDepthRead));
    invalidDepthRead = depthReadBudget;
    --invalidDepthRead.maxRetainedOwners;
    assert(DepthRead::validateIntent(invalidDepthRead));
    const auto missingDepth = DepthRead::inspect({}, {}, depthReadBudget);
    assert(missingDepth.diagnostic && !missingDepth.cells && !missingDepth.textureTexelBytes
        && !missingDepth.nativeExecutionProven());
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = DepthRead::lowering(backend);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.nativeInputValidationAvailable == (backend == Backend::WebGPU));
        assert(!decision.executionAvailable);
        assert(bool(decision.diagnostic) == (backend != Backend::WebGPU));
        assert(!DepthRead::lowering(backend, 4u).nativeInputValidationAvailable);
    }
    using Publication = OriginalCompletedTextureDescriptorContract;
    for (const auto control : {1u, 2u, 3u, 4u, 5u}) {
        assert(bool(DepthSource::validateControl(1u, control)) == (control != 2u));
        assert(bool(DepthSource::validateControl(2u, control)) == (control != 4u));
        assert(DepthSource::validateControl(3u, control));
        assert(Publication::validateControl(control, 4u));
    }
    const OriginalCompletedTextureDescriptorRequest nativeDepthPublication{
        4u, true, 1u, 1u, DepthSource::completeRetainedOwnerCount};
    assert(!Publication::validateIntent(nativeDepthPublication));
    const OriginalCompletedTextureDescriptorRequest depthPublication{
        3u, true, 1u, 1u, DepthSource::completeRetainedOwnerCount};
    assert(!Publication::validateIntent(depthPublication));
    auto insufficient = depthPublication;
    --insufficient.maxRetainedSourceOwners;
    assert(Publication::validateIntent(insufficient));
    insufficient = depthPublication;
    insufficient.prototypeOptIn = false;
    assert(Publication::validateIntent(insufficient));
    assert(Publication::validateSource(std::shared_ptr<const DepthSource>{}, 3u));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = Publication::lowering(backend, 3u);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.publicationAvailable == (backend == Backend::WebGPU));
        assert(!decision.shaderExecutionAvailable);
        assert(bool(decision.diagnostic) == (backend != Backend::WebGPU));
        const auto nativeDecision = Publication::lowering(backend, 4u);
        assert(nativeDecision.publicationAvailable
            == (backend == Backend::Vulkan || backend == Backend::Metal));
        assert(nativeDecision.validatorOnly == (backend == Backend::Null));
        assert(!nativeDecision.shaderExecutionAvailable);
        assert(bool(nativeDecision.diagnostic)
            == (backend != Backend::Vulkan && backend != Backend::Metal));
        const auto hdr = Publication::lowering(backend, 5u);
        assert(
            hdr.publicationAvailable == (backend == Backend::Vulkan || backend == Backend::Metal));
        assert(hdr.validatorOnly == (backend == Backend::Null) && !hdr.shaderExecutionAvailable);
        assert(Publication::lowering(backend, 6u).publicationAvailable
            == (backend == Backend::WebGPU));
        assert(!Publication::lowering(backend, 7u).publicationAvailable);
    }
    {
        auto device = createNullDevice();
        DescriptorLayoutDesc layout;
        layout.name = "depth publication refusal";
        layout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
        layout.bindings.front().depthTexture = true;
        layout.bindings.front().textureViewDimensionConstrained = true;
        layout.bindings.front().textureViewDimension = TextureViewDimension::Texture2D;
        auto arena = device->createDescriptorArena({"depth publication refusal", 1u, 1u});
        assert(arena);
        auto set = arena->allocateDescriptorSet({"depth publication refusal", layout});
        assert(set);
        const auto revision = set->contentRevision();
        DescriptorBindingWrite write;
        write.type = DescriptorType::SampledTexture;
        write.resourceKind = DescriptorResourceKind::TextureView;
        write.resourceState = ResourceState::ShaderRead;
        const auto refused = set->writeOriginalCompletedTexture(
            *device, write, std::shared_ptr<const DepthSource>{}, depthPublication);
        assert(
            !refused.success && set->contentRevision() == revision && !set->hasWrittenBinding(0u));
    }
    // A whole-texture observation can never stand in for typed per-cell depth proof.
    for (const auto control : {1u, 2u, 3u, 4u, 5u})
        assert(Publication::validateControl(control, 3u));
    using Program = OriginalBindlessRootProgramContract;
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto program = semantics::OriginalBindlessRootProgramLowering::lowering(backend, 2u);
        assert(program.preparationAvailable == (backend == Backend::WebGPU));
        assert(!program.executionAvailable && !program.completionAvailable);
        const auto live = semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 12u);
        assert(live.executionAvailable == (backend == Backend::WebGPU));
        const auto closure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 4u);
        assert(closure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU));
        assert(!closure.executionAvailable);
    }
    assert(!Program::validateIntent({2u, true, 32u, 256u}));
    assert(!Program::validateIntent({3u, true, 32u, 256u}));
    assert(!Program::validateIntent({4u, true, 32u, 256u}));
    assert(!Program::validateIntent({5u, true, 32u, 256u}));
    assert(!Program::validateIntent({6u, true, 32u, 256u}));
    assert(Program::validateIntent({7u, true, 32u, 256u}));
    assert(!Program::validateOperation(CommandUsageKind::TextureBarrier, 3u));
    assert(!Program::validateOperation(CommandUsageKind::Dispatch, 3u));
    assert(Program::validateOperation(CommandUsageKind::Draw, 3u));
    assert(!Program::validateOperation(CommandUsageKind::Draw, 2u));
    assert(Program::validateOperation(CommandUsageKind::Draw, 1u));
    assert(Program::validateOperation(CommandUsageKind::Dispatch, 2u));
    assert(Program::validateOperation(CommandUsageKind::DrawIndexed, 2u));
    assert(!OriginalCommandNativeLifecycleContract::validateContentProfile({12u}, 9u));
    for (const auto content : {0u, 3u, 6u, 8u})
        assert(OriginalCommandNativeLifecycleContract::validateContentProfile({12u}, content));
    assert(OriginalCommandNativeLifecycleContract::validateContentProfile({11u}, 9u));
    OriginalBindlessRootNativeClosureBudget attachmentBudget{4u, true, 32u, 32u, 64u, 65536u};
    assert(OriginalBindlessRootNativeClosureContract::validateBudget(attachmentBudget));
    attachmentBudget.maxAttachmentViews = 1u;
    assert(!OriginalBindlessRootNativeClosureContract::validateBudget(attachmentBudget));
    attachmentBudget.contractVersion = 1u;
    assert(OriginalBindlessRootNativeClosureContract::validateBudget(attachmentBudget));
    using Creation = OriginalTextureContentCreationContract;
    assert(Creation::isCellCreation(5u) && !Creation::hasNativeAllocationBudget(5u));
    assert(Creation::cellControlMatchesPlan(5u, 9u) && !Creation::cellControlMatchesPlan(5u, 2u)
        && !Creation::cellControlMatchesPlan(5u, 3u) && !Creation::cellControlMatchesPlan(2u, 9u));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto cell = Creation::lowering(backend, 5u);
        assert(cell.exclusiveCreationAvailable == (backend == Backend::WebGPU)
            && !cell.nativeContentExecutionAvailable);
    }
    auto logicalAttachment =
        OriginalTextureContentCreationBudget{5u, true, 1u, 1u, 16u, 8u, 65536u, 1u, 0u};
    assert(!Creation::validateIntent(logicalAttachment));
    logicalAttachment.maxNativeAllocationBytes = 16u;
    assert(Creation::validateIntent(logicalAttachment));
    OriginalResourceRegistrationBudget rootRegistry{9u, true, 9u, 1u, 1u, 64u, 65536u};
    rootRegistry.maxDescriptorArenas = rootRegistry.maxDescriptorSets = 1u;
    using DescriptorLoss = OriginalDescriptorNativeLossRegistrationContract;
    assert(!DescriptorLoss::validate({3u, true}, rootRegistry));
    auto invalidRegistry = rootRegistry;
    invalidRegistry.samplers = {1u, true, 1u};
    assert(!DescriptorLoss::validate({3u, true}, invalidRegistry));
    assert(DescriptorLoss::validate({1u, true}, invalidRegistry));
    invalidRegistry.samplers.prototypeOptIn = false;
    assert(DescriptorLoss::validate({3u, true}, invalidRegistry));
    invalidRegistry = rootRegistry;
    invalidRegistry.maxPipelineLayouts = invalidRegistry.maxGraphicsPipelines = 1u;
    assert(DescriptorLoss::validate({3u, true}, invalidRegistry));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto join = DescriptorLoss::lowering(backend, 3u);
        assert(
            join.registrationAvailable == (backend == Backend::WebGPU) && !join.executionAvailable);
    }
    using Range = arp::rhi::OriginalBufferRangeDescriptorContract;
    for (const auto backend : {arp::rhi::Backend::Null,
             arp::rhi::Backend::Vulkan,
             arp::rhi::Backend::Metal,
             arp::rhi::Backend::WebGPU}) {
        const auto d = Range::lowering(backend);
        assert(!d.shaderExecutionAvailable);
        assert(d.validatorOnly == (backend == arp::rhi::Backend::Null));
        assert(d.publicationAvailable
            == (backend == arp::rhi::Backend::Null || backend == arp::rhi::Backend::Vulkan
                || backend == arp::rhi::Backend::WebGPU));
        assert(bool(d.diagnostic) == !d.publicationAvailable);
    }
    assert(Range::validateIntent({0u, true, 1u}));
    assert(Range::validateIntent({1u, false, 1u}));
    assert(Range::validateIntent({1u, true, 0u}));
    assert(Range::validateIntent({1u, true, 2u}));
    assert(!Range::validateIntent({1u, true, 1u}));
    arp::test::CompletionAdmissionTransactionFixture::verify();
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto projection =
            semantics::OriginalBindlessEntryAccessContract::lowering(backend, 3u);
        assert(projection.projectionAvailable == (backend != Backend::WebGL)
            && projection.validatorOnly == (backend == Backend::Null)
            && !projection.executionAvailable);
        const auto sceneProjection =
            semantics::OriginalBindlessEntryAccessContract::lowering(backend, 4u);
        assert(sceneProjection.projectionAvailable == (backend != Backend::WebGL)
            && sceneProjection.validatorOnly == (backend == Backend::Null)
            && !sceneProjection.executionAvailable);
        assert(!semantics::OriginalBindlessEntryAccessContract::lowering(backend, 5u)
                .projectionAvailable);
    }
    {
        using Contract = OriginalHostGeometryContentContract;
        auto device = createNullDevice();
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        assert(extension);
        const char* id = nullptr;
        const BufferDesc desc{"public-initialized-null",
            64u,
            ResourceUsage::UniformBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead};
        assert(!extension->queryOriginalInitializedBufferAllocation(*device, desc, 2u, id)
            && std::string_view(id) == "RHI-HOST-GEOMETRY-CONTENT-NULL-VALIDATOR");
        assert(!extension->queryOriginalInitializedBufferAllocation(*device, desc, 3u, id)
            && std::string_view(id) == "RHI-HOST-GEOMETRY-CONTENT-VERSION");
        const std::array<std::uint32_t, 16u> bytes{};
        OriginalInitializedBufferRequest request{desc, bytes.data(), sizeof(bytes)};
        request.budget = {2u,
            true,
            1u,
            Contract::retainedOwnerBound,
            64u,
            65536u,
            64u,
            65536u,
            65536u,
            Contract::Staging::ShaderContentIntent{1u, true, DescriptorType::UniformBuffer, 64u}};
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        assert(!extension->prepareOriginalInitializedBuffer(*device, request, id)
            && std::string_view(id) == "RHI-HOST-GEOMETRY-CONTENT-NULL-VALIDATOR");
        request.bytes = nullptr;
        assert(!extension->prepareOriginalInitializedBuffer(*device, request, id)
            && std::string_view(id) == "RHI-HOST-GEOMETRY-CONTENT-PAYLOAD");
        request.bytes = bytes.data();
        request.budget.prototypeOptIn = false;
        assert(!extension->prepareOriginalInitializedBuffer(*device, request, id)
            && std::string_view(id) == "RHI-HOST-GEOMETRY-CONTENT-OPT-IN");
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    arp::test::TransferDependencyLoweringFixture::verify();
    BindlessContractFixture::verifyOwnershipAndProducerContracts();
    verifyDepthSnapshotPreparationContract();
    verifySampledGraphicsPreparationContract();
    arp::test::OriginalBindlessGraphicsContentContractFixture::verify();
    arp::test::CanonicalGraphicsColorContractFixture::verify();
    BindlessContractFixture::verifyReflectionAndLowering();
    verifyCompletePipelineInterface();
    BindlessContractFixture::verifyNullPacketAndFaultContracts();
    BindlessContractFixture::verifyNullSnapshotContracts();
}
