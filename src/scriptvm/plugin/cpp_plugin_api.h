#pragma once

// C++ plugin boundary for source-built ArpScript modules. The entry symbol has
// C linkage, while registration is performed through the dedicated v2
// ScriptPluginRegistrar. Plugins never receive ModuleRegistry or an internal
// module registry implementation.

#include "scriptvm/plugin/plugin_registrar.h"

#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#define ARP_SCRIPT_PLUGIN_EXPORT __declspec(dllexport)
#else
#define ARP_SCRIPT_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

struct ArpScriptPluginHostV2 {
    uint32_t abiVersion;
    arp::scriptvm::ScriptPluginRegistrar* registrar;
    arp::scriptvm::ScriptPluginServiceRegistry* services;
    const char* platform;
    const char* buildConfiguration;
    // Comma-separated host capability names. Plugins must gate optional
    // exports on this contract instead of probing native backend objects.
    const char* capabilities = nullptr;
};

using ArpScriptPluginEntryV2 = int (*)(ArpScriptPluginHostV2* host);

static const uint32_t ARP_SCRIPT_PLUGIN_ABI_VERSION_V2 = 2u;
