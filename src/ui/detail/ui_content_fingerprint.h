#pragma once

#include "core/hash/stable_hash.h"
#include "resource/resource_manager.h"
#include "ui/ui.h"
#include "ui/ui_font.h"
#include "ui/ui_font_source.h"

#include <filesystem>
#include <string_view>
#include <type_traits>

namespace arp::ui::detail {

// Cold, allocation-free encoding for UiContentFingerprint/1. Strings and
// sequences are length delimited; pointers, padding and filesystem IO never
// participate. Native path code units are exact, build-local location identity.
class UiContentFingerprint final {
public:
    explicit UiContentFingerprint(core::StableHash64& hash) noexcept : hash_(hash) {}

    void text(std::string_view value) noexcept
    {
        number(value.size());
        hash_.appendString(value);
    }

    void number(std::uint64_t value) noexcept { hash_.appendU64(value); }

    void scalar(float value) noexcept { hash_.appendFloat(value); }

    void path(const std::filesystem::path& value) noexcept
    {
        using Unit = std::make_unsigned_t<std::filesystem::path::value_type>;
        number(sizeof(Unit));
        number(value.native().size());
        for (const auto unit : value.native())
            number(static_cast<Unit>(unit));
    }

    void rect(const LayoutRect& value) noexcept
    {
        scalar(value.x);
        scalar(value.y);
        scalar(value.width);
        scalar(value.height);
    }

    void color(const UiControlColorValue& value) noexcept
    {
        scalar(value.r);
        scalar(value.g);
        scalar(value.b);
        scalar(value.a);
    }

    void font(const UiFontFaceDesc& value) noexcept
    {
        text(value.tokenId);
        text(value.family);
        path(value.sourcePath);
        number(value.fallbackChain.size());
        for (const auto& token : value.fallbackChain)
            text(token);
        number(value.supportedScripts.size());
        for (const auto& script : value.supportedScripts)
            text(script);
        text(value.charset);
        number(value.sizePx);
        number(value.weight);
        number(value.lineHeightPx);
        text(value.renderMode);
        number(value.source != nullptr);
        if (value.source) {
            // Source transport versions, mount handles and acquisition budgets
            // are provenance, not font content. Hash the retained raw bytes.
            const auto bytes = value.source->bytes();
            number(bytes.size());
            hash_.appendBytes(bytes.data(), bytes.size());
        }
    }

    void resource(const resource::ResourceBase& value, std::string_view contract) noexcept
    {
        text("UiContentFingerprint/1");
        text(contract);
        text(value.reference().alias);
        number(value.dependencies().size());
        for (const auto& dependency : value.dependencies()) {
            text(dependency.id);
            text(dependency.usage);
        }
    }

    void draw(const DrawRecord& value) noexcept
    {
        number(static_cast<std::uint64_t>(value.kind));
        number(value.widgetId.value());
        rect(value.rect);
        rect(value.clipRect);
        text(value.text);
        text(value.textureResourceId);
        rect(value.textureSourceRect);
        number(value.hasTextureSourceRect);
        scalar(value.sliceLeft);
        scalar(value.sliceTop);
        scalar(value.sliceRight);
        scalar(value.sliceBottom);
        number(value.hasNineSlice);
        text(value.styleId);
        text(value.fontToken);
        number(value.sortKey);
        number(value.zOrder);
        // Material/texture runtime handles are assigned by consumers and are
        // deliberately excluded from authoring content identity.
    }

    // UI style dictionaries are unordered. Cold fingerprinting selects keys
    // lexicographically without allocating a temporary array in a noexcept hook.
    // This is O(n^2) in the admitted style count, never a per-frame operation.
    template <class Map, class Append> void dictionary(const Map& values, Append append) noexcept
    {
        number(values.size());
        const typename Map::value_type* previous = nullptr;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const typename Map::value_type* next = nullptr;
            for (const auto& entry : values)
                if ((!previous || previous->first < entry.first)
                    && (!next || entry.first < next->first))
                    next = &entry;
            text(next->first);
            append(next->second);
            previous = next;
        }
    }

private:
    core::StableHash64& hash_;
};

} // namespace arp::ui::detail
