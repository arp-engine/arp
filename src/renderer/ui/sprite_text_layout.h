#pragma once
#include "2d/sprite_draw_plan.h"
#include "renderer/ui/ui_text_layout.h"

namespace arp::renderer::ui {
using SpriteGlyph = scene2d::SpriteFontGlyph;

// Adapter from the existing shaping service to pre-baked bitmap sprite glyphs.
// Glyph advances are authored metadata, never guessed from bitmap content.
// Prepare when text changes; append uses retained bounded layouts each frame.
class SpriteTextLayout final {
public:
    explicit SpriteTextLayout(std::uint32_t capacity) : capacity_(capacity)
    {
        glyphs_.reserve(capacity);
    }

    bool setCapacity(std::uint32_t capacity)
    {
        if (capacity == 0u)
            return false;
        capacity_ = capacity;
        glyphs_.clear();
        glyphs_.reserve(capacity);
        width_ = 0.0f;
        diagnostic_ = nullptr;
        return true;
    }

    void clear()
    {
        glyphs_.clear();
        width_ = 0.0f;
        diagnostic_ = nullptr;
    }

    bool prepare(std::string_view text, const core::pmr::Vector<SpriteGlyph>& font)
    {
        diagnostic_ = nullptr;
        glyphs_.clear();
        width_ = 0;
        if (text.size() > capacity_) {
            diagnostic_ = "ARP-SPRITE-TEXT-CAPACITY";
            return false;
        }
        TextLayoutKey key;
        key.text = core::pmr::String(text);
        key.script = TextScript::Latin;
        key.direction = TextDirection::LeftToRight;
        TextShapingService shaping(TextShapingCapability::BasicLatinUtf8);
        auto result = shaping.shape(key);
        if (!result.success) {
            diagnostic_ = "ARP-SPRITE-TEXT-SHAPING";
            return false;
        }
        for (const auto& glyph : result.glyphs) {
            const auto found = std::find_if(font.begin(), font.end(), [&](const auto& f) {
                return f.codepoint == glyph.codepoint;
            });
            if (found == font.end() || !found->sprite.ready() || !std::isfinite(found->advance)
                || found->advance <= 0) {
                glyphs_.clear();
                diagnostic_ = "ARP-SPRITE-TEXT-GLYPH";
                return false;
            }
            glyphs_.push_back({found->sprite, width_});
            width_ += found->advance;
        }
        return true;
    }

    bool append(core::pmr::Vector<scene2d::SpriteDrawItem>& output,
        std::uint32_t outputBudget,
        core::Vec2 topLeft,
        float scale,
        core::Vec4 color,
        std::int32_t layer,
        std::uint64_t& objectId) const
    {
        if (diagnostic_ || !std::isfinite(scale) || scale <= 0 || output.size() > outputBudget
            || glyphs_.size() > outputBudget - output.size()
            || glyphs_.size() > UINT64_MAX - objectId)
            return false;
        for (const auto& glyph : glyphs_) {
            scene2d::SpriteDrawItem item;
            item.sprite = glyph.sprite;
            item.color = color;
            item.world.translation = {topLeft.x + glyph.x * scale, topLeft.y, 0};
            item.world.scale = {scale, scale, 1};
            item.sorting = {layer, 0, false, 0, objectId++};
            output.push_back(std::move(item));
        }
        return true;
    }

    std::size_t glyphCount() const { return glyphs_.size(); }

    float width() const { return width_; }

    const char* diagnostic() const { return diagnostic_; }

private:
    struct Glyph {
        resource::ResourceHandle<scene2d::SpriteResource> sprite;
        float x = 0;
    };

    std::uint32_t capacity_;
    float width_ = 0;
    const char* diagnostic_ = nullptr;
    core::pmr::Vector<Glyph> glyphs_;
};
} // namespace arp::renderer::ui
