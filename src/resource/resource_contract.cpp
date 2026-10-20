#include "resource/resource_contract.h"

#include "core/sha1.h"

#include <charconv>

namespace arp::resource {

namespace {

core::pmr::String text(ResourceContractNameView value)
{
    return core::pmr::String(value.data(), value.size(), &core::defaultCpuMemoryResource());
}

ResourceValidationResult success()
{
    ResourceValidationResult result;
    result.success = true;
    return result;
}

void append(core::pmr::String& out, ResourceContractNameView value)
{
    out.append(value.data(), value.size());
}

int promotionRank(ResourceStatus status) noexcept
{
    switch (status) {
    case ResourceStatus::ContractComplete:
        return 0;
    case ResourceStatus::PrototypeComplete:
        return 1;
    case ResourceStatus::BackendValidated:
        return 2;
    case ResourceStatus::PlatformValidated:
        return 3;
    case ResourceStatus::ProductionReady:
        return 4;
    case ResourceStatus::LegacyFallback:
    case ResourceStatus::Invalid:
        return -1;
    }
    return -1;
}

} // namespace

core::pmr::String deriveResourceUuid(const std::string_view seed, const std::string_view role)
{
    core::pmr::String identity(seed, &core::defaultCpuMemoryResource());
    identity += "|";
    identity.append(role.data(), role.size());
    auto digest = core::Sha1::digest(core::StringView(identity.data(), identity.size()));
    digest[6] = static_cast<std::uint8_t>((digest[6] & 0x0fu) | 0x50u);
    digest[8] = static_cast<std::uint8_t>((digest[8] & 0x3fu) | 0x80u);

    constexpr char digits[] = "0123456789abcdef";
    core::pmr::String uuid(&core::defaultCpuMemoryResource());
    uuid.reserve(36u);
    for (std::size_t index = 0; index < 16u; ++index) {
        if (index == 4u || index == 6u || index == 8u || index == 10u) {
            uuid.push_back('-');
        }
        uuid.push_back(digits[(digest[index] >> 4u) & 0x0fu]);
        uuid.push_back(digits[digest[index] & 0x0fu]);
    }
    return uuid;
}

core::pmr::String deriveResourceUuid(
    const std::string_view seed, const std::string_view role, const std::uint64_t ordinal)
{
    char ordinalText[32]{};
    const auto converted = std::to_chars(ordinalText, ordinalText + sizeof(ordinalText), ordinal);
    if (converted.ec != std::errc{}) {
        return {};
    }
    core::pmr::String fullRole(role, &core::defaultCpuMemoryResource());
    fullRole.push_back('/');
    fullRole.append(ordinalText, converted.ptr);
    return deriveResourceUuid(seed, fullRole);
}

ResourceContractNameView resourceStatusName(ResourceStatus status) noexcept
{
    switch (status) {
    case ResourceStatus::ContractComplete:
        return "Contract Complete";
    case ResourceStatus::PrototypeComplete:
        return "Prototype Complete";
    case ResourceStatus::BackendValidated:
        return "Backend Validated";
    case ResourceStatus::PlatformValidated:
        return "Platform Validated";
    case ResourceStatus::ProductionReady:
        return "Production Ready";
    case ResourceStatus::LegacyFallback:
        return "Legacy fallback";
    case ResourceStatus::Invalid:
        return "Invalid";
    }
    return "Invalid";
}

ResourceStatusParseResult parseResourceStatus(ResourceContractNameView value)
{
    ResourceStatusParseResult result;
    const ResourceStatus known[] = {
        ResourceStatus::ContractComplete,
        ResourceStatus::PrototypeComplete,
        ResourceStatus::BackendValidated,
        ResourceStatus::PlatformValidated,
        ResourceStatus::ProductionReady,
        ResourceStatus::LegacyFallback,
    };
    for (const auto status : known) {
        if (value == resourceStatusName(status)) {
            result.success = true;
            result.status = status;
            return result;
        }
    }
    result.diagnostics = text("unknown resource status: ");
    append(result.diagnostics, value);
    return result;
}

ResourceValidationResult validateResourceStatus(ResourceContractNameView value,
    std::initializer_list<ResourceStatus> allowed,
    ResourceContractNameView subject)
{
    const auto parsed = parseResourceStatus(value);
    if (!parsed.success) {
        ResourceValidationResult result;
        result.diagnostics = parsed.diagnostics;
        return result;
    }
    for (const auto status : allowed) {
        if (status == parsed.status) {
            return success();
        }
    }
    ResourceValidationResult result;
    result.diagnostics = text("resource status not allowed for ");
    append(result.diagnostics, subject);
    result.diagnostics += ": ";
    append(result.diagnostics, resourceStatusName(parsed.status));
    return result;
}

ResourceValidationResult validateResourceStatusPromotion(ResourceStatus from, ResourceStatus to)
{
    const auto fromRank = promotionRank(from);
    const auto toRank = promotionRank(to);
    if (from == to
        || (from == ResourceStatus::LegacyFallback && to == ResourceStatus::PrototypeComplete)
        || (fromRank >= 0 && toRank == fromRank + 1)) {
        return success();
    }
    ResourceValidationResult result;
    result.diagnostics = text("invalid resource status promotion: ");
    append(result.diagnostics, resourceStatusName(from));
    result.diagnostics += " -> ";
    append(result.diagnostics, resourceStatusName(to));
    return result;
}

ResourceValidationResult validateResourceContract(ResourceContractNameView actualType,
    ResourceContractNameView actualContractVersion,
    const ResourceContractDescriptor& expected)
{
    ResourceValidationResult result;
    if (actualType != expected.type) {
        result.diagnostics = text("resource type mismatch: expected ");
        append(result.diagnostics, expected.type);
        result.diagnostics += ", got ";
        append(result.diagnostics, actualType);
        return result;
    }
    if (actualContractVersion != expected.contractVersion) {
        result.diagnostics = text("resource contract version mismatch for ");
        append(result.diagnostics, expected.type);
        result.diagnostics += ": expected ";
        append(result.diagnostics, expected.contractVersion);
        result.diagnostics += ", got ";
        append(result.diagnostics, actualContractVersion);
        return result;
    }
    result.success = true;
    return result;
}

ResourceValidationResult validateResourceDependencyUsage(
    ResourceContractNameView actualUsage, ResourceContractNameView expectedUsage)
{
    ResourceValidationResult result;
    if (actualUsage == expectedUsage) {
        result.success = true;
        return result;
    }
    result.diagnostics = text("resource dependency usage mismatch: expected ");
    append(result.diagnostics, expectedUsage);
    result.diagnostics += ", got ";
    append(result.diagnostics, actualUsage);
    return result;
}

ResourceValidationResult validateResourceContractRedirect(const ResourceContractRedirect& redirect)
{
    ResourceValidationResult result;
    if (redirect.status != ResourceStatus::LegacyFallback) {
        result.diagnostics = text("resource contract redirect requires Legacy fallback status");
        return result;
    }
    if (redirect.source.type.empty() || redirect.source.contractVersion.empty()
        || redirect.target.type.empty() || redirect.target.contractVersion.empty()) {
        result.diagnostics =
            text("resource contract redirect requires explicit source and target contracts");
        return result;
    }
    if (redirect.source.type != redirect.target.type) {
        result.diagnostics = text("resource contract redirect cannot change resource type");
        return result;
    }
    if (redirect.source.contractVersion == redirect.target.contractVersion) {
        result.diagnostics = text("resource contract redirect source and target must differ");
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::resource
