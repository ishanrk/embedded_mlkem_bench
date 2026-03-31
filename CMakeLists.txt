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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

    if(PQC_POLY_LTO AND NOT PQC_POLY_SANITIZE)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if(ipo_supported)
            set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
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

add_library(pqc_poly_selector STATIC src/selector.cpp)
pqc_poly_configure(pqc_poly_selector)

add_library(pqc_poly_compiler_plan STATIC src/compiler_plan.cpp)
pqc_poly_configure(pqc_poly_compiler_plan)
target_link_libraries(pqc_poly_compiler_plan PUBLIC pqc_poly_selector)

add_library(pqc_poly_ir STATIC src/ir.cpp)
pqc_poly_configure(pqc_poly_ir)
target_link_libraries(pqc_poly_ir PUBLIC pqc_poly_selector)

add_library(pqc_poly_tuning STATIC src/tuning.cpp)
pqc_poly_configure(pqc_poly_tuning)

add_library(pqc_poly_codegen STATIC src/codegen.cpp)
pqc_poly_configure(pqc_poly_codegen)
target_link_libraries(pqc_poly_codegen PUBLIC pqc_poly_selector)

add_library(pqc_poly_host_tuner STATIC src/host_tuner.cpp)
pqc_poly_configure(pqc_poly_host_tuner)
target_link_libraries(
    pqc_poly_host_tuner
    PUBLIC
        pqc_poly_codegen
        pqc_poly_ir
        pqc_poly_tuning)
target_compile_definitions(
    pqc_poly_host_tuner
    PRIVATE
        PQC_POLY_HOST_CXX="${CMAKE_CXX_COMPILER}"
        PQC_POLY_HOST_CXX_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
        PQC_POLY_HOST_SIZE="${PQC_POLY_SIZE_TOOL}")

add_library(pqc_poly_explore STATIC src/explore.cpp)
pqc_poly_configure(pqc_poly_explore)
target_link_libraries(
    pqc_poly_explore
    PUBLIC
        pqc_poly_codegen
        pqc_poly_compiler_plan
        pqc_poly_host_tuner
        pqc_poly_ir
        pqc_poly_selector
        pqc_poly_tuning)

add_executable(pqc-poly-bench src/main.cpp)
pqc_poly_configure(pqc-poly-bench)
target_link_libraries(pqc-poly-bench PRIVATE pqc_poly_explore pqc_poly_formula)

add_executable(pqc-poly-formula-bench benchmarks/formula_bench.cpp)
pqc_poly_configure(pqc-poly-formula-bench)
target_link_libraries(pqc-poly-formula-bench PRIVATE pqc_poly_formula)

if(BUILD_TESTING)
    add_executable(pqc-poly-ring-test tests/ring_test.cpp)
    pqc_poly_configure(pqc-poly-ring-test)
    target_link_libraries(pqc-poly-ring-test PRIVATE pqc_poly_ring)
    add_test(NAME ring COMMAND pqc-poly-ring-test)

    add_executable(pqc-poly-formula-test tests/formula_test.cpp)
    pqc_poly_configure(pqc-poly-formula-test)
    target_link_libraries(pqc-poly-formula-test PRIVATE pqc_poly_formula)
    add_test(NAME formula COMMAND pqc-poly-formula-test)

    add_executable(pqc-poly-selector-test tests/selector_tests.cpp)
    pqc_poly_configure(pqc-poly-selector-test)
    target_link_libraries(pqc-poly-selector-test PRIVATE pqc_poly_selector)
    add_test(NAME selector COMMAND pqc-poly-selector-test)

    add_executable(pqc-poly-ir-test tests/ir_test.cpp)
    pqc_poly_configure(pqc-poly-ir-test)
    target_link_libraries(pqc-poly-ir-test PRIVATE pqc_poly_ir)
    add_test(NAME ir COMMAND pqc-poly-ir-test)

    add_executable(pqc-poly-compiler-plan-test tests/compiler_plan_test.cpp)
    pqc_poly_configure(pqc-poly-compiler-plan-test)
    target_link_libraries(pqc-poly-compiler-plan-test PRIVATE pqc_poly_compiler_plan)
    add_test(NAME compiler-plan COMMAND pqc-poly-compiler-plan-test)

    add_executable(pqc-poly-tuning-test tests/tuning_test.cpp)
    pqc_poly_configure(pqc-poly-tuning-test)
    target_link_libraries(pqc-poly-tuning-test PRIVATE pqc_poly_tuning)
    add_test(NAME tuning COMMAND pqc-poly-tuning-test)

    add_executable(pqc-poly-codegen-test tests/codegen_test.cpp)
    pqc_poly_configure(pqc-poly-codegen-test)
    target_link_libraries(pqc-poly-codegen-test PRIVATE pqc_poly_codegen)
    target_compile_definitions(
        pqc-poly-codegen-test
        PRIVATE PQC_POLY_TEST_CXX="${CMAKE_CXX_COMPILER}")
    add_test(NAME codegen COMMAND pqc-poly-codegen-test)

    add_executable(pqc-poly-host-tuner-test tests/host_tuner_test.cpp)
    pqc_poly_configure(pqc-poly-host-tuner-test)
    target_link_libraries(pqc-poly-host-tuner-test PRIVATE pqc_poly_host_tuner)
    add_test(NAME host-tuner COMMAND pqc-poly-host-tuner-test)

    add_executable(pqc-poly-explore-test tests/explore_test.cpp)
    pqc_poly_configure(pqc-poly-explore-test)
    target_link_libraries(pqc-poly-explore-test PRIVATE pqc_poly_explore)
    add_test(NAME explore COMMAND pqc-poly-explore-test)

    add_test(NAME cli-help COMMAND pqc-poly-bench --help)
endif()
