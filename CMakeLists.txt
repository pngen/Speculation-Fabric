cmake_minimum_required(VERSION 3.24)

# Speculation Fabric — a vendor-neutral runtime for governing speculative inference.
project(SpeculationFabric
    VERSION 1.0.0
    DESCRIPTION "Vendor-neutral runtime governing speculative inference"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Policy / options
# ---------------------------------------------------------------------------
option(SF_ENABLE_CUDA "Build the CUDA proposer/verifier executors (requires CUDA toolkit)" ON)
option(SF_BUILD_TESTS "Build the Speculation Fabric test suite" ON)
option(SF_BUILD_EXAMPLES "Build the Speculation Fabric examples" ON)
option(SF_BUILD_CLI "Build the Speculation Fabric command-line tool" ON)
option(SF_BUILD_BENCHMARKS "Build the Speculation Fabric benchmarks" OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SF_IS_TOP_LEVEL ON)
else()
    set(SF_IS_TOP_LEVEL OFF)
endif()

# ---------------------------------------------------------------------------
# Compiler settings: /W4 /WX on MSVC, equivalent strictness elsewhere.
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus /utf-8)
    add_compile_options(/EHsc)
else()
    add_compile_options(-Wall -Wextra -Werror -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")

# ---------------------------------------------------------------------------
# CUDA detection
# ---------------------------------------------------------------------------
set(SF_CUDA_ENABLED FALSE)
if(SF_ENABLE_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        enable_language(CUDA)
        set(SF_CUDA_ENABLED TRUE)
    else()
        message(WARNING "SF_ENABLE_CUDA is ON but CUDAToolkit was not found; building CPU-only.")
        set(SF_CUDA_ENABLED FALSE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Core library sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SF_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(SF_CUDA_SOURCES)

# ---------------------------------------------------------------------------
# The SpeculationFabric library.
# ---------------------------------------------------------------------------
add_library(SpeculationFabric STATIC ${SF_CORE_SOURCES})
add_library(SpeculationFabric::SpeculationFabric ALIAS SpeculationFabric)

target_include_directories(SpeculationFabric
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(SpeculationFabric PUBLIC cxx_std_20)

if(SF_CUDA_ENABLED)
    target_sources(SpeculationFabric PRIVATE ${SF_CUDA_SOURCES})
    target_compile_features(SpeculationFabric PUBLIC cuda_std_17)
    target_link_libraries(SpeculationFabric PUBLIC CUDA::cudart)
    set_property(TARGET SpeculationFabric PROPERTY CUDA_ARCHITECTURES 120)
endif()

# ---------------------------------------------------------------------------
# Install / export configuration.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(SF_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/SpeculationFabric")

install(TARGETS SpeculationFabric
    EXPORT SpeculationFabricTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT SpeculationFabricTargets
    FILE SpeculationFabricTargets.cmake
    NAMESPACE SpeculationFabric::
    DESTINATION "${SF_INSTALL_CMAKEDIR}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpeculationFabricConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SpeculationFabricConfig.cmake"
    INSTALL_DESTINATION "${SF_INSTALL_CMAKEDIR}")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SpeculationFabricConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SpeculationFabricConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SpeculationFabricConfigVersion.cmake"
    DESTINATION "${SF_INSTALL_CMAKEDIR}")

# ---------------------------------------------------------------------------
# Sub-builds
# ---------------------------------------------------------------------------
if(SF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SF_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(SF_BUILD_CLI)
    add_subdirectory(tools)
endif()

if(SF_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()