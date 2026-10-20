#pragma once

#include "core/unsigned_range.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/gpu_bindless.h"

#include <algorithm>
#include <limits>

namespace arp::rhi {

struct BindlessMetadataTableSource {
    const BindlessTableMapping* mapping = nullptr;
    std::uint32_t activeCount = 0;
};

// A stable preparation-only view. The builder owns neither resources nor memory.
class BindlessSnapshotMetadataSource {
public:
    virtual ~BindlessSnapshotMetadataSource() = default;
    virtual std::size_t tableCount() const noexcept = 0;
    virtual bool readTable(std::size_t, BindlessMetadataTableSource&) const = 0;
    virtual bool readElementCount(std::size_t, std::uint32_t, std::uint32_t&) const = 0;
};

// One actual buffer per logical table. This preparation-only source derives
// counts from approved strides and actual extents; it issues no authorization.
struct BindlessMetadataBufferResource {
    std::uint32_t tableId = 0u;
    GpuResourceId resource;
    std::uint64_t sizeBytes = 0u;
};

class BindlessSingleBufferMetadataSource final : public BindlessSnapshotMetadataSource {
public:
    BindlessSingleBufferMetadataSource(const BindlessTableMapping* tables,
        std::size_t tableCount,
        const BindlessMetadataBufferResource* resources,
        std::size_t resourceCount) noexcept
        : tables_(tables), tableCount_(tableCount), resources_(resources),
          resourceCount_(resourceCount)
    {}

    std::size_t tableCount() const noexcept override { return tableCount_; }

    bool readTable(std::size_t i, BindlessMetadataTableSource& out) const override
    {
        out = {};
        if (!tables_ || !resources_ || i >= tableCount_ || tableCount_ != resourceCount_)
            return false;
        out = {&tables_[i], 1u};
        return true;
    }

    bool readElementCount(std::size_t i, std::uint32_t element, std::uint32_t& out) const override
    {
        out = 0u;
        if (!tables_ || !resources_ || i >= tableCount_ || tableCount_ != resourceCount_ || element)
            return false;
        const BindlessMetadataBufferResource* found = nullptr;
        for (std::size_t r = 0u; r < resourceCount_; ++r) {
            const auto& resource = resources_[r];
            if (!resource.tableId || !resource.resource.isValid() || !resource.sizeBytes)
                return false;
            for (std::size_t previous = 0u; previous < r; ++previous)
                if (resources_[previous].tableId == resource.tableId
                    || resources_[previous].resource == resource.resource)
                    return false;
            if (resource.tableId == tables_[i].tableId) {
                if (found)
                    return false;
                found = &resource;
            }
        }
        const auto stride = tables_[i].bufferLayout.elementStrideBytes;
        if (!found || !stride || found->sizeBytes % stride
            || found->sizeBytes / stride > UINT32_MAX)
            return false;
        out = static_cast<std::uint32_t>(found->sizeBytes / stride);
        return out != 0u;
    }

private:
    const BindlessTableMapping* tables_;
    std::size_t tableCount_;
    const BindlessMetadataBufferResource* resources_;
    std::size_t resourceCount_;
};

// One explicitly ordered sampled-image table. Active count comes from admitted
// inventory rows; no inactive padding or generated placeholder is supplied.
class BindlessTextureInventoryMetadataSource final : public BindlessSnapshotMetadataSource {
public:
    BindlessTextureInventoryMetadataSource(const BindlessTableMapping& table,
        const BindlessTextureInventoryResource* resources,
        std::size_t count) noexcept
        : table_(table), resources_(resources), count_(count)
    {}

    std::size_t tableCount() const noexcept override { return 1u; }

    bool readTable(std::size_t i, BindlessMetadataTableSource& out) const override
    {
        out = {};
        if (i || diagnostic())
            return false;
        out = {&table_, static_cast<std::uint32_t>(count_)};
        return true;
    }

    bool readElementCount(std::size_t i, std::uint32_t element, std::uint32_t& out) const override
    {
        out = 0u;
        // SnapshotMetadata/1 non-buffer element-count fields are exactly0.
        return !i && element < count_ && !diagnostic();
    }

