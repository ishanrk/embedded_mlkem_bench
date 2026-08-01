cmake_minimum_required(VERSION 3.20)

project(
    pqc_poly_bench
    VERSION 0.4.0
    DESCRIPTION "ML-KEM custom instruction measurements on PicoRV32"
    LANGUAGES C CXX)

include(CheckIPOSupported)
include(CTest)

option(PQC_POLY_LTO "enable link time optimization in release builds" ON)
option(PQC_POLY_SANITIZE "enable address and undefined behavior sanitizers" OFF)
option(PQC_POLY_WARNINGS_AS_ERRORS "treat compiler warnings as errors" ON)
option(PQC_POLY_PICORV32 "enable the PicoRV32 simulation targets" OFF)
option(PQC_POLY_PICORV32_MLKEM "enable the complete ML-KEM measurements" OFF)
option(PQC_POLY_PICORV32_FORMAL "enable the direct PCPI formal checks" OFF)
option(PQC_POLY_PICORV32_SYNTHESIS "enable the ECP5 synthesis targets" OFF)
set(PQC_POLY_BUILD_JOBS 4 CACHE STRING "maximum workers for optional Verilator builds")

if(NOT PQC_POLY_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "PQC_POLY_BUILD_JOBS must be positive")
endif()

if(PQC_POLY_PICORV32_MLKEM OR PQC_POLY_PICORV32_FORMAL OR
   PQC_POLY_PICORV32_SYNTHESIS)
    set(PQC_POLY_PICORV32 ON)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PQC_POLY_IPO_SUPPORTED OFF)
if(PQC_POLY_LTO AND NOT PQC_POLY_SANITIZE)
    check_ipo_supported(RESULT PQC_POLY_IPO_SUPPORTED)
endif()

add_library(pqc_poly_options INTERFACE)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
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
    target_link_libraries(${target} PRIVATE pqc_poly_options)
    if(PQC_POLY_IPO_SUPPORTED)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    function(pqc_poly_add_test name source)
        set(target pqc-poly-${name}-test)
        add_executable(${target} ${source})
        pqc_poly_configure(${target})
        add_test(NAME ${name} COMMAND ${target})
    endfunction()

    pqc_poly_add_test(fqmul tests/fqmul_test.cpp)
    pqc_poly_add_test(red32 tests/red32_test.cpp)
    pqc_poly_add_test(fsri tests/fsri_test.cpp)
    add_test(
        NAME results-summary
        COMMAND
            "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/results.py"
            --input "${PROJECT_SOURCE_DIR}/results/raw" --output
            "${PROJECT_SOURCE_DIR}/results/summary.json" --allow-missing --check)

    # each build uses the same backend source with one primitive selection
    foreach(pqc_level IN ITEMS 2 3 4)
        foreach(pqc_primitive IN ITEMS software fqmul red32)
            set(pqc_name fixed-backend-k${pqc_level}-${pqc_primitive})
            set(pqc_target pqc-poly-${pqc_name}-test)
            add_executable(
                ${pqc_target}
                tests/fixed_backend_test.cpp
                targets/picorv32/mlkem/fixed_backend.c)
            target_compile_definitions(${pqc_target} PRIVATE PQC_MLKEM_K=${pqc_level})
            if(pqc_primitive STREQUAL "fqmul")
                target_compile_definitions(${pqc_target} PRIVATE PQC_USE_FQMUL=1)
            elseif(pqc_primitive STREQUAL "red32")
                target_compile_definitions(${pqc_target} PRIVATE PQC_USE_RED32=1)
            endif()
            pqc_poly_configure(${pqc_target})
            add_test(NAME ${pqc_name} COMMAND ${pqc_target})
        endforeach()
    endforeach()
endif()

if(PQC_POLY_PICORV32)
    add_subdirectory(targets/picorv32)
endif()
