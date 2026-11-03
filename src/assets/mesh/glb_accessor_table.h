#pragma once

#include "assets/mesh/glb_geometry_schema.h"
#include "core/encoding/unsigned_element_span.h"

#include <cstring>
#include <limits>

namespace arp::assets::detail {

// Borrowed little-endian glTF values, in accessor component order. MAT4 remains
// glTF column order here; this byte view does not establish a CPU/GPU matrix ABI.
// The immutable source bytes must outlive the table and all copied spans.
// No read allocates or mutates the output on failure.
class GlbAccessorSpan final {
public:
    std::uint32_t count() const noexcept { return count_; }

    std::uint32_t components() const noexcept { return components_; }

    std::uint32_t componentType() const noexcept { return componentType_; }

    bool normalized() const noexcept { return normalized_; }

    bool readUnsigned(
        std::uint32_t element, std::uint32_t component, std::uint32_t& output) const noexcept
    {
        if (normalized_ || componentType_ == 5126u)
            return false;
        std::uint32_t value;
        if (!rawUnsigned(element, component, value))
            return false;
        output = value;
        return true;
    }

    bool readFloat(std::uint32_t element, std::uint32_t component, float& output) const noexcept
    {
        if (componentType_ != 5126u && !normalized_)
            return false;
        std::uint32_t bits;
        if (!rawUnsigned(element, component, bits))
            return false;
        float value;
        if (componentType_ == 5126u) {
            static_assert(sizeof(float) == sizeof(bits) && std::numeric_limits<float>::is_iec559);
            std::memcpy(&value, &bits, sizeof(value));
        } else if (componentType_ == 5121u)
            value = static_cast<float>(bits) / UINT8_MAX;
        else if (componentType_ == 5123u)
            value = static_cast<float>(bits) / UINT16_MAX;
        else
            return false;
        if (!std::isfinite(value))
            return false;
        output = value;
        return true;
    }

private:
    friend class GlbAccessorTable;

    bool rawUnsigned(
        std::uint32_t element, std::uint32_t component, std::uint32_t& output) const noexcept
    {
        if (!bytes_ || element >= count_ || component >= components_)
            return false;
        const auto* data = bytes_ + offset_ + std::size_t(element) * stride_ + component * width_;
        if (width_ == 1) {
            output = *data;
            return true;
        }
        const core::UnsignedElementSpan values(data,
            width_,
            width_ == 2 ? core::UnsignedElementSpan::Width::Uint16
                        : core::UnsignedElementSpan::Width::Uint32,
            core::UnsignedElementSpan::ByteOrder::LittleEndian);
        return values.read(0, output);
    }

    const std::uint8_t* bytes_ = nullptr;
    std::size_t offset_ = 0, stride_ = 0;
    std::uint32_t count_ = 0, components_ = 0, componentType_ = 0, width_ = 0;
    bool normalized_ = false;
};

// Cold layout preparation only, not a skin/animation/material validator or an
// importer. The caller supplies a strict, bounded JSON tree. Owned descriptor
// storage, including the previous table plus its replacement candidate, is
// bounded separately from borrowed JSON/BIN bytes. Failed preparation preserves
// the previous complete table.
class GlbAccessorTable final : private GlbGeometrySchema {
public:
    struct Limits final {
        std::size_t maxBinaryBytes = 0, maxBufferViews = 0, maxAccessors = 0;
        std::size_t maxDescriptorBytes = 0;
        std::uint64_t maxElements = 0, maxScalars = 0;
    };

    const GlbAccessorSpan* find(std::size_t index) const noexcept
    {
        return index < accessors_.size() ? &accessors_[index] : nullptr;
    }

    std::size_t size() const noexcept { return accessors_.size(); }

    bool prepare(
        const Json& root, const core::pmr::String& binary, Limits limits, const char*& diagnostic)
    {
        diagnostic = nullptr;
        try {
            require(limits.maxBinaryBytes && limits.maxBinaryBytes <= UINT32_MAX
                    && limits.maxBufferViews && limits.maxBufferViews <= UINT32_MAX
                    && limits.maxAccessors && limits.maxAccessors <= UINT32_MAX
                    && limits.maxDescriptorBytes && limits.maxDescriptorBytes != SIZE_MAX
                    && limits.maxElements && limits.maxElements != UINT64_MAX && limits.maxScalars
                    && limits.maxScalars != UINT64_MAX,
                "ASSET-GLB-ACCESSOR-LIMITS");
            const auto& views = array(root, "bufferViews");
            const auto& definitions = array(root, "accessors");
            const auto descriptorSlots = limits.maxDescriptorBytes / sizeof(GlbAccessorSpan);
            require(binary.size() <= limits.maxBinaryBytes && views.size() <= limits.maxBufferViews
                    && definitions.size() <= limits.maxAccessors
                    && accessors_.capacity() <= descriptorSlots
                    && definitions.size() <= descriptorSlots - accessors_.capacity(),
                "ASSET-GLB-ACCESSOR-BUDGET");
            validateLayout(root, binary, GlbLayoutProfile::Animated);
            core::pmr::Vector<GlbAccessorSpan> candidate{accessors_.get_allocator().resource()};
            candidate.reserve(definitions.size());
            require(candidate.capacity() <= descriptorSlots - accessors_.capacity(),
                "ASSET-GLB-ACCESSOR-BUDGET");
            std::uint64_t elements = 0, scalars = 0;
            for (const auto& definition : definitions) {
                const auto& view = views[number(definition, "bufferView")];
                GlbAccessorSpan value;
                value.bytes_ = reinterpret_cast<const std::uint8_t*>(binary.data());
                value.offset_ = std::size_t(optional(view, "byteOffset", 0))
                    + optional(definition, "byteOffset", 0);
                value.count_ = number(definition, "count");
                value.componentType_ = number(definition, "componentType");
                value.components_ = dimensions(definition, GlbLayoutProfile::Animated);
                value.width_ = componentBytes(value.componentType_);
                value.stride_ = optional(view, "byteStride", value.width_ * value.components_);
                if (const auto* normalized = definition.find("normalized"))
                    value.normalized_ = normalized->boolValue;
                require(value.count_ <= limits.maxElements - elements
                        && std::uint64_t(value.count_) * value.components_
                            <= limits.maxScalars - scalars,
                    "ASSET-GLB-ACCESSOR-BUDGET");
                elements += value.count_;
                scalars += std::uint64_t(value.count_) * value.components_;
                candidate.push_back(value);
            }
            accessors_.swap(candidate);
            return true;
        } catch (const Failure& failure) {
            diagnostic = failure.diagnostic;
        } catch (const std::bad_alloc&) {
            diagnostic = "ASSET-GLB-ACCESSOR-ALLOCATION";
        }
        return false;
    }

private:
    core::pmr::Vector<GlbAccessorSpan> accessors_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::assets::detail
