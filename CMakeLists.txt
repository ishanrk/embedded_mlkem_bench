cmake_minimum_required(VERSION 3.20)

project(
    pqc_poly_bench
    VERSION 0.3.0
    DESCRIPTION "constraint-driven polynomial kernel compilation and autotuning"
    LANGUAGES CXX)

include(CheckIPOSupported)
include(CTest)

find_program(PQC_POLY_SIZE_TOOL NAMES llvm-size size)
if(NOT PQC_POLY_SIZE_TOOL)
    set(PQC_POLY_SIZE_TOOL size)
endif()

option(PQC_POLY_NATIVE "enable host-specific vector instructions" OFF)
option(PQC_POLY_LTO "enable link-time optimization in release builds" ON)
option(PQC_POLY_SANITIZE "enable address and undefined behavior sanitizers" OFF)
option(PQC_POLY_WARNINGS_AS_ERRORS "treat compiler warnings as errors" ON)
option(PQC_POLY_PICORV32 "enable the opt in picorv32 simulation target" OFF)
option(PQC_POLY_PICORV32_SYNTHESIS "enable the opt in picorv32 synthesis target" OFF)

if(PQC_POLY_PICORV32_SYNTHESIS)
    set(PQC_POLY_PICORV32 ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PQC_POLY_IPO_SUPPORTED OFF)
if(PQC_POLY_LTO AND NOT PQC_POLY_SANITIZE)
    check_ipo_supported(RESULT PQC_POLY_IPO_SUPPORTED)
endif()

add_library(pqc_poly_options INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqc_poly_options
        INTERFACE
            -Wall
            -Wextra
            -Wcast-align
            -Wconversion
            -Wformat=2
            -Wshadow
            -Wundef)

    if(PQC_POLY_WARNINGS_AS_ERRORS)
        target_compile_options(pqc_poly_options INTERFACE -Werror)
    endif()

    if(PQC_POLY_SANITIZE)
        target_compile_options(
            pqc_poly_options
            INTERFACE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined)
        target_link_options(
            pqc_poly_options
            INTERFACE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined)
    endif()
endif()

function(pqc_poly_configure target)
    target_include_directories(
        ${target}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
    target_link_libraries(${target} PRIVATE pqc_poly_options)

    if(PQC_POLY_IPO_SUPPORTED)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

add_library(pqc_poly_ring STATIC src/ring.cpp)
pqc_poly_configure(pqc_poly_ring)

add_library(
    pqc_poly_formula
    STATIC
        src/formula/formula.cpp
        src/formula/karatsuba.cpp
        src/formula/ntt.cpp
        src/formula/schoolbook.cpp
        src/formula/tmvp.cpp
        src/formula/toom_cook.cpp)
pqc_poly_configure(pqc_poly_formula)
target_link_libraries(pqc_poly_formula PUBLIC pqc_poly_ring)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqc_poly_formula
        PRIVATE
            -fno-exceptions
            -fno-rtti
            -fno-semantic-interposition)

    if(PQC_POLY_NATIVE)
        target_compile_options(pqc_poly_formula PRIVATE -march=native)
    endif()
endif()

add_library(
    pqc_poly_compiler
    STATIC
        src/codegen.cpp
        src/explore.cpp
        src/host_tuner.cpp
        src/selector.cpp
        src/target_measurement.cpp
        src/tuning.cpp)
pqc_poly_configure(pqc_poly_compiler)
target_compile_definitions(
    pqc_poly_compiler
    PRIVATE
        PQC_POLY_HOST_CXX="${CMAKE_CXX_COMPILER}"
        PQC_POLY_HOST_CXX_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
        PQC_POLY_HOST_SIZE="${PQC_POLY_SIZE_TOOL}")

add_executable(pqc-poly-bench src/main.cpp)
pqc_poly_configure(pqc-poly-bench)
target_link_libraries(pqc-poly-bench PRIVATE pqc_poly_compiler)

add_executable(pqc-poly-formula-bench benchmarks/formula_bench.cpp)
pqc_poly_configure(pqc-poly-formula-bench)
target_link_libraries(pqc-poly-formula-bench PRIVATE pqc_poly_formula)

if(BUILD_TESTING)
    function(pqc_poly_add_test name source library)
        set(target pqc-poly-${name}-test)
        add_executable(${target} ${source})
        pqc_poly_configure(${target})
        target_link_libraries(${target} PRIVATE ${library})
        add_test(NAME ${name} COMMAND ${target})
    endfunction()

    pqc_poly_add_test(ring tests/ring_test.cpp pqc_poly_ring)
    pqc_poly_add_test(formula tests/formula_test.cpp pqc_poly_formula)
    pqc_poly_add_test(selector tests/selector_tests.cpp pqc_poly_compiler)
    pqc_poly_add_test(tuning tests/tuning_test.cpp pqc_poly_compiler)
    pqc_poly_add_test(codegen tests/codegen_test.cpp pqc_poly_compiler)
    target_compile_definitions(
        pqc-poly-codegen-test
        PRIVATE PQC_POLY_TEST_CXX="${CMAKE_CXX_COMPILER}")
    pqc_poly_add_test(host-tuner tests/host_tuner_test.cpp pqc_poly_compiler)
    pqc_poly_add_test(explore tests/explore_test.cpp pqc_poly_compiler)
    pqc_poly_add_test(
        target-measurement tests/target_measurement_test.cpp pqc_poly_compiler)
    pqc_poly_add_test(
        picorv32-manifest tests/picorv32_manifest_test.cpp pqc_poly_compiler)
    target_compile_definitions(
        pqc-poly-picorv32-manifest-test
        PRIVATE PQC_POLY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

    add_test(NAME cli-help COMMAND pqc-poly-bench --help)
endif()

if(PQC_POLY_PICORV32)
    add_subdirectory(targets/picorv32)
endif()
