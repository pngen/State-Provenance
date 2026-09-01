cmake_minimum_required(VERSION 3.21)
project(StateProvenance
        VERSION 1.0.0
        DESCRIPTION "State Provenance: provenance, derivation, lineage and reuse-eligibility for machine-produced AI state"
        LANGUAGES CXX)

# Global options
option(STATE_PROVENANCE_BUILD_TESTS "Build the State Provenance test suites" ON)
option(STATE_PROVENANCE_BUILD_EXAMPLES "Build the State Provenance examples" ON)
option(STATE_PROVENANCE_BUILD_BENCHMARKS "Build the State Provenance benchmarks" ON)
option(STATE_PROVENANCE_BUILD_CUDA "Build the CUDA hardware-backed provenance proof" ON)
option(STATE_PROVENANCE_BUILD_CLI "Build the spc CLI" ON)
option(STATE_PROVENANCE_BUILD_DOWNSTREAM "Build/validate the downstream consumer (needs install)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _WINSOCK_DEPRECATED_NO_WARNINGS)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(GNUInstallDirs)

# Core header-only library
add_library(stateprovenance INTERFACE)
add_library(StateProvenance::stateprovenance ALIAS stateprovenance)
target_include_directories(stateprovenance INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(stateprovenance INTERFACE cxx_std_20)
target_compile_options(stateprovenance INTERFACE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive- /Zc:__cplusplus>)

# Smoke
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.cpp)
  add_executable(stateprovenance_smoke tests/smoke.cpp)
  target_link_libraries(stateprovenance_smoke PRIVATE StateProvenance::stateprovenance)
endif()

# ---------------------------------------------------------------------------
# Distributed multiprocess authority proof (framed TCP)
# ---------------------------------------------------------------------------
if(WIN32)
  set(SP_NET_LIB ws2_32)
endif()
add_executable(sp_coordinator tools/coordinator.cpp)
target_link_libraries(sp_coordinator PRIVATE StateProvenance::stateprovenance)
if(WIN32)
  target_link_libraries(sp_coordinator PRIVATE ws2_32)
endif()

add_executable(sp_worker tools/worker.cpp)
target_link_libraries(sp_worker PRIVATE StateProvenance::stateprovenance)
if(WIN32)
  target_link_libraries(sp_worker PRIVATE ws2_32)
endif()

add_executable(sp_mp_proof tools/mp_proof.cpp)
target_link_libraries(sp_mp_proof PRIVATE StateProvenance::stateprovenance)
if(WIN32)
  target_link_libraries(sp_mp_proof PRIVATE ws2_32)
endif()


# ---------------------------------------------------------------------------
# CUDA hardware-backed provenance proof (real RTX 5090, sm_120)
# ---------------------------------------------------------------------------
if(STATE_PROVENANCE_BUILD_CUDA)
  # The Visual Studio generator does not discover the CUDA VS toolset, so the
  # CUDA proof is built with nvcc 13.1 (arch sm_120) directly by a scripted
  # custom target.  This keeps the rest of the build on the standard toolchain.
  add_custom_target(sp_cuda_proof
    COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_cuda.ps1
            -SrcDir ${CMAKE_CURRENT_SOURCE_DIR} -BuildDir ${CMAKE_CURRENT_BINARY_DIR} -Config $<CONFIG>
    DEPENDS cuda/cuda_proof.cu
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/sp_cuda_proof.exe
    COMMENT "Building sp_cuda_proof (nvcc 13.1 / sm_120) via scripts/build_cuda.ps1"
    USES_TERMINAL)
endif()


# ---------------------------------------------------------------------------
# Test suites (no timeouts / watchdogs / process limits)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
if(STATE_PROVENANCE_BUILD_CLI)
  add_executable(spc cli/main.cpp)
  target_link_libraries(spc PRIVATE StateProvenance::stateprovenance)
  if(WIN32)
    target_link_libraries(spc PRIVATE ws2_32)
  endif()
endif()

# ---------------------------------------------------------------------------
# Examples (globby, runnable)
# ---------------------------------------------------------------------------
if(STATE_PROVENANCE_BUILD_EXAMPLES)
  file(GLOB sp_example_srcs examples/*.cpp)
  foreach(src ${sp_example_srcs})
    get_filename_component(name ${src} NAME_WE)
    add_executable(example_${name} ${src})
    target_include_directories(example_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    target_link_libraries(example_${name} PRIVATE StateProvenance::stateprovenance)
    if(WIN32)
      target_link_libraries(example_${name} PRIVATE ws2_32)
    endif()
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------
if(STATE_PROVENANCE_BUILD_BENCHMARKS)
  add_executable(sp_benchmark benchmarks/bench.cpp)
  target_link_libraries(sp_benchmark PRIVATE StateProvenance::stateprovenance)
  if(WIN32)
    target_link_libraries(sp_benchmark PRIVATE ws2_32)
  endif()
endif()

# ---------------------------------------------------------------------------
# Install + CMake package (find_package(StateProvenance CONFIG REQUIRED))
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS stateprovenance EXPORT StateProvenanceTargets)
install(EXPORT StateProvenanceTargets FILE StateProvenanceTargets.cmake
        NAMESPACE StateProvenance:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StateProvenance)
configure_package_config_file(cmake/StateProvenanceConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/StateProvenanceConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StateProvenance)
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/StateProvenanceConfigVersion.cmake
        VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/StateProvenanceConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/StateProvenanceConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StateProvenance)
if(STATE_PROVENANCE_BUILD_TESTS)
  add_executable(stateprovenance_tests
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_persist.cpp
    tests/test_concurrency.cpp
    tests/test_adversarial.cpp)
  target_include_directories(stateprovenance_tests PRIVATE tests)
  target_link_libraries(stateprovenance_tests PRIVATE StateProvenance::stateprovenance)
  if(WIN32)
    target_link_libraries(stateprovenance_tests PRIVATE ws2_32)
  endif()
endif()

