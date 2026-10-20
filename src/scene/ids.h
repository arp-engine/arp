#pragma once

#include <cstdint>

namespace arp::scene {

class SceneId {
public:
    SceneId() = default;

    static SceneId invalid() { return SceneId(); }

    static SceneId fromValue(uint64_t value) { return SceneId(value); }

    bool valid() const { return value_ != 0; }

    uint64_t value() const { return value_; }

    friend bool operator==(SceneId lhs, SceneId rhs) { return lhs.value_ == rhs.value_; }

    friend bool operator!=(SceneId lhs, SceneId rhs) { return !(lhs == rhs); }

private:
    explicit SceneId(uint64_t value) : value_(value) {}

    uint64_t value_ = 0;
};

class GameObjectId {
public:
    GameObjectId() = default;

    static GameObjectId invalid() { return GameObjectId(); }

    static GameObjectId fromValue(uint64_t value) { return GameObjectId(value); }

    bool valid() const { return value_ != 0; }

    uint64_t value() const { return value_; }

    friend bool operator==(GameObjectId lhs, GameObjectId rhs) { return lhs.value_ == rhs.value_; }

    friend bool operator!=(GameObjectId lhs, GameObjectId rhs) { return !(lhs == rhs); }

private:
    explicit GameObjectId(uint64_t value) : value_(value) {}

    uint64_t value_ = 0;
};

class ComponentId {
public:
    ComponentId() = default;

    static ComponentId invalid() { return ComponentId(); }

    static ComponentId fromValue(uint64_t value) { return ComponentId(value); }

    bool valid() const { return value_ != 0; }

    uint64_t value() const { return value_; }

    friend bool operator==(ComponentId lhs, ComponentId rhs) { return lhs.value_ == rhs.value_; }

    friend bool operator!=(ComponentId lhs, ComponentId rhs) { return !(lhs == rhs); }

private:
    explicit ComponentId(uint64_t value) : value_(value) {}

    uint64_t value_ = 0;
};

} // namespace arp::scene
