#include "core/string_utils.h"

#include <cassert>

namespace {

void comparesAsciiCaseInsensitively()
{
    assert(arp::core::asciiEqualsIgnoreCase("Upgrade", "upgrade"));
    assert(arp::core::asciiEqualsIgnoreCase("WEBSOCKET", "websocket"));
    assert(!arp::core::asciiEqualsIgnoreCase("websocket", "web socket"));
    assert(!arp::core::asciiEqualsIgnoreCase("abc", "abcd"));
}

void trimsHttpOptionalWhitespace()
{
    assert(arp::core::trimAsciiOws(" \tvalue\t ") == "value");
    assert(arp::core::trimAsciiOws("value") == "value");
    assert(arp::core::trimAsciiOws(" \t") == "");
    assert(arp::core::trimAsciiOws("") == "");
    assert(arp::core::trimAsciiOws("\r value \n") == "\r value \n");
}

void trimsAsciiWhitespace()
{
    assert(arp::core::trimAsciiWhitespace("\r\n value \t") == "value");
    assert(arp::core::trimAsciiWhitespace("value") == "value");
    assert(arp::core::trimAsciiWhitespace("\r\n\t ") == "");
}

void scansCommaSeparatedTokens()
{
    assert(arp::core::containsCommaSeparatedAsciiToken("keep-alive, Upgrade", "upgrade"));
    assert(arp::core::containsCommaSeparatedAsciiToken(" upgrade , keep-alive ", "Upgrade"));
    assert(!arp::core::containsCommaSeparatedAsciiToken("x-upgrade", "upgrade"));
    assert(!arp::core::containsCommaSeparatedAsciiToken("", "upgrade"));
    assert(!arp::core::containsCommaSeparatedAsciiToken("upgrade", ""));
}

void manipulatesScriptStrings()
{
    auto* resource = &arp::core::defaultCpuMemoryResource();
    const auto parts = arp::core::splitPmr("a,,c", ",", resource);
    assert(parts.size() == 3);
    assert(parts[0] == "a");
    assert(parts[1].empty());
    assert(parts[2] == "c");

    assert(arp::core::joinPmr(parts, "-", resource) == "a--c");
    assert(arp::core::replaceAllPmr("hello", "l", "L", resource) == "heLLo");
    assert(arp::core::stripPmr("\t hi \r\n", resource) == "hi");
    assert(arp::core::toUpperPmr("Abc!", resource) == "ABC!");
    assert(arp::core::toLowerPmr("AbC!", resource) == "abc!");
    assert(arp::core::indexOf("hello", "ll") == 2);
    assert(arp::core::indexOf("hello", "x") == -1);
    assert(arp::core::contains("hello", "ell"));
    assert(arp::core::startsWith("hello", "he"));
    assert(arp::core::endsWith("hello", "lo"));
}

} // namespace

int main()
{
    comparesAsciiCaseInsensitively();
    trimsHttpOptionalWhitespace();
    trimsAsciiWhitespace();
    scansCommaSeparatedTokens();
    manipulatesScriptStrings();
    return 0;
}
