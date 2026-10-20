# Use CMAKE_CURRENT_SOURCE_DIR (not CMAKE_SOURCE_DIR) so the engine resolves
# its own 3rd/ tree correctly when consumed as a subproject (e.g. via
# add_subdirectory by the wocc repo). CMAKE_SOURCE_DIR would point at the
# consumer's root and miss the engine's bundled 3rd/ dependencies.
set(ARP_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd" CACHE PATH "Third-party dependency directory")

if(MINGW AND EXISTS "C:/mingw64/bin/windres.exe")
    set(CMAKE_RC_COMPILER "C:/mingw64/bin/windres.exe" CACHE FILEPATH "MinGW resource compiler" FORCE)
    set(
        CMAKE_RC_COMPILE_OBJECT
        "\"C:/mingw64/bin/windres.exe\" -O coff <DEFINES> <INCLUDES> <FLAGS> <SOURCE> <OBJECT>"
    )
endif()

set(ARP_VULKAN_SDK "$ENV{VULKAN_SDK}" CACHE PATH "Vulkan SDK root")
if(NOT ARP_VULKAN_SDK AND DEFINED VULKAN_SDK)
    set(ARP_VULKAN_SDK "${VULKAN_SDK}")
endif()
if(NOT CMAKE_CROSSCOMPILING
        AND NOT ARP_VULKAN_SDK AND EXISTS "C:/VulkanSDK/1.4.350.0")
    set(ARP_VULKAN_SDK "C:/VulkanSDK/1.4.350.0")
endif()

if(ARP_VULKAN_SDK AND EXISTS "${ARP_VULKAN_SDK}/Include/vulkan/vulkan.h")
    add_library(arp_vulkan_headers INTERFACE)
    target_include_directories(arp_vulkan_headers INTERFACE "${ARP_VULKAN_SDK}/Include")
    set(ARP_HAS_VULKAN_HEADERS ON)
else()
    set(ARP_HAS_VULKAN_HEADERS OFF)
endif()

# SPIRV-Reflect (Khronos, shipped with Vulkan SDK or supplied as a pinned
# source/package root) - used for offline shader layout reflection
# (push-constant / UBO member offsets, alignment, decorations) and contract
# tests that cross-check generated C++ bindings against the real SPIR-V
# payload. Keep the dependency behind one ARP-owned target so consumers do not
# branch on platform-specific library names.
set(ARP_SPIRV_REFLECT_ROOT "$ENV{SPIRV_REFLECT_ROOT}" CACHE PATH
    "SPIRV-Reflect source/package root for non-Vulkan-SDK platforms")
set(ARP_SPIRV_REFLECT_LIBRARY "$ENV{SPIRV_REFLECT_LIBRARY}" CACHE FILEPATH
    "SPIRV-Reflect static library override")

# These are internal probes.  Clear stale NOTFOUND cache entries when a
# caller supplies a root/library after an earlier configure without the
# dependency; otherwise CMake will not re-run find_path/find_library.
unset(_arp_spirv_reflect_header CACHE)
unset(_arp_spirv_reflect_library CACHE)
set(_arp_spirv_reflect_header "")
set(_arp_spirv_reflect_library "")
if(ARP_SPIRV_REFLECT_ROOT)
    foreach(_arp_spirv_reflect_include IN ITEMS
            "${ARP_SPIRV_REFLECT_ROOT}"
            "${ARP_SPIRV_REFLECT_ROOT}/include"
            "${ARP_SPIRV_REFLECT_ROOT}/Source/SPIRV-Reflect")
        if(EXISTS "${_arp_spirv_reflect_include}/spirv_reflect.h")
            set(_arp_spirv_reflect_header "${_arp_spirv_reflect_include}")
            break()
        endif()
    endforeach()
    if(ARP_SPIRV_REFLECT_LIBRARY)
        set(_arp_spirv_reflect_library "${ARP_SPIRV_REFLECT_LIBRARY}")
    else()
        foreach(_arp_spirv_reflect_candidate IN ITEMS
                "${ARP_SPIRV_REFLECT_ROOT}/lib/libspirv-reflect-static.a"
                "${ARP_SPIRV_REFLECT_ROOT}/lib/spirv-reflect-static.lib"
                "${ARP_SPIRV_REFLECT_ROOT}/build/libspirv-reflect-static.a"
                "${ARP_SPIRV_REFLECT_ROOT}/build/spirv-reflect-static.lib"
                "${ARP_SPIRV_REFLECT_ROOT}/build/lib/libspirv-reflect-static.a")
            if(EXISTS "${_arp_spirv_reflect_candidate}")
                set(_arp_spirv_reflect_library "${_arp_spirv_reflect_candidate}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(_arp_spirv_reflect_header
        AND EXISTS "${_arp_spirv_reflect_header}/spirv_reflect.h"
        AND EXISTS "${_arp_spirv_reflect_library}")
    add_library(arp_spirv_reflect UNKNOWN IMPORTED GLOBAL)
    set_target_properties(arp_spirv_reflect PROPERTIES
        IMPORTED_LOCATION "${_arp_spirv_reflect_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_arp_spirv_reflect_header}")
    set(ARP_HAS_SPIRV_REFLECT ON)
elseif(ARP_VULKAN_SDK
        AND EXISTS "${ARP_VULKAN_SDK}/Source/SPIRV-Reflect/spirv_reflect.h"
        AND EXISTS "${ARP_VULKAN_SDK}/Lib/spirv-reflect-static.lib")
    add_library(arp_spirv_reflect INTERFACE)
    target_include_directories(arp_spirv_reflect INTERFACE
        "${ARP_VULKAN_SDK}/Source/SPIRV-Reflect")
    target_link_libraries(arp_spirv_reflect INTERFACE
        "${ARP_VULKAN_SDK}/Lib/spirv-reflect-static.lib")
    set(ARP_HAS_SPIRV_REFLECT ON)
else()
    set(ARP_HAS_SPIRV_REFLECT OFF)
    message(WARNING "SPIRV-Reflect not found. Set ARP_SPIRV_REFLECT_ROOT and "
                    "ARP_SPIRV_REFLECT_LIBRARY for a platform package, or provide "
                    "the Windows Vulkan SDK under ARP_VULKAN_SDK; shader binding "
                    "codegen and related contract tests will be unavailable.")
endif()

set(ARP_VMA_ROOT "${ARP_THIRD_PARTY_DIR}/VulkanMemoryAllocator" CACHE PATH "Vulkan Memory Allocator root")
if(EXISTS "${ARP_VMA_ROOT}/include/vk_mem_alloc.h")
    add_library(arp_vma_headers INTERFACE)
    target_include_directories(arp_vma_headers INTERFACE "${ARP_VMA_ROOT}/include")
    set(ARP_HAS_VMA ON)
else()
    set(ARP_HAS_VMA OFF)
endif()

set(ARP_FKYAML_ROOT "${ARP_THIRD_PARTY_DIR}/fkYAML" CACHE PATH "fkYAML header-only root")
if(EXISTS "${ARP_FKYAML_ROOT}/include/fkYAML/node.hpp")
    add_library(arp_fkyaml_headers INTERFACE)
    target_include_directories(arp_fkyaml_headers INTERFACE "${ARP_FKYAML_ROOT}/include")
    set(ARP_HAS_FKYAML ON)
else()
    set(ARP_HAS_FKYAML OFF)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ArpMimalloc.cmake")

set(SLANGC_EXECUTABLE "" CACHE FILEPATH "Path to slangc executable")
if(NOT SLANGC_EXECUTABLE AND ARP_VULKAN_SDK AND EXISTS "${ARP_VULKAN_SDK}/Bin/slangc.exe")
    set(SLANGC_EXECUTABLE "${ARP_VULKAN_SDK}/Bin/slangc.exe" CACHE FILEPATH "Path to slangc executable" FORCE)
endif()


set(ARP_ASSIMP_ROOT "" CACHE PATH "Optional Assimp package root for offline asset import")
set(ARP_MESHOPTIMIZER_ROOT "" CACHE PATH "Optional meshoptimizer package root for offline asset import")
set(ARP_KTX_SOFTWARE_ROOT "" CACHE PATH "Optional KTX-Software package root for offline texture packaging")
set(ARP_NLOHMANN_JSON_ROOT "" CACHE PATH "Optional nlohmann_json package root for asset manifests")
set(ARP_FREETYPE_ROOT "${ARP_THIRD_PARTY_DIR}/freetype" CACHE PATH "Optional FreeType package root for UI font rasterization")
if(NOT ARP_FREETYPE_ROOT)
    set(ARP_FREETYPE_ROOT "${ARP_THIRD_PARTY_DIR}/freetype" CACHE PATH "Optional FreeType package root for UI font rasterization" FORCE)
endif()

set(ARP_ASSIMP_SOURCE_DIR "" CACHE PATH "Optional Assimp source tree for vendored offline asset import")
option(
    ARP_ENABLE_VENDORED_ASSIMP
    "Configure the vendored Assimp source tree for offline broad asset import."
    ON
)

if(ARP_ENABLE_VENDORED_ASSIMP AND NOT ARP_ASSIMP_SOURCE_DIR)
    file(GLOB ARP_ASSIMP_SOURCE_CANDIDATES "${ARP_THIRD_PARTY_DIR}/assimp-*")
    list(SORT ARP_ASSIMP_SOURCE_CANDIDATES)
    list(REVERSE ARP_ASSIMP_SOURCE_CANDIDATES)
    foreach(candidate IN LISTS ARP_ASSIMP_SOURCE_CANDIDATES)
        if(EXISTS "${candidate}/CMakeLists.txt" AND EXISTS "${candidate}/include/assimp/Importer.hpp")
            set(ARP_ASSIMP_SOURCE_DIR "${candidate}" CACHE PATH "Optional Assimp source tree for vendored offline asset import" FORCE)
            break()
        endif()
    endforeach()
endif()

if(ARP_ENABLE_VENDORED_ASSIMP AND ARP_ASSIMP_SOURCE_DIR AND EXISTS "${ARP_ASSIMP_SOURCE_DIR}/CMakeLists.txt")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default" FORCE)
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable vendored Assimp tests" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Disable vendored Assimp command-line tools" FORCE)
    set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Disable vendored Assimp samples" FORCE)
    set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "Disable vendored Assimp docs" FORCE)
    set(ASSIMP_NO_EXPORT ON CACHE BOOL "Disable vendored Assimp exporters" FORCE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "Disable vendored Assimp exporters by default" FORCE)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "Disable vendored Assimp importers by default" FORCE)
    set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "Build vendored zlib for Assimp" FORCE)
    set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "Enable Assimp FBX importer" FORCE)
    set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "Enable Assimp glTF/GLB importer" FORCE)
    set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "Enable Assimp OBJ importer" FORCE)
    set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "Enable Assimp Collada importer" FORCE)
    set(ASSIMP_INSTALL OFF CACHE BOOL "Disable vendored Assimp install rules" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "Do not treat vendored Assimp warnings as errors" FORCE)
    set(ASSIMP_BUILD_USE_CCACHE OFF CACHE BOOL "Disable vendored Assimp global ccache launch rules" FORCE)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps/assimp/generated")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        list(FIND CMAKE_C_COMPILE_FEATURES c_std_99 ARP_C_STD_99_FEATURE_INDEX)
        if(ARP_C_STD_99_FEATURE_INDEX LESS 0)
            list(APPEND CMAKE_C_COMPILE_FEATURES c_std_99)
        endif()
    endif()
    add_subdirectory("${ARP_ASSIMP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/assimp" EXCLUDE_FROM_ALL)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "")
    set(ARP_HAS_ASSIMP ON)
