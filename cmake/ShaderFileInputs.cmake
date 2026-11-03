include_guard(GLOBAL)
# Native offline producers use an explicit build-owned host tool. Cross builds
# retain their existing host-tool path until an explicit distribution is selected.
set(ARP_SHADER_TOOL_INPUT_ARGS)
set(ARP_SHADER_TOOL_INPUT_DEPENDENCIES)
if(NOT EMSCRIPTEN)
    set(ARP_SHADER_TOOL_INPUT_ARGS --io-tool "$<TARGET_FILE:arp_tool_file_inventory>")
    set(ARP_SHADER_TOOL_INPUT_DEPENDENCIES arp_tool_file_inventory
        "${CMAKE_CURRENT_LIST_DIR}/../scripts/shader_file_inputs.py"
        "${CMAKE_CURRENT_LIST_DIR}/../scripts/tool_file_inventory.py")
endif()
