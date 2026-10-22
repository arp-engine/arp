#include "shader/shader_compiler.h"

#include "core/memory/cpu_allocator.h"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>

namespace arp::shader {

namespace {

bool contains(const std::pmr::string& haystack, const std::pmr::string& needle)
{
    return haystack.find(needle) != std::pmr::string::npos;
}

bool isIdentifierChar(char value)
{
    return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
}

bool containsIdentifier(const std::pmr::string& haystack, const std::pmr::string& needle)
{
    size_t offset = haystack.find(needle);
    while (offset != std::pmr::string::npos) {
        const bool validBegin = offset == 0 || !isIdentifierChar(haystack[offset - 1]);
        const auto end = offset + needle.size();
        const bool validEnd = end >= haystack.size() || !isIdentifierChar(haystack[end]);
        if (validBegin && validEnd) {
            return true;
        }
        offset = haystack.find(needle, offset + 1);
    }
    return false;
}

std::pmr::string trim(const std::pmr::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::pmr::string readIdentifier(const std::pmr::string& source, size_t& offset)
{
    while (offset < source.size() && std::isspace(static_cast<unsigned char>(source[offset]))) {
        ++offset;
    }

    const size_t begin = offset;
    while (offset < source.size()) {
        const char c = source[offset];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            break;
        }
        ++offset;
    }

    return source.substr(begin, offset - begin);
}

std::pmr::string structBody(const std::pmr::string& source, const std::pmr::string& structName)
{
    const auto structOffset = source.find("struct " + structName);
    if (structOffset == std::pmr::string::npos) {
        return {};
    }

    const auto bodyBegin = source.find('{', structOffset);
    const auto bodyEnd = source.find("};", bodyBegin);
    if (bodyBegin == std::pmr::string::npos || bodyEnd == std::pmr::string::npos
        || bodyEnd <= bodyBegin) {
        return {};
    }

    return source.substr(bodyBegin + 1, bodyEnd - bodyBegin - 1);
}

uint32_t floatTypeSizeBytes(const std::pmr::string& type)
{
    if (type == "float") {
        return 4;
    }
    if (type == "float2") {
        return 8;
    }
    if (type == "float3") {
        return 12;
    }
    if (type == "float4") {
        return 16;
    }
    if (type == "float4x4") {
        return 64;
    }
    // Integer vector types (uint4, int4, etc.) occupy the same byte size as
    // their float counterparts (4 bytes per component). The DDGI push-constant
    // uses uint4 for packed probe-count/ray-count fields.
    if (type == "uint" || type == "int") {
        return 4;
    }
    if (type == "uint2" || type == "int2") {
        return 8;
    }
    if (type == "uint3" || type == "int3") {
        return 12;
    }
    if (type == "uint4" || type == "int4") {
        return 16;
    }
    return 0;
}

std::pmr::string readTypeIdentifier(const std::pmr::string& statement, size_t& tokenOffset)
{
    auto type = readIdentifier(statement, tokenOffset);
    while (type == "row_major" || type == "column_major") {
        type = readIdentifier(statement, tokenOffset);
    }
    return type;
}

VertexAttributeFormat vertexFormatFor(const std::pmr::string& type)
{
    if (type == "float") {
        return VertexAttributeFormat::Float;
    }
    if (type == "float2") {
        return VertexAttributeFormat::Float2;
    }
    if (type == "float3") {
        return VertexAttributeFormat::Float3;
    }
    if (type == "float4") {
        return VertexAttributeFormat::Float4;
    }
    return VertexAttributeFormat::Unknown;
}

uint32_t reflectedPushConstantSize(const std::pmr::string& source)
{
    const auto marker = source.find("[[vk::push_constant]]");
    if (marker == std::pmr::string::npos) {
        return 0;
    }

    size_t typeOffset = marker + std::pmr::string("[[vk::push_constant]]").size();
    const auto typeName = readIdentifier(source, typeOffset);
    if (typeName.empty()) {
        return 0;
    }

    const auto body = structBody(source, typeName);
    if (body.empty()) {
        return 0;
    }

    uint32_t sizeBytes = 0;
    size_t statementBegin = 0;
    while (statementBegin < body.size()) {
        const auto statementEnd = body.find(';', statementBegin);
        if (statementEnd == std::pmr::string::npos) {
            break;
        }

        const auto statement = trim(body.substr(statementBegin, statementEnd - statementBegin));
        size_t tokenOffset = 0;
        const auto type = readTypeIdentifier(statement, tokenOffset);
        // Detect a fixed-size array suffix `[N]` after the field name and
        // multiply the element size by N (std430/std140: float4x4 RowMajor
        // stride = 64B, float4 = 16B - matches the scalar element size with
        // no inter-element padding for these matrix/vector types).
        uint32_t arrayCount = 1u;
        const auto bracketPos = statement.find('[', tokenOffset);
        if (bracketPos != std::pmr::string::npos) {
            const auto bracketEnd = statement.find(']', bracketPos);
            if (bracketEnd != std::pmr::string::npos) {
                const auto countStr = statement.substr(bracketPos + 1, bracketEnd - bracketPos - 1);
                if (!countStr.empty()) {
                    try {
                        arrayCount = static_cast<uint32_t>(std::stoul(std::string(countStr)));
                    } catch (...) {
                        arrayCount = 1u;
                    }
                }
            }
        }
        sizeBytes += floatTypeSizeBytes(type) * arrayCount;
        statementBegin = statementEnd + 1;
    }

    return sizeBytes;
}

std::pmr::vector<ShaderVertexAttribute> reflectedVertexAttributes(const std::pmr::string& source)
{
    std::pmr::vector<ShaderVertexAttribute> attributes;
    const auto body = structBody(source, "VertexInput");
    if (body.empty()) {
        return attributes;
    }

    size_t statementBegin = 0;
    while (statementBegin < body.size()) {
        const auto statementEnd = body.find(';', statementBegin);
        if (statementEnd == std::pmr::string::npos) {
            break;
        }

        const auto statement = trim(body.substr(statementBegin, statementEnd - statementBegin));
        const auto semanticSeparator = statement.find(':');
        if (semanticSeparator != std::pmr::string::npos) {
            const auto declaration = trim(statement.substr(0, semanticSeparator));
            const auto semantic = trim(statement.substr(semanticSeparator + 1));
            size_t tokenOffset = 0;
            const auto type = readTypeIdentifier(declaration, tokenOffset);
            const auto name = readIdentifier(declaration, tokenOffset);
            attributes.push_back(ShaderVertexAttribute{
                name,
                semantic,
                vertexFormatFor(type),
                static_cast<uint32_t>(attributes.size()),
            });
        }

        statementBegin = statementEnd + 1;
    }

    return attributes;
}

void appendBindingIfPresent(std::pmr::vector<ShaderBinding>& bindings,
    const std::pmr::string& source,
    const std::pmr::string& token,
    ShaderBinding binding)
{
    if (containsIdentifier(source, token)) {
        bindings.push_back(std::move(binding));
    }
}

std::pmr::string stripComments(const std::pmr::string& source)
{
    std::pmr::string result;
    result.reserve(source.size());
    bool lineComment = false;
    bool blockComment = false;
    for (size_t index = 0; index < source.size(); ++index) {
        const char current = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';
        if (lineComment) {
            if (current == '\n') {
                lineComment = false;
                result.push_back(current);
            }
            continue;
        }
        if (blockComment) {
            if (current == '*' && next == '/') {
                blockComment = false;
                ++index;
            } else if (current == '\n') {
                result.push_back(current);
            }
            continue;
        }
        if (current == '/' && next == '/') {
            lineComment = true;
            ++index;
        } else if (current == '/' && next == '*') {
            blockComment = true;
            ++index;
        } else {
            result.push_back(current);
        }
    }
    return result;
}

void appendSubpassInputBindings(
    std::pmr::vector<ShaderBinding>& bindings, const std::pmr::string& source)
{
    const auto uncommented = stripComments(source);
    auto isOccupied = [&](const std::uint32_t binding) {
        for (const auto& existing : bindings) {
            if (existing.set == 0 && existing.binding == binding) {
                return true;
            }
        }
        return false;
    };
    std::uint32_t logicalBinding = 0;
    size_t searchOffset = 0;
    while (true) {
        const auto colorOffset = uncommented.find("SUBPASS_INPUT_COLOR", searchOffset);
        const auto depthOffset = uncommented.find("SUBPASS_INPUT_DEPTH", searchOffset);
        size_t macroOffset = colorOffset;
        std::pmr::string macro;
        if (macroOffset == std::pmr::string::npos
            || (depthOffset != std::pmr::string::npos && depthOffset < macroOffset)) {
            macroOffset = depthOffset;
            macro = "SUBPASS_INPUT_DEPTH";
        } else {
            macro = "SUBPASS_INPUT_COLOR";
        }
        if (macroOffset == std::pmr::string::npos) {
            break;
        }
        const auto open = uncommented.find('(', macroOffset + macro.size());
        const auto comma = open == std::pmr::string::npos ? std::pmr::string::npos
                                                          : uncommented.find(',', open + 1);
        const auto close = comma == std::pmr::string::npos ? std::pmr::string::npos
                                                           : uncommented.find(')', comma + 1);
        if (open == std::pmr::string::npos || comma == std::pmr::string::npos
            || close == std::pmr::string::npos) {
            break;
        }

        const auto name = trim(uncommented.substr(open + 1, comma - open - 1));
        const auto indexText = trim(uncommented.substr(comma + 1, close - comma - 1));
        std::uint64_t attachmentIndex = 0;
        bool validIndex = !indexText.empty();
        for (const char digit : indexText) {
            if (digit < '0' || digit > '9') {
                validIndex = false;
                break;
            }
            attachmentIndex = attachmentIndex * 10u + static_cast<std::uint64_t>(digit - '0');
            if (attachmentIndex > UINT32_MAX) {
                validIndex = false;
                break;
            }
        }
        if (!name.empty() && validIndex) {
            while (isOccupied(logicalBinding)) {
                ++logicalBinding;
            }
            bindings.push_back(ShaderBinding{
                name,
                BindingType::InputAttachment,
                0,
                logicalBinding++,
                static_cast<std::uint32_t>(attachmentIndex),
                macro == "SUBPASS_INPUT_DEPTH",
            });
        }
        searchOffset = close + 1;
    }
}

} // namespace

ShaderCompileResult ShaderCompiler::compile(const ShaderCompileRequest& request) const
{
    ShaderCompileResult result;
    result.target = request.target;

    // ShaderCompileTargetAdmission/1: target identities are not RHI backend
    // ordinals. Reject unknown values before publishing source metadata.
    const char* targetName = nullptr;
    switch (request.target) {
    case TargetBackend::SpirV:
        targetName = "spirv";
        break;
    case TargetBackend::Msl:
        targetName = "msl";
        break;
    case TargetBackend::Wgsl:
        targetName = "wgsl";
        break;
    default:
        result.diagnostics = "SHADER-COMPILE-TARGET-UNKNOWN";
        return result;
    }

    if (request.moduleName.empty()) {
        result.diagnostics = "moduleName is required";
        return result;
    }

    if (request.sourceCode.empty()) {
        result.diagnostics = "sourceCode is empty";
        return result;
    }

    if (request.entryPoints.empty()) {
        result.diagnostics = "at least one entry point is required";
        return result;
    }

    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneConstants",
        ShaderBinding{"SceneConstants", BindingType::UniformBuffer, 0, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ClusterLightList",
        ShaderBinding{"ClusterLightList", BindingType::StorageBuffer, 0, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "GiProbeState",
        ShaderBinding{"GiProbeState", BindingType::StorageBuffer, 0, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "GiIrradianceOutput",
        ShaderBinding{"GiIrradianceOutput", BindingType::StorageTexture, 0, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "GiIrradianceInput",
        ShaderBinding{"GiIrradianceInput", BindingType::SampledTexture, 2, 7});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "GiIrradianceSampler",
        ShaderBinding{"GiIrradianceSampler", BindingType::Sampler, 2, 8});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ComputeSmokeOutput",
        // P01 browser smoke uses RWStructuredBuffer<float4>, not the removed
        // RWTexture2D prototype. Keep this runtime reflection in lockstep
        // with the explicit Slang binding and offline reflection artifact.
        ShaderBinding{"ComputeSmokeOutput", BindingType::StorageBuffer, 0, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ComputeSmokeInput",
        ShaderBinding{"ComputeSmokeInput", BindingType::SampledTexture, 1, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ComputeSmokeSampler",
        ShaderBinding{"ComputeSmokeSampler", BindingType::Sampler, 1, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ShadowSkinningMatrices",
        ShaderBinding{"ShadowSkinningMatrices", BindingType::StorageBuffer, 0, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "BaseColorTexture",
        ShaderBinding{"BaseColorTexture", BindingType::SampledTexture, 1, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "NormalTexture",
        ShaderBinding{"NormalTexture", BindingType::SampledTexture, 1, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "MetallicRoughnessTexture",
        ShaderBinding{"MetallicRoughnessTexture", BindingType::SampledTexture, 1, 2});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "OcclusionTexture",
        ShaderBinding{"OcclusionTexture", BindingType::SampledTexture, 1, 4});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "EmissiveTexture",
        ShaderBinding{"EmissiveTexture", BindingType::SampledTexture, 1, 3});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "LinearSampler",
        ShaderBinding{"LinearSampler", BindingType::Sampler, 1, 5});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ClearcoatTexture",
        ShaderBinding{"ClearcoatTexture", BindingType::SampledTexture, 1, 6});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ClearcoatRoughnessTexture",
        ShaderBinding{"ClearcoatRoughnessTexture", BindingType::SampledTexture, 1, 7});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SpecularTexture",
        ShaderBinding{"SpecularTexture", BindingType::SampledTexture, 1, 8});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SpecularColorTexture",
        ShaderBinding{"SpecularColorTexture", BindingType::SampledTexture, 1, 9});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SheenColorTexture",
        ShaderBinding{"SheenColorTexture", BindingType::SampledTexture, 1, 10});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SheenRoughnessTexture",
        ShaderBinding{"SheenRoughnessTexture", BindingType::SampledTexture, 1, 11});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "TransmissionTexture",
        ShaderBinding{"TransmissionTexture", BindingType::SampledTexture, 1, 12});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ThicknessTexture",
        ShaderBinding{"ThicknessTexture", BindingType::SampledTexture, 1, 13});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "IridescenceTexture",
        ShaderBinding{"IridescenceTexture", BindingType::SampledTexture, 1, 14});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "IridescenceThicknessTexture",
        ShaderBinding{"IridescenceThicknessTexture", BindingType::SampledTexture, 1, 15});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ClearcoatNormalTexture",
        ShaderBinding{"ClearcoatNormalTexture", BindingType::SampledTexture, 1, 16});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "IlmTexture",
        ShaderBinding{"IlmTexture", BindingType::SampledTexture, 1, 17});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "LightingRampTexture",
        ShaderBinding{"LightingRampTexture", BindingType::SampledTexture, 1, 18});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "FaceSdfTexture",
        ShaderBinding{"FaceSdfTexture", BindingType::SampledTexture, 1, 19});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "MatcapTexture",
        ShaderBinding{"MatcapTexture", BindingType::SampledTexture, 1, 20});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "FlowTexture",
        ShaderBinding{"FlowTexture", BindingType::SampledTexture, 1, 21});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ScreenTexture",
        ShaderBinding{"ScreenTexture", BindingType::SampledTexture, 1, 22});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "FurCoverageTexture",
        ShaderBinding{"FurCoverageTexture", BindingType::SampledTexture, 1, 23});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "FurNoiseTexture",
        ShaderBinding{"FurNoiseTexture", BindingType::SampledTexture, 1, 24});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneIrradianceCube",
        ShaderBinding{"SceneIrradianceCube", BindingType::SampledTexture, 2, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ScenePrefilteredSpecularCube",
        ShaderBinding{"ScenePrefilteredSpecularCube", BindingType::SampledTexture, 2, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneBrdfIntegrationLut",
        ShaderBinding{"SceneBrdfIntegrationLut", BindingType::SampledTexture, 2, 2});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneIblSampler",
        ShaderBinding{"SceneIblSampler", BindingType::Sampler, 2, 3});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "IrradianceCube",
        ShaderBinding{"IrradianceCube", BindingType::SampledTexture, 2, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "PrefilteredSpecularCube",
        ShaderBinding{"PrefilteredSpecularCube", BindingType::SampledTexture, 2, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "BrdfIntegrationLut",
        ShaderBinding{"BrdfIntegrationLut", BindingType::SampledTexture, 2, 2});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "IblSampler",
        ShaderBinding{"IblSampler", BindingType::Sampler, 2, 3});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ShadowMap",
        ShaderBinding{"ShadowMap", BindingType::SampledTexture, 2, 4});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ShadowSampler",
        ShaderBinding{"ShadowSampler", BindingType::Sampler, 2, 5});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "GiIrradiance",
        ShaderBinding{"GiIrradiance", BindingType::SampledTexture, 2, 6});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "GiSampler",
        ShaderBinding{"GiSampler", BindingType::Sampler, 2, 7});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneShadowAtlas",
        ShaderBinding{"SceneShadowAtlas", BindingType::SampledTexture, 2, 4});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneShadowSampler",
        ShaderBinding{"SceneShadowSampler", BindingType::Sampler, 2, 5});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneShadowParams",
        ShaderBinding{"SceneShadowParams", BindingType::UniformBuffer, 2, 6});
    // TAA resolve shader uses SceneColorTexture at set 0 binding 0 (not the
    // post-process set 1 binding 0 convention). Gate the generic rule below
    // to skip when HistoryColorTexture (TAA marker) is present; the dedicated
    // TAA block further down emits the correct set 0 layout.
    const bool isTaaShader = containsIdentifier(request.sourceCode, "HistoryColorTexture");
    if (!isTaaShader) {
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "SceneColorTexture",
            ShaderBinding{"SceneColorTexture", BindingType::SampledTexture, 1, 0});
    }
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneDepthTexture",
        ShaderBinding{"SceneDepthTexture", BindingType::SampledTexture, 1, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneInputSampler",
        ShaderBinding{"SceneInputSampler", BindingType::Sampler, 1, 2});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneColorSampler",
        ShaderBinding{"SceneColorSampler", BindingType::Sampler, 1, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "StartupSplashLogoTexture",
        ShaderBinding{"StartupSplashLogoTexture", BindingType::SampledTexture, 1, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "StartupSplashLogoSampler",
        ShaderBinding{"StartupSplashLogoSampler", BindingType::Sampler, 1, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "PostProcessInputTexture",
        ShaderBinding{"PostProcessInputTexture", BindingType::SampledTexture, 0, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "PostProcessInputSampler",
        ShaderBinding{"PostProcessInputSampler", BindingType::Sampler, 0, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "BloomTexture",
        ShaderBinding{"BloomTexture", BindingType::SampledTexture, 0, 2});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "LutTexture",
        ShaderBinding{"LutTexture", BindingType::SampledTexture, 0, 3});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ProbeIrradianceOut",
        ShaderBinding{"ProbeIrradianceOut", BindingType::StorageTexture, 0, 0});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ProbeIrradianceHistory",
        ShaderBinding{"ProbeIrradianceHistory", BindingType::SampledTexture, 0, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ProbeSampler",
        ShaderBinding{"ProbeSampler", BindingType::Sampler, 0, 2});
    // Phase 2: depth moments atlas (Rg16Float). Binding 3 is the write target
    // (ProbeDepthMomentsOut, storage), binding 8 is the history read (sampled).
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ProbeDepthMomentsOut",
        ShaderBinding{"ProbeDepthMomentsOut", BindingType::StorageTexture, 0, 3});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ProbeDepthMomentsHistory",
        ShaderBinding{"ProbeDepthMomentsHistory", BindingType::SampledTexture, 0, 8});
    // Phase 3: probe-state SSBO (ProbeStateBuffer, storage read_write). Authored
    // by giProbeRelocateComputeMain, read by probe-update + resolve.
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ProbeStateBuffer",
        ShaderBinding{"ProbeStateBuffer", BindingType::StorageBuffer, 0, 9});
    // Phase 4: scene BVH SSBOs (storage read-only). SceneBvhNodes (binding 10)
    // is the flat AABB tree traversed by the probe-update pass; SceneBvhLeaves
    // (binding 11) is the per-triangle leaf data (v0/v1/v2 + normal + matIdx).
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneBvhNodes",
        ShaderBinding{"SceneBvhNodes", BindingType::StorageBuffer, 0, 10});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneBvhLeaves",
        ShaderBinding{"SceneBvhLeaves", BindingType::StorageBuffer, 0, 11});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "TerrainData",
        ShaderBinding{"TerrainData", BindingType::UniformBuffer, 0, 7});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "GiIrradianceOutput",
        ShaderBinding{"GiIrradianceOutput", BindingType::StorageTexture, 0, 4});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SceneDepth",
        ShaderBinding{"SceneDepth", BindingType::SampledTexture, 0, 5});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "DepthSampler",
        ShaderBinding{"DepthSampler", BindingType::Sampler, 0, 6});
    // --- WoCC SSR bindings --------------------------------------------------
    // Set 0: SceneColor(0, sampled) + SceneDepth(1, sampled) +
    //        SsrReflectionOut(3, storage) + SsrReflectionInput(4, sampled).
    // LinearSampler is intentionally NOT re-registered here: the global
    // LinearSampler entry above (set 1, binding 5) already matches any source
    // declaring "SamplerState LinearSampler", and the C++ SSR renderer does
    // not consume this runtime-hint binding number - it builds its descriptor
    // set layout directly from the shader's [[vk::binding]] annotations and
    // the offline SPIRV-Reflect output in wocc_ssr.reflection.yaml (the
    // authoritative binding source of truth). Gated to SSR-only so the TAA
    // shader (which also declares SceneColorTexture but at a different layout)
    // doesn't pick up SSR's set 0 binding 0 entry coincidentally.
    const bool isSsrShader = containsIdentifier(request.sourceCode, "SsrReflectionOut");
    if (isSsrShader) {
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "SceneColorTexture",
            ShaderBinding{"SceneColorTexture", BindingType::SampledTexture, 0, 0});
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "SceneDepthTexture",
            ShaderBinding{"SceneDepthTexture", BindingType::SampledTexture, 0, 1});
    }
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SsrReflectionOut",
        ShaderBinding{"SsrReflectionOut", BindingType::StorageTexture, 0, 3});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "SsrReflectionInput",
        ShaderBinding{"SsrReflectionInput", BindingType::SampledTexture, 0, 4});
    // --- TAA resolve (wocc_taa) ----------------------------------------------
    // All 8 TAA descriptors live in set 0: scene color (0), history color
    // (1), signed velocity (2), current depth (3), reactive mask (4),
    // history depth (5), history moments (6), linear-clamp sampler (7).
    // HistoryColorTexture
    // is the TAA marker - when present, the generic SceneColorTexture rule
    // above (set 1 binding 0) is gated off, so these dedicated entries emit
    // the correct set 0 layout. The authoritative binding source of truth is
    // the [[vk::binding]] annotations + wocc_taa.reflection.yaml.
    if (isTaaShader) {
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "SceneColorTexture",
            ShaderBinding{"SceneColorTexture", BindingType::SampledTexture, 0, 0});
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "DepthTexture",
            ShaderBinding{"DepthTexture", BindingType::SampledTexture, 0, 3});
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "ReactiveTexture",
            ShaderBinding{"ReactiveTexture", BindingType::SampledTexture, 0, 4});
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "HistoryDepthTexture",
            ShaderBinding{"HistoryDepthTexture", BindingType::SampledTexture, 0, 5});
        appendBindingIfPresent(result.bindings,
            request.sourceCode,
            "HistoryMomentsTexture",
            ShaderBinding{"HistoryMomentsTexture", BindingType::SampledTexture, 0, 6});
    }
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "HistoryColorTexture",
        ShaderBinding{"HistoryColorTexture", BindingType::SampledTexture, 0, 1});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "VelocityTexture",
        ShaderBinding{"VelocityTexture", BindingType::SampledTexture, 0, 2});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "LinearClampSampler",
        ShaderBinding{"LinearClampSampler",
            BindingType::Sampler,
            isTaaShader ? 0u : 0u,
            isTaaShader ? 7u : 4u});
    // --- Subpass input attachments -------------------------------------------
    // The macro call is a logical source declaration. Its attachment index is
    // compiler metadata; no backend target branch is needed in Slang.
    appendSubpassInputBindings(result.bindings, request.sourceCode);
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "DepthInputSampler",
        ShaderBinding{"DepthInputSampler", BindingType::Sampler, 0, 2});
    appendBindingIfPresent(result.bindings,
        request.sourceCode,
        "ColorInputSampler",
        ShaderBinding{"ColorInputSampler", BindingType::Sampler, 0, 3});
    result.reflection.entryPoints = request.entryPoints;
    result.reflection.bindings = result.bindings;
    result.reflection.pushConstantSizeBytes = reflectedPushConstantSize(request.sourceCode);
    result.reflection.vertexAttributes = reflectedVertexAttributes(request.sourceCode);

    std::ostringstream stream;
    stream << "stub-compiled:" << request.moduleName << ":";
    stream << targetName;
    const auto text = arp::core::toPmrString(stream.str());

    result.bytecode.assign(text.begin(), text.end());
    result.success = true;
    return result;
}

} // namespace arp::shader
