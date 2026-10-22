# Generate the static Plugin descriptor from the same reflection metadata that
# is compiled into the Plugin. This keeps static and dynamic loading on one
# contract instead of maintaining a second hand-written catalog.

if(NOT DEFINED ARP_PLUGIN_MANIFEST
    OR NOT DEFINED ARP_PLUGIN_REFLECTION_JSON
    OR NOT DEFINED ARP_PLUGIN_OUTPUT)
    message(FATAL_ERROR
        "GenerateScriptPluginCatalog requires manifest, reflection JSON and output")
endif()

file(READ "${ARP_PLUGIN_MANIFEST}" _manifest)
file(SHA256 "${ARP_PLUGIN_REFLECTION_JSON}" _reflection_hash)

function(read_scalar _key _out)
    set(_value "")
    file(STRINGS "${ARP_PLUGIN_MANIFEST}" _scalar_lines)
    foreach(_line IN LISTS _scalar_lines)
        string(STRIP "${_line}" _trimmed)
        if(_trimmed MATCHES "^${_key}:[ \\t]*(.+)$")
            set(_value "${CMAKE_MATCH_1}")
            break()
        endif()
    endforeach()
    if(_value STREQUAL "")
        message(FATAL_ERROR "Plugin manifest is missing ${_key}: ${ARP_PLUGIN_MANIFEST}")
    endif()
    string(STRIP "${_value}" _value)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" _value "${_value}")
    set(${_out} "${_value}" PARENT_SCOPE)
endfunction()

read_scalar("module" _module)
read_scalar("moduleVersion" _module_version)
read_scalar("entry" _entry)
read_scalar("exportContractHash" _export_hash)

set(_services)
set(_service_id "")
file(STRINGS "${ARP_PLUGIN_MANIFEST}" _manifest_lines)
foreach(_line IN LISTS _manifest_lines)
    string(STRIP "${_line}" _trimmed)
    if(_trimmed MATCHES "^-[ \\t]*id:[ \\t]*(.+)$")
        set(_service_id "${CMAKE_MATCH_1}")
        string(STRIP "${_service_id}" _service_id)
        string(REGEX REPLACE "^\"(.*)\"$" "\\1" _service_id "${_service_id}")
    elseif(_service_id AND _trimmed MATCHES "^version:[ \\t]*([0-9]+)$")
        list(APPEND _services "${_service_id}|${CMAKE_MATCH_1}")
        set(_service_id "")
    elseif(_trimmed MATCHES "^[A-Za-z][A-Za-z0-9_]*:")
        set(_service_id "")
    endif()
endforeach()

get_filename_component(_output_dir "${ARP_PLUGIN_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")
string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _symbol "${_module}")

set(_content "")
string(APPEND _content
    "#include <utility>\n"
    "#include \"scriptvm/plugin/static_plugin_catalog.h\"\n"
    "#include \"scriptvm/plugin/cpp_plugin_api.h\"\n"
    "extern \"C\" int ${_entry}(ArpScriptPluginHostV2*);\n"
    "bool arp_add_${_symbol}_static_plugin(arp::scriptvm::StaticPluginCatalog& catalog) {\n"
    "    arp::scriptvm::ScriptPluginDescriptor descriptor;\n"
    "    descriptor.module = arp::core::pmr::String(\"${_module}\", &arp::core::defaultCpuMemoryResource());\n"
    "    descriptor.contractVersion = 2u;\n"
    "    descriptor.moduleVersion = ${_module_version}u;\n"
    "    descriptor.entry = &${_entry};\n"
    "    descriptor.exportContractHash = arp::core::pmr::String(\"${_export_hash}\", &arp::core::defaultCpuMemoryResource());\n"
    "    descriptor.reflectionRegistryHash = arp::core::pmr::String(\"sha256:${_reflection_hash}\", &arp::core::defaultCpuMemoryResource());\n")

foreach(_service IN LISTS _services)
    string(REPLACE "|" ";" _parts "${_service}")
    list(GET _parts 0 _id)
    list(GET _parts 1 _version)
    string(APPEND _content
        "    { arp::scriptvm::ScriptPluginServiceRequirement service; service.serviceId = arp::core::pmr::String(\"${_id}\", &arp::core::defaultCpuMemoryResource()); service.version = ${_version}u; descriptor.requiredServices.push_back(std::move(service)); }\n")
endforeach()

string(APPEND _content
    "    return catalog.add(std::move(descriptor));\n"
    "}\n"
    "const char* arp_${_symbol}_manifest_module() { return \"${_module}\"; }\n"
    "unsigned int arp_${_symbol}_manifest_version() { return ${_module_version}u; }\n"
    "const char* arp_${_symbol}_manifest_export_contract_hash() { return \"${_export_hash}\"; }\n"
    "const char* arp_${_symbol}_manifest_reflection_registry_hash() { return \"sha256:${_reflection_hash}\"; }\n")

file(WRITE "${ARP_PLUGIN_OUTPUT}" "${_content}")
