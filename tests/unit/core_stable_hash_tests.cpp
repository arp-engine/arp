#include "core/stable_hash.h"

#include <cassert>
#include <cstdint>

namespace {

void hashesBytesAndStringViews()
{
    const std::uint8_t bytes[] = {'a', 'r', 'p'};
    arp::core::StableHash64 bytesHash;
    bytesHash.appendBytes(bytes, sizeof(bytes));

    arp::core::StableHash64 stringHash;
    stringHash.appendString("arp");

    assert(bytesHash.value() == stringHash.value());
    assert(bytesHash.value() == 0xe19f0e510de7f784ull);
}

void hashesUnsignedIntegersLittleEndian()
{
    arp::core::StableHash64 hash;
    hash.appendU64(0x0102030405060708ull);
    assert(hash.value() == 0x361e2e8261da3acbull);
}

void hashesFloatBitPatterns()
{
    arp::core::StableHash64 positiveZero;
    positiveZero.appendFloat(0.0f);

    arp::core::StableHash64 negativeZero;
    negativeZero.appendFloat(-0.0f);

    arp::core::StableHash64 one;
    one.appendFloat(1.0f);

    assert(positiveZero.value() != 0u);
    assert(negativeZero.value() != positiveZero.value());
    assert(one.value() != positiveZero.value());
    assert(one.value() == 0x0278152792978efaull);
}

void combineOrderIsDeterministic()
{
    arp::core::StableHash64 first;
    first.appendString("a");
    first.appendU64(7u);

    arp::core::StableHash64 repeated;
    repeated.appendString("a");
    repeated.appendU64(7u);

    arp::core::StableHash64 reversed;
    reversed.appendU64(7u);
    reversed.appendString("a");

    assert(first.value() == repeated.value());
    assert(first.value() != reversed.value());
}

} // namespace

int main()
{
    hashesBytesAndStringViews();
    hashesUnsignedIntegersLittleEndian();
    hashesFloatBitPatterns();
    combineOrderIsDeterministic();
    return 0;
}
