macro(pqc_require_program variable executable release)
    find_program(${variable} NAMES ${executable})
    if(NOT ${variable})
        list(APPEND pqc_missing_tools "${executable} from ${release}")
    endif()
endmacro()

set(pqc_missing_tools)

pqc_require_program(PQC_VERILATOR verilator "OSS CAD Suite ${PQC_CAD_RELEASE}")
pqc_require_program(PQC_NINJA ninja "OSS CAD Suite ${PQC_CAD_RELEASE}")

if(PQC_POLY_PICORV32_MLKEM)
    pqc_require_program(
        PQC_RISCV_GCC
        riscv32-unknown-elf-gcc
        "RISC-V GNU toolchain ${PQC_RISCV_RELEASE}")
    pqc_require_program(
        PQC_RISCV_OBJCOPY
        riscv32-unknown-elf-objcopy
        "RISC-V GNU toolchain ${PQC_RISCV_RELEASE}")
    pqc_require_program(
        PQC_RISCV_OBJDUMP
        riscv32-unknown-elf-objdump
        "RISC-V GNU toolchain ${PQC_RISCV_RELEASE}")
endif()

if(PQC_POLY_PICORV32_FORMAL)
    pqc_require_program(PQC_SBY sby "OSS CAD Suite ${PQC_CAD_RELEASE}")
endif()

if(PQC_POLY_PICORV32_SYNTHESIS)
    pqc_require_program(PQC_PYTHON python3 "OSS CAD Suite ${PQC_CAD_RELEASE}")
    pqc_require_program(PQC_YOSYS yosys "OSS CAD Suite ${PQC_CAD_RELEASE}")
    pqc_require_program(
        PQC_NEXTPNR nextpnr-ecp5 "OSS CAD Suite ${PQC_CAD_RELEASE}")
    pqc_require_program(PQC_ECPPACK ecppack "OSS CAD Suite ${PQC_CAD_RELEASE}")
endif()

if(pqc_missing_tools)
    list(JOIN pqc_missing_tools "\n  " pqc_missing_text)
    message(FATAL_ERROR "missing required target executables:\n  ${pqc_missing_text}")
endif()
