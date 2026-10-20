#pragma once

#include "core/artifact_path.h"
#include "core/file_io.h"
#include "core/sha256.h"
#include "core/unsigned_element_span.h"
#include "core/yaml_contract.h"
#include "original_graphics_wocc_ibl_data_fixture.h"

namespace arp::test {
// Cold test evidence input, not a production resource format or bake fallback.
// All platforms consume the exact authored bytes and existing upload metadata.
class WoccIblFrozenInputsFixture final {
    using Data = OriginalGraphicsWoccIblDataFixture;
    using Y = core::YamlContract;

    static void require(bool valid, const char* diagnostic)
    {
        if (!valid)
            throw core::YamlContractError{diagnostic};
    }

public:
    static constexpr std::uint64_t payloadBytes = 2u * Data::cubePixelBytes + 128u * 128u * 8u;
    static constexpr std::uint64_t manifestBytes = 65536u;

    static std::shared_ptr<const Data::Inputs> load(
        const std::filesystem::path& manifest, std::uint64_t maxPayloadBytes)
    {
        const auto text = core::readTextFile(manifest, manifestBytes);
        require(text.success, "BINDLESS-IBL-INPUTS-MANIFEST-READ");
        return fromText(text.text, manifest.parent_path(), maxPayloadBytes);
    }

    static std::shared_ptr<const Data::Inputs> fromText(core::StringView text,
        const std::filesystem::path& directory,
        std::uint64_t maxPayloadBytes)
    {
        require(text.size() <= manifestBytes, "BINDLESS-IBL-INPUTS-MANIFEST-BUDGET");
        const auto node = Y::Node::deserialize(text.begin(), text.end());
        require(Y::text(node, "type") == "BindlessWoccIblInputs"
                && Y::uint32(node, "contractVersion") == 1u,
            "BINDLESS-IBL-INPUTS-SCHEMA");
        require(Y::text(node, "encoding") == "Rgba16FloatLittleEndian"
                && Y::text(node, "cubeFaceOrder") == "+X,-X,+Y,-Y,+Z,-Z",
            "BINDLESS-IBL-INPUTS-ENCODING");
        require(Y::uint64(Y::required(node, "payloadBytes")) == payloadBytes
                && maxPayloadBytes >= payloadBytes,
            "BINDLESS-IBL-INPUTS-BUDGET");
        const auto& resources = Y::sequence(node, "resources");
        require(resources.size() == 3u, "BINDLESS-IBL-INPUTS-RESOURCES");
        const std::array<const char*, 3u> roles{"horizon", "zenith", "brdf"};
        const std::array<std::uint32_t, 3u> bindings{0u, 3u, 1u};
        auto result = core::makeShared<Data::Inputs>(Data::Inputs::Frozen{});
        for (std::size_t i = 0u; i < resources.size(); ++i) {
            const auto& resource = resources[i];
            const bool cube = i != 2u;
            const auto width = cube ? 64u : 128u;
            const auto layers = cube ? 6u : 1u, mips = cube ? 7u : 1u;
            const auto bytes = cube ? Data::cubePixelBytes : 128u * 128u * 8u;
            require(Y::text(resource, "role") == roles[i]
                    && Y::uint32(resource, "binding") == bindings[i]
                    && Y::text(resource, "dimension") == (cube ? "TextureCube" : "Texture2D")
                    && Y::uint32(resource, "width") == width
                    && Y::uint32(resource, "height") == width
                    && Y::uint32(resource, "layers") == layers
                    && Y::uint32(resource, "mipLevels") == mips
                    && Y::uint64(Y::required(resource, "byteCount")) == bytes,
                "BINDLESS-IBL-INPUTS-RESOURCE-METADATA");
            const auto name = Y::text(resource, "file");
            require(core::isFlatArtifactFilename(name), "BINDLESS-IBL-INPUTS-PATH");
            const auto& layouts = Y::sequence(resource, "mipLayouts");
            require(layouts.size() == mips, "BINDLESS-IBL-INPUTS-MIP-LAYOUT");
            std::uint64_t offset = 0u;
            for (std::uint32_t mip = 0u; mip < mips; ++mip) {
                const auto& layout = layouts[mip];
                const auto size = width >> mip;
                const std::uint64_t count = std::uint64_t{size} * size * layers * 4u;
                require(Y::uint32(layout, "level") == mip
                        && Y::uint64(Y::required(layout, "offsetBytes")) == offset
                        && Y::uint64(Y::required(layout, "texelCount")) == count,
                    "BINDLESS-IBL-INPUTS-MIP-LAYOUT");
                if (cube)
                    result->cubes[i].mips.push_back(
                        {static_cast<std::size_t>(offset / 2u), static_cast<std::size_t>(count)});
                offset += count * 2u;
            }
            require(offset == bytes, "BINDLESS-IBL-INPUTS-MIP-LAYOUT");
            const auto input = core::readBinaryFile(directory / name.c_str(), bytes);
            require(input.success && input.bytes.size() == bytes, "BINDLESS-IBL-INPUTS-READ");
            const auto hash = Y::text(resource, "sha256");
            require(
                core::Sha256::matchesHexDigest(
                    {reinterpret_cast<const char*>(input.bytes.data()), input.bytes.size()}, hash),
                "BINDLESS-IBL-INPUTS-HASH");
            core::UnsignedElementSpan elements(input.bytes.data(),
                input.bytes.size(),
                core::UnsignedElementSpan::Width::Uint16,
                core::UnsignedElementSpan::ByteOrder::LittleEndian);
            require(!elements.diagnostic(), "BINDLESS-IBL-INPUTS-ENCODING");
            auto& storage = cube ? result->cubes[i].storage : result->lut;
            storage.resize(elements.size());
            for (std::size_t word = 0u; word < storage.size(); ++word) {
                std::uint32_t value = 0u;
                require(elements.read(word, value), "BINDLESS-IBL-INPUTS-ENCODING");
                storage[word] = static_cast<std::uint16_t>(value);
                require(std::isfinite(core::ibl::halfToFloat(storage[word])),
                    "BINDLESS-IBL-INPUTS-NONFINITE");
            }
            if (cube) {
                result->cubes[i].baseResolution = width;
                result->cubes[i].mipCount = mips;
            }
        }
        return result;
    }
};
} // namespace arp::test
