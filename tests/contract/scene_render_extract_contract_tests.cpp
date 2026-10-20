#include "scene/component.h"
#include "scene/render_extract.h"
#include "scene/scene.h"

#include <cassert>
#include <cstdint>
#include <utility>

namespace {

using namespace arp::scene;

// ---------------------------------------------------------------------------
// Test sink — records every accepted payload for assertions.
// ---------------------------------------------------------------------------
class RecordingSink final : public RenderExtractSink {
public:
    void accept(const RenderExtractContext& ctx, const RenderExtractPayload& payload) override
    {
        contexts_.push_back(ctx);
        payloads_.push_back(payload);
    }

    const arp::core::pmr::Vector<RenderExtractPayload>& payloads() const { return payloads_; }

    const arp::core::pmr::Vector<RenderExtractContext>& contexts() const { return contexts_; }

    std::size_t count() const { return payloads_.size(); }

private:
    arp::core::pmr::Vector<RenderExtractContext> contexts_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<RenderExtractPayload> payloads_{&arp::core::defaultCpuMemoryResource()};
};

// ---------------------------------------------------------------------------
// A concrete Component that DOES override extract() (emits one Box payload).
// ---------------------------------------------------------------------------
class BoxComponent final : public Component {
public:
    void extract(const RenderExtractContext& ctx, RenderExtractSink& sink) const override
    {
        RenderExtractPayload payload;
        payload.kind = RenderExtractKind::Box;
        payload.objectId = ctx.owner ? ctx.owner->id() : GameObjectId{};
        payload.rect = {1.0f, 2.0f, 3.0f, 4.0f};
        payload.color[0] = 0.1f;
        payload.color[1] = 0.2f;
        payload.color[2] = 0.3f;
        payload.color[3] = 0.4f;
        payload.sortKey = 42;
        sink.accept(ctx, payload);
    }
};

// ---------------------------------------------------------------------------
// A concrete Component that does NOT override extract() (uses default no-op).
// ---------------------------------------------------------------------------
class DataOnlyComponent final : public Component {
public:
    void setValue(int v) { value_ = v; }