endif()

if(NOT EMSCRIPTEN)
    find_package(assimp CONFIG QUIET)
endif()
find_package(meshoptimizer CONFIG QUIET)
find_package(Ktx CONFIG QUIET)
find_package(nlohmann_json CONFIG QUIET)
find_package(Freetype QUIET)

if(NOT TARGET Freetype::Freetype
        AND NOT TARGET freetype
        AND EXISTS "${ARP_FREETYPE_ROOT}/CMakeLists.txt"
        AND EXISTS "${ARP_FREETYPE_ROOT}/include/ft2build.h")
    set(FT_DISABLE_ZLIB ON CACHE BOOL "Disable vendored FreeType zlib dependency" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "Disable vendored FreeType bzip2 dependency" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "Disable vendored FreeType png dependency" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "Disable vendored FreeType harfbuzz dependency" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "Disable vendored FreeType brotli dependency" FORCE)
    set(FT_DISABLE_HVF ON CACHE BOOL "Disable vendored FreeType HVF dependency" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "Disable vendored FreeType install rules" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default" FORCE)
    add_subdirectory("${ARP_FREETYPE_ROOT}" "${CMAKE_BINARY_DIR}/_deps/freetype" EXCLUDE_FROM_ALL)
endif()

if(ARP_HAS_ASSIMP OR assimp_FOUND OR EXISTS "${ARP_ASSIMP_ROOT}/include/assimp/Importer.hpp" OR EXISTS "${ARP_THIRD_PARTY_DIR}/assimp/include/assimp/Importer.hpp")
    set(ARP_HAS_ASSIMP ON)
else()
    set(ARP_HAS_ASSIMP OFF)
endif()

if(meshoptimizer_FOUND OR EXISTS "${ARP_MESHOPTIMIZER_ROOT}/src/meshoptimizer.h" OR EXISTS "${ARP_THIRD_PARTY_DIR}/meshoptimizer/src/meshoptimizer.h")
    set(ARP_HAS_MESHOPTIMIZER ON)
else()
    set(ARP_HAS_MESHOPTIMIZER OFF)
endif()

if(Ktx_FOUND OR EXISTS "${ARP_KTX_SOFTWARE_ROOT}/include/ktx.h" OR EXISTS "${ARP_THIRD_PARTY_DIR}/KTX-Software/include/ktx.h")
    set(ARP_HAS_KTX_SOFTWARE ON)
else()
    set(ARP_HAS_KTX_SOFTWARE OFF)
endif()

if(nlohmann_json_FOUND OR EXISTS "${ARP_NLOHMANN_JSON_ROOT}/include/nlohmann/json.hpp" OR EXISTS "${ARP_THIRD_PARTY_DIR}/json/include/nlohmann/json.hpp")
    set(ARP_HAS_NLOHMANN_JSON ON)
else()
    set(ARP_HAS_NLOHMANN_JSON OFF)
endif()

if(Freetype_FOUND OR TARGET Freetype::Freetype OR TARGET freetype OR EXISTS "${ARP_FREETYPE_ROOT}/include/freetype2/ft2build.h" OR EXISTS "${ARP_FREETYPE_ROOT}/include/ft2build.h")
    set(ARP_HAS_FREETYPE ON)
else()
    set(ARP_HAS_FREETYPE OFF)
endif()

add_library(arp_freetype INTERFACE)
target_compile_definitions(arp_freetype INTERFACE ARP_HAS_FREETYPE=$<BOOL:${ARP_HAS_FREETYPE}>)
if(TARGET Freetype::Freetype)
    target_link_libraries(arp_freetype INTERFACE Freetype::Freetype)
elseif(TARGET freetype)
    target_link_libraries(arp_freetype INTERFACE freetype)
elseif(Freetype_FOUND)
    target_link_libraries(arp_freetype INTERFACE Freetype::Freetype)
elseif(EXISTS "${ARP_FREETYPE_ROOT}/include/freetype2/ft2build.h")
    target_include_directories(arp_freetype INTERFACE "${ARP_FREETYPE_ROOT}/include/freetype2" "${ARP_FREETYPE_ROOT}/include")
    if(EXISTS "${ARP_FREETYPE_ROOT}/lib")
        target_link_directories(arp_freetype INTERFACE "${ARP_FREETYPE_ROOT}/lib")
        target_link_libraries(arp_freetype INTERFACE freetype)
    endif()
elseif(EXISTS "${ARP_FREETYPE_ROOT}/include/ft2build.h")
    target_include_directories(arp_freetype INTERFACE "${ARP_FREETYPE_ROOT}/include")
    if(EXISTS "${ARP_FREETYPE_ROOT}/lib")
        target_link_directories(arp_freetype INTERFACE "${ARP_FREETYPE_ROOT}/lib")
        target_link_libraries(arp_freetype INTERFACE freetype)
    endif()
endif()

add_library(arp_asset_import_deps INTERFACE)
target_compile_definitions(arp_asset_import_deps INTERFACE
    ARP_HAS_ASSIMP=$<BOOL:${ARP_HAS_ASSIMP}>
    ARP_HAS_MESHOPTIMIZER=$<BOOL:${ARP_HAS_MESHOPTIMIZER}>
    ARP_HAS_KTX_SOFTWARE=$<BOOL:${ARP_HAS_KTX_SOFTWARE}>
    ARP_HAS_NLOHMANN_JSON=$<BOOL:${ARP_HAS_NLOHMANN_JSON}>
)

if(TARGET assimp::assimp)
    target_link_libraries(arp_asset_import_deps INTERFACE assimp::assimp)
    if(MINGW)
        target_link_libraries(arp_asset_import_deps INTERFACE "-Wl,-Bdynamic")
    endif()
elseif(assimp_FOUND)
    target_link_libraries(arp_asset_import_deps INTERFACE assimp::assimp)
    if(MINGW)
        target_link_libraries(arp_asset_import_deps INTERFACE "-Wl,-Bdynamic")
    endif()
elseif(EXISTS "${ARP_ASSIMP_ROOT}/include/assimp/Importer.hpp")
    target_include_directories(arp_asset_import_deps INTERFACE "${ARP_ASSIMP_ROOT}/include")
    if(EXISTS "${ARP_ASSIMP_ROOT}/lib")
        target_link_directories(arp_asset_import_deps INTERFACE "${ARP_ASSIMP_ROOT}/lib")
        target_link_libraries(arp_asset_import_deps INTERFACE assimp)
    endif()
elseif(EXISTS "${ARP_THIRD_PARTY_DIR}/assimp/include/assimp/Importer.hpp")
    target_include_directories(arp_asset_import_deps INTERFACE "${ARP_THIRD_PARTY_DIR}/assimp/include")
    if(EXISTS "${ARP_THIRD_PARTY_DIR}/assimp/lib")
        target_link_directories(arp_asset_import_deps INTERFACE "${ARP_THIRD_PARTY_DIR}/assimp/lib")
        target_link_libraries(arp_asset_import_deps INTERFACE assimp)
    endif()
endif()

# ---------------------------------------------------------------------------
# JoltPhysics (vendored). Built as a static library via its upstream
# Build/CMakeLists.txt, with the noisy options disabled so it integrates
# cleanly under the engine's MinGW toolchain (no -Wall -Werror, no LTO,
# no debug renderer, no object stream, no install rules).
#
# Capability contract: when ARP_HAS_JOLT is OFF, arp_physics must fail
# closed (no dummy physics world). Downstream code branches on the
# ARP_HAS_JOLT compile definition.
# ---------------------------------------------------------------------------
set(ARP_JOLT_ROOT "${ARP_THIRD_PARTY_DIR}/JoltPhysics-5.6.0" CACHE PATH "Vendored JoltPhysics source root")
if(EXISTS "${ARP_JOLT_ROOT}/Build/CMakeLists.txt" AND EXISTS "${ARP_JOLT_ROOT}/Jolt/Jolt.cmake")
    set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "Build Jolt as a static library" FORCE)
    set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "Disable Jolt ObjectStream (not used by engine)" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "Disable Jolt install rules" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Do not inject Jolt -Wall -Werror" FORCE)
    set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "Do not let Jolt override engine Debug/Release flags" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "Disable Jolt LTO (MinGW link issues)" FORCE)
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "Disable Jolt debug renderer in Debug/Release" FORCE)
    set(DEBUG_RENDERER_IN_DISTRIBUTION OFF CACHE BOOL "Disable Jolt debug renderer in all builds" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Do not static-link MSVC runtime (MinGW)" FORCE)
    set(USE_ASSERTS OFF CACHE BOOL "Disable Jolt asserts in all builds" FORCE)
    set(CPP_EXCEPTIONS_ENABLED OFF CACHE BOOL "Disable Jolt C++ exceptions" FORCE)
    set(CPP_RTTI_ENABLED OFF CACHE BOOL "Disable Jolt RTTI" FORCE)
    set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "Generate Jolt debug symbols" FORCE)
    # GPU compute shaders are unused by the engine's CPU physics path.
    set(JPH_USE_DX12 OFF CACHE BOOL "Disable Jolt DX12 compute" FORCE)
    set(JPH_USE_VK OFF CACHE BOOL "Disable Jolt Vulkan compute" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "Disable Jolt Metal compute" FORCE)
    set(JPH_USE_CPU_COMPUTE ON CACHE BOOL "Enable Jolt CPU compute fallback" FORCE)
    # Disable AVX2/AVX512 in Jolt. Jolt sets these as PUBLIC compile options
    # which propagate to all engine targets that link Jolt. On MinGW-W64
    # (GCC), -mavx2 causes the compiler to emit vmovdqa (32-byte aligned)
    # instructions that assume 32-byte stack alignment, but the Windows x64
    # ABI only guarantees 16-byte alignment at call boundaries. This causes
    # non-deterministic SIGSEGV crashes in functions with large stack frames
    # (e.g. GameScene::loadMeshAssetsAsync with its ~700-byte TerrainHeightField
    # local). Falling back to SSE4.2 keeps Jolt performant without poisoning
    # the engine's codegen with unsafe alignment assumptions.
    set(USE_AVX512 OFF CACHE BOOL "Disable Jolt AVX512" FORCE)
    set(USE_AVX2 OFF CACHE BOOL "Disable Jolt AVX2 (stack alignment safety)" FORCE)
    set(USE_AVX OFF CACHE BOOL "Disable Jolt AVX (stack alignment safety)" FORCE)
    set(USE_SSE4_2 ON CACHE BOOL "Use SSE4.2 for Jolt" FORCE)
    set(USE_FMADD OFF CACHE BOOL "Disable Jolt FMA (stack alignment safety)" FORCE)
    add_subdirectory("${ARP_JOLT_ROOT}/Build" "${CMAKE_BINARY_DIR}/_deps/jolt" EXCLUDE_FROM_ALL)
    # Even after disabling the AVX options above, strip any remaining
    # INTERFACE_COMPILE_OPTIONS that Jolt's Jolt.cmake may have set as PUBLIC
    # (e.g. -mfpmath=sse). These are fine for Jolt's own translation units
    # but should not leak into engine code.
    if(TARGET Jolt)
        set_target_properties(Jolt PROPERTIES INTERFACE_COMPILE_OPTIONS "")
    endif()
    set(ARP_HAS_JOLT ON)
else()
    set(ARP_HAS_JOLT OFF)
endif()

add_library(arp_jolt_headers INTERFACE)
target_include_directories(arp_jolt_headers INTERFACE "${ARP_JOLT_ROOT}/Jolt")
if(ARP_HAS_JOLT AND TARGET Jolt)
    target_link_libraries(arp_jolt_headers INTERFACE Jolt)
endif()
target_compile_definitions(arp_jolt_headers INTERFACE ARP_HAS_JOLT=$<BOOL:${ARP_HAS_JOLT}>)
