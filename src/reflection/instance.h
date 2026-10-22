#pragma once

// instance -- mirrors ae::hybrid/instance.h, adapted for arp::reflection.
// Uses PMR Map instead of std::map.

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/def.h"
#include "reflection/object.h"

namespace arp::reflection {

class ARP_API instance final {
public:
    explicit instance(const class_info* cls);
    const class_info* class_() const;
    object __getitem__(NameView key);
    object __setitem__(NameView key, const object& value);
    object operator[](NameView key);
    object& slot(NameView key);

    bool alive() const noexcept { return alive_; }

    void invalidate() noexcept
    {
        alive_ = false;
        dict_.clear();
    }

    const core::pmr::Map<core::pmr::String, object>& slots() const noexcept { return dict_; }

private:
    const class_info* cls_ = nullptr;
    bool alive_ = true;
    core::pmr::Map<core::pmr::String, object> dict_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::reflection
