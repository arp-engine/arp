#include "rhi/conformance/clear_workload.h"

#include "core/sha256.h"
#include "core/yaml_contract.h"

#include <utility>

namespace arp::rhi {

Color ConformanceClearFrame::clearColor() const noexcept
{
    // UNORM8 encoding, not an empirically chosen shader/lighting parameter.
    return {rgba8[0] / 255.f, rgba8[1] / 255.f, rgba8[2] / 255.f, rgba8[3] / 255.f};
}

ConformanceClearWorkload::ConformanceClearWorkload(ConstructionKey,
    core::pmr::String id,
    core::pmr::String hash,
    core::pmr::Vector<ConformanceClearFrame> frames)
    : id_(std::move(id)), sourceSha256_(std::move(hash)), frames_(std::move(frames))
{}

core::UniquePtr<ConformanceClearWorkload> ConformanceClearWorkload::parse(
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
        if (!root.is_mapping() || root.size() != 4u
            || Yaml::text(root, "type") != "ConformanceClearWorkload")
            return {};
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-VERSION";
        if (Yaml::uint32(root, "contractVersion") != 1u)
            return {};
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
        auto id = Yaml::text(root, "id");
        if (id.empty())
            return {};
        const auto& sequence = Yaml::sequence(root, "frames");
        diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
        if (sequence.empty() || sequence.size() > budget.maxFrames)
            return {};
        core::pmr::Vector<ConformanceClearFrame> frames{&core::defaultCpuMemoryResource()};
        frames.reserve(sequence.size());
        for (const auto& input : sequence) {
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
            if (!input.is_mapping() || input.size() != 2u)
                return {};
            const auto& extent = Yaml::required(input, "extent");
            if (!extent.is_mapping() || extent.size() != 2u)
                return {};
            ConformanceClearFrame frame;
            frame.extent = {Yaml::uint32(extent, "width"), Yaml::uint32(extent, "height")};
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-BUDGET";
            if (!frame.extent.width || !frame.extent.height
                || std::uint64_t{frame.extent.width} * frame.extent.height
                    > budget.maxPixelsPerFrame)
                return {};
            diagnostic = "RHI-CONFORMANCE-WORKLOAD-SCHEMA";
            frame.rgba8 = Yaml::byteArray<4>(input, "rgba8");
            if (frame.rgba8[3] != UINT8_MAX)
                return {};
            frames.push_back(frame);
        }
        auto result = core::makeUnique<ConformanceClearWorkload>(
            ConstructionKey{}, std::move(id), core::Sha256::hexDigest(source), std::move(frames));
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