    int value() const { return value_; }

private:
    int value_ = 0;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void default_extract_is_a_no_op_and_does_not_touch_sink()
{
    RecordingSink sink;
    RenderExtractContext ctx;
    DataOnlyComponent comp;
    comp.extract(ctx, sink);
    assert(sink.count() == 0);
}

void overridden_extract_pushes_payload_into_sink()
{
    RecordingSink sink;
    Scene scene("extract_scene");
    auto& obj = scene.createObject("box_object");
    assert(scene.reparent(obj.id(), scene.rootId()).ok());
    obj.addComponent<BoxComponent>();

    // components()[0] is the mandatory TransformComponent; [1] is BoxComponent.
    assert(obj.components().size() == 2);
    const Component& asBase = *obj.components()[1];

    RenderExtractContext ctx;
    ctx.owner = &obj;
    ctx.activeInHierarchy = true;
    asBase.extract(ctx, sink); // virtual dispatch through base pointer

    assert(sink.count() == 1);
    const auto& payload = sink.payloads()[0];
    assert(payload.kind == RenderExtractKind::Box);
    assert(payload.objectId == obj.id());
    assert(payload.rect.x == 1.0f);
    assert(payload.rect.y == 2.0f);
    assert(payload.rect.width == 3.0f);
    assert(payload.rect.height == 4.0f);
    assert(payload.color[0] == 0.1f);
    assert(payload.color[1] == 0.2f);
    assert(payload.color[2] == 0.3f);
    assert(payload.color[3] == 0.4f);
    assert(payload.sortKey == 42);
    assert(sink.contexts()[0].owner == &obj);
}

void payload_default_constructed_has_custom_kind_and_white_color()
{
    RenderExtractPayload payload;
    assert(payload.kind == RenderExtractKind::Custom);
    assert(payload.color[0] == 1.0f);
    assert(payload.color[1] == 1.0f);
    assert(payload.color[2] == 1.0f);
    assert(payload.color[3] == 1.0f);
    assert(payload.sortKey == 0);
    assert(payload.data == nullptr);
    assert(payload.hasClipRect == false);
    assert(payload.text.empty());
    assert(payload.typeId.empty());
}

void payload_is_copyable_and_movable()
{
    RenderExtractPayload src;
    src.kind = RenderExtractKind::Text;
    src.text = "hello";
    src.typeId = "wocc.hudtext";
    src.sortKey = 7;
    src.color[0] = 0.5f;

    RenderExtractPayload copy = src; // copy
    assert(copy.kind == RenderExtractKind::Text);
    assert(copy.text == "hello");
    assert(copy.typeId == "wocc.hudtext");
    assert(copy.sortKey == 7);
    assert(copy.color[0] == 0.5f);

    RenderExtractPayload moved = std::move(copy); // move
    assert(moved.kind == RenderExtractKind::Text);
    assert(moved.text == "hello");
    assert(moved.typeId == "wocc.hudtext");
    assert(moved.sortKey == 7);
}

void custom_channel_carries_opaque_data_and_typeid()
{
    struct WoccMeshPayload {
        const char* resourceId;
        const float* baseColor;
    };

    const char* rid = "wocc.mesh.001";
    const float bc[4] = {0.8f, 0.7f, 0.6f, 1.0f};
    WoccMeshPayload domain{rid, bc};

    RenderExtractPayload payload;
    payload.kind = RenderExtractKind::Custom;
    payload.typeId = "wocc.mesh";
    payload.data = &domain;

    assert(payload.kind == RenderExtractKind::Custom);
    assert(payload.typeId == "wocc.mesh");
    assert(payload.data == &domain);

    // Sink reinterprets the opaque pointer against the typeId contract.
    const auto* resolved = static_cast<const WoccMeshPayload*>(payload.data);
    assert(resolved->resourceId == rid);
    assert(resolved->baseColor[0] == 0.8f);
}

void sink_dispatch_is_polymorphic()
{
    RecordingSink sink;
    RenderExtractContext ctx;
    RenderExtractPayload payload;
    payload.kind = RenderExtractKind::Mesh;

    // Dispatch through the abstract base pointer.
    RenderExtractSink& asBase = sink;
    asBase.accept(ctx, payload);
    asBase.accept(ctx, payload);
    assert(sink.count() == 2);
    assert(sink.payloads()[0].kind == RenderExtractKind::Mesh);
    assert(sink.payloads()[1].kind == RenderExtractKind::Mesh);
}

void extract_kind_enum_has_all_expected_values()
{
    // Guard against accidental reordering/removal of kind tags — extractors
    // route on these values.
    assert(RenderExtractKind::Box != RenderExtractKind::Text);
    assert(RenderExtractKind::Mesh != RenderExtractKind::Terrain);
    assert(RenderExtractKind::Sprite != RenderExtractKind::TileMapChunk);
    assert(RenderExtractKind::Custom != RenderExtractKind::Box);
    // All distinct enumerated values.
    arp::core::pmr::Vector<RenderExtractKind> kinds{&arp::core::defaultCpuMemoryResource()};
    kinds.push_back(RenderExtractKind::Box);
    kinds.push_back(RenderExtractKind::Text);
    kinds.push_back(RenderExtractKind::Image);
    kinds.push_back(RenderExtractKind::Mesh);
    kinds.push_back(RenderExtractKind::Terrain);
    kinds.push_back(RenderExtractKind::Animation);
    kinds.push_back(RenderExtractKind::Sprite);
    kinds.push_back(RenderExtractKind::TileMapChunk);
    kinds.push_back(RenderExtractKind::Custom);
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        for (std::size_t j = i + 1; j < kinds.size(); ++j) {
            assert(kinds[i] != kinds[j]);
        }
    }
}

} // namespace

int main()
{
    default_extract_is_a_no_op_and_does_not_touch_sink();
    overridden_extract_pushes_payload_into_sink();
    payload_default_constructed_has_custom_kind_and_white_color();
    payload_is_copyable_and_movable();
    custom_channel_carries_opaque_data_and_typeid();
    sink_dispatch_is_polymorphic();
    extract_kind_enum_has_all_expected_values();
    return 0;
}
