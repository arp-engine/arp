#include "shader/shader_pipeline_reflection_validator.h"

#include "core/sha256.h"
#include "shader/shader_reflection_cache.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace arp::shader {

namespace {

ShaderPipelineValidationResult memberFailure(std::size_t index, const char* reason)
{
    std::ostringstream stream;
    stream << "push constant member " << index << " " << reason;
    return ShaderPipelineValidationResult{
        false,
        arp::core::toPmrString(stream.str()),
    };
}

bool arrayLayoutValid(
    const ShaderPushConstantMember& member, std::uint64_t elementSize, std::uint64_t& expectedSize)
{
    if (member.arrayDims.empty()) {
        expectedSize = elementSize;
        return true;
    }
    if (member.arrayStride < elementSize) {
        return false;
    }
    std::uint64_t elementCount = 1u;
    for (const auto dimension : member.arrayDims) {
        if (dimension == 0u || elementCount > UINT32_MAX / dimension) {
            return false;
        }
        elementCount *= dimension;
    }
    expectedSize = static_cast<std::uint64_t>(member.arrayStride) * elementCount;
    return expectedSize <= UINT32_MAX;
}

bool isHexSha256(const std::pmr::string& value)
{
    if (value.size() != 64u) {
        return false;
    }
    for (const auto character : value) {
        if (!std::isxdigit(static_cast<unsigned char>(character))) {
            return false;
        }
    }
    return true;
}

std::string normalizedPath(const std::pmr::string& value)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(std::filesystem::path(value.c_str()), error);
    if (error) {
        return {};
    }
    auto normalized = absolute.lexically_normal().generic_string();
#ifdef _WIN32
    for (auto& character : normalized) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
#endif
    return normalized;
}

bool pathsMatch(const std::pmr::string& generated, const std::pmr::string& expected)
{
#if defined(__EMSCRIPTEN__)
    // Reflection files are generated on the host and retain the host's
    // absolute paths. Browser packages expose the same artifacts through
    // Emscripten's virtual filesystem, so compare the artifact filename while
    // using the package path for the actual file/hash validation below.
    if (!generated.empty() && !expected.empty()) {
        const auto generatedSlash = generated.find_last_of("/\\");
        const auto expectedSlash = expected.find_last_of("/\\");
        const auto generatedName =
            generated.substr(generatedSlash == std::pmr::string::npos ? 0u : generatedSlash + 1u);
        const auto expectedName =
            expected.substr(expectedSlash == std::pmr::string::npos ? 0u : expectedSlash + 1u);
        return generatedName == expectedName;
    }
    return false;
#else
    return !generated.empty() && !expected.empty()
        && normalizedPath(generated) == normalizedPath(expected);
#endif
}

std::pmr::string relocatedArtifactPath(
    const std::pmr::string& generated, const std::pmr::string& expected)
{
    if (pathsMatch(generated, expected)) {
        return expected;
    }
    // Generated reflection keeps the build machine's absolute paths. A
    // packaged runtime may relocate the complete artifact set, so permit the
    // narrow relocation case where the runtime path exists and the artifact
    // basename is identical. This preserves artifact identity while avoiding
    // cwd/project-root fallback or arbitrary path substitution.
    if (generated.empty() || expected.empty()) {
        return {};
    }
    const auto generatedName = std::filesystem::path(generated.c_str()).filename();
    const auto expectedPath = std::filesystem::path(expected.c_str());
    std::error_code error;
    if (generatedName.empty() || generatedName != expectedPath.filename()
        || !std::filesystem::is_regular_file(expectedPath, error) || error) {
        return {};
    }
    return expected;
}

std::pmr::string sourceSha256(const std::pmr::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return {};
    }
    const std::string contents{
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto digest = core::Sha256::digest(core::StringView(contents.data(), contents.size()));
    static constexpr char kHex[] = "0123456789abcdef";
    std::pmr::string result;
    result.reserve(digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(kHex[(byte >> 4u) & 0x0fu]);
        result.push_back(kHex[byte & 0x0fu]);
    }
    return result;
}

bool regularFile(const std::pmr::string& path)
{
    std::error_code error;
    return !path.empty()
        && std::filesystem::is_regular_file(std::filesystem::path(path.c_str()), error) && !error;
}

} // namespace

