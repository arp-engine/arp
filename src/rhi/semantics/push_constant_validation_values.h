#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdint>

namespace arp::rhi {
enum class ShaderStage : std::uint32_t;
struct PushConstantRange;
struct ShaderReflectionDesc;
struct ValidationResult;
} // namespace arp::rhi

namespace arp::rhi::semantics {

// Stable diagnostic code prefixes shared by all four backends.
// Every validator failure must include exactly one prefix; tests assert
// the same code string regardless of backend.
namespace PushConstantDiagCode {
inline constexpr const char* kStagesNone = "[PC-E001] ";
inline constexpr const char* kOffsetNonZero = "[PC-E002] ";
inline constexpr const char* kSizeZero = "[PC-E003] ";
inline constexpr const char* kDataNull = "[PC-E004] ";
inline constexpr const char* kNoPipeline = "[PC-E005] ";
inline constexpr const char* kStagesNoPass = "[PC-E006] ";
inline constexpr const char* kRangeCount = "[PC-E007] ";
inline constexpr const char* kRangeOffset = "[PC-E008] ";
inline constexpr const char* kSizeMismatch = "[PC-E009] ";
inline constexpr const char* kStagesNoRange = "[PC-E010] ";
inline constexpr const char* kNoExecPayload = "[PC-E011] ";
inline constexpr const char* kSizeOverflow = "[PC-E012] ";
inline constexpr const char* kDuplicateUnconsumed = "[PC-E013] ";
inline constexpr const char* kSecondaryNotSupported = "[PC-E014] ";
inline constexpr const char* kReflectionContract = "[PC-E015] ";
inline constexpr const char* kReflectionStages = "[PC-E016] ";
// Native payload failures use a separate range so a backend cannot
// accidentally reuse a semantic validator code for a different failure.
inline constexpr const char* kNativeRange = "[PC-E017] ";
inline constexpr const char* kNativeEncoder = "[PC-E018] ";
inline constexpr const char* kNativeBinding = "[PC-E019] ";
inline constexpr const char* kNativeSlot = "[PC-E020] ";
inline constexpr const char* kNativeUploadRing = "[PC-E021] ";
inline constexpr const char* kNativeBindGroup = "[PC-E022] ";
inline constexpr const char* kNativeRuntime = "[PC-E023] ";
inline constexpr const char* kSecondaryDataRange = "[PC-E024] ";
} // namespace PushConstantDiagCode

/// Validate a push-constant command against the AtomicFullBlockPush profile.
///
/// This is the L1 shared validator — every backend MUST call it before
/// lowering.  The AtomicFullBlockPush profile requires:
///
///   - offsetBytes == 0            (no partial update)
///   - sizeBytes > 0               (non-empty payload)
///   - data != nullptr             (backed payload)
///   - stages != ShaderStage::None (declared visibility)
///
/// @return ValidationResult with success=true and empty diagnostics on pass,
///         or success=false with a stable diagnostic string on failure.
// Allocation-free form for original immutable command preflight/revalidation.
// Same diagnostics and semantics as the public encoder validator.
const char* pushConstantCommandDiagnostic(ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const void* data) noexcept;

ValidationResult validatePushConstantCommand(
    ShaderStage stages, std::uint32_t offsetBytes, std::uint32_t sizeBytes, const void* data);

const char* pushConstantBindingDiagnostic(const core::pmr::Vector<PushConstantRange>& ranges,
    ShaderStage passType,
    ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const ShaderReflectionDesc* reflection) noexcept;

// Same gate for an immutable owning projection after the source layout dies.
ValidationResult validatePushConstantBinding(const core::pmr::Vector<PushConstantRange>& ranges,
    ShaderStage passType,
    ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const ShaderReflectionDesc* reflection);

} // namespace arp::rhi::semantics
