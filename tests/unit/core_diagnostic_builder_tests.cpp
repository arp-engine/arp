#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"

#include <cassert>
#include <memory_resource>
#include <string>

namespace {

void appendsNothingForEmptyEntries()
{
    std::pmr::string diagnostics{&arp::core::defaultCpuMemoryResource()};
    arp::core::appendDiagnostic(diagnostics, "");
    assert(diagnostics.empty());
}

void joinsWithSemicolon()
{
    std::pmr::string diagnostics{&arp::core::defaultCpuMemoryResource()};
    arp::core::appendDiagnostic(diagnostics, "first");
    arp::core::appendDiagnostic(diagnostics, "second");
    assert(diagnostics == "first; second");
}

void builderAppendsRepeatedEntries()
{
    std::pmr::string diagnostics{&arp::core::defaultCpuMemoryResource()};
    arp::core::DiagnosticBuilder builder(diagnostics);
    builder.append("alpha");
    builder.append("");
    builder.append("beta");
    builder.append("gamma");
    assert(diagnostics == "alpha; beta; gamma");
}

void acceptsCoreAndStdPmrStrings()
{
    arp::core::pmr::String coreText("core", &arp::core::defaultCpuMemoryResource());
    std::pmr::string pmrText("pmr", &arp::core::defaultCpuMemoryResource());
    std::pmr::string diagnostics{&arp::core::defaultCpuMemoryResource()};

    arp::core::DiagnosticBuilder builder(diagnostics);
    builder.append(coreText);
    builder.append(pmrText);

    assert(diagnostics == "core; pmr");
}

} // namespace

int main()
{
    appendsNothingForEmptyEntries();
    joinsWithSemicolon();
    builderAppendsRepeatedEntries();
    acceptsCoreAndStdPmrStrings();
    return 0;
}
