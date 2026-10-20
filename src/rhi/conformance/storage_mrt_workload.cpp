#include "rhi/conformance/storage_mrt_workload.h"

#include "core/sha256.h"
#include "core/yaml_contract.h"

#include <algorithm>
#include <utility>

namespace arp::rhi {
ConformanceStorageMrtWorkload::ConformanceStorageMrtWorkload(ConstructionKey,
    core::pmr::String id,
    core::pmr::String source,
    core::pmr::String shader,
    core::pmr::String compute,
    bool alias,
    core::pmr::Vector<ConformanceStorageMrtFrame> frames)
    : id_(std::move(id)), sourceSha256_(std::move(source)), shaderSourceSha256_(std::move(shader)),
      computeSourceSha256_(std::move(compute)), requireTransientAliasing_(alias),
      frames_(std::move(frames))
{}

const char* ConformanceStorageMrtWorkload::executionDiagnostic(
    const DeviceCapabilities& caps) const noexcept
{
    if (!caps.core.compute || !caps.features.computeDispatch)
        return "RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-UNAVAILABLE";
    if (!caps.core.graphics)
        return "RHI-CONFORMANCE-STORAGE-MRT-GRAPHICS-UNAVAILABLE";
    if (!caps.features.storageBufferBinding)
        return "RHI-CONFORMANCE-STORAGE-MRT-BUFFER-UNAVAILABLE";
    if (!caps.features.storageTextureBinding)
        return "RHI-CONFORMANCE-STORAGE-MRT-TEXTURE-UNAVAILABLE";
    if (!caps.features.rgba16FloatStorageTexture)
        return "RHI-CONFORMANCE-STORAGE-MRT-FORMAT-UNAVAILABLE";
    if (requireTransientAliasing_) {
        if (!caps.features.transientAliasing || !caps.memory.transientAliasing)
            return "RHI-CONFORMANCE-STORAGE-MRT-ALIAS-CAPABILITY";
        return "RHI-CONFORMANCE-STORAGE-MRT-ALIAS-EXECUTION-UNAVAILABLE";
    }
    return nullptr;
}

core::UniquePtr<ConformanceStorageMrtWorkload> ConformanceStorageMrtWorkload::parse(
    core::StringView source, const ConformanceWorkloadBudget& budget, const char*& diagnostic)
{
    diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
    if (!budget.maxYamlBytes || !budget.maxFrames || !budget.maxPixelsPerFrame || source.empty()
        || source.size() > budget.maxYamlBytes)
        return {};
    using Yaml = core::YamlContract;
    try {
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
        const auto root = Yaml::parseSingleDocument(source);
        if (!root.is_mapping() || root.size() != 7
            || Yaml::text(root, "type") != "ConformanceStorageMrtWorkload")
            return {};
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-VERSION";
        if (Yaml::uint32(root, "contractVersion") != 1)
            return {};
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
        auto id = Yaml::text(root, "id"), shader = Yaml::text(root, "shaderSourceSha256");
        if (id.empty() || shader.size() != 64
            || !std::all_of(shader.begin(), shader.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               }))
            return {};
        auto compute = Yaml::text(root, "computeSourceSha256");
        if (compute.size() != 64 || !std::all_of(compute.begin(), compute.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }))
            return {};
        const bool alias = Yaml::boolean(root, "requireTransientAliasing");
        const auto& sequence = Yaml::sequence(root, "frames");
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
        if (sequence.empty() || sequence.size() > budget.maxFrames)
            return {};
        core::pmr::Vector<ConformanceStorageMrtFrame> frames{&core::defaultCpuMemoryResource()};
        frames.reserve(sequence.size());
        for (const auto& input : sequence) {
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
            if (!input.is_mapping() || input.size() != 5)
                return {};
            const auto& extent = Yaml::required(input, "extent");
            if (!extent.is_mapping() || extent.size() != 2)
                return {};
            ConformanceStorageMrtFrame frame;
            frame.extent = {Yaml::uint32(extent, "width"), Yaml::uint32(extent, "height")};
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
            if (!frame.extent.width || !frame.extent.height
                || std::uint64_t{frame.extent.width} * frame.extent.height
                    > budget.maxPixelsPerFrame)
                return {};
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
            frame.values = Yaml::floatArray<12>(input, "values");
            frame.expectedColor0 = Yaml::byteArray<4>(input, "expectedColor0");
            frame.expectedColor1 = Yaml::byteArray<4>(input, "expectedColor1");
            frame.expectedStorage = Yaml::floatArray<4>(input, "expectedStorage");
            // This version uses finite normalized values, including exact opaque alpha.
            if (std::any_of(frame.values.begin(),
                    frame.values.end(),
                    [](float v) {
                        return v < 0 || v > 1;
                    })
                || std::any_of(frame.expectedStorage.begin(),
                    frame.expectedStorage.end(),
                    [](float v) {
                        return v < 0 || v > 1;
                    })
                || frame.values[3] != 1 || frame.values[7] != 1 || frame.values[11] != 1
                || frame.expectedColor0[3] != 255 || frame.expectedColor1[3] != 255
                || frame.expectedStorage[3] != 1)
                return {};
            frames.push_back(std::move(frame));
        }
        auto result = core::makeUnique<ConformanceStorageMrtWorkload>(ConstructionKey{},
            std::move(id),
            core::Sha256::hexDigest(source),
            std::move(shader),
            std::move(compute),
            alias,
            std::move(frames));
        diagnostic = nullptr;
        return result;
    } catch (const core::YamlContractError&) {
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
    } catch (const fkyaml::exception&) {
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-YAML";
    }
    return {};
}
} // namespace arp::rhi
