#pragma once

#include "assets/asset_pipeline.h"
#include "assets/image_decoder.h"
#include "core/container/array_view.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace arp::renderer {

inline constexpr std::uint32_t kDdgiSceneMaterialFlag = 0x40000000u;
inline constexpr std::uint32_t kDdgiMaterialRecordWords = 12u;

// DdgiSceneMaterials/2: immutable RGBA8; header[3]: 0=sRGB, 1=reference UNORM.
// Version 1 remains valid with header[3]=0.
// A zero-count table is an explicitly empty material domain, not a dummy buffer.
class DdgiSceneMaterials final {
public:
    static float decodeFloat(std::uint32_t bits) noexcept
    {
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    static std::uint32_t encodeFloat(float value) noexcept
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    static constexpr std::size_t kMaximumBytes = 128u * 1024u * 1024u;

    static bool valid(arp::core::ArrayView<std::uint32_t> words) noexcept
    {
        if (words.size() < 4u || (words[2] != 1u && words[2] != 2u) || words[3] > 1u
            || (words[2] == 1u && words[3] != 0u) || words.size_bytes() > kMaximumBytes
            || std::uint64_t(words[1]) != 4ull + std::uint64_t(words[0]) * kDdgiMaterialRecordWords
            || words[1] > words.size())
            return false;
        const auto unit = [](std::uint32_t bits) {
            const auto v = decodeFloat(bits);
            return std::isfinite(v) && v >= 0.f && v <= 1.f;
        };
        const auto wrap = [](std::uint32_t v) {
            return v == 10497u || v == 33071u || v == 33648u;
        };
        for (std::size_t i = 0; i < words[0]; ++i) {
            const auto r = 4u + i * kDdgiMaterialRecordWords;
            for (unsigned c = 0; c < 4; ++c)
                if (!unit(words[r + c]))
                    return false;
            const auto w = words[r + 5], h = words[r + 6];
            if ((w == 0) != (h == 0) || words[r + 7] > 2u || !unit(words[r + 8])
                || !wrap(words[r + 9]) || !wrap(words[r + 10]) || words[r + 11] != 0u
                || std::uint64_t(words[r + 4]) + std::uint64_t(w) * h > words.size() - words[1])
                return false;
        }
        return true;
    }

    static std::array<float, 4> sample(
        core::ArrayView<std::uint32_t> words, std::uint32_t material, float u, float v)
    {
        const auto slot = material & 0x3fffffffu;
        if ((material & 0xc0000000u) != kDdgiSceneMaterialFlag || slot >= words[0])
            throw std::runtime_error("DDGI-MATERIAL-SAMPLE-DOMAIN-INVALID");
        const auto record = 4u + slot * kDdgiMaterialRecordWords;
        std::array<float, 4> result{};
        for (unsigned c = 0; c < 4; ++c)
            result[c] = decodeFloat(words[record + c]);
        const auto w = words[record + 5], h = words[record + 6];
        if (!w || !h)
            return result;
        const auto address = [](int x, int n, std::uint32_t wrap) {
            if (wrap == 33071u)
                return std::clamp(x, 0, n - 1);
            if (wrap == 33648u) {
                const int p = ((x % (2 * n)) + 2 * n) % (2 * n);
                return p < n ? p : 2 * n - 1 - p;
            }
            return ((x % n) + n) % n;
        };
        const float px = u * w - .5f, py = v * h - .5f;
        const int x = int(std::floor(px)), y = int(std::floor(py));
        const float fx = px - x, fy = py - y;
        std::array<float, 4> filtered{};
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const auto index = words[1] + words[record + 4]
                    + address(y + dy, int(h), words[record + 10]) * w
                    + address(x + dx, int(w), words[record + 9]);
                const auto pixel = words[index];
                const float weight = (dx ? fx : 1.f - fx) * (dy ? fy : 1.f - fy);
                for (unsigned c = 0; c < 4; ++c) {
                    float value = float((pixel >> (c * 8u)) & 255u) / 255.f;
                    if (c < 3 && words[3] == 0u)
                        value = value <= .04045f ? value / 12.92f
                                                 : std::pow((value + .055f) / 1.055f, 2.4f);
                    filtered[c] += value * weight;
                }
            }
        for (unsigned c = 0; c < 4; ++c)
            result[c] *= filtered[c];
        return result;
    }

    static bool visible(core::ArrayView<std::uint32_t> words, std::uint32_t material, float alpha)
    {
        const auto r = 4u + (material & 0x3fffffffu) * kDdgiMaterialRecordWords;
        return words[r + 7] == 0u || (words[r + 7] == 1u && alpha >= decodeFloat(words[r + 8]));
    }

