if(NOT TARGET arp_rhi)
    return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/BindlessWorkloadResources.cmake)
add_executable(arp_bindless_pipeline_demand_tests tests/contract/bindless_pipeline_demand_tests.cpp)
target_link_libraries(arp_bindless_pipeline_demand_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_pipeline_demand_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_pipeline_demand_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_pipeline_demand_tests>")
    endif()
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND Python3_Interpreter_FOUND)
        add_test(NAME arp_bindless_device_limit_plan_browser_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_device_limit_plan_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
                --runner "$<TARGET_FILE_DIR:arp_bindless_pipeline_demand_tests>"
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-device-limit-plan")
        set_tests_properties(arp_bindless_device_limit_plan_browser_smoke
            PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE)
    endif()
else()
    add_test(NAME arp_bindless_pipeline_demand_tests COMMAND arp_bindless_pipeline_demand_tests)
endif()
