set(PQC_RISCV_RELEASE "2026.07.15")
set(PQC_CAD_RELEASE "2026-07-29")
set(PQC_POLY_BUILD_JOBS 4 CACHE STRING "workers for each Verilator model build")

if(NOT PQC_POLY_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "PQC_POLY_BUILD_JOBS must be positive")
endif()

function(pqc_find_build_tool variable executable release)
    find_program(${variable} NAMES ${executable})
    if(NOT ${variable})
        message(
            FATAL_ERROR
                "missing required executable ${executable} from ${release}")
    endif()
    set(${variable} "${${variable}}" PARENT_SCOPE)
endfunction()

pqc_find_build_tool(
    PQC_VERILATOR verilator "OSS CAD Suite ${PQC_CAD_RELEASE}")
pqc_find_build_tool(
    PQC_RISCV_GCC
    riscv32-unknown-elf-gcc
    "RISC-V GNU toolchain ${PQC_RISCV_RELEASE}")
pqc_find_build_tool(
    PQC_RISCV_OBJCOPY
    riscv32-unknown-elf-objcopy
    "RISC-V GNU toolchain ${PQC_RISCV_RELEASE}")
pqc_find_build_tool(
    PQC_RISCV_OBJDUMP
    riscv32-unknown-elf-objdump
    "RISC-V GNU toolchain ${PQC_RISCV_RELEASE}")

set(pqc_target_dir "${CMAKE_CURRENT_BINARY_DIR}")
set(pqc_firmware_dir "${CMAKE_CURRENT_SOURCE_DIR}/firmware")
set(pqc_mlkem_dir "${CMAKE_CURRENT_SOURCE_DIR}/mlkem")
set(pqc_rtl_dir "${CMAKE_CURRENT_SOURCE_DIR}/rtl")
set(pqc_sim_dir "${CMAKE_CURRENT_SOURCE_DIR}/sim")
set(pqc_results "${pqc_target_dir}/results")

include("${PROJECT_SOURCE_DIR}/cmake/FetchPinned.cmake")
pqc_poly_fetch_picorv32()
pqc_poly_fetch_mlkem_native()

set(pqc_picorv32_source "${PQC_POLY_PICORV32_SOURCE_DIR}/picorv32.v")
set(pqc_mlkem_root "${PQC_POLY_MLKEM_NATIVE_SOURCE_DIR}/mlkem")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Simulation.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Firmware.cmake")
