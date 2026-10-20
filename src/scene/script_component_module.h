#pragma once

#include "scene/scene.h"
#include "scene/script_behavior_component.h"
#include "scriptvm/runtime/script_session.h"
#include "scriptvm/runtime/semantic_runtime.h"

namespace arp::scriptvm {

// One module execution environment shared by all attached instances. This is
// the Scene/VM integration, not a gameplay host or a second object model.
class ScriptComponentModule final {
public:
    struct Field {
        core::pmr::String name;
        TypePtr type;
    };

    struct Hook {
        core::pmr::String name;
        ScriptSession::Entry entry;
        bool delta = false;
    };

    struct PhysicsHook {
        core::pmr::String name;
        ScriptSession::Entry entry;
    };

    struct Type {
        core::pmr::String name;
        core::pmr::Vector<Field> fields;
        core::pmr::Vector<core::pmr::String> fieldNames;
        core::pmr::Vector<Hook> hooks;
        core::pmr::Vector<PhysicsHook> physicsHooks;
        ScriptSession::Entry constructor;
        bool hasConstructor = false;
        bool hasEvent = false;
        ScriptSession::Entry event;
    };

    // The exact SDK prefix is included in source and AOT hashes. The normal
    // frontend checks inheritance and field/method contracts, for both modes.
    static core::pmr::String compose(const core::pmr::String& source);
    bool load(const core::pmr::String& source,
        const core::pmr::String& moduleId,
        const core::pmr::String& resourceId,
        std::uint32_t schemaVersion,
        ModuleRegistry&,
        const core::pmr::Vector<core::pmr::String>& dependencies,
        AotExecutionBudgetV5,
        std::uint32_t maxInstances,
        const AotPackageDescriptorV5* nativePackage = nullptr);
    bool bind(scene::Scene&);
    core::UniquePtr<scene::Component> create(
        scene::GameObject&, const core::pmr::String&, bool runConstructor = true);
    const Type* find(const core::pmr::String&) const;

    const auto& types() const { return types_; }

    const auto& diagnostic() const
    {
        return diagnostic_.empty() ? session_->diagnostic() : diagnostic_;
    }

    const auto& resourceId() const { return resourceId_; }

    std::uint32_t schemaVersion() const { return schemaVersion_; }

    std::uint32_t liveInstances() const { return counters_->live; }

    const auto& session() const { return *session_; }

    auto& session() { return *session_; }

private:
    struct Counters {
        struct Construction {
            const scene::GameObject* owner;
            const Type* type;
        };

        std::uint32_t live = 0, maximum = 0;
        core::pmr::Vector<Construction> constructing;
    };

    bool fail(const char* message)
    {
        diagnostic_ = message;
        return false;
    }

    core::SharedPtr<ScriptSession> session_ = core::makeShared<ScriptSession>();
    core::SharedPtr<Counters> counters_ = core::makeShared<Counters>();
    core::pmr::Vector<core::SharedPtr<Type>> types_;
    core::pmr::String resourceId_, diagnostic_;
    std::uint32_t schemaVersion_ = 0;
};
} // namespace arp::scriptvm
