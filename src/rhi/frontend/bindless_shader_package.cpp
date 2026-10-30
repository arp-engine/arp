#include "rhi/frontend/bindless_shader_package.h"

#include "bindless_zero_base_identity.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/hash/stable_hash.h"
#include "core/io/artifact_path.h"
#include "core/io/file_io.h"
#include "core/text/text_decoder.h"
#include "rhi/frontend/bindless_storage_image_domain.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/pipeline_bindless_interface_merger.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_graphics_entry_selection_lowering.h"
#include "shader/buffer_vector_record.h"
#include "shader/shader_artifact_files.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/spirv_buffer_declaration_layout.h"
#include "shader/spirv_image_declaration.h"
#include "shader/spirv_wgsl_buffer_memory_lowering.h"
#include "shader/wgsl_buffer_declaration_layout.h"

#include <algorithm>
#include <limits>

namespace arp::rhi {
namespace {
using Y = core::YamlContract;
using Node = Y::Node;

struct Rejection {
    const char* diagnostic;
};

void require(bool valid, const char* diagnostic)
{
    if (!valid)
        throw Rejection{diagnostic};
}

core::StringView textBytes(const core::ByteBuffer& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

core::pmr::String decodedText(core::StringView bytes)
{
    core::TextDecodeDesc desc;
    desc.bom = core::TextBomPolicy::Reject;
    desc.nul = core::TextNulPolicy::Reject;
    desc.maxOutputBytes = bytes.size();
    auto result = core::TextDecoder::decode(bytes, desc);
    require(result.status == core::TextDecodeStatus::Success,
        result.diagnosticId ? result.diagnosticId : "RHI-BINDLESS-PACKAGE-TEXT");
    return std::move(result.text);
}

core::pmr::String decodedText(const core::ByteBuffer& bytes)
{
    return decodedText(textBytes(bytes));
}

DescriptorType descriptorType(const core::pmr::String& name)
{
    if (name == "SampledTexture")
        return DescriptorType::SampledTexture;
    if (name == "Sampler")
        return DescriptorType::Sampler;
    if (name == "StorageTexture")
        return DescriptorType::StorageTexture;
    if (name == "UniformBuffer")
        return DescriptorType::UniformBuffer;
    if (name == "ReadOnlyStorageBuffer")
        return DescriptorType::ReadOnlyStorageBuffer;
    if (name == "StorageBuffer")
        return DescriptorType::StorageBuffer;
    throw Rejection{"RHI-BINDLESS-PACKAGE-RESOURCE-TYPE"};
}

DescriptorType descriptorType(shader::BindingType type)
{
    switch (type) {
    case shader::BindingType::SampledTexture:
        return DescriptorType::SampledTexture;
    case shader::BindingType::Sampler:
        return DescriptorType::Sampler;
    case shader::BindingType::StorageTexture:
        return DescriptorType::StorageTexture;
    case shader::BindingType::UniformBuffer:
        return DescriptorType::UniformBuffer;
    case shader::BindingType::ReadOnlyStorageBuffer:
        return DescriptorType::ReadOnlyStorageBuffer;
    case shader::BindingType::StorageBuffer:
        return DescriptorType::StorageBuffer;
    default:
        throw Rejection{"RHI-BINDLESS-PACKAGE-RESOURCE-TYPE"};
    }
}

DescriptorBindingDesc ordinaryOpaqueBinding(const Node& node)
{
    DescriptorBindingDesc binding;
    binding.binding = Y::uint32(node, "binding");
    binding.type = descriptorType(Y::text(node, "type"));
    binding.arrayCount = Y::uint32(node, "arrayCount");
    const auto stages = Y::uint32(node, "stageMask");
    require(binding.arrayCount == 1u && stages && !(stages & ~7u),
        "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
    binding.stages = static_cast<ShaderStage>(stages);
    if (binding.type == DescriptorType::Sampler) {
        require(node.size() == 7u, "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
        const auto filtering = Y::text(node, "samplerFiltering");
        require(
            filtering == "Filtering" || filtering == "NonFiltering" || filtering == "Comparison",
            "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
        binding.sampledTextureFilterable = filtering == "Filtering";
        binding.comparisonSampler = filtering == "Comparison";
    } else {
        require(binding.type == DescriptorType::SampledTexture && node.size() == 9u,
            "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
        const auto dimension = Y::text(node, "textureViewDimension");
        require(dimension == "Texture2D" || dimension == "Texture2DArray"
                || dimension == "Texture3D" || dimension == "TextureCube",
            "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
        binding.textureViewDimensionConstrained = true;
        binding.textureViewDimension = dimension == "Texture2DArray"
            ? TextureViewDimension::Texture2DArray
            : dimension == "Texture3D"   ? TextureViewDimension::Texture3D
            : dimension == "TextureCube" ? TextureViewDimension::TextureCube
                                         : TextureViewDimension::Texture2D;
        binding.depthTexture = Y::boolean(node, "depthTexture");
        binding.sampledTextureFilterable = Y::boolean(node, "sampledTextureFilterable");
        require(!binding.depthTexture
                || (!binding.sampledTextureFilterable
                    && binding.textureViewDimension != TextureViewDimension::Texture3D),
            "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
    }
    return binding;
}

void verifyOrdinaryOpaqueBinary(
    const core::ByteBuffer& spirv, std::uint32_t set, const DescriptorBindingDesc& binding)
{
    using Verifier = shader::SpirvImageDeclarationVerifier;
    if (binding.type == DescriptorType::Sampler) {
        std::uint32_t count = 0u;
        require(!Verifier::inspectSampler(spirv.data(), spirv.size(), set, binding.binding, count)
                && count == 1u,
            "RHI-BINDLESS-PACKAGE-ORDINARY-IMAGE-BINARY");
        return;
    }
    shader::SpirvImageDeclaration image;
    const auto dimension = binding.textureViewDimension == TextureViewDimension::TextureCube ? 3u
        : binding.textureViewDimension == TextureViewDimension::Texture3D                    ? 2u
                                                                                             : 1u;
    require(!Verifier::inspect(spirv.data(), spirv.size(), set, binding.binding, image)
            && image.descriptorCount == 1u && image.dimension == dimension
            && image.arrayed
                == (binding.textureViewDimension == TextureViewDimension::Texture2DArray ? 1u : 0u)
            && !image.multisampled && image.sampled == 1u && image.format == 0u && image.floatScalar
            && image.scalarWidth == 32u
            && (binding.depthTexture ? image.depth == 1u : image.depth == 0u || image.depth == 2u),
        "RHI-BINDLESS-PACKAGE-ORDINARY-IMAGE-BINARY");
}

core::StringView ordinaryOpaqueWgslType(const DescriptorBindingDesc& binding)
{
    if (binding.type == DescriptorType::Sampler)
        return binding.comparisonSampler ? "sampler_comparison" : "sampler";
    switch (binding.textureViewDimension) {
    case TextureViewDimension::Texture2D:
        return binding.depthTexture ? "texture_depth_2d" : "texture_2d";
    case TextureViewDimension::Texture2DArray:
        return binding.depthTexture ? "texture_depth_2d_array" : "texture_2d_array";
    case TextureViewDimension::Texture3D:
        return "texture_3d";
    case TextureViewDimension::TextureCube:
        return binding.depthTexture ? "texture_depth_cube" : "texture_cube";
    }
    throw Rejection{"RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA"};
}

BindlessOperation operation(const core::pmr::String& name)
{
    if (name == "load")
        return BindlessOperation::Load;
    if (name == "query")
        return BindlessOperation::Query;
    if (name == "sampleLevel")
        return BindlessOperation::SampleLevel;
    if (name == "sampleGrad")
        return BindlessOperation::SampleGrad;
    if (name == "store")
        return BindlessOperation::Store;
    if (name == "atomicAdd" || name == "atomicOr")
        return BindlessOperation::Atomic;
    throw Rejection{"RHI-BINDLESS-PACKAGE-OPERATION"};
}

std::uint32_t fileIndex(
    const core::pmr::Vector<BindlessPackageFile>& files, const core::pmr::String& name)
{
    for (std::uint32_t i = 0; i < files.size(); ++i)
        if (files[i].name == name)
            return i;
    throw Rejection{"RHI-BINDLESS-PACKAGE-REFERENCED-FILE-MISSING"};
}
} // namespace

std::shared_ptr<const BindlessShaderPackage> BindlessShaderPackage::load(
    const std::filesystem::path& publication,
    const BindlessPackageLoadBudget& budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        require(budget.maxFileBytes && budget.maxTotalBytes && budget.maxFiles && budget.maxModules
                && budget.maxBindings && budget.maxTables,
            "RHI-BINDLESS-PACKAGE-LOAD-BUDGET");
        if (const auto* request = budget.push) {
            require(request->contractVersion == 1u && request->maxBytes
                    && request->maxBytes <= kL2AtomicPushConstantLimitBytes && request->entryCount
                    && request->entryCount <= budget.maxModules && request->entries,
                "RHI-BINDLESS-PACKAGE-PUSH-REQUEST");
            for (std::size_t i = 0u; i < request->entryCount; ++i) {
                const auto& entry = request->entries[i];
                require(!entry.entry.empty() && entry.sourceTypes.maxNodes
                        && entry.sourceTypes.maxMembers && entry.sourceTypes.maxRoots,
                    "RHI-BINDLESS-PACKAGE-PUSH-ENTRY");
                for (std::size_t j = 0u; j < i; ++j)
                    require(entry.entry != request->entries[j].entry,
                        "RHI-BINDLESS-PACKAGE-PUSH-ENTRY-DUPLICATE");
            }
        }
        if (const auto* request = budget.ordinaryMemory) {
            require((request->contractVersion == 1u || request->contractVersion == 2u)
                    && request->entryCount && request->entryCount <= budget.maxModules
                    && request->entries,
                "RHI-BINDLESS-PACKAGE-MEMORY-REQUEST");
            for (std::size_t i = 0u; i < request->entryCount; ++i) {
                const auto& entry = request->entries[i];
                require(!entry.entry.empty(), "RHI-BINDLESS-PACKAGE-MEMORY-ENTRY");
                for (std::size_t j = 0u; j < i; ++j)
                    require(entry.entry != request->entries[j].entry,
                        "RHI-BINDLESS-PACKAGE-MEMORY-ENTRY-DUPLICATE");
                if (request->contractVersion == 1u) {
                    require(!entry.native, "RHI-BINDLESS-PACKAGE-MEMORY-NATIVE-VERSION");
                    if (const auto* id = shader::TargetBufferMemoryDeclarations::validateRequest(
                            entry.buffers, budget.maxBindings))
                        throw Rejection{id};
                    require(!entry.buffers.push && !entry.buffers.pushArrays.count
                            && !entry.buffers.pushArrays.matrixCount,
                        "RHI-BINDLESS-PACKAGE-MEMORY-PUSH");
                } else {
                    require(entry.native && !entry.buffers.contractVersion
                            && !entry.buffers.bindingCount && !entry.buffers.bindings
                            && !entry.buffers.push && !entry.buffers.pushBinary
                            && !entry.buffers.maxPairs && !entry.buffers.maxArrayRules
                            && !entry.buffers.maxMatrixRules && !entry.buffers.sourceTypes.maxNodes
                            && !entry.buffers.sourceTypes.maxMembers
                            && !entry.buffers.sourceTypes.maxRoots
                            && !entry.buffers.targetTypes.maxNodes
                            && !entry.buffers.targetTypes.maxMembers
                            && !entry.buffers.targetTypes.maxRoots
                            && !entry.buffers.pushArrays.count
                            && !entry.buffers.pushArrays.matrixCount
                            && !entry.buffers.pushArrays.arrayValueWrappers
                            && !entry.buffers.pushArrays.matrixColumnValues,
                        "RHI-BINDLESS-PACKAGE-MEMORY-NATIVE-REQUEST");
                    if (const auto* id = shader::NativeBufferMemoryDeclarations::validateRequest(
                            *entry.native, budget.maxBindings))
                        throw Rejection{id};
                }
            }
        }
        auto result = core::makeShared<BindlessShaderPackage>(ConstructionKey{});
        auto read = [&](const std::filesystem::path& path) {
            const auto remaining = budget.maxTotalBytes - result->sourceBytes_;
            require(remaining > 0, "RHI-BINDLESS-PACKAGE-BYTE-BUDGET");
            if (budget.source) {
                auto file = budget.source->read(path, std::min(budget.maxFileBytes, remaining));
                require(!file.diagnostic, file.diagnostic);
                require(!file.bytes.empty()
                        && file.bytes.size() <= std::min(budget.maxFileBytes, remaining),
                    "RHI-BINDLESS-PACKAGE-BYTE-BUDGET");
                result->sourceBytes_ += file.bytes.size();
                return std::move(file.bytes);
            }
            auto file = core::readBinaryFile(path, std::min(budget.maxFileBytes, remaining));
            require(file.success && !file.bytes.empty(), "RHI-BINDLESS-PACKAGE-FILE-READ-FAILED");
            result->sourceBytes_ += file.bytes.size();
            return std::move(file.bytes);
        };
        auto envelopeBytes = read(publication);
        const auto envelopeText = decodedText(envelopeBytes);
        const auto envelope = Node::deserialize(envelopeText.begin(), envelopeText.end());
        require(Y::text(envelope, "type") == "BindlessPackagePublication"
                && Y::uint32(envelope, "contractVersion") == 1,
            "RHI-BINDLESS-PACKAGE-PUBLICATION-VERSION");
        const auto payloadName = Y::text(envelope, "payload");
        require(core::isFlatArtifactFilename(payloadName), "RHI-BINDLESS-PACKAGE-FILENAME");
        result->sha256_ = Y::text(envelope, "shaderPackageIdentity");
        auto payloadBytes = read(publication.parent_path() / payloadName.c_str());
        require(core::Sha256::matchesHexDigest(textBytes(payloadBytes), result->sha256_),
            "RHI-BINDLESS-PACKAGE-HASH-MISMATCH");
        const auto payloadText = decodedText(payloadBytes);
        const auto payload = Node::deserialize(payloadText.begin(), payloadText.end());
        require(Y::text(payload, "type") == "BindlessShaderArtifact"
                && Y::uint32(payload, "contractVersion") == kBindlessContractVersion
                && Y::text(payload, "validationStatus") == "TargetReflectionValidated"
                && Y::text(payload, "matrixLayout") == "row-major",
            "RHI-BINDLESS-PACKAGE-ARTIFACT-CONTRACT");
        auto& artifact = result->artifact_;
        const auto mode = Y::text(payload, "execution");
        require(mode == "Native" || mode == "Emulated", "RHI-BINDLESS-PACKAGE-EXECUTION");
        artifact.execution =
            mode == "Native" ? BindlessExecution::Native : BindlessExecution::Emulated;
        require(!budget.ordinaryMemory
                || (budget.ordinaryMemory->contractVersion == 1u
                        ? artifact.execution == BindlessExecution::Emulated
                        : artifact.execution == BindlessExecution::Native),
            "RHI-BINDLESS-PACKAGE-MEMORY-EXECUTION");
        artifact.set = Y::uint32(payload, "set");
        if (payload.contains("pipelineInterfaceContractVersion")) {
            artifact.pipelineInterfaceContractVersion =
                Y::uint32(payload, "pipelineInterfaceContractVersion");
            require(artifact.pipelineInterfaceContractVersion == 1u,
                "RHI-BINDLESS-PACKAGE-PIPELINE-INTERFACE-VERSION");
        }
        artifact.switchCases = Y::uint32(payload, "switchCases");
        artifact.snapshotBytes = Y::uint64(Y::required(payload, "snapshotBytes"));
        require(artifact.snapshotBytes > 0u, "RHI-BINDLESS-PACKAGE-SNAPSHOT-BUDGET");
        artifact.activePrefixContractVersion = Y::uint32(payload, "activePrefixContractVersion");
        require(
            artifact.activePrefixContractVersion == 2, "RHI-BINDLESS-PACKAGE-ACTIVE-PREFIX-ABI");
        artifact.faultReportingContractVersion =
            Y::uint32(payload, "faultReportingContractVersion");
        require(artifact.faultReportingContractVersion <= 1u,
            "RHI-BINDLESS-PACKAGE-FAULT-REPORTING-ABI");
        const auto textureShapeVersion = payload.contains("textureShapeContractVersion")
            ? Y::uint32(payload, "textureShapeContractVersion")
            : 0u;
        require(textureShapeVersion <= 1u, "RHI-BINDLESS-PACKAGE-TEXTURE-SHAPE-VERSION");
        const auto sampledDepthVersion = payload.contains("sampledDepthContractVersion")
            ? Y::uint32(payload, "sampledDepthContractVersion")
            : 0u;
        require(sampledDepthVersion <= 1u, "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-VERSION");
        const auto storageImageVersion = payload.contains("storageImageContractVersion")
            ? Y::uint32(payload, "storageImageContractVersion")
            : 0u;
        require(storageImageVersion <= BindlessStorageImageDomain::contractVersion,
            "RHI-BINDLESS-PACKAGE-STORAGE-IMAGE-VERSION");
        if (payload.contains("indirectInstanceBaseContractVersion")) {
            artifact.indirectInstanceBaseContractVersion =
                Y::uint32(payload, "indirectInstanceBaseContractVersion");
            require(artifact.indirectInstanceBaseContractVersion == 1u,
                "RHI-BINDLESS-PACKAGE-INDIRECT-INSTANCE-BASE-ABI");
        }
        const auto& metadata = Y::required(payload, "snapshotMetadata");
        artifact.snapshotMetadata = {Y::uint32(metadata, "contractVersion"),
            Y::uint32(metadata, "binding"),
            Y::uint32(metadata, "wordCount"),
            static_cast<ShaderStage>(Y::uint32(metadata, "stageMask"))};
        require(
            artifact.snapshotMetadata.contractVersion == 1, "RHI-BINDLESS-PACKAGE-METADATA-ABI");
        if (payload.contains("faultLatch")) {
            const auto& latch = Y::required(payload, "faultLatch");
            artifact.faultLatch = {Y::uint32(latch, "contractVersion"),
                Y::uint32(latch, "binding"),
                static_cast<ShaderStage>(Y::uint32(latch, "stageMask"))};
            require(
                artifact.faultLatch.contractVersion == 1u, "RHI-BINDLESS-PACKAGE-FAULT-LATCH-ABI");
        }
        require((artifact.faultReportingContractVersion == 1u)
                == (artifact.faultLatch.contractVersion == 1u),
            "RHI-BINDLESS-PACKAGE-FAULT-REPORTING-ABI");
        require(!artifact.indirectInstanceBaseContractVersion
                || artifact.faultReportingContractVersion == 1u,
            "RHI-BINDLESS-PACKAGE-INDIRECT-INSTANCE-BASE-ABI");
        if (payload.contains("zeroBaseIndirectProducerContractVersion")) {
            artifact.zeroBaseIndirectProducerContractVersion =
                Y::uint32(payload, "zeroBaseIndirectProducerContractVersion");
            require(artifact.zeroBaseIndirectProducerContractVersion == 1u
                    && artifact.indirectInstanceBaseContractVersion == 1u
                    && artifact.faultReportingContractVersion == 1u,
                "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-PRODUCER-ABI");
            const core::StringView expected = artifact.execution == BindlessExecution::Native
                ? compiler_contract::zeroBaseIndirectNativePackageSha256
                : compiler_contract::zeroBaseIndirectEmulatedPackageSha256;
            require(!expected.empty() && result->sha256_ == expected,
                "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-PRODUCER-PROVENANCE");
        }
        if (payload.contains("gpuVisibilityConsumerContractVersion")) {
            result->gpuVisibilityConsumerVersion_ =
                Y::uint32(payload, "gpuVisibilityConsumerContractVersion");
            require(result->gpuVisibilityConsumerVersion_ == 2u
                    && payload.contains("gpuVisibilityKernelContractVersion"),
                "RHI-BINDLESS-PACKAGE-GPU-VISIBILITY-CONSUMER-ABI");
        }
        if (payload.contains("gpuVisibilityKernelContractVersion")) {
            result->gpuVisibilityVersion_ =
                Y::uint32(payload, "gpuVisibilityKernelContractVersion");
            const core::StringView expected = result->gpuVisibilityConsumerVersion_
                ? (artifact.execution == BindlessExecution::Native
                          ? compiler_contract::gpuVisibilityConsumerNativePackageSha256
                          : compiler_contract::gpuVisibilityConsumerEmulatedPackageSha256)
                : (artifact.execution == BindlessExecution::Native
                          ? compiler_contract::gpuVisibilityNativePackageSha256
                          : compiler_contract::gpuVisibilityEmulatedPackageSha256);
            require(result->gpuVisibilityVersion_ == 1u
                    && artifact.faultReportingContractVersion == 1u
                    && !artifact.zeroBaseIndirectProducerContractVersion,
                "RHI-BINDLESS-PACKAGE-GPU-VISIBILITY-ABI");
            require(!expected.empty() && result->sha256_ == expected,
                "RHI-BINDLESS-PACKAGE-GPU-VISIBILITY-PROVENANCE");
        }
        if (payload.contains("indirectInstanceConsumer")) {
            const auto& consumer = Y::required(payload, "indirectInstanceConsumer");
            artifact.indirectInstanceConsumer = {Y::uint32(consumer, "contractVersion"),
                Y::uint32(consumer, "binding"),
                Y::uint32(consumer, "recordIndex"),
                Y::uint32(consumer, "instanceCapacity")};
            require(artifact.indirectInstanceConsumer.contractVersion == 1u
                    && artifact.indirectInstanceConsumer.instanceCapacity
                    && artifact.indirectInstanceBaseContractVersion == 1u
                    && (artifact.faultLatch.stages & ShaderStage::Fragment) != ShaderStage::None,
                "RHI-BINDLESS-PACKAGE-INDIRECT-INSTANCE-CONSUMER-ABI");
        }
        if (payload.contains("zeroBaseIndirectConsumerContractVersion")) {
            artifact.zeroBaseIndirectConsumerContractVersion =
                Y::uint32(payload, "zeroBaseIndirectConsumerContractVersion");
            require(artifact.zeroBaseIndirectConsumerContractVersion == 1u
                    && !artifact.zeroBaseIndirectProducerContractVersion
                    && artifact.indirectInstanceConsumer.contractVersion == 1u,
                "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-CONSUMER-ABI");
            const core::StringView expected = artifact.execution == BindlessExecution::Native
                ? compiler_contract::zeroBaseIndirectConsumerNativePackageSha256
                : compiler_contract::zeroBaseIndirectConsumerEmulatedPackageSha256;
            require(!expected.empty() && result->sha256_ == expected,
                "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-CONSUMER-PROVENANCE");
        }
        artifact.physicalLayout.name = Y::text(payload, "name");
        core::StableHash64 identity;
        identity.appendString(result->sha256_);
        artifact.shaderPackageIdentity = identity.value();
        require(artifact.shaderPackageIdentity != 0, "RHI-BINDLESS-PACKAGE-IDENTITY-INVALID");
        const auto loweringHash = Y::text(payload, "loweringSha256");

        const auto& artifacts = Y::required(payload, "artifacts");
        require(artifacts.is_mapping() && artifacts.size() > 0 && budget.maxFiles >= 2
                && artifacts.size() <= budget.maxFiles - 2u,
            "RHI-BINDLESS-PACKAGE-FILE-BUDGET");
        result->files_.reserve(artifacts.size());
        for (const auto& pair : artifacts.get_value_ref<const Node::mapping_type&>()) {
            BindlessPackageFile file;
            file.name = Y::text(pair.first);
            file.sha256 = Y::text(pair.second);
            require(core::isFlatArtifactFilename(file.name) && file.name != payloadName
                    && file.name != publication.filename().generic_string().c_str(),
                "RHI-BINDLESS-PACKAGE-FILENAME");
            file.bytes = read(publication.parent_path() / file.name.c_str());
            require(core::Sha256::matchesHexDigest(textBytes(file.bytes), file.sha256),
                "RHI-BINDLESS-PACKAGE-HASH-MISMATCH");
            // All declared non-SPIR-V members of this versioned package are
            // generated text. Validate without changing the bytes used by hashes.
            if (std::filesystem::path(file.name.c_str()).extension() != ".spv")
                (void)decodedText(file.bytes);
            result->files_.push_back(std::move(file));
        }

        const auto& bindings = Y::sequence(payload, "bindings");
        require(!bindings.empty() && bindings.size() <= budget.maxBindings,
            "RHI-BINDLESS-PACKAGE-BINDING-BUDGET");
        core::pmr::Vector<core::pmr::String> names{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::uint32_t> observedStages{&core::defaultCpuMemoryResource()};
        artifact.physicalLayout.bindings.reserve(bindings.size());
        names.reserve(bindings.size());
        observedStages.reserve(bindings.size());
        bool hasSampledDepth = false;
        bool hasStorageImage = false;
        for (const auto& node : bindings) {
            DescriptorBindingDesc binding;
            binding.binding = Y::uint32(node, "binding");
            binding.type = descriptorType(Y::text(node, "type"));
            binding.arrayCount = Y::uint32(node, "arrayCount");
            const auto stageMask = Y::uint32(node, "stageMask");
            binding.stages = static_cast<ShaderStage>(stageMask);
            require(Y::uint32(node, "set") == artifact.set && binding.arrayCount && stageMask
                    && !(stageMask & ~7u),
                "RHI-BINDLESS-PACKAGE-BINDING-INVALID");
            for (const auto& previous : artifact.physicalLayout.bindings)
                require(
                    previous.binding != binding.binding, "RHI-BINDLESS-PACKAGE-BINDING-COLLISION");
            if (binding.type == DescriptorType::StorageTexture) {
                hasStorageImage = true;
                const auto format = Y::text(node, "storageTextureFormat");
                binding.storageTextureFormat = format == "r32float" ? TextureFormat::R32Float
                    : format == "rgba16float"                       ? TextureFormat::Rgba16Float
                                                                    : TextureFormat::Unknown;
                require(BindlessStorageImageDomain::declaration(
                            binding.storageTextureFormat, storageImageVersion)
                            .floatLanes,
                    "RHI-BINDLESS-PACKAGE-STORAGE-FORMAT");
                const auto declaration = BindlessStorageImageDomain::declaration(
                    binding.storageTextureFormat, storageImageVersion);
                require(Y::text(node, "storageTextureAccess")
                        == (declaration.access == StorageTextureAccess::WriteOnly ? "WriteOnly"
                                                                                  : "ReadWrite"),
                    "RHI-BINDLESS-PACKAGE-STORAGE-ACCESS");
                binding.storageTextureAccess = declaration.access;
            } else {
                require(
                    !node.contains("storageTextureAccess"), "RHI-BINDLESS-PACKAGE-STORAGE-ACCESS");
            }
            binding.textureViewDimensionConstrained = binding.type == DescriptorType::SampledTexture
                || binding.type == DescriptorType::StorageTexture;
            if (textureShapeVersion && binding.textureViewDimensionConstrained) {
                const auto dimension = Y::text(node, "textureViewDimension");
                require(dimension == "Texture2D"
                        || (dimension == "Texture2DArray"
                            && binding.type == DescriptorType::SampledTexture),
                    "RHI-BINDLESS-PACKAGE-TEXTURE-DIMENSION");
                binding.textureViewDimension = dimension == "Texture2DArray"
                    ? TextureViewDimension::Texture2DArray
                    : TextureViewDimension::Texture2D;
            } else
                require(!node.contains("textureViewDimension"),
                    "RHI-BINDLESS-PACKAGE-TEXTURE-DIMENSION");
            if (node.contains("depthTexture")) {
                require(Y::boolean(node, "depthTexture")
                        && binding.type == DescriptorType::SampledTexture
                        && textureShapeVersion == 1u
                        && binding.textureViewDimension == TextureViewDimension::Texture2D,
                    "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-DOMAIN");
                require(sampledDepthVersion == 1u, "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-VERSION");
                binding.depthTexture = true;
                hasSampledDepth = true;
            }
            if (node.contains("samplerFiltering")) {
                require(sampledDepthVersion == 1u && binding.type == DescriptorType::Sampler
                        && Y::text(node, "samplerFiltering") == "NonFiltering",
                    "RHI-BINDLESS-PACKAGE-SAMPLER-FILTERING");
                binding.sampledTextureFilterable = false;
            }
            artifact.physicalLayout.bindings.push_back(binding);
            names.push_back(Y::text(node, "name"));
            observedStages.push_back(0u);
        }
        require(
            !storageImageVersion || hasStorageImage, "RHI-BINDLESS-PACKAGE-STORAGE-IMAGE-UNUSED");
        require(bool(sampledDepthVersion) == hasSampledDepth,
            "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-DOMAIN");
        const auto indexedBindings = bindings.size();
        core::pmr::Vector<bool> covered(indexedBindings, false, &core::defaultCpuMemoryResource());
        bool metadataCovered = false;
        for (std::size_t i = 0; i < indexedBindings; ++i) {
            const auto& b = artifact.physicalLayout.bindings[i];
            if (b.binding != artifact.snapshotMetadata.binding)
                continue;
            require(b.type == DescriptorType::ReadOnlyStorageBuffer && b.arrayCount == 1
                    && b.stages == artifact.snapshotMetadata.stages
                    && names[i] == "ArpBindlessSnapshotMetadata",
                "RHI-BINDLESS-PACKAGE-METADATA-ABI");
            covered[i] = metadataCovered = true;
        }
        require(metadataCovered, "RHI-BINDLESS-PACKAGE-METADATA-ABI");
        bool faultLatchCovered = false;
        if (artifact.faultLatch.contractVersion) {
            for (std::size_t i = 0; i < indexedBindings; ++i) {
                const auto& b = artifact.physicalLayout.bindings[i];
                if (b.binding != artifact.faultLatch.binding)
                    continue;
                require(b.type == DescriptorType::StorageBuffer && b.arrayCount == 1
                        && b.stages == artifact.faultLatch.stages
                        && names[i] == "ArpBindlessFaultLatch" && !covered[i],
                    "RHI-BINDLESS-PACKAGE-FAULT-LATCH-ABI");
                covered[i] = faultLatchCovered = true;
            }
            require(faultLatchCovered, "RHI-BINDLESS-PACKAGE-FAULT-LATCH-ABI");
        }
        if (artifact.indirectInstanceConsumer.contractVersion) {
            bool consumerCovered = false;
            for (std::size_t i = 0; i < indexedBindings; ++i) {
                const auto& b = artifact.physicalLayout.bindings[i];
                if (b.binding != artifact.indirectInstanceConsumer.binding)
                    continue;
                require(!covered[i] && b.type == DescriptorType::ReadOnlyStorageBuffer
                        && b.arrayCount == 1u && b.stages == ShaderStage::Vertex
                        && names[i] == "ArpBindlessIndirectInstanceMappings",
                    "RHI-BINDLESS-PACKAGE-INDIRECT-INSTANCE-CONSUMER-ABI");
                covered[i] = consumerCovered = true;
            }
            require(consumerCovered, "RHI-BINDLESS-PACKAGE-INDIRECT-INSTANCE-CONSUMER-ABI");
        }
        std::uint64_t metadataWords = 4;
        ShaderStage metadataStages = ShaderStage::None;
        ShaderStage faultStages = ShaderStage::None;
        const auto& tables = Y::sequence(payload, "tables");
        require(!tables.empty() && tables.size() <= budget.maxTables,
            "RHI-BINDLESS-PACKAGE-TABLE-BUDGET");
        for (const auto& node : tables) {
            BindlessTableMapping table;
            table.tableId = Y::uint32(node, "tableId");
            table.firstBinding = Y::uint32(node, "firstBinding");
            table.capacity = Y::uint32(node, "capacity");
            table.metadataWordOffset = Y::uint32(node, "metadataWordOffset");
            require(table.metadataWordOffset == metadataWords, "RHI-BINDLESS-PACKAGE-METADATA-ABI");
            metadataWords += 4ull + table.capacity;
            table.nonUniform = Y::boolean(node, "nonUniform");
            if (node.contains("samplerTableId"))
                table.samplerTableId = Y::uint32(node, "samplerTableId");
            for (const auto& op : Y::sequence(node, "operations")) {
                const auto value = operation(Y::text(op));
                require(!(static_cast<std::uint32_t>(table.operations)
                            & static_cast<std::uint32_t>(value)),
                    "RHI-BINDLESS-PACKAGE-OPERATION-DUPLICATE");
                table.operations = table.operations | value;
            }
            const auto first = std::find_if(artifact.physicalLayout.bindings.begin(),
                artifact.physicalLayout.bindings.end(),
                [&](const auto& b) {
                    return b.binding == table.firstBinding;
                });
            require(first != artifact.physicalLayout.bindings.end(),
                "RHI-BINDLESS-PACKAGE-TABLE-MAPPING");
            const auto type = first->type;
            metadataStages = metadataStages | first->stages;
            if (table.operations != BindlessOperation::None)
                faultStages = faultStages | first->stages;
            const bool buffer = type == DescriptorType::UniformBuffer
                || type == DescriptorType::ReadOnlyStorageBuffer
                || type == DescriptorType::StorageBuffer;
            require(node.contains("bufferLayout") == buffer, "RHI-BINDLESS-PACKAGE-BUFFER-ABI");
            if (node.contains("bufferLayout")) {
                const auto& shape = Y::required(node, "bufferLayout");
                table.bufferLayout.identity = Y::hex64(shape, "identity");
                table.bufferLayout.elementStrideBytes = Y::uint32(shape, "elementStrideBytes");
                table.bufferLayout.elementAlignmentBytes =
                    Y::uint32(shape, "elementAlignmentBytes");
                table.bufferLayout.minBindingSizeBytes =
                    Y::uint64(Y::required(shape, "minBindingSizeBytes"));
                core::pmr::String recordIdentity{&core::defaultCpuMemoryResource()};
                const bool record = shape.contains("record");
                std::uint32_t recordStride = 0u;
                if (record) {
                    require(payload.contains("vectorRecordContractVersion")
                            && Y::uint32(payload, "vectorRecordContractVersion") == 1u,
                        "RHI-BINDLESS-PACKAGE-RECORD-VERSION");
                    const auto& fields = Y::sequence(Y::required(shape, "record"), "fields");
                    require(!fields.empty() && fields.size() <= UINT32_MAX / 16u,
                        "RHI-BINDLESS-PACKAGE-RECORD-FIELDS");
                    recordIdentity = "Slang.vector-record/1|";
                    core::pmr::Vector<core::pmr::String> fieldNames{
                        &core::defaultCpuMemoryResource()};
                    for (const auto& field : fields) {
                        const auto name = Y::text(field, "name"), scalar = Y::text(field, "type");
                        require(!name.empty()
                                && (scalar == "float4" || scalar == "uint4" || scalar == "int4")
                                && std::find(fieldNames.begin(), fieldNames.end(), name)
                                    == fieldNames.end(),
                            "RHI-BINDLESS-PACKAGE-RECORD-FIELD");
                        fieldNames.push_back(name);
                        recordIdentity += name;
                        recordIdentity += ':';
                        recordIdentity += scalar;
                        recordIdentity += ';';
                    }
                    recordStride = static_cast<std::uint32_t>(fields.size()) * 16u;
                }
                const core::StringView abi = record ? "Slang.vector-record/1"
                    : type == DescriptorType::UniformBuffer
                    ? (artifact.zeroBaseIndirectProducerContractVersion ? "Slang.uint4.uniform/1"
                                                                        : "Slang.float4.uniform/1")
                    : "Slang.uint32.structured/1";
                const auto digest =
                    core::Sha256::digest(record ? core::StringView(recordIdentity) : abi);
                std::uint64_t expectedIdentity = 0;
                for (std::size_t i = 0; i < 8u; ++i)
                    expectedIdentity = (expectedIdentity << 8u) | digest[i];
                const auto stride = record                  ? recordStride
                    : type == DescriptorType::UniformBuffer ? 16u
                                                            : 4u;
                require(Y::text(shape, "abi") == abi
                        && table.bufferLayout.identity == expectedIdentity
                        && table.bufferLayout.elementStrideBytes == stride
                        && table.bufferLayout.elementAlignmentBytes == (record ? 16u : stride)
                        && table.bufferLayout.minBindingSizeBytes == stride,
                    "RHI-BINDLESS-PACKAGE-BUFFER-ABI");
                require(!record
                        || !(static_cast<std::uint32_t>(table.operations)
                            & static_cast<std::uint32_t>(BindlessOperation::Atomic)),
                    "RHI-BINDLESS-PACKAGE-RECORD-ATOMIC");
            }
            require(table.tableId && table.capacity && table.nonUniform,
                "RHI-BINDLESS-PACKAGE-TABLE-INVALID");
            using O = BindlessOperation;
            const auto allowed = type == DescriptorType::SampledTexture
                ? O::Load | O::Query | O::SampleLevel | O::SampleGrad
                : type == DescriptorType::StorageTexture        ? O::Load | O::Store | O::Query
                : type == DescriptorType::UniformBuffer         ? O::Load
                : type == DescriptorType::ReadOnlyStorageBuffer ? O::Load | O::Query
                : type == DescriptorType::StorageBuffer ? O::Load | O::Store | O::Query | O::Atomic
                                                        : O::None;
            const auto operations = static_cast<std::uint32_t>(table.operations);
            if (first->depthTexture)
                require(!(operations
                            & ~static_cast<std::uint32_t>(O::Load | O::Query | O::SampleLevel)),
                    "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-OPERATION");
            if (type == DescriptorType::StorageTexture
                && first->storageTextureAccess == StorageTextureAccess::WriteOnly)
                require(!(operations & static_cast<std::uint32_t>(O::Load)),
                    "RHI-BINDLESS-PACKAGE-STORAGE-IMAGE-OPERATION");
            require(!(operations & ~static_cast<std::uint32_t>(allowed))
                    && (operations || type == DescriptorType::Sampler),
                "RHI-BINDLESS-PACKAGE-OPERATION");
            require(!(operations & static_cast<std::uint32_t>(O::SampleGrad))
                    || first->stages == ShaderStage::Fragment,
                "RHI-BINDLESS-PACKAGE-OPERATION-STAGE");
            const auto count =
                artifact.execution == BindlessExecution::Emulated ? table.capacity : 1u;
            require(count <= indexedBindings && count - 1u <= UINT32_MAX - table.firstBinding,
                "RHI-BINDLESS-PACKAGE-TABLE-MAPPING");
            const auto tableName = Y::text(node, "name");
            for (std::uint32_t element = 0; element < count; ++element) {
                const auto physical = std::find_if(artifact.physicalLayout.bindings.begin(),
                    artifact.physicalLayout.bindings.end(),
                    [&](const auto& b) {
                        return b.binding == table.firstBinding + element;
                    });
                require(physical != artifact.physicalLayout.bindings.end(),
                    "RHI-BINDLESS-PACKAGE-TABLE-MAPPING");
                const auto index =
                    static_cast<std::size_t>(physical - artifact.physicalLayout.bindings.begin());
                require(!covered[index] && physical->type == first->type
                        && physical->stages == first->stages
                        && physical->storageTextureFormat == first->storageTextureFormat
                        && physical->storageTextureAccess == first->storageTextureAccess
                        && physical->depthTexture == first->depthTexture
                        && physical->sampledTextureFilterable == first->sampledTextureFilterable
                        && physical->textureViewDimension == first->textureViewDimension
                        && physical->arrayCount
                            == (artifact.execution == BindlessExecution::Emulated ? 1u
                                                                                  : table.capacity),
                    "RHI-BINDLESS-PACKAGE-TABLE-MAPPING");
                // The closed generated ABI names every scalar deterministically.
                char suffix[16];
                auto end = std::to_chars(suffix, suffix + sizeof(suffix), element).ptr;
                auto expectedName = tableName;
                if (artifact.execution == BindlessExecution::Emulated) {
                    expectedName += '_';
                    expectedName.append(suffix, end);
                }
                require(names[index] == expectedName, "RHI-BINDLESS-PACKAGE-TABLE-MAPPING");
                covered[index] = true;
            }
            for (const auto& previous : artifact.tables)
                require(previous.tableId != table.tableId, "RHI-BINDLESS-PACKAGE-TABLE-COLLISION");
            artifact.tables.push_back(table);
        }
        require(metadataWords <= UINT32_MAX / 4u
                && metadataWords == artifact.snapshotMetadata.wordCount
                && metadataStages == artifact.snapshotMetadata.stages,
            "RHI-BINDLESS-PACKAGE-METADATA-ABI");
        if (faultLatchCovered)
            require(faultStages == artifact.faultLatch.stages
                    && !(static_cast<std::uint32_t>(faultStages) & ~7u),
                "RHI-BINDLESS-PACKAGE-FAULT-LATCH-ABI");
        require(std::all_of(covered.begin(),
                    covered.end(),
                    [](bool value) {
                        return value;
                    }),
            "RHI-BINDLESS-PACKAGE-TABLE-COVERAGE");
        std::uint64_t switchCases = 0;
        for (const auto& table : artifact.tables) {
            const auto sampleOps = static_cast<std::uint32_t>(table.operations)
                & static_cast<std::uint32_t>(
                    BindlessOperation::SampleLevel | BindlessOperation::SampleGrad);
            require(bool(sampleOps) == bool(table.samplerTableId),
                "RHI-BINDLESS-PACKAGE-SAMPLER-MAPPING");
            std::uint32_t samplerCapacity = 1;
            if (sampleOps) {
                const auto sampler = std::find_if(
                    artifact.tables.begin(), artifact.tables.end(), [&](const auto& t) {
                        return t.tableId == table.samplerTableId;
                    });
                require(sampler != artifact.tables.end(), "RHI-BINDLESS-PACKAGE-SAMPLER-MAPPING");
                const auto binding = std::find_if(artifact.physicalLayout.bindings.begin(),
                    artifact.physicalLayout.bindings.end(),
                    [&](const auto& b) {
                        return b.binding == sampler->firstBinding;
                    });
                const auto texture = std::find_if(artifact.physicalLayout.bindings.begin(),
                    artifact.physicalLayout.bindings.end(),
                    [&](const auto& b) {
                        return b.binding == table.firstBinding;
                    });
                require(binding->type == DescriptorType::Sampler
                        && !(static_cast<std::uint32_t>(texture->stages)
                            & ~static_cast<std::uint32_t>(binding->stages)),
                    "RHI-BINDLESS-PACKAGE-SAMPLER-MAPPING");
                samplerCapacity = sampler->capacity;
            }
            for (std::uint32_t bit = 1;
                bit <= static_cast<std::uint32_t>(BindlessOperation::Atomic);
                bit <<= 1u) {
                if (!(static_cast<std::uint32_t>(table.operations) & bit))
                    continue;
                const auto cases =
                    std::uint64_t(table.capacity) * ((sampleOps & bit) ? samplerCapacity : 1u);
                require(cases <= UINT32_MAX && switchCases <= UINT32_MAX - cases,
                    "RHI-BINDLESS-PACKAGE-SWITCH-BUDGET");
                switchCases += cases;
            }
        }
        require((artifact.execution == BindlessExecution::Emulated ? switchCases : 0u)
                == artifact.switchCases,
            "RHI-BINDLESS-PACKAGE-SWITCH-BUDGET");
        // Hash exact compiler-published lowering bytes instead of attempting
        // cross-language YAML/JSON canonicalization. Reflection binds the same
        // digest to the source and all target interfaces.
        const auto loweringYaml = Y::text(payload, "loweringYaml");
        require(core::Sha256::matchesHexDigest(loweringYaml, loweringHash),
            "RHI-BINDLESS-PACKAGE-LOWERING-HASH");
        const auto loweringText =
            decodedText(core::StringView(loweringYaml.data(), loweringYaml.size()));
        const auto lowering = Node::deserialize(loweringText.begin(), loweringText.end());
        std::uint32_t pushBytes = 0u, pushStages = 0u, observedPushStages = 0u;
        std::size_t observedPushEntries = 0u;
        if (const auto* request = budget.push) {
            require(payload.contains("pushConstants") && lowering.contains("pushConstants"),
                "RHI-BINDLESS-PACKAGE-PUSH-UNUSED");
            const auto& push = Y::required(payload, "pushConstants");
            require(push.is_mapping() && push.size() == 5u
                    && Y::uint32(push, "contractVersion") == 1u
                    && artifact.pipelineInterfaceContractVersion == 1u
                    && push == Y::required(lowering, "pushConstants"),
                "RHI-BINDLESS-PACKAGE-PUSH-SCHEMA");
            pushBytes = Y::uint32(push, "sizeBytes");
            pushStages = Y::uint32(push, "stageMask");
            require(pushBytes && pushBytes % 4u == 0u && pushBytes <= request->maxBytes,
                "RHI-BINDLESS-PACKAGE-PUSH-BYTE-BUDGET");
            require(pushStages && !(pushStages & ~7u) && (!(pushStages & 4u) || pushStages == 4u),
                "RHI-BINDLESS-PACKAGE-PUSH-STAGES");
            (void)Y::uint32(push, "webGpuGroup");
            (void)Y::uint32(push, "webGpuBinding");
        }
        require(lowering.contains("ordinaryOpaqueBindings")
                == payload.contains("ordinaryOpaqueBindings"),
            "RHI-BINDLESS-PACKAGE-OPAQUE-LOWERING-MISMATCH");
        if (payload.contains("ordinaryOpaqueBindings"))
            require(Y::required(lowering, "ordinaryOpaqueBindings")
                    == Y::required(payload, "ordinaryOpaqueBindings"),
                "RHI-BINDLESS-PACKAGE-OPAQUE-LOWERING-MISMATCH");
        for (const auto* extension : {"textureShapeContractVersion",
                 "sampledDepthContractVersion",
                 "storageImageContractVersion",
                 "pipelineInterfaceContractVersion"}) {
            require(lowering.contains(extension) == payload.contains(extension),
                "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-VERSION");
            if (payload.contains(extension))
                require(Y::required(lowering, extension) == Y::required(payload, extension),
                    "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-VERSION");
        }
        for (const auto* field : {"type",
                 "contractVersion",
                 "name",
                 "activePrefixContractVersion",
                 "faultReportingContractVersion",
                 "execution",
                 "set",
                 "switchCases",
                 "snapshotBytes",
                 "sourceSha256",
                 "bindings",
                 "tables",
                 "snapshotMetadata"})
            require(Y::required(lowering, field) == Y::required(payload, field),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        require(lowering.contains("faultLatch") == payload.contains("faultLatch"),
            "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        require(lowering.contains("indirectInstanceBaseContractVersion")
                == payload.contains("indirectInstanceBaseContractVersion"),
            "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        if (artifact.indirectInstanceBaseContractVersion)
            require(Y::required(lowering, "indirectInstanceBaseContractVersion")
                    == Y::required(payload, "indirectInstanceBaseContractVersion"),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        require(lowering.contains("indirectInstanceConsumer")
                == payload.contains("indirectInstanceConsumer"),
            "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        if (artifact.indirectInstanceConsumer.contractVersion)
            require(Y::required(lowering, "indirectInstanceConsumer")
                    == Y::required(payload, "indirectInstanceConsumer"),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        require(lowering.contains("zeroBaseIndirectProducerContractVersion")
                == payload.contains("zeroBaseIndirectProducerContractVersion"),
            "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        require(lowering.contains("gpuVisibilityKernelContractVersion")
                == payload.contains("gpuVisibilityKernelContractVersion"),
            "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        if (result->gpuVisibilityVersion_)
            require(Y::required(lowering, "gpuVisibilityKernelContractVersion")
                    == Y::required(payload, "gpuVisibilityKernelContractVersion"),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        require(lowering.contains("gpuVisibilityConsumerContractVersion")
                == payload.contains("gpuVisibilityConsumerContractVersion"),
            "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        if (result->gpuVisibilityConsumerVersion_)
            require(Y::required(lowering, "gpuVisibilityConsumerContractVersion")
                    == Y::required(payload, "gpuVisibilityConsumerContractVersion"),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        if (artifact.zeroBaseIndirectProducerContractVersion)
            require(Y::required(lowering, "zeroBaseIndirectProducerContractVersion")
                    == Y::required(payload, "zeroBaseIndirectProducerContractVersion"),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        require(lowering.contains("zeroBaseIndirectConsumerContractVersion")
                == payload.contains("zeroBaseIndirectConsumerContractVersion"),
            "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        if (artifact.zeroBaseIndirectConsumerContractVersion)
            require(Y::required(lowering, "zeroBaseIndirectConsumerContractVersion")
                    == Y::required(payload, "zeroBaseIndirectConsumerContractVersion"),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");
        if (artifact.faultLatch.contractVersion)
            require(Y::required(lowering, "faultLatch") == Y::required(payload, "faultLatch"),
                "RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH");

        const auto& modules = Y::sequence(payload, "modules");
        require(!modules.empty() && modules.size() <= budget.maxModules,
            "RHI-BINDLESS-PACKAGE-MODULE-BUDGET");
        if (const auto* request = budget.push)
            for (std::size_t i = 0u; i < request->entryCount; ++i) {
                std::size_t matches = 0u;
                for (const auto& node : modules)
                    if (request->entries[i].entry == Y::text(node, "entry"))
                        ++matches;
                require(matches == 1u, "RHI-BINDLESS-PACKAGE-PUSH-ENTRY");
            }
        if (const auto* request = budget.ordinaryMemory)
            for (std::size_t i = 0u; i < request->entryCount; ++i) {
                std::size_t matches = 0u;
                for (const auto& node : modules)
                    if (request->entries[i].entry == Y::text(node, "entry"))
                        ++matches;
                require(matches == 1u, "RHI-BINDLESS-PACKAGE-MEMORY-ENTRY");
            }
        require(lowering.contains("vectorRecordContractVersion")
                == payload.contains("vectorRecordContractVersion"),
            "RHI-BINDLESS-PACKAGE-RECORD-VERSION");
        if (payload.contains("vectorRecordContractVersion"))
            require(Y::uint32(payload, "vectorRecordContractVersion") == 1u
                    && Y::required(lowering, "vectorRecordContractVersion")
                        == Y::required(payload, "vectorRecordContractVersion"),
                "RHI-BINDLESS-PACKAGE-RECORD-VERSION");
        core::pmr::Vector<bool> referenced(
            result->files_.size(), false, &core::defaultCpuMemoryResource());

        struct OrdinaryDomainBinding {
            std::uint32_t set = 0, binding = 0;
            core::pmr::String name{&core::defaultCpuMemoryResource()};
        };

        core::pmr::Vector<OrdinaryDomainBinding> ordinaryDomainBindings{
            &core::defaultCpuMemoryResource()};

        struct OrdinaryOpaqueBinding {
            std::uint32_t set = 0u, observedStages = 0u;
            core::pmr::String name{&core::defaultCpuMemoryResource()};
            DescriptorBindingDesc binding;
        };

        core::pmr::Vector<OrdinaryOpaqueBinding> ordinaryOpaqueBindings{
            &core::defaultCpuMemoryResource()};
        if (payload.contains("ordinaryOpaqueBindings")) {
            const auto& declarations = Y::sequence(payload, "ordinaryOpaqueBindings");
            require(artifact.pipelineInterfaceContractVersion == 1u && !declarations.empty()
                    && declarations.size() <= budget.maxBindings,
                "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
            ordinaryOpaqueBindings.reserve(declarations.size());
            for (const auto& declaration : declarations) {
                OrdinaryOpaqueBinding item;
                item.set = Y::uint32(declaration, "set");
                item.name = Y::text(declaration, "name");
                item.binding = ordinaryOpaqueBinding(declaration);
                require(item.set != artifact.set && !item.name.empty(),
                    "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
                for (const auto& previous : ordinaryOpaqueBindings)
                    require(previous.name != item.name
                            && (previous.set != item.set
                                || previous.binding.binding != item.binding.binding),
                        "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-SCHEMA");
                ordinaryOpaqueBindings.push_back(std::move(item));
            }
        }
        core::pmr::Vector<shader::BufferDeclarationTypeLayouts> ordinaryModuleTypes{
            &core::defaultCpuMemoryResource()};

        struct NativeSourceRetention {
            std::size_t module, graph;
            const shader::NativeBufferMemoryLoadRequest* request;
        };

        core::pmr::Vector<NativeSourceRetention> nativeSourceRetentions{
            &core::defaultCpuMemoryResource()};
        for (const auto& node : modules) {
            BindlessPackageModule module;
            module.entry = Y::text(node, "entry");
            if (node.contains("initializedGraphics")) {
                const auto& profile = Y::required(node, "initializedGraphics");
                module.initializedGraphics = {Y::uint32(profile, "contractVersion"),
                    Y::uint32(profile, "interfaceIdentity"),
                    Y::boolean(profile, "prototypeOptIn")};
                require(!semantics::BindlessInitializedGraphicsContract::validate(
                            module.initializedGraphics),
                    "RHI-BINDLESS-PACKAGE-GRAPHICS-PROFILE");
            }
            const shader::TargetBufferMemoryLoadRequest* ordinaryRequest = nullptr;
            const shader::NativeBufferMemoryLoadRequest* nativeRequest = nullptr;
            if (const auto* request = budget.ordinaryMemory)
                for (std::size_t i = 0u; i < request->entryCount; ++i)
                    if (request->entries[i].entry == module.entry) {
                        if (request->contractVersion == 1u)
                            ordinaryRequest = &request->entries[i].buffers;
                        else
                            nativeRequest = request->entries[i].native;
                    }
            std::size_t ordinaryBufferCount = 0u;
            for (const auto& previous : result->modules_)
                require(previous.entry != module.entry, "RHI-BINDLESS-PACKAGE-ENTRY-COLLISION");
            module.reflectionFile = fileIndex(result->files_, Y::text(node, "reflection"));
            const auto reflectionText = textBytes(result->files_[module.reflectionFile].bytes);
            const auto reflectionNode =
                Node::deserialize(reflectionText.begin(), reflectionText.end());
            for (const auto* field : {"bindings", "webGpuBindingSpace", "metalArgumentBuffer"})
                require(Y::required(node, field) == Y::required(reflectionNode, field),
                    "RHI-BINDLESS-PACKAGE-MODULE-METADATA-MISMATCH");
            require(node.contains("initializedGraphics")
                        == reflectionNode.contains("initializedGraphics")
                    && (!node.contains("initializedGraphics")
                        || Y::required(node, "initializedGraphics")
                            == Y::required(reflectionNode, "initializedGraphics")),
                "RHI-BINDLESS-PACKAGE-GRAPHICS-PROFILE-MISMATCH");
            auto reflected = shader::ShaderReflectionCache::loadFromYaml(
                core::pmr::String(reflectionText, &core::defaultCpuMemoryResource()));
            require(reflected.success, "RHI-BINDLESS-PACKAGE-REFLECTION-INVALID");
            const auto& reflection = reflected.module.reflection;
            require(shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(reflection)
                        .success
                    && reflection.bindlessContractVersion == "BindlessCross/1"
                    && reflection.bindlessExecution == mode
                    && reflection.bindlessLoweringSha256 == loweringHash
                    && reflection.entryPoints.size() == 1u
                    && reflection.entryPoints[0].name == module.entry,
                "RHI-BINDLESS-PACKAGE-REFLECTION-MISMATCH");
            const auto& entryNodes = Y::sequence(reflectionNode, "entryPoints");
            require(entryNodes.size() == 1u, "RHI-BINDLESS-PACKAGE-REFLECTION-MISMATCH");
            const auto stage = Y::text(entryNodes[0], "stage");
            const std::uint32_t stageMask = stage == "Vertex" ? 1u
                : stage == "Fragment"                         ? 2u
                : stage == "Compute"                          ? 4u
                                                              : 0u;
            require(stageMask == 4u || node.contains("initializedGraphics"),
                "RHI-BINDLESS-PACKAGE-GRAPHICS-PROFILE-DECLARATION");
            require(module.initializedGraphics.empty() || (stageMask == 1u || stageMask == 2u),
                "RHI-BINDLESS-PACKAGE-GRAPHICS-PROFILE-STAGE");
            require(stageMask && (!reflection.pushConstantSizeBytes || budget.push),
                "RHI-BINDLESS-PACKAGE-ENTRY-ABI");
            if (stageMask == 4u)
                require(reflection.entryPoints[0].computeWorkgroupSizeX
                        && reflection.entryPoints[0].computeWorkgroupSizeY
                        && reflection.entryPoints[0].computeWorkgroupSizeZ,
                    "RHI-BINDLESS-PACKAGE-ENTRY-ABI");
            module.sourceFile = fileIndex(result->files_,
                core::pmr::String(core::artifactFilename(reflected.module.sourcePath),
                    &core::defaultCpuMemoryResource()));
            module.spirvFile = fileIndex(result->files_,
                core::pmr::String(core::artifactFilename(reflected.module.spirvPath),
                    &core::defaultCpuMemoryResource()));
            module.metalFile = fileIndex(result->files_,
                core::pmr::String(core::artifactFilename(reflected.module.metalPath),
                    &core::defaultCpuMemoryResource()));
            if (artifact.execution == BindlessExecution::Emulated)
                module.wgslFile = fileIndex(result->files_,
                    core::pmr::String(core::artifactFilename(reflected.module.wgslPath),
                        &core::defaultCpuMemoryResource()));
            const auto role = [&](std::uint32_t index, core::StringView suffix) {
                const auto& name = result->files_[index].name;
                require(!referenced[index] && name.size() > suffix.size()
                        && core::StringView(name).substr(name.size() - suffix.size()) == suffix,
                    "RHI-BINDLESS-PACKAGE-FILE-ROLE");
                referenced[index] = true;
            };
            role(module.sourceFile, ".slang");
            role(module.spirvFile, ".spv");
            role(module.metalFile, ".metal");
            role(module.reflectionFile, ".reflection.yaml");
            if (artifact.execution == BindlessExecution::Emulated)
                role(module.wgslFile, ".wgsl");
            require(
                core::Sha256::matchesHexDigest(
                    textBytes(result->files_[module.sourceFile].bytes), reflection.sourceSha256),
                "RHI-BINDLESS-PACKAGE-SOURCE-HASH-MISMATCH");
            if (artifact.zeroBaseIndirectProducerContractVersion) {
                const auto expected = artifact.execution == BindlessExecution::Native
                    ? compiler_contract::zeroBaseIndirectNativeSourceSha256
                    : compiler_contract::zeroBaseIndirectEmulatedSourceSha256;
                require(modules.size() == 1u && module.entry == "arpZeroBaseIndirectMain"
                        && stageMask == 4u && reflection.entryPoints[0].computeWorkgroupSizeX == 1u
                        && reflection.entryPoints[0].computeWorkgroupSizeY == 1u
                        && reflection.entryPoints[0].computeWorkgroupSizeZ == 1u
                        && core::Sha256::matchesHexDigest(
                            textBytes(result->files_[module.sourceFile].bytes), expected),
                    "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-PRODUCER-PROVENANCE");
            }
            if (result->gpuVisibilityVersion_) {
                static constexpr const char* entries[]{"visibilityResetComputeMain",
                    "visibilityCullComputeMain",
                    "visibilityEmitComputeMain",
                    "visibilityFinalizeComputeMain",
                    "visibilityVertexMain",
                    "visibilityFragmentMain"};
                const auto ordinal = result->modules_.size();
                const auto expected = result->gpuVisibilityConsumerVersion_
                    ? (artifact.execution == BindlessExecution::Native
                              ? compiler_contract::gpuVisibilityConsumerNativeSourceSha256
                              : compiler_contract::gpuVisibilityConsumerEmulatedSourceSha256)
                    : (artifact.execution == BindlessExecution::Native
                              ? compiler_contract::gpuVisibilityNativeSourceSha256
                              : compiler_contract::gpuVisibilityEmulatedSourceSha256);
                const auto count = result->gpuVisibilityConsumerVersion_ ? 6u : 4u;
                const bool stage = ordinal < 4u
                    ? (stageMask == 4u && reflection.entryPoints[0].computeWorkgroupSizeX == 64u
                          && reflection.entryPoints[0].computeWorkgroupSizeY == 1u
                          && reflection.entryPoints[0].computeWorkgroupSizeZ == 1u)
                    : (ordinal == 4u ? stageMask == 1u : stageMask == 2u);
                require(modules.size() == count && ordinal < count
                        && module.entry == entries[ordinal] && stage
                        && core::Sha256::matchesHexDigest(
                            textBytes(result->files_[module.sourceFile].bytes), expected),
                    "RHI-BINDLESS-PACKAGE-GPU-VISIBILITY-MODULE-PROVENANCE");
            }
            if (artifact.zeroBaseIndirectConsumerContractVersion) {
                const auto expected = artifact.execution == BindlessExecution::Native
                    ? compiler_contract::zeroBaseIndirectConsumerNativeSourceSha256
                    : compiler_contract::zeroBaseIndirectConsumerEmulatedSourceSha256;
                const auto moduleIndex = result->modules_.size();
                require(modules.size() == 2u
                        && Y::text(reflectionNode, "vertexInputContractVersion")
                            == "VertexInputInterface/1"
                        && ((moduleIndex == 0u
                                && module.entry == "arpZeroBaseIndirectConsumerVertex"
                                && stageMask == 1u)
                            || (moduleIndex == 1u
                                && module.entry == "arpZeroBaseIndirectConsumerFragment"
                                && stageMask == 2u))
                        && core::Sha256::matchesHexDigest(
                            textBytes(result->files_[module.sourceFile].bytes), expected),
                    "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-CONSUMER-PROVENANCE");
            }
            const auto& spirv = result->files_[module.spirvFile].bytes;
            require(spirv.size() >= 20u && spirv.size() % 4u == 0 && spirv[0] == 3u
                    && spirv[1] == 2u && spirv[2] == 35u && spirv[3] == 7u,
                "RHI-BINDLESS-PACKAGE-SPIRV-INVALID");
            if (budget.push) {
                const BindlessPackagePushLoadRequest::Entry* request = nullptr;
                for (std::size_t i = 0u; i < budget.push->entryCount; ++i)
                    if (budget.push->entries[i].entry == module.entry)
                        request = &budget.push->entries[i];
                require(bool(request) == bool(reflection.pushConstantSizeBytes),
                    "RHI-BINDLESS-PACKAGE-PUSH-COVERAGE");
                if (request) {
                    require(reflection.pushConstantSizeBytes == pushBytes
                            && reflection.pushConstantContractVersion == "AtomicFullBlockPush/1"
                            && parseShaderReflectionPushConstantStages(
                                   reflection.pushConstantStages)
                                == static_cast<ShaderStage>(stageMask)
                            && (pushStages & stageMask) == stageMask,
                        "RHI-BINDLESS-PACKAGE-PUSH-REFLECTION");
                    if (artifact.execution == BindlessExecution::Emulated) {
                        const auto& push = Y::required(payload, "pushConstants");
                        require(reflection.pushConstantWebGpuBindingGroup
                                    == Y::uint32(push, "webGpuGroup")
                                && reflection.pushConstantWebGpuBindingIndex
                                    == Y::uint32(push, "webGpuBinding"),
                            "RHI-BINDLESS-PACKAGE-PUSH-TARGET-BINDING");
                    }
                    shader::BufferDeclarationTypeLayouts types;
                    core::pmr::Vector<shader::SpirvBufferDeclarationLayout> buffers{
                        &core::defaultCpuMemoryResource()};
                    shader::SpirvPushConstantDeclarationLayout push;
                    if (const auto* id =
                            shader::SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(
                                spirv.data(),
                                spirv.size(),
                                budget.maxBindings,
                                request->sourceTypes,
                                buffers,
                                types,
                                push))
                        throw Rejection{id};
                    if (const auto* id = shader::TargetBufferMemoryDeclarations::pushBinaryAbi(
                            types, push, reflection, request->binary))
                        throw Rejection{id};
                    require(push.present && push.occupiedExtentBytes == pushBytes,
                        "RHI-BINDLESS-PACKAGE-PUSH-BINARY-SIZE");
                    module.push = {
                        pushBytes, push.occupiedExtentBytes, static_cast<ShaderStage>(stageMask)};
                    if (const auto* target = request->target) {
                        require(artifact.execution == BindlessExecution::Emulated,
                            "RHI-BINDLESS-PACKAGE-PUSH-TARGET-EXECUTION");
                        require(target->push && target->pushBinary == request->binary,
                            "RHI-BINDLESS-PACKAGE-PUSH-TARGET-REQUEST");
                        shader::TargetBufferMemoryDeclarations declarations;
                        if (const auto* id = declarations.build(spirv,
                                textBytes(result->files_[module.wgslFile].bytes),
                                reflection,
                                buffers,
                                budget.maxBindings,
                                *target))
                            throw Rejection{id};
                        if (const auto* id = declarations.lookupPush(module.targetPush))
                            throw Rejection{id};
                    }
                    observedPushStages |= stageMask;
                    ++observedPushEntries;
                }
            }
            core::pmr::Vector<std::uint64_t> moduleBindings{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> ordinaryBufferLayouts{
                &core::defaultCpuMemoryResource()};
            core::pmr::Vector<shader::WgslBufferDeclarationLayout> ordinaryWgslLayouts{
                &core::defaultCpuMemoryResource()};
            core::pmr::Vector<shader::WgslOpaqueDeclaration> opaqueWgslLayouts{
                &core::defaultCpuMemoryResource()};
            shader::BufferDeclarationTypeLayouts ordinarySourceTypes, ordinaryTargetTypes;
            bool ordinaryBuffersInspected = false;
            bool opaqueWgslInspected = false;
            if (payload.contains("vectorRecordContractVersion")) {
                using Graph = shader::BufferDeclarationTypeLayouts;
                using Record = shader::BufferVectorRecordContract;
                Graph sourceTypes, targetTypes;
                core::pmr::Vector<shader::SpirvBufferDeclarationLayout> sourceLayouts{
                    &core::defaultCpuMemoryResource()};
                require(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(spirv.data(),
                            spirv.size(),
                            budget.maxBindings,
                            {spirv.size() / 4u, spirv.size() / 4u, budget.maxBindings},
                            sourceLayouts,
                            sourceTypes),
                    "RHI-BINDLESS-PACKAGE-RECORD-BINARY");
                if (artifact.execution == BindlessExecution::Emulated) {
                    core::pmr::Vector<shader::WgslBufferDeclarationLayout> targetLayouts{
                        &core::defaultCpuMemoryResource()};
                    const auto wgsl = textBytes(result->files_[module.wgslFile].bytes);
                    require(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(wgsl,
                                budget.maxBindings,
                                {wgsl.size(), wgsl.size(), budget.maxBindings},
                                targetLayouts,
                                targetTypes),
                        "RHI-BINDLESS-PACKAGE-RECORD-WGSL");
                }
                for (const auto& tableNode : Y::sequence(payload, "tables")) {
                    if (!tableNode.contains("bufferLayout"))
                        continue;
                    const auto& shape = Y::required(tableNode, "bufferLayout");
                    if (!shape.contains("record"))
                        continue;
                    const auto first = Y::uint32(tableNode, "firstBinding");
                    const auto capacity = Y::uint32(tableNode, "capacity");
                    core::pmr::Vector<Record::Field> fields{&core::defaultCpuMemoryResource()};
                    for (const auto& field : Y::sequence(Y::required(shape, "record"), "fields")) {
                        const auto type = Y::text(field, "type");
                        fields.push_back({type == "float4" ? Graph::Scalar::FloatingPoint
                                : type == "uint4"          ? Graph::Scalar::UnsignedInteger
                                                           : Graph::Scalar::SignedInteger});
                    }
                    for (const auto& active : reflection.bindings) {
                        if (active.set != artifact.set || active.binding < first
                            || active.binding - first
                                >= (artifact.execution == BindlessExecution::Native ? 1u
                                                                                    : capacity))
                            continue;
                        const bool uniform = active.type == shader::BindingType::UniformBuffer;
                        auto root = Graph::absent;
                        for (std::size_t i = 0u; i < sourceTypes.roots().size(); ++i)
                            if (sourceTypes.roots()[i].set == artifact.set
                                && sourceTypes.roots()[i].binding == active.binding)
                                root = i;
                        require(root != Graph::absent
                                && sourceTypes.roots()[root].descriptorCount == active.arrayCount
                                && !Record::inspect(
                                    sourceTypes, root, uniform, true, fields.data(), fields.size()),
                            "RHI-BINDLESS-PACKAGE-RECORD-BINARY-TYPE");
                        if (artifact.execution == BindlessExecution::Emulated) {
                            root = Graph::absent;
                            for (const auto& physical : reflection.webGpuBindingSpace) {
                                if (!physical.active || physical.set != artifact.set
                                    || physical.binding != active.binding)
                                    continue;
                                for (std::size_t i = 0u; i < targetTypes.roots().size(); ++i)
                                    if (targetTypes.roots()[i].set == physical.group
                                        && targetTypes.roots()[i].binding == physical.webGpuBinding)
                                        root = i;
                            }
                            const auto storage = uniform ? Graph::Storage::Uniform
                                : active.type == shader::BindingType::StorageBuffer
                                ? Graph::Storage::StorageReadWrite
                                : Graph::Storage::StorageRead;
                            require(root != Graph::absent
                                    && targetTypes.roots()[root].storage == storage
                                    && !Record::inspect(targetTypes,
                                        root,
                                        uniform,
                                        false,
                                        fields.data(),
                                        fields.size()),
                                "RHI-BINDLESS-PACKAGE-RECORD-WGSL-TYPE");
                        }
                    }
                }
            }
            for (const auto& active : reflection.bindings) {
                require(
                    (active.set == artifact.set || artifact.pipelineInterfaceContractVersion == 1u)
                        && active.stageMask == stageMask,
                    "RHI-BINDLESS-PACKAGE-BINDING-INVALID");
                const auto key = (std::uint64_t(active.set) << 32u) | active.binding;
                require(active.arrayCount
                        && std::find(moduleBindings.begin(), moduleBindings.end(), key)
                            == moduleBindings.end(),
                    "RHI-BINDLESS-PACKAGE-BINDING-INVALID");
                moduleBindings.push_back(key);
                if (active.set != artifact.set) {
                    const auto type = descriptorType(active.type);
                    const bool buffer = type == DescriptorType::UniformBuffer
                        || type == DescriptorType::StorageBuffer
                        || type == DescriptorType::ReadOnlyStorageBuffer;
                    DescriptorBindingDesc declared;
                    declared.binding = active.binding;
                    declared.type = type;
                    declared.arrayCount = active.arrayCount;
                    require((!buffer || !active.depthTexture)
                            && (artifact.execution != BindlessExecution::Emulated
                                || active.arrayCount == 1u),
                        "RHI-BINDLESS-PACKAGE-ORDINARY-BINDING-SHAPE");
                    if (!buffer) {
                        auto intent = std::find_if(ordinaryOpaqueBindings.begin(),
                            ordinaryOpaqueBindings.end(),
                            [&](const auto& b) {
                                return b.set == active.set && b.binding.binding == active.binding;
                            });
                        require(intent != ordinaryOpaqueBindings.end(),
                            "RHI-BINDLESS-PACKAGE-ORDINARY-SHAPE-UNAVAILABLE");
                        require(intent->name == active.name && intent->binding.type == type
                                && active.arrayCount == 1u,
                            "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-MAPPING");
                        declared = intent->binding;
                        intent->observedStages |= active.stageMask;
                        verifyOrdinaryOpaqueBinary(spirv, active.set, declared);
                        if (artifact.execution == BindlessExecution::Emulated) {
                            if (!opaqueWgslInspected) {
                                require(!shader::WgslBufferDeclarationLayoutVerifier::
                                            verifyWithOpaqueDeclarations(
                                                textBytes(result->files_[module.wgslFile].bytes),
                                                budget.maxBindings,
                                                ordinaryWgslLayouts,
                                                opaqueWgslLayouts),
                                    "RHI-BINDLESS-PACKAGE-OPAQUE-WGSL-DECLARATIONS");
                                opaqueWgslInspected = true;
                            }
                            const shader::ShaderWebGpuBindingSpaceBinding* mapped = nullptr;
                            for (const auto& candidate : reflection.webGpuBindingSpace)
                                if (candidate.active && candidate.set == active.set
                                    && candidate.binding == active.binding) {
                                    require(
                                        !mapped, "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-PHYSICAL");
                                    mapped = &candidate;
                                }
                            require(mapped && mapped->physicalType == active.type
                                    && mapped->capacity == 1u && !mapped->arrayElement
                                    && mapped->depthTexture == declared.depthTexture,
                                "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-PHYSICAL");
                            const auto target = std::find_if(opaqueWgslLayouts.begin(),
                                opaqueWgslLayouts.end(),
                                [&](const auto& b) {
                                    return b.group == mapped->group
                                        && b.binding == mapped->webGpuBinding;
                                });
                            require(target != opaqueWgslLayouts.end()
                                    && target->type == ordinaryOpaqueWgslType(declared)
                                    && target->scalar
                                        == (type == DescriptorType::SampledTexture
                                                    && !declared.depthTexture
                                                ? "f32"
                                                : "")
                                    && target->format.empty() && target->access.empty(),
                                "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-WGSL");
                        }
                    } else {
                        ++ordinaryBufferCount;
                        const shader::TargetBufferMemoryLoadRequest::BindingPlan* memoryPlan =
                            nullptr;
                        if (ordinaryRequest) {
                            for (std::size_t i = 0u; i < ordinaryRequest->bindingCount; ++i)
                                if (ordinaryRequest->bindings[i].set == active.set
                                    && ordinaryRequest->bindings[i].binding == active.binding)
                                    memoryPlan = &ordinaryRequest->bindings[i];
                            require(memoryPlan, "RHI-BINDLESS-PACKAGE-MEMORY-COVERAGE");
                        }
                        if (!ordinaryBuffersInspected) {
                            const auto sourceWords = spirv.size() / sizeof(std::uint32_t);
                            require(
                                !shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                                    spirv.data(),
                                    spirv.size(),
                                    budget.maxBindings,
                                    ordinaryRequest ? ordinaryRequest->sourceTypes
                                        : nativeRequest
                                        ? nativeRequest->sourceTypes
                                        : shader::BufferDeclarationTypeLayouts::Budget{sourceWords,
                                              sourceWords,
                                              budget.maxBindings},
                                    ordinaryBufferLayouts,
                                    ordinarySourceTypes),
                                "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-BINARY");
                            if (artifact.execution == BindlessExecution::Emulated) {
                                const auto wgsl = textBytes(result->files_[module.wgslFile].bytes);
                                require(
                                    !shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                                        wgsl,
                                        budget.maxBindings,
                                        ordinaryRequest ? ordinaryRequest->targetTypes
                                                        : shader::BufferDeclarationTypeLayouts::
                                                              Budget{wgsl.size(),
                                                                  wgsl.size(),
                                                                  budget.maxBindings},
                                        ordinaryWgslLayouts,
                                        ordinaryTargetTypes),
                                    "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-WGSL");
                            }
                            ordinaryBuffersInspected = true;
                        }
                        const auto binary = std::find_if(ordinaryBufferLayouts.begin(),
                            ordinaryBufferLayouts.end(),
                            [&](const auto& b) {
                                return b.set == active.set && b.binding == active.binding;
                            });
                        require(binary != ordinaryBufferLayouts.end()
                                && binary->descriptorCount == active.arrayCount
                                && binary->storageClass
                                    == (type == DescriptorType::UniformBuffer ? 2u : 12u),
                            "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-BINARY");
                        module.ordinaryBinaryMemory.push_back(*binary);
                        using Graph = shader::BufferDeclarationTypeLayouts;
                        auto sourceRoot = Graph::absent;
                        for (std::size_t i = 0u; i < ordinarySourceTypes.roots().size(); ++i) {
                            const auto& root = ordinarySourceTypes.roots()[i];
                            if (root.set == active.set && root.binding == active.binding)
                                sourceRoot = i;
                        }
                        require(sourceRoot != Graph::absent,
                            "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY");
                        for (const auto& previous : ordinaryModuleTypes)
                            for (const auto& root : previous.roots()) {
                                if (root.set != active.set || root.binding != active.binding)
                                    continue;
                                bool equal = false;
                                require(!Graph::compare(previous,
                                            root.type,
                                            ordinarySourceTypes,
                                            ordinarySourceTypes.roots()[sourceRoot].type,
                                            previous.nodes().size()
                                                + ordinarySourceTypes.nodes().size(),
                                            equal,
                                            Graph::Comparison::MemoryRepresentation)
                                        && equal,
                                    "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-STAGE-MEMORY");
                            }
                        if (artifact.execution == BindlessExecution::Emulated) {
                            using Address = shader::WgslBufferDeclarationLayout::AddressSpace;
                            using Lowering = shader::SpirvWgslBufferMemoryLowering;
                            const shader::ShaderWebGpuBindingSpaceBinding* mapped = nullptr;
                            for (const auto& candidate : reflection.webGpuBindingSpace)
                                if (candidate.active && candidate.set == active.set
                                    && candidate.binding == active.binding) {
                                    require(
                                        !mapped, "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-PHYSICAL");
                                    mapped = &candidate;
                                }
                            require(mapped && mapped->physicalType == active.type
                                    && mapped->capacity == active.arrayCount
                                    && !mapped->arrayElement,
                                "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-PHYSICAL");
                            const auto target = std::find_if(ordinaryWgslLayouts.begin(),
                                ordinaryWgslLayouts.end(),
                                [&](const auto& b) {
                                    return b.group == mapped->group
                                        && b.binding == mapped->webGpuBinding;
                                });
                            const auto address = type == DescriptorType::UniformBuffer
                                ? Address::Uniform
                                : type == DescriptorType::ReadOnlyStorageBuffer
                                ? Address::StorageRead
                                : Address::StorageReadWrite;
                            require(target != ordinaryWgslLayouts.end()
                                    && target->addressSpace == address,
                                "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-WGSL");
                            auto targetRoot = Graph::absent;
                            for (std::size_t i = 0u; i < ordinaryTargetTypes.roots().size(); ++i) {
                                const auto& root = ordinaryTargetTypes.roots()[i];
                                if (root.set == mapped->group
                                    && root.binding == mapped->webGpuBinding)
                                    targetRoot = i;
                            }
                            require(sourceRoot != Graph::absent && targetRoot != Graph::absent,
                                "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY");
                            const auto targetKind =
                                ordinaryTargetTypes
                                    .nodes()[ordinaryTargetTypes.roots()[targetRoot].type]
                                    .kind;
                            // Exact target declaration selects one approved relation;
                            // no retry with a looser form or inferred array wrapper.
                            const auto form = targetKind == Graph::Kind::Struct
                                ? Lowering::RootForm::BlockStruct
                                : type == DescriptorType::UniformBuffer
                                ? Lowering::RootForm::UniformValue
                                : Lowering::RootForm::StorageRuntimeArray;
                            Lowering::Facts memory;
                            const auto* memoryDiagnostic = Lowering::compare(ordinarySourceTypes,
                                sourceRoot,
                                ordinaryTargetTypes,
                                targetRoot,
                                memoryPlan ? memoryPlan->form : form,
                                ordinaryRequest ? ordinaryRequest->maxPairs
                                                : ordinarySourceTypes.nodes().size()
                                        + ordinaryTargetTypes.nodes().size(),
                                memory,
                                memoryPlan ? memoryPlan->arrays : Lowering::Plan{nullptr, 0u});
                            require(!memoryDiagnostic,
                                ordinaryRequest ? memoryDiagnostic
                                                : "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY");
                            module.ordinaryMemory.push_back(
                                {active.set, active.binding, active.type, *target, *binary});
                        }
                    }
                    auto domain = std::find_if(artifact.additionalDomains.begin(),
                        artifact.additionalDomains.end(),
                        [&](const auto& d) {
                            return d.set == active.set;
                        });
                    if (domain == artifact.additionalDomains.end()) {
                        require(artifact.additionalDomains.size() < budget.maxBindings,
                            "RHI-BINDLESS-PACKAGE-BINDING-BUDGET");
                        BindlessPipelineDescriptorDomain added;
                        added.set = active.set;
                        added.layout.name = artifact.physicalLayout.name;
                        char suffix[16];
                        const auto end =
                            std::to_chars(suffix, suffix + sizeof(suffix), active.set).ptr;
                        added.layout.name += ".set";
                        added.layout.name.append(suffix, end);
                        artifact.additionalDomains.push_back(std::move(added));
                        domain = artifact.additionalDomains.end() - 1u;
                    }
                    auto physical = std::find_if(domain->layout.bindings.begin(),
                        domain->layout.bindings.end(),
                        [&](const auto& b) {
                            return b.binding == active.binding;
                        });
                    if (physical == domain->layout.bindings.end()) {
                        require(
                            artifact.physicalLayout.bindings.size() + ordinaryDomainBindings.size()
                                < budget.maxBindings,
                            "RHI-BINDLESS-PACKAGE-BINDING-BUDGET");
                        declared.stages = ShaderStage::None;
                        domain->layout.bindings.push_back(declared);
                        physical = domain->layout.bindings.end() - 1u;
                        ordinaryDomainBindings.push_back({active.set,
                            active.binding,
                            core::pmr::String{active.name, &core::defaultCpuMemoryResource()}});
                    }
                    const auto named = std::find_if(ordinaryDomainBindings.begin(),
                        ordinaryDomainBindings.end(),
                        [&](const auto& b) {
                            return b.set == active.set && b.binding == active.binding;
                        });
                    require(named != ordinaryDomainBindings.end() && named->name == active.name
                            && physical->type == type && physical->arrayCount == active.arrayCount,
                        "RHI-BINDLESS-PACKAGE-PHYSICAL-MAPPING-MISMATCH");
                    physical->stages =
                        physical->stages | static_cast<ShaderStage>(active.stageMask);
                    continue;
                }
                auto found = artifact.physicalLayout.bindings.size();
                for (std::size_t i = 0; i < artifact.physicalLayout.bindings.size(); ++i)
                    if (artifact.physicalLayout.bindings[i].binding == active.binding) {
                        found = i;
                        break;
                    }
                if (found == artifact.physicalLayout.bindings.size()) {
                    require(found + ordinaryDomainBindings.size() < budget.maxBindings,
                        "RHI-BINDLESS-PACKAGE-BINDING-BUDGET");
                    DescriptorBindingDesc ordinary;
                    ordinary.binding = active.binding;
                    ordinary.type = descriptorType(active.type);
                    ordinary.arrayCount = active.arrayCount;
                    ordinary.stages = static_cast<ShaderStage>(active.stageMask);
                    // Ordinary texture shape/access metadata is not in the
                    // closed publication schema yet; do not guess it.
                    require(ordinary.type == DescriptorType::UniformBuffer
                            || ordinary.type == DescriptorType::StorageBuffer
                            || ordinary.type == DescriptorType::ReadOnlyStorageBuffer,
                        "RHI-BINDLESS-PACKAGE-ORDINARY-SHAPE-UNAVAILABLE");
                    artifact.physicalLayout.bindings.push_back(ordinary);
                    names.push_back(active.name);
                    observedStages.push_back(0u);
                }
                auto& physical = artifact.physicalLayout.bindings[found];
                require(names[found] == active.name && physical.type == descriptorType(active.type)
                        && physical.arrayCount == active.arrayCount,
                    "RHI-BINDLESS-PACKAGE-PHYSICAL-MAPPING-MISMATCH");
                require(physical.depthTexture == active.depthTexture,
                    "RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-REFLECTION");
                if (physical.type == DescriptorType::Sampler) {
                    std::uint32_t descriptorCount = 0u;
                    require(!shader::SpirvImageDeclarationVerifier::inspectSampler(spirv.data(),
                                spirv.size(),
                                artifact.set,
                                physical.binding,
                                descriptorCount)
                            && descriptorCount == physical.arrayCount,
                        "RHI-BINDLESS-PACKAGE-SAMPLER-SHAPE-MISMATCH");
                    if (artifact.execution == BindlessExecution::Emulated) {
                        if (!opaqueWgslInspected) {
                            require(!shader::WgslBufferDeclarationLayoutVerifier::
                                        verifyWithOpaqueDeclarations(
                                            textBytes(result->files_[module.wgslFile].bytes),
                                            budget.maxBindings,
                                            ordinaryWgslLayouts,
                                            opaqueWgslLayouts),
                                "RHI-BINDLESS-PACKAGE-OPAQUE-WGSL-DECLARATIONS");
                            opaqueWgslInspected = true;
                        }
                        const auto target = std::find_if(
                            opaqueWgslLayouts.begin(), opaqueWgslLayouts.end(), [&](const auto& b) {
                                return b.group == artifact.set && b.binding == physical.binding;
                            });
                        require(target != opaqueWgslLayouts.end()
                                && target->type
                                    == (physical.comparisonSampler ? "sampler_comparison"
                                                                   : "sampler"),
                            "RHI-BINDLESS-PACKAGE-SAMPLER-WGSL-MISMATCH");
                    }
                }
                if (physical.textureViewDimensionConstrained) {
                    shader::SpirvImageDeclaration image;
                    require(!shader::SpirvImageDeclarationVerifier::inspect(
                                spirv.data(), spirv.size(), artifact.set, physical.binding, image)
                            && image.descriptorCount == physical.arrayCount && image.dimension == 1u
                            && (physical.depthTexture ? image.depth == 1u
                                                      : image.depth == 0u || image.depth == 2u)
                            && image.arrayed
                                == (physical.textureViewDimension
                                            == TextureViewDimension::Texture2DArray
                                        ? 1u
                                        : 0u)
                            && image.multisampled == 0u && image.floatScalar
                            && image.scalarWidth == 32u
                            && image.sampled
                                == (physical.type == DescriptorType::StorageTexture ? 2u : 1u)
                            && image.format
                                == (physical.type == DescriptorType::StorageTexture
                                        ? BindlessStorageImageDomain::declaration(
                                              physical.storageTextureFormat, storageImageVersion)
                                              .spirvImageFormat
                                        : 0u),
                        "RHI-BINDLESS-PACKAGE-IMAGE-SHAPE-MISMATCH");
                    if (physical.type == DescriptorType::StorageTexture
                        && artifact.execution == BindlessExecution::Emulated) {
                        if (!opaqueWgslInspected) {
                            require(!shader::WgslBufferDeclarationLayoutVerifier::
                                        verifyWithOpaqueDeclarations(
                                            textBytes(result->files_[module.wgslFile].bytes),
                                            budget.maxBindings,
                                            ordinaryWgslLayouts,
                                            opaqueWgslLayouts),
                                "RHI-BINDLESS-PACKAGE-OPAQUE-WGSL-DECLARATIONS");
                            opaqueWgslInspected = true;
                        }
                        const auto target = std::find_if(
                            opaqueWgslLayouts.begin(), opaqueWgslLayouts.end(), [&](const auto& b) {
                                return b.group == artifact.set && b.binding == physical.binding;
                            });
                        require(target != opaqueWgslLayouts.end()
                                && target->type == "texture_storage_2d"
                                && target->format
                                    == BindlessStorageImageDomain::declaration(
                                        physical.storageTextureFormat, storageImageVersion)
                                        .wgslFormat
                                && target->access
                                    == (physical.storageTextureAccess
                                                == StorageTextureAccess::WriteOnly
                                            ? "write"
                                            : "read_write"),
                            "RHI-BINDLESS-PACKAGE-STORAGE-IMAGE-WGSL-MISMATCH");
                    }
                }
                observedStages[found] |= active.stageMask;
                if (found >= indexedBindings)
                    physical.stages = static_cast<ShaderStage>(observedStages[found]);
            }
            require(!ordinaryRequest || ordinaryBufferCount == ordinaryRequest->bindingCount,
                "RHI-BINDLESS-PACKAGE-MEMORY-COVERAGE");
            require(!nativeRequest || ordinaryBufferCount == nativeRequest->bindingCount,
                "RHI-BINDLESS-PACKAGE-MEMORY-NATIVE-COVERAGE");
            if (nativeRequest)
                nativeSourceRetentions.push_back(
                    {result->modules_.size(), ordinaryModuleTypes.size(), nativeRequest});
            if (ordinaryBuffersInspected)
                ordinaryModuleTypes.push_back(std::move(ordinarySourceTypes));
            module.reflection = std::move(reflected.module);
            result->modules_.push_back(std::move(module));
        }
        for (const auto& retained : nativeSourceRetentions) {
            require(retained.graph < ordinaryModuleTypes.size(),
                "RHI-BINDLESS-PACKAGE-MEMORY-NATIVE-SOURCE");
            if (const auto* id = result->modules_[retained.module].nativeMemory.build(
                    std::move(ordinaryModuleTypes[retained.graph]),
                    *retained.request,
                    budget.maxBindings))
                throw Rejection{id};
            for (const auto& binary : result->modules_[retained.module].ordinaryBinaryMemory)
                require(result->modules_[retained.module].nativeMemory.lookup(
                            binary.set, binary.binding),
                    "RHI-BINDLESS-PACKAGE-MEMORY-NATIVE-COVERAGE");
        }
        require(!budget.push
                || (observedPushStages == pushStages
                    && observedPushEntries == budget.push->entryCount),
            "RHI-BINDLESS-PACKAGE-PUSH-COVERAGE");
        for (std::size_t i = 0; i < indexedBindings; ++i)
            require(observedStages[i]
                    == static_cast<std::uint32_t>(artifact.physicalLayout.bindings[i].stages),
                "RHI-BINDLESS-PACKAGE-STAGE-COVERAGE-MISMATCH");
        for (const auto& intent : ordinaryOpaqueBindings)
            require(intent.observedStages == static_cast<std::uint32_t>(intent.binding.stages),
                "RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-STAGES");
        require(std::all_of(referenced.begin(),
                    referenced.end(),
                    [](bool value) {
                        return value;
                    }),
            "RHI-BINDLESS-PACKAGE-UNREFERENCED-FILE");
        return result;
    } catch (const Rejection& error) {
        diagnostic = error.diagnostic;
    } catch (const core::YamlContractError&) {
        diagnostic = "RHI-BINDLESS-PACKAGE-SCHEMA-INVALID";
    } catch (const std::exception&) {
        diagnostic = "RHI-BINDLESS-PACKAGE-LOAD-FAILED";
    }
    return {};
}

const char* BindlessShaderPackage::originalPushDeclaration(
    const std::shared_ptr<const BindlessShaderPackage>& package,
    core::StringView entry,
    BindlessPackagePushDeclaration& output,
    std::uint32_t version) noexcept
{
    output = {};
    if (version != 1u)
        return "RHI-BINDLESS-PACKAGE-PUSH-VERSION";
    if (!package || !package->sharedOwnerMatches(package))
        return "RHI-BINDLESS-PACKAGE-PUSH-ORIGINAL-OWNER";
    for (const auto& module : package->modules_)
        if (module.entry == entry) {
            if (!module.push.sizeBytes)
                return "RHI-BINDLESS-PACKAGE-PUSH-ABSENT";
            output = module.push;
            return nullptr;
        }
    return "RHI-BINDLESS-PACKAGE-PUSH-ENTRY";
}

const char* BindlessShaderPackage::originalTargetPushMemory(
    const std::shared_ptr<const BindlessShaderPackage>& package,
    core::StringView entry,
    shader::TargetPushMemoryLayout& output,
    std::uint32_t version) noexcept
{
    output = {};
    BindlessPackagePushDeclaration source;
    if (const auto* id = originalPushDeclaration(package, entry, source, version))
        return id;
    for (const auto& module : package->modules_)
        if (module.entry == entry) {
            if (!module.targetPush.targetDeclarationBytes)
                return "RHI-BINDLESS-PACKAGE-PUSH-TARGET-NOT-REQUESTED";
            output = module.targetPush;
            return nullptr;
        }
    return "RHI-BINDLESS-PACKAGE-PUSH-ENTRY";
}

const char* BindlessShaderPackage::originalOrdinaryBufferMemory(
    const std::shared_ptr<const BindlessShaderPackage>& package,
    core::StringView entry,
    std::uint32_t set,
    std::uint32_t binding,
    shader::TargetBufferMemoryLayout& output) noexcept
{
    output = {};
    if (!package || !package->sharedOwnerMatches(package))
        return "RHI-BINDLESS-PACKAGE-MEMORY-ORIGINAL-OWNER";
    for (const auto& module : package->modules_)
        if (module.entry == entry) {
            for (const auto& facts : module.ordinaryMemory)
                if (facts.set == set && facts.binding == binding) {
                    output = facts;
                    return nullptr;
                }
            return "RHI-BINDLESS-PACKAGE-MEMORY-BINDING";
        }
    return "RHI-BINDLESS-PACKAGE-MEMORY-ENTRY";
}

const char* BindlessShaderPackage::originalNativeBufferMemory(
    const std::shared_ptr<const BindlessShaderPackage>& package,
    const BindlessPackageModule* selected,
    std::uint32_t set,
    std::uint32_t binding,
    const shader::NativeBufferMemoryDeclarations*& output) noexcept
{
    output = nullptr;
    if (!package || !package->sharedOwnerMatches(package))
        return "RHI-BINDLESS-PACKAGE-MEMORY-ORIGINAL-OWNER";
    for (const auto& module : package->modules_)
        if (&module == selected) {
            if (package->artifact_.execution != BindlessExecution::Native
                || !module.nativeMemory.lookup(set, binding))
                return "RHI-BINDLESS-PACKAGE-MEMORY-NATIVE-BINDING";
            output = &module.nativeMemory;
            return nullptr;
        }
    return "RHI-BINDLESS-PACKAGE-MEMORY-NATIVE-ENTRY";
}

const char* BindlessGraphicsPipelineEntries::capture(const BindlessShaderPackage& package,
    const RenderPipelineDesc& desc,
    BindlessGraphicsPipelineEntries& output) noexcept
{
    output = {};
    if (desc.vertexShader.stage != ShaderStage::Vertex
        || (!desc.fragmentShader.entryPoint.empty()
            && desc.fragmentShader.stage != ShaderStage::Fragment))
        return "RHI-BINDLESS-GRAPHICS-ENTRY-STAGE";
    BindlessGraphicsPipelineEntries candidate;
    for (const auto& module : package.modules()) {
        const auto& reflected = module.reflection.reflection;
        if (reflected.entryPoints.size() != 1u
            || reflected.entryPoints.front().name != module.entry)
            return "RHI-BINDLESS-GRAPHICS-ENTRY-MODULE";
        const auto stage = reflected.entryPoints.front().stage;
        if (module.entry == desc.vertexShader.entryPoint && stage == shader::ShaderStage::Vertex) {
            if (candidate.vertex_)
                return "RHI-BINDLESS-GRAPHICS-ENTRY-AMBIGUOUS";
            candidate.vertex_ = &module;
        }
        if (!desc.fragmentShader.entryPoint.empty()
            && module.entry == desc.fragmentShader.entryPoint
            && stage == shader::ShaderStage::Fragment) {
            if (candidate.fragment_)
                return "RHI-BINDLESS-GRAPHICS-ENTRY-AMBIGUOUS";
            candidate.fragment_ = &module;
        }
    }
    if (!candidate.vertex_ || (!desc.fragmentShader.entryPoint.empty() && !candidate.fragment_))
        return "RHI-BINDLESS-GRAPHICS-ENTRY-MODULE";
    if (candidate.vertex_->sourceFile >= package.files().size())
        return "RHI-BINDLESS-GRAPHICS-ENTRY-SOURCE";
    // appendBindlessShaderEntry uses the first selected source as the canonical
    // L2 module path. Each retained module still has its own authenticated bytes.
    const auto& path = package.files()[candidate.vertex_->sourceFile].name;
    if (desc.vertexShader.modulePath != path
        || (candidate.fragment_ && desc.fragmentShader.modulePath != path))
        return "RHI-BINDLESS-GRAPHICS-ENTRY-SOURCE";
    output = candidate;
    return nullptr;
}

const char* BindlessGraphicsPipelineEntries::bufferRequirement(ShaderStage stage,
    std::uint32_t set,
    std::uint32_t binding,
    bool equivalentTarget,
    ShaderBufferMemoryRequirement& output,
    std::uint32_t version) const noexcept
{
    output = {};
    if (version != 1u && version != 2u)
        return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
    const auto* selected = module(stage, 1u);
    if (!selected)
        return "RHI-BINDLESS-ORDINARY-RANGE-STAGE";
    const shader::SpirvBufferDeclarationLayout* source = nullptr;
    for (const auto& facts : selected->ordinaryBinaryMemory)
        if (facts.set == set && facts.binding == binding) {
            if (source)
                return "RHI-BINDLESS-ORDINARY-RANGE-DUPLICATE";
            source = &facts;
        }
    if (!source)
        return "RHI-BINDLESS-ORDINARY-RANGE-BINDING";
    if ((source->hasRuntimeArray && version == 1u) || source->descriptorCount != 1u
        || (!source->hasRuntimeArray && !source->staticOccupiedExtentBytes))
        return "RHI-BINDLESS-ORDINARY-RANGE-DOMAIN";
    ShaderBufferMemoryRequirement result;
    result.sourceOccupiedBytes = source->staticOccupiedExtentBytes;
    result.sourceMinimumBytes = result.sourceOccupiedBytes;
    if (source->hasRuntimeArray) {
        if (!source->runtimeArrayStrideBytes || !source->runtimeElementOccupiedExtentBytes
            || source->runtimeElementOccupiedExtentBytes > source->runtimeArrayStrideBytes
            || source->runtimeArrayStrideBytes > UINT64_MAX - source->runtimeArrayOffsetBytes)
            return "RHI-BINDLESS-ORDINARY-RANGE-DOMAIN";
        result.hasRuntimeArray = true;
        result.sourceRuntimeArrayOffsetBytes = source->runtimeArrayOffsetBytes;
        result.sourceRuntimeArrayStrideBytes = source->runtimeArrayStrideBytes;
        result.sourceMinimumBytes =
            source->runtimeArrayOffsetBytes + source->runtimeArrayStrideBytes;
        result.targetRuntimeArrayOffsetBytes = source->runtimeArrayOffsetBytes;
        result.targetRuntimeArrayStrideBytes = source->runtimeArrayStrideBytes;
    }
    result.minimumBytes = result.sourceMinimumBytes;
    result.evidence = ShaderBufferMemoryRequirement::Evidence::BinaryDeclaration;
    if (equivalentTarget) {
        const shader::TargetBufferMemoryLayout* target = nullptr;
        for (const auto& facts : selected->ordinaryMemory)
            if (facts.set == set && facts.binding == binding) {
                if (target)
                    return "RHI-BINDLESS-ORDINARY-RANGE-DUPLICATE";
                target = &facts;
            }
        if (!target || !target->source.matches(*source) || !target->target.minimumBindingBytes
            || target->target.hasRuntimeArray != source->hasRuntimeArray
            || (source->hasRuntimeArray && !target->target.runtimeArrayStrideBytes))
            return "RHI-BINDLESS-ORDINARY-RANGE-TARGET";
        result.minimumBytes = target->target.minimumBindingBytes;
        result.alignmentBytes = target->target.alignmentBytes;
        result.targetRuntimeArrayOffsetBytes = target->target.runtimeArrayOffsetBytes;
        result.targetRuntimeArrayStrideBytes = target->target.runtimeArrayStrideBytes;
        result.evidence = ShaderBufferMemoryRequirement::Evidence::ValidatedTargetDeclaration;
    }
    output = result;
    return nullptr;
}

const char* BindlessGraphicsPipelineEntries::bufferMemberMemory(ShaderStage stage,
    std::uint32_t set,
    std::uint32_t binding,
    bool nativeBinaryIdentity,
    ShaderBufferMemberMemory& output,
    std::uint32_t version) const noexcept
{
    output = {};
    if (version != 1u && version != 2u)
        return "RHI-BINDLESS-MEMBER-MEMORY-VERSION";
    ShaderBufferMemoryRequirement requirement;
    if (const auto* id =
            bufferRequirement(stage, set, binding, !nativeBinaryIdentity, requirement, version))
        return id;
    ShaderBufferMemberMemory candidate{requirement.sourceOccupiedBytes,
        requirement.minimumBytes,
        nativeBinaryIdentity ? ShaderBufferMemberMemory::Evidence::NativeBinaryIdentity
                             : ShaderBufferMemberMemory::Evidence::ValidatedTargetMembers};
    if (version == 2u) {
        candidate.sourceMinimumBytes = requirement.sourceMinimumBytes;
        candidate.hasRuntimeArray = requirement.hasRuntimeArray;
        candidate.sourceRuntimeArrayOffsetBytes = requirement.sourceRuntimeArrayOffsetBytes;
        candidate.sourceRuntimeArrayStrideBytes = requirement.sourceRuntimeArrayStrideBytes;
        candidate.targetRuntimeArrayOffsetBytes = requirement.targetRuntimeArrayOffsetBytes;
        candidate.targetRuntimeArrayStrideBytes = requirement.targetRuntimeArrayStrideBytes;
    }
    output = candidate;
    return nullptr;
}

semantics::BindlessInitializedGraphicsContract::Selection selectBindlessInitializedGraphics(
    const BindlessShaderPackage& package,
    core::StringView vertex,
    core::StringView fragment) noexcept
{
    const BindlessPackageModule *v = nullptr, *f = nullptr;
    for (const auto& module : package.modules()) {
        if (module.entry == vertex)
            v = &module;
        if (module.entry == fragment)
            f = &module;
    }
    if (v && !f && fragment.empty() && v->initializedGraphics.empty())
        return {};
    if (!v || !f)
        return {false, "RHI-BINDLESS-INITIALIZED-GRAPHICS-ENTRY"};
    if (v->reflection.reflection.entryPoints.size() != 1u
        || f->reflection.reflection.entryPoints.size() != 1u
        || v->reflection.reflection.entryPoints[0].stage != shader::ShaderStage::Vertex
        || f->reflection.reflection.entryPoints[0].stage != shader::ShaderStage::Fragment)
        return {false, "RHI-BINDLESS-INITIALIZED-GRAPHICS-STAGE"};
    return semantics::BindlessInitializedGraphicsContract::select(
        v->initializedGraphics, f->initializedGraphics);
}

const BindlessPackageModule* selectBindlessGraphicsEntryAccess(const BindlessShaderPackage& package,
    const RenderPipeline& pipeline,
    core::StringView entry,
    ShaderStage& selectedStage,
    const char*& diagnostic) noexcept
{
    selectedStage = ShaderStage::None;
    diagnostic = nullptr;
    const auto fail = [&](const char* id) -> const BindlessPackageModule* {
        diagnostic = id;
        return nullptr;
    };
    const auto decision = semantics::OriginalBindlessGraphicsEntrySelectionLowering::lowering(
        pipeline.validationReport().backend);
    if (!decision.inspectionAvailable || decision.diagnostic)
        return fail(decision.diagnostic);
    const auto authorization = pipeline.bindlessAuthorization();
    if (!authorization || !authorization->current(authorization)
        || authorization->originalPackageOwner().get() != &package)
        return fail("RHI-BINDLESS-GRAPHICS-ENTRY-AUTHORIZATION");
    const auto stages = pipeline.bindlessShaderStages();
    if (stages != ShaderStage::Vertex && stages != (ShaderStage::Vertex | ShaderStage::Fragment))
        return fail("RHI-BINDLESS-GRAPHICS-ENTRY-STAGE");
    const BindlessPackageModule* selected = nullptr;
    ShaderStage candidateStage = ShaderStage::None;
    for (const auto stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
        const auto* module = pipeline.bindlessShaderModule(stage, 1u);
        if ((stages & stage) == ShaderStage::None) {
            if (module)
                return fail("RHI-BINDLESS-GRAPHICS-ENTRY-STAGE");
            continue;
        }
        if (!module)
            return fail("RHI-BINDLESS-GRAPHICS-ENTRY-UNAVAILABLE");
        bool owned = false;
        for (const auto& original : package.modules())
            owned = owned || &original == module;
        if (!owned)
            return fail("RHI-BINDLESS-GRAPHICS-ENTRY-OWNER");
        const auto& reflected = module->reflection.reflection;
        const auto expected = stage == ShaderStage::Vertex ? shader::ShaderStage::Vertex
                                                           : shader::ShaderStage::Fragment;
        if (reflected.entryPoints.size() != 1u || reflected.entryPoints.front().stage != expected
            || reflected.entryPoints.front().name != module->entry)
            return fail("RHI-BINDLESS-GRAPHICS-ENTRY-MODULE");
        if (module->entry == entry) {
            if (selected)
                return fail("RHI-BINDLESS-GRAPHICS-ENTRY-AMBIGUOUS");
            selected = module;
            candidateStage = stage;
        }
    }
    if (!selected)
        return fail("RHI-BINDLESS-ORDINARY-MEMORY-ENTRY");
    selectedStage = candidateStage;
    return selected;
}

const BindlessPackageModule* selectBindlessComputeEntryAccess(const BindlessShaderPackage& package,
    const ShaderReflectionDesc& issued,
    const char*& diagnostic) noexcept
{
    diagnostic = nullptr;
    const BindlessPackageModule* selected = nullptr;
    for (const auto& module : package.modules()) {
        const auto& reflected = module.reflection.reflection;
        if (reflected.entryPoints.size() != 1u
            || reflected.entryPoints.front().stage != shader::ShaderStage::Compute
            || reflected.entryPoints.front().name != module.entry
            || reflected.sourceSha256 != issued.sourceSha256
            || reflected.reflectionContractVersion != issued.reflectionContractVersion
            || reflected.bindlessContractVersion != issued.bindlessContractVersion
            || reflected.bindlessExecution != issued.bindlessExecution
            || reflected.bindlessLoweringSha256 != issued.bindlessLoweringSha256
            || reflected.entryPoints.front().computeWorkgroupSizeX != issued.computeWorkgroupSizeX
            || reflected.entryPoints.front().computeWorkgroupSizeY != issued.computeWorkgroupSizeY
            || reflected.entryPoints.front().computeWorkgroupSizeZ != issued.computeWorkgroupSizeZ
            || reflected.bindings.size() != issued.descriptorBindings.size())
            continue;
        bool matches = true;
        for (const auto& logical : reflected.bindings) {
            const auto* layout = semantics::BindlessPipelineInterfaceContract::descriptorLayout(
                package.artifact(), logical.set);
            const DescriptorBindingDesc* physical = nullptr;
            if (layout) {
                const auto found = std::find_if(
                    layout->bindings.begin(), layout->bindings.end(), [&](const auto& candidate) {
                        return candidate.binding == logical.binding;
                    });
                if (found != layout->bindings.end())
                    physical = &*found;
            }
            if (!physical
                || logical.stageMask != static_cast<std::uint32_t>(ShaderStage::Compute)) {
                matches = false;
                break;
            }
            const auto expected = makeShaderReflectionBinding(logical.set, *physical);
            std::uint32_t count = 0u;
            for (const auto& actual : issued.descriptorBindings)
                if (actual.set == logical.set && actual.binding == logical.binding) {
                    if (actual.type != expected.type || actual.arrayCount != expected.arrayCount
                        || actual.stages != ShaderStage::Compute
                        || actual.depthTexture != expected.depthTexture
                        || actual.subpassAttachmentIndex != expected.subpassAttachmentIndex) {
                        matches = false;
                        break;
                    }
                    ++count;
                }
            if (count != 1u) {
                matches = false;
                break;
            }
        }
        if (!matches)
            continue;
        if (selected) {
            diagnostic = "RHI-BINDLESS-ENTRY-ACCESS-AMBIGUOUS";
            return nullptr;
        }
        selected = &module;
    }
    if (!selected)
        diagnostic = "RHI-BINDLESS-ENTRY-ACCESS-INTERFACE";
    return selected;
}

namespace {
bool appendBindlessShaderEntryImpl(const BindlessShaderPackage& package,
    core::StringView entry,
    ShaderStage stage,
    ShaderPackageDesc& view,
    core::pmr::String& diagnostic,
    const BindlessPackageModule** selectedModule,
    bool declarationOnly)
{
    if (selectedModule)
        *selectedModule = nullptr;
    if (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment
        && stage != ShaderStage::Compute) {
        diagnostic = "RHI-BINDLESS-PIPELINE-ENTRY-MISMATCH";
        return false;
    }
    const BindlessPackageModule* selected = nullptr;
    for (const auto& module : package.modules())
        if (module.entry == entry)
            selected = &module;
    const auto expected = stage == ShaderStage::Compute ? shader::ShaderStage::Compute
        : stage == ShaderStage::Vertex                  ? shader::ShaderStage::Vertex
                                                        : shader::ShaderStage::Fragment;
    if (!selected || selected->reflection.reflection.entryPoints[0].stage != expected) {
        diagnostic = "RHI-BINDLESS-PIPELINE-ENTRY-MISMATCH";
        return false;
    }
    const auto& reflected = selected->reflection.reflection;
    // An unrelated entry's push declaration cannot become this entry's ABI.
    // The selected push consumer still needs the authorized composition path.
    if (selected->push.sizeBytes && !declarationOnly) {
        diagnostic = "RHI-BINDLESS-PIPELINE-PUSH-EXECUTION-UNAVAILABLE";
        return false;
    }
    const auto& artifact = package.artifact();
    // Bindless publications retain a separately compiled target module per
    // entry. The L2 package view has one source identity, so use the first
    // retained source as its canonical path and merge the remaining entries
    // by reflection; the sealed package remains authoritative for each target.
    if (view.slangModulePath.empty())
        view.slangModulePath = package.files()[selected->sourceFile].name;
    view.artifacts.spirvPath = package.files()[selected->spirvFile].name;
    view.artifacts.mslPath = package.files()[selected->metalFile].name;
    view.artifacts.reflectionPath = package.files()[selected->reflectionFile].name;
    if (selected->wgslFile != UINT32_MAX)
        view.artifacts.wgslPath = package.files()[selected->wgslFile].name;
    auto& reflection = view.reflection;
    copyArtifactOwnedShaderReflection(reflection, reflected);
    const auto& reflectedEntry = reflected.entryPoints[0];
    if (stage == ShaderStage::Compute) {
        reflection.computeWorkgroupSizeX = reflectedEntry.computeWorkgroupSizeX;
        reflection.computeWorkgroupSizeY = reflectedEntry.computeWorkgroupSizeY;
        reflection.computeWorkgroupSizeZ = reflectedEntry.computeWorkgroupSizeZ;
    }
    for (const auto& active : reflected.bindings) {
        const DescriptorLayoutDesc* layout = &artifact.physicalLayout;
        if (active.set != artifact.set) {
            const auto domain = std::find_if(artifact.additionalDomains.begin(),
                artifact.additionalDomains.end(),
                [&](const auto& d) {
                    return d.set == active.set;
                });
            if (domain == artifact.additionalDomains.end()) {
                diagnostic = "RHI-BINDLESS-PIPELINE-DOMAIN-MISSING";
                return false;
            }
            layout = &domain->layout;
        }
        for (const auto& physical : layout->bindings)
            if (active.binding == physical.binding) {
                auto logical = makeShaderReflectionBinding(active.set, physical);
                logical.stages = static_cast<ShaderStage>(active.stageMask);
                const auto existing = std::find_if(reflection.descriptorBindings.begin(),
                    reflection.descriptorBindings.end(),
                    [&](const auto& b) {
                        return b.set == logical.set && b.binding == logical.binding;
                    });
                if (existing == reflection.descriptorBindings.end())
                    reflection.descriptorBindings.push_back(logical);
                else
                    existing->stages = existing->stages | logical.stages;
            }
    }
    ShaderEntrypoint shaderEntry;
    shaderEntry.modulePath = view.slangModulePath;
    shaderEntry.entryPoint = selected->entry;
    shaderEntry.stage = stage;
    shaderEntry.sourceLanguage = ShaderSourceLanguage::Slang;
    shaderEntry.computeWorkgroupSizeX = reflectedEntry.computeWorkgroupSizeX;
    shaderEntry.computeWorkgroupSizeY = reflectedEntry.computeWorkgroupSizeY;
    shaderEntry.computeWorkgroupSizeZ = reflectedEntry.computeWorkgroupSizeZ;
    view.entrypoints.push_back(shaderEntry);
    if (selectedModule)
        *selectedModule = selected;
    return true;
}
} // namespace

bool appendBindlessShaderEntry(const BindlessShaderPackage& package,
    core::StringView entry,
    ShaderStage stage,
    ShaderPackageDesc& view,
    core::pmr::String& diagnostic,
    const BindlessPackageModule** selectedModule)
{
    return appendBindlessShaderEntryImpl(
        package, entry, stage, view, diagnostic, selectedModule, false);
}

ValidationResult BindlessGraphicsPipelineEntries::composeStage(
    const std::shared_ptr<const BindlessShaderPackage>& package,
    core::StringView entry,
    ShaderStage stage,
    ShaderPackageDesc& output,
    const BindlessPackageModule*& selected,
    std::uint32_t version)
{
    output = {};
    selected = nullptr;
    if (version != 1u)
        return {false, "RHI-BINDLESS-GRAPHICS-INTERFACE-VERSION"};
    if (!package || !package->sharedOwnerMatches(package))
        return {false, "RHI-BINDLESS-GRAPHICS-INTERFACE-ORIGINAL-OWNER"};
    if (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment)
        return {false, "RHI-BINDLESS-GRAPHICS-INTERFACE-STAGE"};
    ShaderPackageDesc view;
    view.label = package->artifact().physicalLayout.name;
    view.generatedReflectionHydrated = true;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    const BindlessPackageModule* module = nullptr;
    if (!appendBindlessShaderEntryImpl(*package, entry, stage, view, diagnostic, &module, true))
        return {false, std::move(diagnostic)};
    if (module->reflection.reflection.pushConstantSizeBytes) {
        if (!module->push.sizeBytes)
            return {false, "RHI-BINDLESS-GRAPHICS-PUSH-SOURCE-PROOF"};
        if (package->artifact().execution == BindlessExecution::Emulated
            && (!module->targetPush.targetDeclarationBytes
                || module->targetPush.targetDeclarationBytes != module->push.sizeBytes))
            return {false, "RHI-BINDLESS-GRAPHICS-PUSH-TARGET-PROOF"};
    }
    const auto valid = validateAuthorizedBindlessShaderPackage(view);
    if (!valid.success)
        return valid;
    output = std::move(view);
    selected = module;
    return {true, {}};
}

ValidationResult BindlessGraphicsPipelineEntries::compose(
    const std::shared_ptr<const BindlessShaderPackage>& package,
    core::StringView vertexEntry,
    core::StringView fragmentEntry,
    ShaderPackageDesc& output,
    PipelineLayoutDesc& outputLayout,
    BindlessGraphicsPipelineEntries& outputEntries,
    std::uint32_t version)
{
    output = {};
    outputLayout = {};
    outputEntries = {};
    if (version != 1u)
        return {false, "RHI-BINDLESS-GRAPHICS-INTERFACE-VERSION"};
    if (!package || !package->sharedOwnerMatches(package))
        return {false, "RHI-BINDLESS-GRAPHICS-INTERFACE-ORIGINAL-OWNER"};
    ShaderPackageDesc vertex, fragment, merged;
    BindlessGraphicsPipelineEntries selected;
    const auto vertexValid =
        composeStage(package, vertexEntry, ShaderStage::Vertex, vertex, selected.vertex_);
    if (!vertexValid.success)
        return vertexValid;
    if (!fragmentEntry.empty()) {
        const auto fragmentValid = composeStage(
            package, fragmentEntry, ShaderStage::Fragment, fragment, selected.fragment_);
        if (!fragmentValid.success)
            return fragmentValid;
        // Both generated entry sources are already authenticated by this package.
        // Canonicalize only after checking their real source identity; target
        // paths and selected module addresses continue to name their own entry.
        if (vertex.reflection.sourceSha256 != fragment.reflection.sourceSha256)
            return {false, "RHI-GRAPHICS-MERGE-SOURCE-IDENTITY"};
        fragment.slangModulePath = vertex.slangModulePath;
        fragment.entrypoints.front().modulePath = vertex.slangModulePath;
        const auto valid = mergeBindlessGraphicsShaderInterface(1u, vertex, fragment, merged);
        if (!valid.success)
            return valid;
    } else {
        const auto valid = validateAuthorizedBindlessShaderPackage(vertex);
        if (!valid.success)
            return valid;
        merged = std::move(vertex);
    }
    auto layout = bindlessPipelineLayout(package->artifact());
    if (merged.reflection.pushConstantByteCount) {
        const auto stages =
            parseShaderReflectionPushConstantStages(merged.reflection.pushConstantStages);
        if (!stages)
            return {false, "RHI-GRAPHICS-MERGE-PUSH-STAGES"};
        layout.pushConstantRanges.push_back({*stages, 0u, merged.reflection.pushConstantByteCount});
    }
    const auto valid = validatePipelineLayoutAgainstShaderReflection(layout, merged.reflection);
    if (!valid.success)
        return valid;
    output = std::move(merged);
    outputLayout = std::move(layout);
    outputEntries = selected;
    return {true, {}};
}

bool selectBindlessEmulatedWgsl(const BindlessShaderPackage& package,
    std::string_view entry,
    ShaderStage stage,
    core::StringView& bytes,
    core::pmr::String& diagnostic)
{
    bytes = {};
    diagnostic.clear();
    if (package.artifact().execution != BindlessExecution::Emulated) {
        diagnostic = "RHI-BINDLESS-WEBGPU-MODE-MISMATCH";
        return false;
    }
    if (stage != ShaderStage::Compute && stage != ShaderStage::Vertex
        && stage != ShaderStage::Fragment) {
        diagnostic = "RHI-BINDLESS-WEBGPU-ENTRY-MISMATCH";
        return false;
    }
    const auto expected = stage == ShaderStage::Compute ? shader::ShaderStage::Compute
        : stage == ShaderStage::Vertex                  ? shader::ShaderStage::Vertex
                                                        : shader::ShaderStage::Fragment;
    for (const auto& module : package.modules()) {
        if (module.entry != entry)
            continue;
        if (module.reflection.reflection.entryPoints.size() != 1
            || module.reflection.reflection.entryPoints[0].stage != expected
            || module.reflection.reflection.bindlessExecution != "Emulated"
            || module.wgslFile >= package.files().size()
            || package.files()[module.wgslFile].bytes.empty()) {
            diagnostic = "RHI-BINDLESS-WEBGPU-ENTRY-MISMATCH";
            return false;
        }
        const auto& retained = package.files()[module.wgslFile].bytes;
        bytes = {reinterpret_cast<const char*>(retained.data()), retained.size()};
        return true;
    }
    diagnostic = "RHI-BINDLESS-WEBGPU-ENTRY-MISMATCH";
    return false;
}

namespace {
bool emptyEntry(const ShaderEntrypoint& entry)
{
    return entry.modulePath.empty() && entry.entryPoint.empty() && entry.stage == ShaderStage::None
        && entry.sourceLanguage == ShaderSourceLanguage::Slang && !entry.computeWorkgroupSizeX
        && !entry.computeWorkgroupSizeY && !entry.computeWorkgroupSizeZ
        && !entry.writesRenderTargetArrayIndex;
}
} // namespace

PipelineLayoutDesc bindlessPipelineLayout(const BindlessShaderArtifact& artifact)
{
    PipelineLayoutDesc layout;
    layout.descriptorLayouts.reserve(artifact.additionalDomains.size() + 1u);
    layout.descriptorSets.reserve(artifact.additionalDomains.size() + 1u);
    layout.descriptorLayouts.push_back(artifact.physicalLayout);
    layout.descriptorSets.push_back({artifact.set, artifact.physicalLayout.name});
    for (const auto& domain : artifact.additionalDomains) {
        layout.descriptorLayouts.push_back(domain.layout);
        layout.descriptorSets.push_back({domain.set, domain.layout.name});
    }
    return layout;
}

ValidationResult validateBindlessAuthoredRenderState(
    const RenderPipelineDesc& authored, bool hasFragment)
{
    if (authored.layout || authored.shaderPackage || !emptyEntry(authored.vertexShader)
        || !emptyEntry(authored.fragmentShader) || !emptyEntry(authored.tessellationControlShader)
        || !emptyEntry(authored.tessellationEvaluationShader)) {
        return {false, "RHI-BINDLESS-PIPELINE-AUTHORED-INTERFACE"};
    }
    if (authored.topology == PrimitiveTopology::PatchList || authored.patchControlPointCount
        || authored.subpassIndex || authored.renderPassCompatibility.enabled()
        || authored.multisample.sampleCount != 1) {
        return {false, "RHI-BINDLESS-PIPELINE-GRAPHICS-ABI-UNAVAILABLE"};
    }
    if (!authored.dynamicViewportScissor && (!authored.hasViewport || !authored.hasScissor)) {
        return {false, "RHI-BINDLESS-PIPELINE-VIEWPORT-SCISSOR-REQUIRED"};
    }
    if (!hasFragment && !isDepthOnlyRenderPipeline(authored)) {
        return {false, "RHI-BINDLESS-PIPELINE-FRAGMENT-REQUIRED"};
    }
    return {true, {}};
}

} // namespace arp::rhi
