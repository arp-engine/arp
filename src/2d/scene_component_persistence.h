#pragma once

#include "2d/physics_components.h"
#include "resource/resource_manager.h"
#include "scene/component_factory.h"

namespace arp::scene2d {
bool registerScene2DPersistence(scene::SceneComponentFactory&,
    resource::ResourceManager&,
    core::SharedPtr<physics2d::PhysicsWorld2D>,
    physics2d::ComponentUsage usage = physics2d::ComponentUsage::Simulation);
}
