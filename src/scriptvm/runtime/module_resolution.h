#pragma once

#include "scriptvm/runtime/module_exports.h"

namespace arp::scriptvm {

enum class ModuleResolutionStatus { Absent, Ready, Failed };

// A bound source failure must not masquerade as an undeclared module.
struct ModuleResolution {
    ModuleResolutionStatus status = ModuleResolutionStatus::Absent;
    core::SharedPtr<ModuleExports> exports;
    core::pmr::String diagnostic;
};

} // namespace arp::scriptvm
