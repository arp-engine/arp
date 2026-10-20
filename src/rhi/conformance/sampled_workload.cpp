#include "rhi/conformance/sampled_workload.h"

#include "core/sha256.h"
#include "core/yaml_contract.h"

#include <algorithm>
#include <utility>

namespace arp::rhi {
ConformanceSampledWorkload::ConformanceSampledWorkload(ConstructionKey,
    core::pmr::String id,
    core::pmr::String source,
    core::pmr::String shader,
    std::array<float, 27> vertices,
    std::array<std::uint16_t, 3> indices,
    core::pmr::Vector<ConformanceSampledFrame> frames)
    : id_(std::move(id)), sourceSha256_(std::move(source)), shaderSourceSha256_(std::move(shader)),
      vertices_(vertices), indices_(indices), frames_(std::move(frames))
{}

core::UniquePtr<ConformanceSampledWorkload> ConformanceSampledWorkload::parse(
    core::StringView source, const ConformanceWorkloadBudget& budget, const char*& diagnostic)
{
    return parse(source,
        ConformanceSampledWorkloadBudget{
            budget.maxYamlBytes, budget.maxPixelsPerFrame, budget.maxFrames, 1},
        diagnostic);
}

core::UniquePtr<ConformanceSampledWorkload> ConformanceSampledWorkload::parse(
    core::StringView source,
    const ConformanceSampledWorkloadBudget& budget,
    const char*& diagnostic)
{
    diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
    if (!budget.maxDrawsPerFrame || !budget.maxYamlBytes || !budget.maxFrames
        || !budget.maxPixelsPerFrame || source.empty() || source.size() > budget.maxYamlBytes)
        return {};
    using Yaml = core::YamlContract;
    try {
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
        const auto root = Yaml::parseSingleDocument(source);
        if (!root.is_mapping() || root.size() != 7
            || Yaml::text(root, "type") != "ConformanceSampledWorkload")
            return {};
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-VERSION";
        const auto version = Yaml::uint32(root, "contractVersion");
        if (version != 1 && version != 2)
            return {};
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
        auto id = Yaml::text(root, "id"), shader = Yaml::text(root, "shaderSourceSha256");
        if (id.empty() || shader.size() != 64
            || !std::all_of(shader.begin(), shader.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               }))
            return {};
        const auto vertices = Yaml::floatArray<27>(root, "vertices");
        const auto& indexSource = Yaml::sequence(root, "indices");
        if (indexSource.size() != 3)
            return {};
        std::array<std::uint16_t, 3> indices{};
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const auto value = Yaml::uint64(indexSource[i]);
            if (value >= 3)
                return {};
            indices[i] = static_cast<std::uint16_t>(value);
        }
        auto sorted = indices;
        std::sort(sorted.begin(), sorted.end());
        if (sorted != std::array<std::uint16_t, 3>{0, 1, 2})
            return {};
        const auto& sequence = Yaml::sequence(root, "frames");
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
        if (sequence.empty() || sequence.size() > budget.maxFrames)
            return {};
        core::pmr::Vector<ConformanceSampledFrame> frames{&core::defaultCpuMemoryResource()};
        frames.reserve(sequence.size());
        for (const auto& input : sequence) {
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
            if (!input.is_mapping() || input.size() != (version == 1 ? 7u : 5u))
                return {};
            const auto& extent = Yaml::required(input, "extent");
            if (!extent.is_mapping() || extent.size() != 2)
                return {};
            ConformanceSampledFrame frame;
            frame.extent = {Yaml::uint32(extent, "width"), Yaml::uint32(extent, "height")};
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
            if (!frame.extent.width || !frame.extent.height
                || std::uint64_t{frame.extent.width} * frame.extent.height
                    > budget.maxPixelsPerFrame)
                return {};
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
            frame.sourceA = Yaml::byteArray<4>(input, "sourceA");
            frame.sourceB = Yaml::byteArray<4>(input, "sourceB");
            if (frame.sourceA[3] != 255 || frame.sourceB[3] != 255)
                return {};
            frame.uniforms = Yaml::floatArray<32>(input, "uniforms");
            if (version == 1) {
                frame.expected = Yaml::byteArray<4>(input, "expected");
                frame.push = Yaml::floatArray<8>(input, "push");
                frame.indexed = Yaml::boolean(input, "indexed");
                frame.draws.push_back({ConformanceSampledResource::SourceA,
                    ConformanceSampledResource::SourceB,
                    ConformanceSampledResource::Output,
                    frame.push,
                    frame.indexed,
                    frame.expected});
            } else {
                const auto& draws = Yaml::sequence(input, "draws");
                diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
                if (draws.empty() || draws.size() > budget.maxDrawsPerFrame
                    || draws.size() > UINT32_MAX / 5u)
                    return {};
                frame.draws.reserve(draws.size());
                const auto resource = [](const core::pmr::String& name) {
                    using R = ConformanceSampledResource;
                    if (name == "SourceA")
                        return R::SourceA;
                    if (name == "SourceB")
                        return R::SourceB;
                    if (name == "PingA")
                        return R::PingA;
                    if (name == "PingB")
                        return R::PingB;
                    if (name == "Output")
                        return R::Output;
                    return R::Count;
                };
                for (const auto& authored : draws) {
                    diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
                    if (!authored.is_mapping() || authored.size() != 6)
                        return {};
                    frame.draws.push_back({resource(Yaml::text(authored, "sourceA")),
                        resource(Yaml::text(authored, "sourceB")),
                        resource(Yaml::text(authored, "target")),
                        Yaml::floatArray<8>(authored, "push"),
                        Yaml::boolean(authored, "indexed"),
                        Yaml::byteArray<4>(authored, "expected")});
                }
                frame.expected = frame.draws.back().expected;
                frame.push = frame.draws.back().push;
                frame.indexed = frame.draws.back().indexed;
            }
            using R = ConformanceSampledResource;
            std::array<bool, static_cast<std::size_t>(R::Count)> initialized{true, true};
            for (std::size_t i = 0; i < frame.draws.size(); ++i) {
                const auto& draw = frame.draws[i];
                diagnostic = "RHI-CONFORMANCE-WORKLOAD-RESOURCE-GRAPH";
                if (draw.sourceA >= R::Output || draw.sourceB >= R::Output || draw.target < R::PingA
                    || draw.target >= R::Count || draw.sourceA == draw.sourceB
                    || draw.target == draw.sourceA || draw.target == draw.sourceB
                    || !initialized[static_cast<std::size_t>(draw.sourceA)]
                    || !initialized[static_cast<std::size_t>(draw.sourceB)]
                    || (draw.target == R::Output) != (i + 1 == frame.draws.size()))
                    return {};
                diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
                if (draw.expected[3] != 255)
                    return {};
                initialized[static_cast<std::size_t>(draw.target)] = true;
            }
            frames.push_back(std::move(frame));
        }
        auto result = core::makeUnique<ConformanceSampledWorkload>(ConstructionKey{},
            std::move(id),
            core::Sha256::hexDigest(source),
            std::move(shader),
            vertices,
            indices,
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
