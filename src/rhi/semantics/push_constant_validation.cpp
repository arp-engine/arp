#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/push_constant_validation_values.h"

#include <optional>
#include <string_view>

namespace arp::rhi::semantics {

namespace {

/// @brief True when every bit set in `required` is also set in `available`.
inline bool coversStages(ShaderStage available, ShaderStage required)
{
    return (static_cast<std::uint32_t>(available) & static_cast<std::uint32_t>(required))
        == static_cast<std::uint32_t>(required);
}

inline bool pushConstantUpdateEndOverflows(std::uint32_t offsetBytes, std::uint32_t sizeBytes)
{
    return static_cast<std::uint64_t>(offsetBytes) + static_cast<std::uint64_t>(sizeBytes)
        > UINT32_MAX;
}

std::optional<ShaderStage> parseAtomicFullBlockPushStages(const std::pmr::string& encoded)
{
    if (encoded.empty()) {
        return std::nullopt;
    }

    ShaderStage stages = ShaderStage::None;
    std::size_t tokenStart = 0;
    while (tokenStart < encoded.size()) {
        const auto separator = encoded.find('|', tokenStart);
        const auto tokenEnd = separator == std::pmr::string::npos ? encoded.size() : separator;
        const std::string_view token(encoded.data() + tokenStart, tokenEnd - tokenStart);
        ShaderStage stage = ShaderStage::None;
        if (token == "VS") {
            stage = ShaderStage::Vertex;
        } else if (token == "FS") {
            stage = ShaderStage::Fragment;
        } else if (token == "CS") {
            stage = ShaderStage::Compute;
        } else {
            return std::nullopt;
        }
        if (coversStages(stages, stage)) {
            return std::nullopt;
        }
        stages = stages | stage;
        if (separator == std::pmr::string::npos) {
            break;
        }
        tokenStart = tokenEnd + 1;
        if (tokenStart == encoded.size()) {
            return std::nullopt;
        }
    }
    return stages;
}

} // namespace

const char* pushConstantCommandDiagnostic(ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const void* data) noexcept
{
    if (stages == ShaderStage::None) {
        return "[PC-E001] AtomicFullBlockPush: shader stage visibility must not be None";
    }
    if (sizeBytes > rhi::kL2AtomicPushConstantLimitBytes) {
        return "[PC-E012] AtomicFullBlockPush: push size exceeds L2 profile ceiling";
    }
    if (pushConstantUpdateEndOverflows(offsetBytes, sizeBytes)) {
        return "[PC-E012] AtomicFullBlockPush: offset + size overflows uint32";
    }
    if (offsetBytes != 0) {
        return "[PC-E002] AtomicFullBlockPush: offset must be 0";
    }
    if (sizeBytes == 0) {
        return "[PC-E003] AtomicFullBlockPush: size must be non-zero";
    }
    if (!data) {
        return "[PC-E004] AtomicFullBlockPush: data pointer must be non-null";
    }
    return nullptr;
}

ValidationResult validatePushConstantCommand(
    ShaderStage stages, std::uint32_t offsetBytes, std::uint32_t sizeBytes, const void* data)
{
    const auto* id = pushConstantCommandDiagnostic(stages, offsetBytes, sizeBytes, data);
    return {id == nullptr, id ? id : ""};
}

const char* pushConstantBindingDiagnostic(const core::pmr::Vector<PushConstantRange>& ranges,
    ShaderStage passType,
    ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const ShaderReflectionDesc* reflection) noexcept
{
    if (stages == ShaderStage::None) {
        return "[PC-E001] shader stage visibility must not be None";
    }
    if (!coversStages(passType, stages)) {
        return "[PC-E006] requested stages not covered by command encoder type";
    }
    if (sizeBytes == 0) {
        return "[PC-E003] size must be non-zero";
    }
    if (offsetBytes != 0) {
        return "[PC-E002] offset must be 0";
    }
    if (pushConstantUpdateEndOverflows(offsetBytes, sizeBytes)) {
        return "[PC-E012] offset + size overflows uint32";
    }

    // AtomicFullBlockPush: exactly one range, offset 0, exact size match.
    if (ranges.size() != 1) {
        return "[PC-E007] pipeline layout must declare exactly 1 push-constant range";
    }

    const auto& range = ranges[0];
    if (range.offsetBytes != 0) {
        return "[PC-E008] sole push-constant range must start at offset 0";
    }
    if (range.sizeBytes != sizeBytes) {
        return "[PC-E009] push size does not match pipeline layout range size";
    }
    if (!reflection) {
        return "[PC-E015] push-constant binding requires generated reflection";
    }
    if (reflection->pushConstantContractVersion != "AtomicFullBlockPush/1") {
        return "[PC-E015] reflection push-constant contract version is not AtomicFullBlockPush/1";
    }
    if (reflection->pushConstantByteCount != range.sizeBytes) {
        return "[PC-E009] reflection push-constant size does not match pipeline layout";
    }
    const auto reflectionStages = parseAtomicFullBlockPushStages(reflection->pushConstantStages);
    if (!reflectionStages || !coversStages(range.stages, *reflectionStages)
        || !coversStages(*reflectionStages, stages)) {
        return "[PC-E016] reflection stage visibility does not cover the push request or pipeline layout";
    }
    if (!coversStages(range.stages, stages)) {
        return "[PC-E010] requested stages not covered by pipeline push-constant range";
    }
    return nullptr;
}

ValidationResult validatePushConstantBinding(const core::pmr::Vector<PushConstantRange>& ranges,
    ShaderStage passType,
    ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const ShaderReflectionDesc* reflection)
{
    const auto* id =
        pushConstantBindingDiagnostic(ranges, passType, stages, offsetBytes, sizeBytes, reflection);
    return {id == nullptr, id ? id : ""};
}

} // namespace arp::rhi::semantics
