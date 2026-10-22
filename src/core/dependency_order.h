#pragma once

#include "core/cpu_allocator.h"

namespace arp::core {
// Stable topological order. Input indices name preparation tasks, not domain
// phases. Cycles and invalid edges fail without publishing a partial order.
class DependencyOrder final {
public:
    using Dependencies = pmr::Vector<pmr::Vector<std::size_t>>;

    static bool resolve(const Dependencies& input, pmr::Vector<std::size_t>& output)
    {
        pmr::Vector<std::size_t> candidate{&defaultCpuMemoryResource()};
        pmr::Vector<bool> done(input.size(), false, &defaultCpuMemoryResource());
        while (candidate.size() < input.size()) {
            const auto before = candidate.size();
            for (std::size_t i = 0; i < input.size(); ++i) {
                if (done[i])
                    continue;
                bool ready = true;
                for (auto dependency : input[i]) {
                    if (dependency >= input.size())
                        return false;
                    if (!done[dependency])
                        ready = false;
                }
                if (ready) {
                    done[i] = true;
                    candidate.push_back(i);
                }
            }
            if (candidate.size() == before)
                return false;
        }
        output = std::move(candidate);
        return true;
    }
};
} // namespace arp::core
