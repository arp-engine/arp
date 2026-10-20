#pragma once
#include "rhi/gpu_descriptors.h"

namespace arp::rhi {
// Cold owning copies of descriptor authoring metadata. Every nested allocator
// is core-owned; no factory identity or successful content is issued by a copy.
class DescriptorStateCopy final {
public:
    static DescriptorLayoutDesc copy(const DescriptorLayoutDesc& source)
    {
        DescriptorLayoutDesc result{core::pmr::String(&core::defaultCpuMemoryResource()),
            core::pmr::Vector<DescriptorBindingDesc>(&core::defaultCpuMemoryResource())};
        result = source;
        return result;
    }

    static DescriptorSetDesc copy(const DescriptorSetDesc& source)
    {
        DescriptorSetDesc result{
            core::pmr::String(&core::defaultCpuMemoryResource()), copy(source.layout)};
        result.label = source.label;
        return result;
    }

    static DescriptorTableDesc copy(const DescriptorTableDesc& source)
    {
        DescriptorTableDesc result{core::pmr::String(&core::defaultCpuMemoryResource())};
        result = source;
        return result;
    }

    static DescriptorArenaDesc copy(const DescriptorArenaDesc& source)
    {
        DescriptorArenaDesc result{core::pmr::String(&core::defaultCpuMemoryResource())};
        result = source;
        return result;
    }
};
} // namespace arp::rhi
