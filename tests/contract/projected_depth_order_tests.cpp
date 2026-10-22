#include "renderer/geometry/projected_depth_order.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <string_view>

namespace {
struct Record {
    arp::core::Vec3 position;
    std::uint32_t id;
    std::array<std::uint32_t, 8> payload;
};

const auto position = [](const Record& r) {
    return r.position;
};
const auto identity = [](const Record& r) {
    return r.id;
};

template <class Records> void order(Records& records, arp::core::Vec3 axis)
{
    arp::core::ProjectedDepthOrder::sort(records.data(), records.size(), axis, position, identity);
}
} // namespace

int main()
{
    const std::array<Record, 4> original{{{{2, 4, 9}, 4, {0xffffffff, 0x80000000}},
        {{1, 3, -5}, 2, {0xc0000001}},
        {{2, 9, 9}, 3, {0x80000001}},
        {{-2, 0, 1}, 1, {17, 19}}}};
    auto records = original;
    order(records, {0, 0, 1});
    assert(records[0].id == 2 && records[1].id == 1 && records[2].id == 3 && records[3].id == 4);
    for (const auto& record : records) {
        const auto it = std::find_if(original.begin(), original.end(), [&](const auto& r) {
            return r.id == record.id;
        });
        assert(it != original.end() && std::memcmp(&record, &*it, sizeof(Record)) == 0);
    }
    auto repeat = original;
    order(repeat, {0, 0, 1});
    assert(std::memcmp(records.data(), repeat.data(), sizeof(records)) == 0);
    order(records, {0, 0, -1});
    assert(records.front().id == 3 && records.back().id == 2);
    const auto rejected = [&](auto input, arp::core::Vec3 axis, std::string_view diagnostic) {
        const auto before = input;
        bool caught = false;
        try {
            order(input, axis);
        } catch (const arp::core::DiagnosticError& e) {
            caught = e.what() == diagnostic;
        }
        assert(caught && std::memcmp(input.data(), before.data(), sizeof(input)) == 0);
    };
    rejected(original, {}, "PROJECTED-DEPTH-ORDER-AXIS-INVALID");
    rejected(original,
        {std::numeric_limits<float>::infinity(), 0, 1},
        "PROJECTED-DEPTH-ORDER-AXIS-INVALID");
    records = original;
    records.back().position.z = std::numeric_limits<float>::quiet_NaN();
    rejected(records, {0, 0, 1}, "PROJECTED-DEPTH-ORDER-POSITION-INVALID");
    arp::core::ProjectedDepthOrder::sort(
        static_cast<Record*>(nullptr), 0, {0, 0, 1}, position, identity);
    bool caught = false;
    try {
        arp::core::ProjectedDepthOrder::sort(
            static_cast<Record*>(nullptr), 1, {0, 0, 1}, position, identity);
    } catch (const arp::core::DiagnosticError& e) {
        caught = std::string_view(e.what()) == "PROJECTED-DEPTH-ORDER-SPAN-INVALID";
    }
    assert(caught);
    records = original;
    records[0].position.z = std::numeric_limits<float>::max();
    order(records, {0, 0, std::numeric_limits<float>::max()});
    assert(records.back().id == 4);
}