    std::uint32_t append(const assets::ImportedMaterialAsset& material,
        const core::pmr::Vector<assets::ImportedTextureAsset>& textures,
        const core::pmr::Vector<assets::ImportedImageAsset>& images,
        bool referenceUnorm = false)
    {
        if (material.baseColorTextureCoordinateSet != 0u)
            throw std::runtime_error("DDGI-MATERIAL-UV-SET-UNSUPPORTED");
        if (!std::isfinite(material.alphaCutoff) || material.alphaCutoff < 0.f
            || material.alphaCutoff > 1.f)
            throw std::runtime_error("DDGI-MATERIAL-ALPHA-INVALID");
        if (!records_.empty() && referenceUnorm != referenceUnorm_)
            throw std::runtime_error("DDGI-MATERIAL-COLOR-DOMAIN-MISMATCH");
        Record record{};
        for (unsigned i = 0; i < 4; ++i) {
            if (!std::isfinite(material.baseColorFactor[i]) || material.baseColorFactor[i] < 0.f
                || material.baseColorFactor[i] > 1.f)
                throw std::runtime_error("DDGI-MATERIAL-FACTOR-INVALID");
            record.words[i] = encodeFloat(material.baseColorFactor[i]);
        }
        record.words[4] = static_cast<std::uint32_t>(pixels_.size());
        record.words[7] = static_cast<std::uint32_t>(material.alphaMode);
        record.words[8] = encodeFloat(material.alphaCutoff);
        record.words[9] = record.words[10] = 10497u;
        if (material.baseColorTexture != assets::invalidImportedTextureIndex) {
            if (material.baseColorTexture >= textures.size())
                throw std::runtime_error("DDGI-MATERIAL-TEXTURE-INVALID");
            const auto& texture = textures[material.baseColorTexture];
            if (texture.sourceImage >= images.size())
                throw std::runtime_error("DDGI-MATERIAL-IMAGE-INVALID");
            const auto validWrap = [](auto wrap) {
                return wrap == 10497u || wrap == 33071u || wrap == 33648u;
            };
            if (!validWrap(texture.wrapS) || !validWrap(texture.wrapT))
                throw std::runtime_error("DDGI-MATERIAL-SAMPLER-INVALID");
            record.words[9] = texture.wrapS;
            record.words[10] = texture.wrapT;
            const auto& image = images[texture.sourceImage];
            auto decoded = assets::decodeImageToRgba8(image.encodedBytes, image.mimeType, true);
            if (!decoded.success || !decoded.image.width || !decoded.image.height)
                throw std::runtime_error("DDGI-MATERIAL-DECODE-FAILED");
            const auto& rgba = decoded.image.rgba8;
            const auto count = static_cast<std::size_t>(decoded.image.width) * decoded.image.height;
            if (rgba.size() != count * 4u
                || (4u + (records_.size() + 1u) * kDdgiMaterialRecordWords + pixels_.size() + count)
                        * 4u
                    > kMaximumBytes)
                throw std::runtime_error("DDGI-MATERIAL-BUDGET-EXCEEDED");
            record.words[5] = decoded.image.width;
            record.words[6] = decoded.image.height;
            pixels_.reserve(pixels_.size() + count);
            for (std::size_t p = 0; p < count; ++p)
                pixels_.push_back(std::uint32_t(rgba[p * 4])
                    | (std::uint32_t(rgba[p * 4 + 1]) << 8u)
                    | (std::uint32_t(rgba[p * 4 + 2]) << 16u)
                    | (std::uint32_t(rgba[p * 4 + 3]) << 24u));
        }
        const auto index = static_cast<std::uint32_t>(records_.size());
        records_.push_back(record);
        referenceUnorm_ = referenceUnorm;
        return kDdgiSceneMaterialFlag | index;
    }

    core::pmr::Vector<std::uint32_t> pack() const
    {
        core::pmr::Vector<std::uint32_t> words(&core::defaultCpuMemoryResource());
        const auto pixelBase = 4u + records_.size() * kDdgiMaterialRecordWords;
        if ((pixelBase + pixels_.size()) * 4u > kMaximumBytes)
            throw std::runtime_error("DDGI-MATERIAL-BUDGET-EXCEEDED");
        words.reserve(pixelBase + pixels_.size());
        words.insert(words.end(),
            {static_cast<std::uint32_t>(records_.size()),
                static_cast<std::uint32_t>(pixelBase),
                2u,
                referenceUnorm_ ? 1u : 0u});
        for (const auto& record : records_)
            words.insert(words.end(), record.words, record.words + kDdgiMaterialRecordWords);
        words.insert(words.end(), pixels_.begin(), pixels_.end());
        return words;
    }

    std::size_t size() const { return records_.size(); }

private:
    bool referenceUnorm_ = false;

    struct Record {
        std::uint32_t words[kDdgiMaterialRecordWords]{};
    };

    core::pmr::Vector<Record> records_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> pixels_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::renderer