ShaderPipelineValidationResult ShaderPipelineReflectionValidator::validatePushConstantMembers(
    const ShaderReflection& reflection)
{
    const auto& members = reflection.pushConstantMembers;
    if (reflection.pushConstantSizeBytes == 0u) {
        if (!members.empty()) {
            return {false, "push constant members exist with a zero push constant size"};
        }
        return {true, ""};
    }

    if (members.empty()) {
        return {false, "push constant reflection is missing member-level layout metadata"};
    }

    std::uint64_t expectedOffset = 0u;
    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto& member = members[index];
        if (member.name.empty()) {
            return memberFailure(index, "has an empty name");
        }
        if (member.size == 0u) {
            return memberFailure(index, "has zero size");
        }
        if (member.offset != expectedOffset) {
            return memberFailure(index, "is not contiguous with the previous member");
        }

        std::uint64_t elementSize = 0u;
        if (member.matrixRows != 0u || member.matrixCols != 0u || member.matrixStride != 0u) {
            if (member.matrixRows == 0u || member.matrixCols == 0u || member.matrixStride == 0u
                || member.scalarWidth == 0u) {
                return memberFailure(index, "has incomplete matrix layout metadata");
            }
            // SPIR-V matrix stride is the distance between columns for a
            // column-major matrix and between rows for a RowMajor matrix.
            const auto majorCount = member.rowMajor ? member.matrixRows : member.matrixCols;
            elementSize = static_cast<std::uint64_t>(member.matrixStride) * majorCount;
        } else if (member.vecSize != 0u) {
            if (member.scalarWidth == 0u || (member.scalarWidth % 8u) != 0u) {
                return memberFailure(index, "has an invalid vector scalar width");
            }
            elementSize = static_cast<std::uint64_t>(member.scalarWidth / 8u) * member.vecSize;
        } else {
            if (member.scalarWidth == 0u || (member.scalarWidth % 8u) != 0u) {
                return memberFailure(index, "has an invalid scalar width");
            }
            elementSize = member.scalarWidth / 8u;
        }

        std::uint64_t expectedSize = 0u;
        if (!arrayLayoutValid(member, elementSize, expectedSize)) {
            return memberFailure(index, "has invalid array layout metadata");
        }

        if (expectedSize == 0u || member.size != expectedSize) {
            return memberFailure(index, "size disagrees with reflected type layout");
        }
        const auto end = expectedOffset + member.size;
        if (end > UINT32_MAX) {
            return memberFailure(index, "overflows the reflected byte range");
        }
        expectedOffset = end;
    }

    if (expectedOffset != reflection.pushConstantSizeBytes) {
        return {false, "push constant member layout end does not match pushConstantSizeBytes"};
    }
    return {true, ""};
}

ShaderPipelineValidationResult ShaderPipelineReflectionValidator::validateGeneratedArtifact(
    const ShaderReflection& reflection)
{
    if (reflection.reflectionContractVersion != kShaderReflectionContractVersion) {
        return {false, "shader reflection artifact has an unsupported or missing contract version"};
    }
    if (!isHexSha256(reflection.sourceSha256)) {
        return {false, "shader reflection artifact has an invalid SHA-256 source fingerprint"};
    }
    if (!reflection.bindlessContractVersion.empty() || !reflection.bindlessExecution.empty()
        || !reflection.bindlessLoweringSha256.empty()) {
        if (reflection.bindlessContractVersion != "BindlessCross/1"
            || (reflection.bindlessExecution != "Native"
                && reflection.bindlessExecution != "Emulated")
            || !isHexSha256(reflection.bindlessLoweringSha256))
            return {false, "RHI-BINDLESS-REFLECTION-INVALID"};
    }
    for (const auto& binding : reflection.bindings) {
        if (binding.stageMask == 0u) {
            return {false,
                "shader reflection artifact is missing authoritative descriptor stage metadata"};
        }
    }
    return validatePushConstantMembers(reflection);
}

