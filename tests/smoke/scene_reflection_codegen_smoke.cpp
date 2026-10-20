#include "2d/bounds.h"
#include "2d/components.h"
#include "2d/scene_2d_render_queue.h"
#include "3d/bounds.h"
#include "3d/components.h"
#include "3d/scene_3d_render_queue.h"
#include "reflection/reflection.h"
#include "scene/camera.h"
#include "scene/component.h"
#include "scene/diagnostics.h"
#include "scene/scene_traversal_snapshot.h"
#include "scene/transform.h"

#include <cassert>

namespace arp::reflection::generated {
void register_scene_runtime_reflection();
}

namespace {

void assertProperty(const arp::reflection::class_info* cls, arp::reflection::NameView name)
{
    assert(cls != nullptr);
    assert(cls->get_field(name).is_property());
}

void assertEnumValue(const arp::reflection::class_info* cls,
    arp::reflection::NameView enumName,
    arp::reflection::NameView valueName,
    int expected)
{
    assert(cls != nullptr);
    auto reflectedEnum = cls->get_field(enumName).cast<arp::reflection::enumeration&>();
    assert(reflectedEnum.get_enum_value(valueName) == expected);
}

} // namespace

int main()
{
    using namespace arp::reflection;

    generated::register_scene_runtime_reflection();

    assertProperty(class_of<arp::scene::Transform>(), "translation");
    assertProperty(class_of<arp::scene::Transform>(), "scale");
    object transform = class_of<arp::scene::Transform>()->new_object();
    assert(!transform.is_undefined());

    assertProperty(class_of<arp::scene::SceneUpdateContext>(), "deltaSeconds");
    assertProperty(class_of<arp::scene::SceneUpdateContext>(), "frameIndex");
    object updateContext = class_of<arp::scene::SceneUpdateContext>()->new_object();
    updateContext.set("deltaSeconds", object(0.25f));
    assert(updateContext["deltaSeconds"].cast<float>() == 0.25f);

    assertProperty(class_of<arp::scene::ViewportRect>(), "width");
    assertProperty(class_of<arp::scene::ViewportRect>(), "height");
    assertProperty(class_of<arp::scene::CameraAspectPolicy>(), "aspectRatio");
    assertProperty(class_of<arp::scene::CameraComponentDesc>(), "priority");
    assertEnumValue(class_of<arp::scene::CameraProjection>(),
        "CameraProjection",
        "Orthographic",
        static_cast<int>(arp::scene::CameraProjection::Orthographic));

    assertProperty(class_of<arp::scene2d::Bounds2D>(), "center");
    assertProperty(class_of<arp::scene2d::Bounds2D>(), "extents");
    assertProperty(class_of<arp::scene2d::SpriteHandle>(), "id");
    assertProperty(class_of<arp::scene2d::TileMapHandle>(), "id");
    assertProperty(class_of<arp::scene2d::ChunkCoord>(), "x");
    assertProperty(class_of<arp::scene2d::Scene2DRenderItem>(), "sortKey");
    assertEnumValue(class_of<arp::scene2d::Scene2DRenderItemKind>(),
        "Scene2DRenderItemKind",
        "Sprite",
        static_cast<int>(arp::scene2d::Scene2DRenderItemKind::Sprite));

    assertProperty(class_of<arp::scene3d::Bounds3D>(), "center");
    assertProperty(class_of<arp::scene3d::Bounds3D>(), "extents");
    assertProperty(class_of<arp::scene3d::TerrainHandle>(), "id");
    assertProperty(class_of<arp::scene3d::TerrainMetadata>(), "metersPerTile");
    assertProperty(class_of<arp::scene3d::Scene3DRenderItem>(), "sortKey");
    assertProperty(class_of<arp::scene3d::Scene3DAnimationBinding>(), "objectId");
    assertEnumValue(class_of<arp::scene3d::Scene3DRenderItemKind>(),
        "Scene3DRenderItemKind",
        "Terrain",
        static_cast<int>(arp::scene3d::Scene3DRenderItemKind::Terrain));

    return 0;
}
