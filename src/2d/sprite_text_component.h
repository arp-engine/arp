#pragma once
#include "2d/components.h"
#include "2d/sprite_resources.h"
#include "core/concurrency/callback.h"
#include "renderer/ui/sprite_text_layout.h"

#include <cmath>

namespace arp::scene2d {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SpriteTextEditorProperties {
    APROP(Serialize,
        Editor,
        ResourceReference,
        OptionalReference,
        ReferenceTarget = "SpriteFontResource",
        Doc = "resource:SpriteFontResource:optional")
    core::pmr::String fontResourceId{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor)
    core::pmr::String text{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor, Doc = "color:rgba")
    core::Vec4 tint{1, 1, 1, 1};
    APROP(Serialize, Editor)
    int layer = 100;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class SpriteTextComponent final : public scene::Component {
public:
    SpriteTextComponent(core::SharedPtr<const core::pmr::Vector<renderer::ui::SpriteGlyph>> font,
        std::uint32_t capacity)
        : font_(std::move(font)), layout_(capacity)
    {}

    const char* scriptTypeName() const override { return "SpriteTextComponent"; }

    explicit SpriteTextComponent(
        core::SharedCallback<resource::ResourceManager*()> resourceManagerProvider)
        : layout_(1u), resourceManagerProvider_(std::move(resourceManagerProvider)),
          resourceManager_(resourceManagerProvider_ ? resourceManagerProvider_() : nullptr),
          providerOwned_(true)
    {}

    arp::reflection::object editorAuthoringProperties() const override
    {
        SpriteTextEditorProperties properties;
        properties.fontResourceId = fontResourceId_;
        properties.text = text_;
        properties.tint = color_;
        properties.layer = layer_;
        return arp::reflection::object(std::move(properties));
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<SpriteTextEditorProperties>())
            return false;
        const auto& properties = value.cast<const SpriteTextEditorProperties&>();
        if (properties.fontResourceId.size() > 512u || properties.text.size() > 512u
            || !std::isfinite(properties.tint.x) || !std::isfinite(properties.tint.y)
            || !std::isfinite(properties.tint.z) || !std::isfinite(properties.tint.w))
            return false;

        auto* activeManager = activeResourceManager();
        invalidateChangedOwner(activeManager);
        resource::ResourceHandle<SpriteFontResource> candidateFont;
        renderer::ui::SpriteTextLayout candidateLayout(1u);
        if (!properties.fontResourceId.empty() && activeManager) {
            candidateFont = activeManager->find<SpriteFontResource>(properties.fontResourceId);
            if (!candidateFont.ready()
                || !candidateLayout.setCapacity(candidateFont.get()->maxTextBytes())
                || !candidateLayout.prepare(properties.text, candidateFont.get()->resolvedGlyphs()))
                return false;
        }

        fontResourceId_ = properties.fontResourceId;
        text_ = properties.text;
        color_ = properties.tint;
        layer_ = properties.layer;
        fontResource_ = candidateFont;
        if (candidateFont.ready())
            layout_ = std::move(candidateLayout);
        else
            layout_.clear();
        return true;
    }

    void onUpdate(const scene::SceneUpdateContext&) override
    {
        if (!providerOwned_)
            return;
        auto* activeManager = activeResourceManager();
        if (activeManager == resourceManager_)
            return;
        invalidateChangedOwner(activeManager);
        if (!activeManager || fontResourceId_.empty())
            return;
        auto candidateFont = activeManager->find<SpriteFontResource>(fontResourceId_);
        renderer::ui::SpriteTextLayout candidateLayout(1u);
        if (candidateFont.ready()
            && candidateLayout.setCapacity(candidateFont.get()->maxTextBytes())
            && candidateLayout.prepare(text_, candidateFont.get()->resolvedGlyphs())) {
            fontResource_ = candidateFont;
            layout_ = std::move(candidateLayout);
        }
    }
    AFUNC()

    bool set_text(const core::pmr::String& text)
    {
        if (providerOwned_) {
            SpriteTextEditorProperties properties;
            properties.fontResourceId = fontResourceId_;
            properties.text = text;
            properties.tint = color_;
            properties.layer = layer_;
            return applyEditorAuthoringProperties(arp::reflection::object(std::move(properties)));
        }
        if (text == text_)
            return true;
        if (!font_ || !layout_.prepare(text, *font_))
            return false;
        text_ = text;
        return true;
    }
    AFUNC()

    void set_tint(core::Vec4 color) { color_ = color; }
    AFUNC()

    void set_layer(int layer) { layer_ = layer; }

    const auto& layout() const { return layout_; }

    core::Vec4 color() const { return color_; }

    int layer() const { return layer_; }

    void extract(
        const scene::RenderExtractContext& context, scene::RenderExtractSink& sink) const override
    {
        if (!enabled() || (providerOwned_ && !fontResourceIsCurrent()))
            return;
        scene::RenderExtractPayload payload;
        payload.typeId = "scene2d.sprite-text/1";
        payload.data = this;
        sink.accept(context, payload);
    }

private:
    resource::ResourceManager* activeResourceManager() const
    {
        return providerOwned_ ? (resourceManagerProvider_ ? resourceManagerProvider_() : nullptr)
                              : resourceManager_;
    }

    void invalidateChangedOwner(resource::ResourceManager* activeManager)
    {
        if (activeManager == resourceManager_)
            return;
        fontResource_ = {};
        resourceManager_ = activeManager;
        layout_.clear();
    }

    bool fontResourceIsCurrent() const
    {
        auto* activeManager = activeResourceManager();
        return activeManager && activeManager == resourceManager_ && fontResource_.ready()
            && fontResource_.id() == fontResourceId_;
    }

    core::SharedPtr<const core::pmr::Vector<renderer::ui::SpriteGlyph>> font_;
    renderer::ui::SpriteTextLayout layout_;
    core::pmr::String text_;
    core::pmr::String fontResourceId_;
    core::Vec4 color_{1, 1, 1, 1};
    int layer_ = 100;
    core::SharedCallback<resource::ResourceManager*()> resourceManagerProvider_;
    resource::ResourceManager* resourceManager_ = nullptr;
    resource::ResourceHandle<SpriteFontResource> fontResource_;
    bool providerOwned_ = false;
};
} // namespace arp::scene2d
