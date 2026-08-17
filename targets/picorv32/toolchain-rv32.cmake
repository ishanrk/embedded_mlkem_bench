set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(PQC_RISCV_GCC riscv32-unknown-elf-gcc)
if(NOT PQC_RISCV_GCC)
    message(
        FATAL_ERROR
            "missing required executable riscv32-unknown-elf-gcc from RISC-V GNU toolchain release 2026.07.15")
endif()

set(CMAKE_C_COMPILER "${PQC_RISCV_GCC}" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${PQC_RISCV_GCC}" CACHE FILEPATH "" FORCE)
