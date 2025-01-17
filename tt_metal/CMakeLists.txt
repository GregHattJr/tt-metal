add_library(tt_metal)
add_library(Metalium::Metal ALIAS tt_metal)

target_sources(
    tt_metal
    PRIVATE
        tt_metal.cpp
        graph/graph_tracking.cpp
)

target_link_libraries(
    tt_metal
    PUBLIC
        metal_header_directories
        umd::device
        metal_common_libs
        magic_enum
        fmt::fmt-header-only
        span
        compiler_flags
        $<$<BOOL:${ENABLE_TRACY}>:TracyClient>
    PRIVATE
        profiler
        common
        jit_build
        llrt
        impl
        detail
        distributed
)

target_precompile_headers(
    tt_metal
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy/public/tracy/Tracy.hpp
        <functional>
        <map>
        <memory>
        <unordered_map>
        <variant>
        <vector>
)

target_include_directories(
    tt_metal
    PUBLIC
        ${UMD_HOME}
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_options(tt_metal PUBLIC -Wno-int-to-pointer-cast)
add_dependencies(tt_metal hw_toolchain)

set_target_properties(
    tt_metal
    PROPERTIES
        INSTALL_RPATH
            "${PROJECT_BINARY_DIR}/lib"
        ADDITIONAL_CLEAN_FILES
            "${PROJECT_BINARY_DIR}/lib;${PROJECT_BINARY_DIR}/obj"
)

add_subdirectory(hw)
add_subdirectory(common)
add_subdirectory(jit_build)
add_subdirectory(llrt)
add_subdirectory(tools)
add_subdirectory(impl)
add_subdirectory(detail)
add_subdirectory(distributed)

if(BUILD_PROGRAMMING_EXAMPLES)
    add_subdirectory(programming_examples)
endif()