    const char* diagnostic() const noexcept
    {
        if (!resources_ || !count_ || count_ > UINT32_MAX || count_ > table_.capacity
            || !table_.tableId || table_.bufferLayout.identity
            || table_.bufferLayout.elementStrideBytes || table_.bufferLayout.elementAlignmentBytes
            || table_.bufferLayout.minBindingSizeBytes)
            return "RHI-BINDLESS-TEXTURE-INVENTORY-TABLE";
        for (std::size_t i = 0u; i < count_; ++i) {
            if (const auto* id = resources_[i].diagnostic())
                return id;
            for (std::size_t j = 0u; j < i; ++j) {
                const auto& a = resources_[i];
                const auto& b = resources_[j];
                if (a.view == b.parent || a.parent == b.view || (a.view == b.view && !a.matches(b))
                    || (a.parent == b.parent && !a.sameParent(b)))
                    return "RHI-BINDLESS-TEXTURE-INVENTORY-CONFLICT";
            }
        }
        return nullptr;
    }

private:
    const BindlessTableMapping& table_;
    const BindlessTextureInventoryResource* resources_;
    std::size_t count_;
};

inline const char* bindlessSnapshotMetadataDiagnostic(const BindlessSnapshotMetadataLayout& layout,
    std::uint64_t generation,
    const BindlessSnapshotMetadataSource& source,
    std::uint32_t* words,
    std::size_t wordCount)
{
    if (layout.contractVersion != 1u || !generation || !words || wordCount != layout.wordCount
        || wordCount < 4u || !source.tableCount() || source.tableCount() > UINT32_MAX)
        return "RHI-BINDLESS-METADATA-BUILD-INVALID";
    std::uint64_t expectedWords = 4u;
    for (std::size_t i = 0u; i < source.tableCount(); ++i) {
        BindlessMetadataTableSource table;
        if (!source.readTable(i, table) || !table.mapping || !table.mapping->tableId
            || !table.mapping->capacity || !table.activeCount
            || table.activeCount > table.mapping->capacity || table.mapping->metadataWordOffset < 4u
            || !core::containsUnsignedRange(std::uint64_t{0u},
                std::uint64_t{wordCount},
                std::uint64_t{table.mapping->metadataWordOffset},
                4ull + table.mapping->capacity))
            return "RHI-BINDLESS-METADATA-BUILD-TABLE";
        expectedWords += 4ull + table.mapping->capacity;
        for (std::size_t previous = 0u; previous < i; ++previous) {
            BindlessMetadataTableSource other;
            if (!source.readTable(previous, other) || !other.mapping)
                return "RHI-BINDLESS-METADATA-BUILD-TABLE";
            const auto begin = std::uint64_t{table.mapping->metadataWordOffset};
            const auto otherBegin = std::uint64_t{other.mapping->metadataWordOffset};
            if (table.mapping->tableId == other.mapping->tableId
                || (begin < otherBegin + 4ull + other.mapping->capacity
                    && otherBegin < begin + 4ull + table.mapping->capacity))
                return "RHI-BINDLESS-METADATA-BUILD-OVERLAP";
        }
        for (std::uint32_t element = 0u; element < table.activeCount; ++element) {
            std::uint32_t count = 0u;
            if (!source.readElementCount(i, element, count)
                || (table.mapping->bufferLayout.elementStrideBytes ? !count : count != 0u))
                return "RHI-BINDLESS-ACCESS-METADATA-MISSING";
        }
    }
    if (expectedWords != wordCount)
        return "RHI-BINDLESS-METADATA-BUILD-EXTENT";
    std::fill_n(words, wordCount, 0u);
    words[0] = layout.contractVersion;
    words[1] = static_cast<std::uint32_t>(source.tableCount());
    words[2] = static_cast<std::uint32_t>(generation);
    words[3] = static_cast<std::uint32_t>(generation >> 32u);
    for (std::size_t i = 0u; i < source.tableCount(); ++i) {
        BindlessMetadataTableSource table;
        if (!source.readTable(i, table) || !table.mapping)
            return "RHI-BINDLESS-METADATA-BUILD-TABLE";
        const auto offset = table.mapping->metadataWordOffset;
        words[offset] = table.mapping->tableId;
        words[offset + 1u] = table.activeCount;
        words[offset + 2u] = table.mapping->capacity;
        for (std::uint32_t element = 0u; element < table.activeCount; ++element)
            if (!source.readElementCount(i, element, words[offset + 4u + element]))
                return "RHI-BINDLESS-ACCESS-METADATA-MISSING";
    }
    return nullptr;
}

inline ValidationResult buildBindlessSnapshotMetadata(const BindlessSnapshotMetadataLayout& layout,
    std::uint64_t generation,
    const BindlessSnapshotMetadataSource& source,
    std::uint32_t* words,
    std::size_t wordCount)
{
    if (const auto* id =
            bindlessSnapshotMetadataDiagnostic(layout, generation, source, words, wordCount))
        return {false, id};
    return {true, {}};
}

} // namespace arp::rhi