ShaderPipelineValidationResult ShaderPipelineReflectionValidator::validateGeneratedArtifact(
    const ShaderReflectionModule& artifact,
    const std::pmr::string& expectedSourcePath,
    const std::pmr::string& expectedSpirvPath,
    const std::pmr::string& expectedMslPath,
    const std::pmr::string& expectedWgslPath)
{
    const auto metadata = validateGeneratedArtifact(artifact.reflection);
    if (!metadata.success) {
        return metadata;
    }
    const auto sourcePath = relocatedArtifactPath(artifact.sourcePath, expectedSourcePath);
    const auto spirvPath = relocatedArtifactPath(artifact.spirvPath, expectedSpirvPath);
    const auto metalPath = relocatedArtifactPath(artifact.metalPath, expectedMslPath);
    const auto wgslPath = expectedWgslPath.empty()
        ? std::pmr::string{}
        : relocatedArtifactPath(artifact.wgslPath, expectedWgslPath);
    if (sourcePath.empty()) {
        return {
            false, "shader reflection artifact sourcePath does not match shader package source"};
    }
    if (spirvPath.empty() || metalPath.empty() || (!expectedWgslPath.empty() && wgslPath.empty())) {
        return {
            false, "shader reflection artifact target paths do not match shader package artifacts"};
    }
    if (!regularFile(sourcePath) || !regularFile(spirvPath) || !regularFile(metalPath)
        || (!expectedWgslPath.empty() && !regularFile(wgslPath))) {
        return {
            false, "shader reflection artifact references a missing generated source or target"};
    }
    const auto actualHash = sourceSha256(sourcePath);
    if (actualHash.empty() || actualHash.size() != artifact.reflection.sourceSha256.size()) {
        return {false, "shader reflection artifact source fingerprint could not be computed"};
    }
    for (std::size_t index = 0; index < actualHash.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(actualHash[index]))
            != std::tolower(static_cast<unsigned char>(artifact.reflection.sourceSha256[index]))) {
            return {false, "shader reflection artifact source fingerprint disagrees with source"};
        }
    }
    return {true, ""};
}

ShaderPipelineValidationResult ShaderPipelineReflectionValidator::validate(
    const ShaderPipelineLayoutDesc& pipeline, const ShaderReflection& reflection)
{
    if (pipeline.pushConstantSizeBytes != reflection.pushConstantSizeBytes) {
        std::ostringstream stream;
        stream << "push constant size mismatch: pipeline=" << pipeline.pushConstantSizeBytes
               << " reflection=" << reflection.pushConstantSizeBytes;
        return ShaderPipelineValidationResult{false, arp::core::toPmrString(stream.str())};
    }

    if (reflection.pushConstantSizeBytes > 0
        && reflection.pushConstantContractVersion != "AtomicFullBlockPush/1") {
        return ShaderPipelineValidationResult{false,
            "push constant reflection contract version mismatch: expected AtomicFullBlockPush/1"};
    }
    if (reflection.pushConstantSizeBytes > 0 && reflection.pushConstantStages.empty()) {
        return ShaderPipelineValidationResult{
            false, "push constant reflection stage visibility is missing"};
    }
    const auto memberValidation = validatePushConstantMembers(reflection);
    if (!memberValidation.success) {
        return memberValidation;
    }

    if (pipeline.vertexAttributes.size() != reflection.vertexAttributes.size()) {
        std::ostringstream stream;
        stream << "vertex attribute count mismatch: pipeline=" << pipeline.vertexAttributes.size()
               << " reflection=" << reflection.vertexAttributes.size();
        return ShaderPipelineValidationResult{false, arp::core::toPmrString(stream.str())};
    }

    for (size_t index = 0; index < pipeline.vertexAttributes.size(); ++index) {
        const auto& pipelineAttribute = pipeline.vertexAttributes[index];
        const auto& reflectionAttribute = reflection.vertexAttributes[index];
        if (pipelineAttribute.location != reflectionAttribute.location
            || pipelineAttribute.semantic != reflectionAttribute.semantic
            || pipelineAttribute.format != reflectionAttribute.format) {
            std::ostringstream stream;
            stream << "vertex attribute mismatch at location " << index;
            return ShaderPipelineValidationResult{false, arp::core::toPmrString(stream.str())};
        }
    }

    return ShaderPipelineValidationResult{true, ""};
}

} // namespace arp::shader
