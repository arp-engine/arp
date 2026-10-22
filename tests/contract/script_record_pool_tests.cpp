#include "core/container/numeric_record_pool.h"

#include <cassert>
#include <cstdio>

int main()
{
    arp::core::NumericRecordPool records(2, 3);
    auto a = records.acquire(), b = records.acquire();
    assert(a >= 0 && b >= 0 && a != b && records.acquire() == -1);
    assert(records.write(a, 1, 42));
    double value = 0;
    assert(records.read(a, 1, value) && value == 42);
    assert(!records.write(a, 3, 1));
    assert(!records.write(a, 0, INFINITY));
    assert(records.release(a) && !records.contains(a) && !records.read(a, 1, value));
    auto c = records.acquire();
    assert(c >= 0 && c != a && c % 2 == a % 2);
    assert(!records.release(a));
    assert(records.read(c, 1, value) && value == 0 && records.size() == 2);
    assert(records.at(2) == -1 && records.at(-1) == -1);
    std::puts(
        "PASS numeric record schemas, bounded allocation, exhaustion, generation, stale ticket and slot reset");
}
