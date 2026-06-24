cmake_minimum_required(VERSION 3.20)

project(
    pqc_poly_bench
    VERSION 0.3.0
    DESCRIPTION "reproducible ML-KEM software and RISC-V ISA co-design experiments"
    LANGUAGES CXX)

include(CheckIPOSupported)
include(CTest)

option(PQC_POLY_LTO "enable link-time optimization in release builds" ON)
option(PQC_POLY_SANITIZE "enable address and undefined behavior sanitizers" OFF)
option(PQC_POLY_WARNINGS_AS_ERRORS "treat compiler warnings as errors" ON)
option(PQC_POLY_PICORV32 "enable the opt in picorv32 simulation target" OFF)
option(PQC_POLY_PICORV32_SYNTHESIS "enable the opt in picorv32 synthesis target" OFF)
option(PQC_POLY_PICORV32_MLKEM "enable the opt in picorv32 mlkem measurements" OFF)
option(PQC_POLY_PICORV32_FQMUL_VERIFY "enable the opt in fqmul formal and cbmc targets" OFF)
option(PQC_POLY_FETCH_MLKEM_NATIVE "fetch the pinned mlkem native source" OFF)

if(PQC_POLY_PICORV32_SYNTHESIS OR PQC_POLY_PICORV32_MLKEM OR
   PQC_POLY_PICORV32_FQMUL_VERIFY)
    set(PQC_POLY_PICORV32 ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(PQC_POLY_FETCH_MLKEM_NATIVE)
    include("${PROJECT_SOURCE_DIR}/cmake/FetchPinned.cmake")
    pqc_poly_fetch_mlkem_native()
endif()

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

add_library(
    pqc_poly_compiler
    STATIC
        src/mlkem_check.cpp
        src/mlkem_codegen.cpp
        src/mlkem_plan.cpp
        src/mlkem_request.cpp
        src/mlkem_red32.cpp
        src/mlkem_red32_check.cpp
        src/mlkem_red32_codegen.cpp
        src/target_measurement.cpp)
pqc_poly_configure(pqc_poly_compiler)

add_executable(pqc-poly-mlkem src/mlkem_main.cpp src/mlkem_explore.cpp)
pqc_poly_configure(pqc-poly-mlkem)
target_link_libraries(pqc-poly-mlkem PRIVATE pqc_poly_compiler)

add_executable(pqc-poly-red32 src/mlkem_red32_main.cpp src/mlkem_red32_explore.cpp)
pqc_poly_configure(pqc-poly-red32)
target_link_libraries(pqc-poly-red32 PRIVATE pqc_poly_compiler)

if(BUILD_TESTING)
    function(pqc_poly_add_test name source)
        set(target pqc-poly-${name}-test)
        add_executable(${target} ${source})
        pqc_poly_configure(${target})
        target_link_libraries(${target} PRIVATE pqc_poly_compiler)
        add_test(NAME ${name} COMMAND ${target})
    endfunction()

    pqc_poly_add_test(target-measurement tests/target_measurement_test.cpp)
    pqc_poly_add_test(picorv32-manifest tests/picorv32_manifest_test.cpp)
    target_compile_definitions(
        pqc-poly-picorv32-manifest-test
        PRIVATE PQC_POLY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    pqc_poly_add_test(mlkem-plan tests/mlkem_plan_test.cpp)
    pqc_poly_add_test(mlkem-check tests/mlkem_check_test.cpp)
    pqc_poly_add_test(fqmul tests/fqmul_test.cpp)
    pqc_poly_add_test(fsri tests/fsri_test.cpp)
    pqc_poly_add_test(mlkem-codegen tests/mlkem_codegen_test.cpp)
    pqc_poly_add_test(mlkem-red32 tests/mlkem_red32_test.cpp)
    pqc_poly_add_test(mlkem-red32-codegen tests/mlkem_red32_codegen_test.cpp)
    target_compile_definitions(
        pqc-poly-mlkem-codegen-test
        PRIVATE
            PQC_POLY_TEST_CXX="${CMAKE_CXX_COMPILER}"
            PQC_POLY_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_compile_definitions(
        pqc-poly-mlkem-red32-codegen-test
        PRIVATE
            PQC_POLY_TEST_CXX="${CMAKE_CXX_COMPILER}"
            PQC_POLY_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    if(PQC_POLY_SANITIZE)
        target_compile_definitions(pqc-poly-mlkem-codegen-test PRIVATE PQC_POLY_TEST_SANITIZE=1)
        target_compile_definitions(pqc-poly-mlkem-red32-codegen-test PRIVATE PQC_POLY_TEST_SANITIZE=1)
    endif()

    add_test(NAME cli-help COMMAND pqc-poly-mlkem --help)
    add_test(NAME red32-cli-help COMMAND pqc-poly-red32 --help)
endif()

if(PQC_POLY_PICORV32)
    add_subdirectory(targets/picorv32)
    include("${PROJECT_SOURCE_DIR}/targets/picorv32/fsri.cmake")
endif()
