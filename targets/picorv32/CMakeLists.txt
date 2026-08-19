set(PQC_RISCV_RELEASE "2026.07.15")
set(PQC_CAD_RELEASE "2026-07-29")

set(pqc_required_tools
    PQC_RISCV_GCC|riscv32-unknown-elf-gcc|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_RISCV_LD|riscv32-unknown-elf-ld|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_RISCV_OBJCOPY|riscv32-unknown-elf-objcopy|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_RISCV_OBJDUMP|riscv32-unknown-elf-objdump|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_RISCV_SIZE|riscv32-unknown-elf-size|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_VERILATOR|verilator|OSS_CAD_Suite_${PQC_CAD_RELEASE}
    PQC_NINJA|ninja|OSS_CAD_Suite_${PQC_CAD_RELEASE}
    PQC_PYTHON|python3|OSS_CAD_Suite_${PQC_CAD_RELEASE})

if(PQC_POLY_PICORV32_SYNTHESIS)
    list(
        APPEND
        pqc_required_tools
        PQC_YOSYS|yosys|OSS_CAD_Suite_${PQC_CAD_RELEASE}
        PQC_NEXTPNR|nextpnr-ecp5|OSS_CAD_Suite_${PQC_CAD_RELEASE}
        PQC_ECPPACK|ecppack|OSS_CAD_Suite_${PQC_CAD_RELEASE}
        PQC_SOLVER|z3|OSS_CAD_Suite_${PQC_CAD_RELEASE})
endif()

if(PQC_POLY_PICORV32_FQMUL_VERIFY)
    list(
        APPEND
        pqc_required_tools
        PQC_SBY|sby|OSS_CAD_Suite_${PQC_CAD_RELEASE}
        PQC_BOOLECTOR|boolector|OSS_CAD_Suite_${PQC_CAD_RELEASE})
endif()

set(pqc_missing_tools)
foreach(pqc_tool IN LISTS pqc_required_tools)
    string(REPLACE "|" ";" pqc_parts "${pqc_tool}")
    list(GET pqc_parts 0 pqc_variable)
    list(GET pqc_parts 1 pqc_executable)
    list(GET pqc_parts 2 pqc_release)
    find_program(${pqc_variable} NAMES ${pqc_executable})
    if(NOT ${pqc_variable})
        string(REPLACE "_" " " pqc_release_text "${pqc_release}")
        list(APPEND pqc_missing_tools "${pqc_executable} from ${pqc_release_text}")
    endif()
endforeach()

if(pqc_missing_tools)
    list(JOIN pqc_missing_tools "\n  " pqc_missing_text)
    message(FATAL_ERROR "missing required target executables:\n  ${pqc_missing_text}")
endif()

get_filename_component(pqc_riscv_bin "${PQC_RISCV_GCC}" DIRECTORY)
foreach(pqc_tool IN ITEMS PQC_RISCV_LD PQC_RISCV_OBJCOPY PQC_RISCV_OBJDUMP PQC_RISCV_SIZE)
    get_filename_component(pqc_tool_bin "${${pqc_tool}}" DIRECTORY)
    if(NOT pqc_tool_bin STREQUAL pqc_riscv_bin)
        message(
            FATAL_ERROR
                "RISC-V tools must come from one RISC-V GNU toolchain release ${PQC_RISCV_RELEASE}")
    endif()
endforeach()

if(PQC_POLY_PICORV32_SYNTHESIS)
    get_filename_component(pqc_cad_bin "${PQC_VERILATOR}" DIRECTORY)
    foreach(pqc_tool IN ITEMS PQC_YOSYS PQC_NEXTPNR PQC_ECPPACK PQC_SOLVER)
        get_filename_component(pqc_tool_bin "${${pqc_tool}}" DIRECTORY)
        if(NOT pqc_tool_bin STREQUAL pqc_cad_bin)
            message(
                FATAL_ERROR
                    "Verilator Yosys nextpnr ecppack and solver must come from OSS CAD Suite release ${PQC_CAD_RELEASE}")
        endif()
    endforeach()
endif()

if(PQC_POLY_PICORV32_FQMUL_VERIFY)
    get_filename_component(pqc_cad_bin "${PQC_VERILATOR}" DIRECTORY)
    foreach(pqc_tool IN ITEMS PQC_SBY PQC_BOOLECTOR)
        get_filename_component(pqc_tool_bin "${${pqc_tool}}" DIRECTORY)
        if(NOT pqc_tool_bin STREQUAL pqc_cad_bin)
            message(FATAL_ERROR "formal tools must come from OSS CAD Suite ${PQC_CAD_RELEASE}")
        endif()
    endforeach()
    find_program(PQC_CBMC NAMES cbmc)
    if(NOT PQC_CBMC)
        message(FATAL_ERROR "missing CBMC 6.10.0")
    endif()
    execute_process(
        COMMAND "${PQC_CBMC}" --version
        OUTPUT_VARIABLE pqc_cbmc_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    if(NOT pqc_cbmc_version MATCHES "^6\\.10\\.0")
        message(FATAL_ERROR "CBMC must be version 6.10.0")
    endif()
endif()

include("${PROJECT_SOURCE_DIR}/cmake/FetchPinned.cmake")
pqc_poly_fetch_picorv32()

set(pqc_target_dir "${CMAKE_CURRENT_BINARY_DIR}")
set(pqc_firmware_dir "${CMAKE_CURRENT_SOURCE_DIR}/firmware")
set(pqc_rtl_dir "${CMAKE_CURRENT_SOURCE_DIR}/rtl")
set(pqc_sim_dir "${CMAKE_CURRENT_SOURCE_DIR}/sim")
set(pqc_synth_dir "${CMAKE_CURRENT_SOURCE_DIR}/synth")
set(pqc_picorv32_source "${PQC_POLY_PICORV32_SOURCE_DIR}/picorv32.v")

set(
    pqc_c_flags
    -std=c11
    -march=rv32imc
    -mabi=ilp32
    -O3
    -DNDEBUG
    -ffreestanding
    -fno-common
    -fdata-sections
    -ffunction-sections
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -fno-pic
    -fno-pie
    -fno-stack-protector
    -fomit-frame-pointer
    -msmall-data-limit=0
    -mstrict-align
    -mcmodel=medlow
    -fstack-usage
    -Wall
    -Wextra
    -Wconversion
    -Werror)
set(
    pqc_link_flags
    -march=rv32imc
    -mabi=ilp32
    -nostdlib
    -nostartfiles
    -nodefaultlibs
    -no-pie
    -static
    -Wl,--gc-sections
    -Wl,--build-id=none
    -Wl,--orphan-handling=error
    -T
    "${pqc_firmware_dir}/link.ld"
    -lgcc)

add_custom_command(
    OUTPUT "${pqc_target_dir}/crt0.o"
    COMMAND
        "${PQC_RISCV_GCC}" -march=rv32imc -mabi=ilp32 -ffreestanding -fno-pic -fno-pie
        -msmall-data-limit=0 -mstrict-align -mcmodel=medlow -Wall -Wextra -Werror -c
        "${pqc_firmware_dir}/crt0.S" -o "${pqc_target_dir}/crt0.o"
    DEPENDS "${pqc_firmware_dir}/crt0.S"
    VERBATIM)

add_custom_command(
    OUTPUT "${pqc_target_dir}/runtime.o"
    BYPRODUCTS "${pqc_target_dir}/runtime.su"
    COMMAND
        "${PQC_RISCV_GCC}" ${pqc_c_flags} -I "${pqc_firmware_dir}" -c
        "${pqc_firmware_dir}/runtime.c" -o "${pqc_target_dir}/runtime.o"
    DEPENDS "${pqc_firmware_dir}/runtime.c" "${pqc_firmware_dir}/bench_mmio.h"
    VERBATIM)

function(pqc_add_firmware name define)
    set(object "${pqc_target_dir}/${name}.o")
    set(elf "${pqc_target_dir}/${name}.elf")
    set(map "${pqc_target_dir}/${name}.map")
    set(binary "${pqc_target_dir}/${name}.bin")
    set(hex "${pqc_target_dir}/${name}.hex")
    set(disassembly "${pqc_target_dir}/${name}.dis")
    set(compile_definition)
    if(NOT "${define}" STREQUAL "")
        set(compile_definition "-D${define}")
    endif()

    add_custom_command(
        OUTPUT "${object}"
        BYPRODUCTS "${pqc_target_dir}/${name}.su"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_c_flags} ${compile_definition} -I "${pqc_firmware_dir}"
            -c "${pqc_firmware_dir}/smoke.c" -o "${object}"
        DEPENDS "${pqc_firmware_dir}/smoke.c" "${pqc_firmware_dir}/bench_mmio.h"
        VERBATIM)
    add_custom_command(
        OUTPUT "${elf}" "${binary}" "${hex}" "${disassembly}"
        BYPRODUCTS "${map}"
        COMMAND
            "${PQC_RISCV_GCC}" "${pqc_target_dir}/crt0.o" "${pqc_target_dir}/runtime.o"
            "${object}" ${pqc_link_flags} "-Wl,-Map,${map}" -o "${elf}"
        COMMAND "${PQC_RISCV_OBJCOPY}" -O binary "${elf}" "${binary}"
        COMMAND
            "${PQC_RISCV_OBJCOPY}" -O verilog --verilog-data-width=4 "${elf}" "${hex}"
        COMMAND
            /bin/sh -c
            "\"${PQC_RISCV_OBJDUMP}\" -d -S \"${elf}\" > \"${disassembly}\""
        DEPENDS "${pqc_target_dir}/crt0.o" "${pqc_target_dir}/runtime.o" "${object}"
                "${pqc_firmware_dir}/link.ld"
        VERBATIM)

    set(${name}_elf "${elf}" PARENT_SCOPE)
    set(${name}_hex "${hex}" PARENT_SCOPE)
    set(${name}_dis "${disassembly}" PARENT_SCOPE)
endfunction()

pqc_add_firmware(baseline "")
pqc_add_firmware(misaligned PQC_TEST_MISALIGNED)
pqc_add_firmware(illegal PQC_TEST_ILLEGAL)

set(pqc_sim_cflags "-std=c++20 -O3 -I${PROJECT_SOURCE_DIR}/include")

function(pqc_add_verilated name top prefix cflags parameter)
    set(model_dir "${pqc_target_dir}/${name}-model")
    set(executable "${model_dir}/${prefix}")
    set(sources ${ARGN})
    set(parameter_arg)
    if(NOT "${parameter}" STREQUAL "")
        set(parameter_arg "${parameter}")
    endif()
    add_custom_command(
        OUTPUT "${executable}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${model_dir}"
        COMMAND
            "${PQC_VERILATOR}" --cc --exe --build -j 0 --Mdir "${model_dir}" --top-module
            "${top}" --prefix "${prefix}" --Wno-fatal -CFLAGS "${cflags}" ${parameter_arg}
            ${sources} "${pqc_sim_dir}/main.cpp" "${PROJECT_SOURCE_DIR}/src/target_measurement.cpp"
        DEPENDS
            "${pqc_sim_dir}/main.cpp"
            "${PROJECT_SOURCE_DIR}/src/target_measurement.cpp"
            "${PROJECT_SOURCE_DIR}/include/pqc_poly/target_measurement.hpp"
            ${sources}
        VERBATIM)
    set(${name}_sim "${executable}" PARENT_SCOPE)
endfunction()

pqc_add_verilated(
    pcpi
    pqc_pcpi_mlkem
    Vpqc_pcpi_mlkem
    "${pqc_sim_cflags} -DPQC_PCPI_UNIT=1"
    -GENABLE_FQMUL=1
    "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv")
pqc_add_verilated(
    project
    pqc_picorv32_sim_top
    Vpqc_picorv32_sim_top
    "${pqc_sim_cflags}"
    ""
    "${pqc_picorv32_source}"
    "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
    "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
    "${pqc_rtl_dir}/pqc_picorv32_sim_top.sv")
pqc_add_verilated(
    fqmul
    pqc_picorv32_sim_top
    Vpqc_picorv32_sim_top
    "${pqc_sim_cflags} -DPQC_FQMUL=1"
    -GENABLE_FQMUL=1
    "${pqc_picorv32_source}"
    "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
    "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
    "${pqc_rtl_dir}/pqc_picorv32_sim_top.sv")
pqc_add_verilated(
    stock
    pqc_picorv32_sim_top
    Vpqc_picorv32_sim_top
    "${pqc_sim_cflags} -DPQC_STOCK_MUL=1"
    -GSTOCK_MUL=1
    "${pqc_picorv32_source}"
    "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
    "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
    "${pqc_rtl_dir}/pqc_picorv32_sim_top.sv")

set(pqc_results "${pqc_target_dir}/results")
set(pqc_baseline_run "${pqc_results}/project-run-1.jsonl")
set(pqc_repeat_run "${pqc_results}/project-run-2.jsonl")
set(pqc_stock_run "${pqc_results}/stock-smoke.jsonl")
set(pqc_stack_result "${pqc_results}/baseline-stack.json")
set(pqc_size_result "${pqc_results}/baseline-code-size.json")
set(pqc_size_text "${pqc_target_dir}/baseline.size")
set(pqc_stack_usage "${pqc_target_dir}/baseline-stack-usage.su")
set(pqc_sim_stamp "${pqc_results}/simulation.stamp")

add_custom_command(
    OUTPUT "${pqc_sim_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
    COMMAND "${pcpi_sim}"
    COMMAND
        /bin/sh -c
        "\"${PQC_RISCV_SIZE}\" -A \"${baseline_elf}\" > \"${pqc_size_text}\""
    COMMAND
        /bin/sh -c
        "cat \"${pqc_target_dir}/runtime.su\" \"${pqc_target_dir}/baseline.su\" > \"${pqc_stack_usage}\""
    COMMAND
        "${project_sim}" "+firmware=${baseline_hex}" --output "${pqc_baseline_run}"
        --stack-output "${pqc_stack_result}" --stack-usage "${pqc_stack_usage}" --disassembly
        "${baseline_dis}" --size-input "${pqc_size_text}" --size-output "${pqc_size_result}"
    COMMAND
        "${project_sim}" "+firmware=${baseline_hex}" --output "${pqc_repeat_run}"
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${pqc_baseline_run}" "${pqc_repeat_run}"
    COMMAND "${stock_sim}" "+firmware=${baseline_hex}" --output "${pqc_stock_run}"
    COMMAND
        "${project_sim}" "+firmware=${misaligned_hex}" --expect-trap --output
        "${pqc_results}/misaligned.jsonl"
    COMMAND
        "${project_sim}" "+firmware=${illegal_hex}" --expect-trap --output
        "${pqc_results}/illegal.jsonl"
    COMMAND
        /bin/sh -c
        "cat \"${pqc_baseline_run}\" \"${pqc_stock_run}\" > \"${pqc_results}/baseline-smoke.jsonl\""
    COMMAND "${CMAKE_COMMAND}" -E touch "${pqc_sim_stamp}"
    DEPENDS
        "${pcpi_sim}"
        "${project_sim}"
        "${stock_sim}"
        "${baseline_elf}"
        "${baseline_hex}"
        "${baseline_dis}"
        "${misaligned_hex}"
        "${illegal_hex}"
    VERBATIM)

add_custom_target(pqc-picorv32-sim DEPENDS "${pqc_sim_stamp}")

if(PQC_POLY_PICORV32_MLKEM)
    pqc_poly_fetch_mlkem_native()

    set(pqc_mlkem_root "${PQC_POLY_MLKEM_NATIVE_SOURCE_DIR}/mlkem")
    set(pqc_mlkem_generated "${pqc_target_dir}/mlkem-generated")
    set(pqc_mlkem_results "${pqc_target_dir}/mlkem-results")
    set(
        pqc_mlkem_generated_files
        "${pqc_mlkem_generated}/mlkem512-candidates.json"
        "${pqc_mlkem_generated}/mlkem768-candidates.json"
        "${pqc_mlkem_generated}/mlkem1024-candidates.json")

    foreach(pqc_level IN ITEMS 512 768 1024)
        foreach(pqc_forward IN ITEMS stage fuse2)
            foreach(pqc_inverse IN ITEMS stage fuse2)
                foreach(pqc_reduction IN ITEMS each pair)
                    foreach(pqc_basemul IN ITEMS cachelate cacheeager directeager)
                        foreach(pqc_instruction IN ITEMS none fqmul)
                            set(
                                pqc_plan_id
                                "mlk${pqc_level}_f${pqc_forward}_i${pqc_inverse}_r${pqc_reduction}_b${pqc_basemul}_x${pqc_instruction}")
                            list(
                                APPEND
                                pqc_mlkem_generated_files
                                "${pqc_mlkem_generated}/backends/${pqc_plan_id}.c")
                        endforeach()
                    endforeach()
                endforeach()
            endforeach()
        endforeach()
    endforeach()

    add_custom_command(
        OUTPUT ${pqc_mlkem_generated_files}
        COMMAND
            "$<TARGET_FILE:pqc-poly-mlkem>" "${PROJECT_SOURCE_DIR}/examples/mlkem.json" -o
            "${pqc_mlkem_generated}"
        DEPENDS pqc-poly-mlkem "${PROJECT_SOURCE_DIR}/examples/mlkem.json"
        VERBATIM)
    add_custom_target(pqc-picorv32-mlkem-generate DEPENDS ${pqc_mlkem_generated_files})

    set(
        pqc_mlkem_flags
        ${pqc_c_flags}
        -DMLK_CONFIG_NO_RANDOMIZED_API
        -DMLK_CONFIG_FILE=\"mlkem_config.h\")
    foreach(pqc_level IN ITEMS 512 768 1024)
        if(pqc_level EQUAL 512)
            set(pqc_k 2)
        elseif(pqc_level EQUAL 768)
            set(pqc_k 3)
        else()
            set(pqc_k 4)
        endif()

        set(pqc_upstream_object "${pqc_target_dir}/mlkem-native-${pqc_level}.o")
        add_custom_command(
            OUTPUT "${pqc_upstream_object}"
            BYPRODUCTS "${pqc_target_dir}/mlkem-native-${pqc_level}.su"
            COMMAND
                "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
                "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" -I "${pqc_firmware_dir}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem" -I "${pqc_mlkem_root}" -c
                "${pqc_mlkem_root}/mlkem_native.c" -o "${pqc_upstream_object}"
            DEPENDS
                "${pqc_mlkem_root}/mlkem_native.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/mlkem_config.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/arith_backend.h"
            VERBATIM)

        set(pqc_portable_object "${pqc_target_dir}/mlkem-native-${pqc_level}-portable.o")
        add_custom_command(
            OUTPUT "${pqc_portable_object}"
            BYPRODUCTS "${pqc_target_dir}/mlkem-native-${pqc_level}-portable.su"
            COMMAND
                "${PQC_RISCV_GCC}" ${pqc_mlkem_flags} -DPQC_MLKEM_PORTABLE
                "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" -I "${pqc_firmware_dir}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem" -I "${pqc_mlkem_root}" -c
                "${pqc_mlkem_root}/mlkem_native.c" -o "${pqc_portable_object}"
            DEPENDS
                "${pqc_mlkem_root}/mlkem_native.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/mlkem_config.h"
            VERBATIM)

        set(pqc_portable_bench "${pqc_target_dir}/mlkem-bench-${pqc_level}-portable.o")
        math(EXPR pqc_portable_scratch "${pqc_k} * 256")
        add_custom_command(
            OUTPUT "${pqc_portable_bench}"
            BYPRODUCTS "${pqc_target_dir}/mlkem-bench-${pqc_level}-portable.su"
            COMMAND
                "${PQC_RISCV_GCC}" ${pqc_mlkem_flags} -DPQC_MLKEM_PORTABLE
                "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" "-DPQC_MLKEM_K=${pqc_k}"
                "-DPQC_MLKEM_SCRATCH_BYTES=${pqc_portable_scratch}"
                -I "${pqc_firmware_dir}" -I "${CMAKE_CURRENT_SOURCE_DIR}/mlkem" -I
                "${pqc_mlkem_root}" -c "${pqc_firmware_dir}/mlkem_bench.c" -o
                "${pqc_portable_bench}"
            DEPENDS
                "${pqc_firmware_dir}/mlkem_bench.c"
                "${pqc_firmware_dir}/bench_mmio.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/mlkem_config.h"
                "${pqc_mlkem_root}/mlkem_native.h"
                "${pqc_mlkem_root}/src/poly.h"
                "${pqc_mlkem_root}/src/poly_k.h"
            VERBATIM)

        foreach(pqc_cache IN ITEMS cached direct)
            if(pqc_cache STREQUAL cached)
                math(EXPR pqc_scratch "${pqc_k} * 256")
            else()
                set(pqc_scratch 0)
            endif()
            set(pqc_bench_object "${pqc_target_dir}/mlkem-bench-${pqc_level}-${pqc_cache}.o")
            add_custom_command(
                OUTPUT "${pqc_bench_object}"
                BYPRODUCTS "${pqc_target_dir}/mlkem-bench-${pqc_level}-${pqc_cache}.su"
                COMMAND
                    "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
                    "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" "-DPQC_MLKEM_K=${pqc_k}"
                    "-DPQC_MLKEM_SCRATCH_BYTES=${pqc_scratch}" -I "${pqc_firmware_dir}"
                    -I "${CMAKE_CURRENT_SOURCE_DIR}/mlkem" -I "${pqc_mlkem_root}" -c
                    "${pqc_firmware_dir}/mlkem_bench.c" -o "${pqc_bench_object}"
                DEPENDS
                    "${pqc_firmware_dir}/mlkem_bench.c"
                    "${pqc_firmware_dir}/bench_mmio.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/mlkem_config.h"
                    "${pqc_mlkem_root}/mlkem_native.h"
                VERBATIM)
            set("pqc_bench_${pqc_level}_${pqc_cache}" "${pqc_bench_object}")
        endforeach()
        set("pqc_upstream_${pqc_level}" "${pqc_upstream_object}")
        set("pqc_upstream_portable_${pqc_level}" "${pqc_portable_object}")
        set("pqc_bench_portable_${pqc_level}" "${pqc_portable_bench}")
    endforeach()

    set(pqc_mlkem_all_binaries)
    set(pqc_mlkem_all_measurements)
    set(pqc_fqmul_all_binaries)
    set(pqc_fqmul_all_measurements)
    foreach(pqc_level IN ITEMS 512 768 1024)
        set(pqc_level_project)
        set(pqc_level_stock)
        foreach(pqc_forward IN ITEMS stage fuse2)
            foreach(pqc_inverse IN ITEMS stage fuse2)
                foreach(pqc_reduction IN ITEMS each pair)
                    foreach(pqc_basemul IN ITEMS cachelate cacheeager directeager)
                        set(
                            pqc_plan_id
                            "mlk${pqc_level}_f${pqc_forward}_i${pqc_inverse}_r${pqc_reduction}_b${pqc_basemul}_xnone")
                        if(pqc_basemul STREQUAL directeager)
                            set(pqc_cache direct)
                        else()
                            set(pqc_cache cached)
                        endif()
                        set(pqc_source "${pqc_mlkem_generated}/backends/${pqc_plan_id}.c")
                        set(pqc_object "${pqc_target_dir}/${pqc_plan_id}.o")
                        set(pqc_elf "${pqc_target_dir}/${pqc_plan_id}.elf")
                        set(pqc_hex "${pqc_target_dir}/${pqc_plan_id}.hex")
                        set(pqc_dis "${pqc_target_dir}/${pqc_plan_id}.dis")
                        set(pqc_size "${pqc_target_dir}/${pqc_plan_id}.size")
                        set(pqc_stack_usage "${pqc_target_dir}/${pqc_plan_id}.stack-usage.su")
                        add_custom_command(
                            OUTPUT "${pqc_object}"
                            BYPRODUCTS "${pqc_target_dir}/${pqc_plan_id}.su"
                            COMMAND
                                "${PQC_RISCV_GCC}" ${pqc_c_flags} -c "${pqc_source}" -o
                                "${pqc_object}"
                            DEPENDS "${pqc_source}"
                            VERBATIM)
                        add_custom_command(
                            OUTPUT "${pqc_elf}" "${pqc_hex}" "${pqc_dis}" "${pqc_size}"
                                   "${pqc_stack_usage}"
                            COMMAND
                                "${PQC_RISCV_GCC}" "${pqc_target_dir}/crt0.o"
                                "${pqc_target_dir}/runtime.o"
                                "${pqc_bench_${pqc_level}_${pqc_cache}}" "${pqc_object}"
                                "${pqc_upstream_${pqc_level}}" ${pqc_link_flags}
                                "-Wl,-Map,${pqc_target_dir}/${pqc_plan_id}.map" -o "${pqc_elf}"
                            COMMAND
                                "${PQC_RISCV_OBJCOPY}" -O verilog --verilog-data-width=4
                                "${pqc_elf}" "${pqc_hex}"
                            COMMAND
                                /bin/sh -c
                                "\"${PQC_RISCV_OBJDUMP}\" -d -S \"${pqc_elf}\" > \"${pqc_dis}\""
                            COMMAND
                                /bin/sh -c
                                "\"${PQC_RISCV_SIZE}\" -A \"${pqc_elf}\" > \"${pqc_size}\""
                            COMMAND
                                /bin/sh -c
                                "cat \"${pqc_target_dir}/runtime.su\" \"${pqc_target_dir}/mlkem-native-${pqc_level}.su\" \"${pqc_target_dir}/mlkem-bench-${pqc_level}-${pqc_cache}.su\" \"${pqc_target_dir}/${pqc_plan_id}.su\" > \"${pqc_stack_usage}\""
                            DEPENDS
                                "${pqc_target_dir}/crt0.o"
                                "${pqc_target_dir}/runtime.o"
                                "${pqc_bench_${pqc_level}_${pqc_cache}}"
                                "${pqc_object}"
                                "${pqc_upstream_${pqc_level}}"
                                "${pqc_firmware_dir}/link.ld"
                            VERBATIM)

                        set(pqc_project_result "${pqc_mlkem_results}/${pqc_plan_id}-project.jsonl")
                        set(pqc_stock_result "${pqc_mlkem_results}/${pqc_plan_id}-stock.jsonl")
                        set(pqc_stack_result "${pqc_mlkem_results}/${pqc_plan_id}-stack.json")
                        set(pqc_size_result "${pqc_mlkem_results}/${pqc_plan_id}-size.json")
                        add_custom_command(
                            OUTPUT "${pqc_project_result}" "${pqc_stock_result}"
                                   "${pqc_stack_result}" "${pqc_size_result}"
                            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_mlkem_results}"
                            COMMAND
                                "${project_sim}" "+firmware=${pqc_hex}" --output
                                "${pqc_project_result}" --plan-id "${pqc_plan_id}" --level
                                "${pqc_level}" --repeat-count 3 --stack-output
                                "${pqc_stack_result}" --stack-usage "${pqc_stack_usage}"
                                --stack-root measured_complete --disassembly "${pqc_dis}"
                                --size-input "${pqc_size}" --size-output "${pqc_size_result}"
                            COMMAND
                                "${stock_sim}" "+firmware=${pqc_hex}" --output
                                "${pqc_stock_result}" --plan-id "${pqc_plan_id}" --level
                                "${pqc_level}" --repeat-count 3
                            DEPENDS "${project_sim}" "${stock_sim}" "${pqc_elf}" "${pqc_hex}"
                                    "${pqc_dis}" "${pqc_size}" "${pqc_stack_usage}"
                            VERBATIM)
                        list(APPEND pqc_mlkem_all_binaries "${pqc_elf}")
                        list(APPEND pqc_mlkem_all_measurements "${pqc_project_result}"
                             "${pqc_stock_result}" "${pqc_stack_result}" "${pqc_size_result}")
                        list(APPEND pqc_level_project "${pqc_project_result}")
                        list(APPEND pqc_level_stock "${pqc_stock_result}")
                    endforeach()
                endforeach()
            endforeach()
        endforeach()
        set(pqc_level_project_output
            "${pqc_mlkem_results}/mlkem${pqc_level}-software-measurements.jsonl")
        set(pqc_level_stock_output
            "${pqc_mlkem_results}/mlkem${pqc_level}-stock-measurements.jsonl")
        list(JOIN pqc_level_project " " pqc_level_project_inputs)
        list(JOIN pqc_level_stock " " pqc_level_stock_inputs)
        add_custom_command(
            OUTPUT "${pqc_level_project_output}" "${pqc_level_stock_output}"
            COMMAND
                /bin/sh -c
                "cat ${pqc_level_project_inputs} > \"${pqc_level_project_output}\""
            COMMAND
                /bin/sh -c
                "cat ${pqc_level_stock_inputs} > \"${pqc_level_stock_output}\""
            DEPENDS ${pqc_level_project} ${pqc_level_stock}
            VERBATIM)
        list(APPEND pqc_mlkem_all_measurements "${pqc_level_project_output}"
             "${pqc_level_stock_output}")
    endforeach()

    foreach(pqc_level IN ITEMS 512 768 1024)
        set(pqc_level_fqmul)
        foreach(pqc_forward IN ITEMS stage fuse2)
            foreach(pqc_inverse IN ITEMS stage fuse2)
                foreach(pqc_reduction IN ITEMS each pair)
                    foreach(pqc_basemul IN ITEMS cachelate cacheeager directeager)
                        set(
                            pqc_plan_id
                            "mlk${pqc_level}_f${pqc_forward}_i${pqc_inverse}_r${pqc_reduction}_b${pqc_basemul}_xfqmul")
                        if(pqc_basemul STREQUAL directeager)
                            set(pqc_cache direct)
                        else()
                            set(pqc_cache cached)
                        endif()
                        set(pqc_source "${pqc_mlkem_generated}/backends/${pqc_plan_id}.c")
                        set(pqc_object "${pqc_target_dir}/${pqc_plan_id}.o")
                        set(pqc_elf "${pqc_target_dir}/${pqc_plan_id}.elf")
                        set(pqc_hex "${pqc_target_dir}/${pqc_plan_id}.hex")
                        set(pqc_dis "${pqc_target_dir}/${pqc_plan_id}.dis")
                        set(pqc_size "${pqc_target_dir}/${pqc_plan_id}.size")
                        set(pqc_stack_usage "${pqc_target_dir}/${pqc_plan_id}.stack-usage.su")
                        add_custom_command(
                            OUTPUT "${pqc_object}"
                            BYPRODUCTS "${pqc_target_dir}/${pqc_plan_id}.su"
                            COMMAND
                                "${PQC_RISCV_GCC}" ${pqc_c_flags}
                                -DPQC_POLY_HAVE_MLK_FQMUL -I
                                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem" -c "${pqc_source}" -o
                                "${pqc_object}"
                            DEPENDS "${pqc_source}" "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/fqmul.h"
                            VERBATIM)
                        add_custom_command(
                            OUTPUT "${pqc_elf}" "${pqc_hex}" "${pqc_dis}" "${pqc_size}"
                                   "${pqc_stack_usage}"
                            COMMAND
                                "${PQC_RISCV_GCC}" "${pqc_target_dir}/crt0.o"
                                "${pqc_target_dir}/runtime.o"
                                "${pqc_bench_${pqc_level}_${pqc_cache}}" "${pqc_object}"
                                "${pqc_upstream_${pqc_level}}" ${pqc_link_flags}
                                "-Wl,-Map,${pqc_target_dir}/${pqc_plan_id}.map" -o "${pqc_elf}"
                            COMMAND
                                "${PQC_RISCV_OBJCOPY}" -O verilog --verilog-data-width=4
                                "${pqc_elf}" "${pqc_hex}"
                            COMMAND
                                /bin/sh -c
                                "\"${PQC_RISCV_OBJDUMP}\" -d -S \"${pqc_elf}\" > \"${pqc_dis}\""
                            COMMAND
                                /bin/sh -c
                                "\"${PQC_RISCV_SIZE}\" -A \"${pqc_elf}\" > \"${pqc_size}\""
                            COMMAND
                                /bin/sh -c
                                "cat \"${pqc_target_dir}/runtime.su\" \"${pqc_target_dir}/mlkem-native-${pqc_level}.su\" \"${pqc_target_dir}/mlkem-bench-${pqc_level}-${pqc_cache}.su\" \"${pqc_target_dir}/${pqc_plan_id}.su\" > \"${pqc_stack_usage}\""
                            DEPENDS
                                "${pqc_target_dir}/crt0.o"
                                "${pqc_target_dir}/runtime.o"
                                "${pqc_bench_${pqc_level}_${pqc_cache}}"
                                "${pqc_object}"
                                "${pqc_upstream_${pqc_level}}"
                                "${pqc_firmware_dir}/link.ld"
                            VERBATIM)

                        set(pqc_fqmul_result "${pqc_mlkem_results}/${pqc_plan_id}-fqmul.jsonl")
                        set(pqc_stack_result "${pqc_mlkem_results}/${pqc_plan_id}-stack.json")
                        set(pqc_size_result "${pqc_mlkem_results}/${pqc_plan_id}-size.json")
                        add_custom_command(
                            OUTPUT "${pqc_fqmul_result}" "${pqc_stack_result}"
                                   "${pqc_size_result}"
                            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_mlkem_results}"
                            COMMAND
                                "${fqmul_sim}" "+firmware=${pqc_hex}" --output
                                "${pqc_fqmul_result}" --plan-id "${pqc_plan_id}" --level
                                "${pqc_level}" --repeat-count 3 --stack-output
                                "${pqc_stack_result}" --stack-usage "${pqc_stack_usage}"
                                --stack-root measured_complete --disassembly "${pqc_dis}"
                                --size-input "${pqc_size}" --size-output "${pqc_size_result}"
                            DEPENDS "${fqmul_sim}" "${pqc_elf}" "${pqc_hex}" "${pqc_dis}"
                                    "${pqc_size}" "${pqc_stack_usage}"
                            VERBATIM)
                        list(APPEND pqc_fqmul_all_binaries "${pqc_elf}")
                        list(APPEND pqc_fqmul_all_measurements "${pqc_fqmul_result}"
                             "${pqc_stack_result}" "${pqc_size_result}")
                        list(APPEND pqc_level_fqmul "${pqc_fqmul_result}")
                    endforeach()
                endforeach()
            endforeach()
        endforeach()
        set(pqc_level_fqmul_output
            "${pqc_mlkem_results}/mlkem${pqc_level}-fqmul-measurements.jsonl")
        list(JOIN pqc_level_fqmul " " pqc_level_fqmul_inputs)
        add_custom_command(
            OUTPUT "${pqc_level_fqmul_output}"
            COMMAND
                /bin/sh -c
                "cat ${pqc_level_fqmul_inputs} > \"${pqc_level_fqmul_output}\""
            DEPENDS ${pqc_level_fqmul}
            VERBATIM)
        list(APPEND pqc_fqmul_all_measurements "${pqc_level_fqmul_output}")
    endforeach()

    foreach(pqc_level IN ITEMS 512 768 1024)
        set(pqc_plan_id "mlk${pqc_level}_portable")
        set(pqc_elf "${pqc_target_dir}/${pqc_plan_id}.elf")
        set(pqc_hex "${pqc_target_dir}/${pqc_plan_id}.hex")
        set(pqc_dis "${pqc_target_dir}/${pqc_plan_id}.dis")
        set(pqc_size "${pqc_target_dir}/${pqc_plan_id}.size")
        set(pqc_stack_usage "${pqc_target_dir}/${pqc_plan_id}.stack-usage.su")
        add_custom_command(
            OUTPUT "${pqc_elf}" "${pqc_hex}" "${pqc_dis}" "${pqc_size}"
                   "${pqc_stack_usage}"
            COMMAND
                "${PQC_RISCV_GCC}" "${pqc_target_dir}/crt0.o"
                "${pqc_target_dir}/runtime.o" "${pqc_bench_portable_${pqc_level}}"
                "${pqc_upstream_portable_${pqc_level}}" ${pqc_link_flags}
                "-Wl,-Map,${pqc_target_dir}/${pqc_plan_id}.map" -o "${pqc_elf}"
            COMMAND
                "${PQC_RISCV_OBJCOPY}" -O verilog --verilog-data-width=4 "${pqc_elf}"
                "${pqc_hex}"
            COMMAND
                /bin/sh -c
                "\"${PQC_RISCV_OBJDUMP}\" -d -S \"${pqc_elf}\" > \"${pqc_dis}\""
            COMMAND
                /bin/sh -c
                "\"${PQC_RISCV_SIZE}\" -A \"${pqc_elf}\" > \"${pqc_size}\""
            COMMAND
                /bin/sh -c
                "cat \"${pqc_target_dir}/runtime.su\" \"${pqc_target_dir}/mlkem-native-${pqc_level}-portable.su\" \"${pqc_target_dir}/mlkem-bench-${pqc_level}-portable.su\" > \"${pqc_stack_usage}\""
            DEPENDS
                "${pqc_target_dir}/crt0.o"
                "${pqc_target_dir}/runtime.o"
                "${pqc_bench_portable_${pqc_level}}"
                "${pqc_upstream_portable_${pqc_level}}"
                "${pqc_firmware_dir}/link.ld"
            VERBATIM)
        set(pqc_project_result "${pqc_mlkem_results}/${pqc_plan_id}-project.jsonl")
        set(pqc_stock_result "${pqc_mlkem_results}/${pqc_plan_id}-stock.jsonl")
        set(pqc_stack_result "${pqc_mlkem_results}/${pqc_plan_id}-stack.json")
        set(pqc_size_result "${pqc_mlkem_results}/${pqc_plan_id}-size.json")
        add_custom_command(
            OUTPUT "${pqc_project_result}" "${pqc_stock_result}" "${pqc_stack_result}"
                   "${pqc_size_result}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_mlkem_results}"
            COMMAND
                "${project_sim}" "+firmware=${pqc_hex}" --output "${pqc_project_result}"
                --plan-id "${pqc_plan_id}" --level "${pqc_level}" --repeat-count 3
                --stack-output "${pqc_stack_result}" --stack-usage "${pqc_stack_usage}"
                --stack-root measured_complete --disassembly "${pqc_dis}" --size-input
                "${pqc_size}" --size-output "${pqc_size_result}"
            COMMAND
                "${stock_sim}" "+firmware=${pqc_hex}" --output "${pqc_stock_result}"
                --plan-id "${pqc_plan_id}" --level "${pqc_level}" --repeat-count 3
            DEPENDS "${project_sim}" "${stock_sim}" "${pqc_elf}" "${pqc_hex}"
                    "${pqc_dis}" "${pqc_size}" "${pqc_stack_usage}"
            VERBATIM)
        list(APPEND pqc_mlkem_all_binaries "${pqc_elf}")
        list(APPEND pqc_mlkem_all_measurements "${pqc_project_result}" "${pqc_stock_result}"
             "${pqc_stack_result}" "${pqc_size_result}")
    endforeach()

    set(
        pqc_mlkem_complete_files
        "${pqc_mlkem_results}/mlkem512-candidates.json"
        "${pqc_mlkem_results}/mlkem768-candidates.json"
        "${pqc_mlkem_results}/mlkem1024-candidates.json"
        "${pqc_mlkem_results}/mlkem512-software-winner.json"
        "${pqc_mlkem_results}/mlkem768-software-winner.json"
        "${pqc_mlkem_results}/mlkem1024-software-winner.json")
    add_custom_command(
        OUTPUT ${pqc_mlkem_complete_files}
        COMMAND
            "${CMAKE_COMMAND}" -E copy
            "${pqc_mlkem_generated}/mlkem512-candidates.json"
            "${pqc_mlkem_results}/mlkem512-candidates.json"
        COMMAND
            "${CMAKE_COMMAND}" -E copy
            "${pqc_mlkem_generated}/mlkem768-candidates.json"
            "${pqc_mlkem_results}/mlkem768-candidates.json"
        COMMAND
            "${CMAKE_COMMAND}" -E copy
            "${pqc_mlkem_generated}/mlkem1024-candidates.json"
            "${pqc_mlkem_results}/mlkem1024-candidates.json"
        COMMAND
            "$<TARGET_FILE:pqc-poly-mlkem>" --finalize
            "${PROJECT_SOURCE_DIR}/examples/mlkem.json" "${pqc_mlkem_results}"
        DEPENDS
            pqc-poly-mlkem
            ${pqc_mlkem_generated_files}
            ${pqc_mlkem_all_measurements}
            "${PROJECT_SOURCE_DIR}/examples/mlkem.json"
        VERBATIM)

    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE pqc_mlkem_git_result
        OUTPUT_VARIABLE pqc_mlkem_repository_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(LENGTH "${pqc_mlkem_repository_sha}" pqc_mlkem_repository_sha_length)
    if(NOT pqc_mlkem_git_result EQUAL 0 OR
       NOT pqc_mlkem_repository_sha MATCHES "^[0-9a-f]+$" OR
       NOT pqc_mlkem_repository_sha_length EQUAL 40)
        message(FATAL_ERROR "cannot identify repository commit for mlkem results")
    endif()
    string(SUBSTRING "${pqc_mlkem_repository_sha}" 0 8 pqc_mlkem_repository_short)
    string(SUBSTRING "${PQC_POLY_MLKEM_NATIVE_SHA}" 0 8 pqc_mlkem_native_short)
    set(
        pqc_mlkem_final_results
        "${PROJECT_SOURCE_DIR}/results/raw/picorv32-step2-${pqc_mlkem_repository_short}-${pqc_mlkem_native_short}")

    add_custom_target(pqc-picorv32-mlkem-build DEPENDS ${pqc_mlkem_all_binaries})
    add_custom_target(pqc-picorv32-fqmul-build DEPENDS ${pqc_fqmul_all_binaries})
    add_custom_target(pqc-picorv32-fqmul DEPENDS ${pqc_fqmul_all_measurements})
    add_custom_target(pqc-picorv32-mlkem DEPENDS ${pqc_mlkem_complete_files})
    add_custom_target(
        pqc-picorv32-mlkem-finalize
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_mlkem_final_results}"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory "${pqc_mlkem_results}"
            "${pqc_mlkem_final_results}"
        DEPENDS ${pqc_mlkem_complete_files}
        VERBATIM)
endif()

if(PQC_POLY_PICORV32_FQMUL_VERIFY)
    set(pqc_fqmul_formal_dir "${pqc_target_dir}/fqmul-formal")
    file(MAKE_DIRECTORY "${pqc_fqmul_formal_dir}")
    configure_file("${pqc_picorv32_source}" "${pqc_fqmul_formal_dir}/picorv32.v" COPYONLY)
    configure_file(
        "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
        "${pqc_fqmul_formal_dir}/pqc_pcpi_mlkem.sv"
        COPYONLY)
    configure_file(
        "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
        "${pqc_fqmul_formal_dir}/pqc_picorv32_core_top.sv"
        COPYONLY)
    foreach(pqc_formal_file IN ITEMS fqmul.sby fqmul_properties.sv rvfi_fqmul_monitor.sv)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/formal/${pqc_formal_file}"
            "${pqc_fqmul_formal_dir}/${pqc_formal_file}"
            COPYONLY)
    endforeach()
    set(
        pqc_fqmul_formal_status
        "${pqc_fqmul_formal_dir}/fqmul_pcpi/status"
        "${pqc_fqmul_formal_dir}/fqmul_m/status"
        "${pqc_fqmul_formal_dir}/fqmul_rvfi/status"
        "${pqc_fqmul_formal_dir}/fqmul_rvfi_cover/status")
    add_custom_command(
        OUTPUT ${pqc_fqmul_formal_status}
        COMMAND "${PQC_SBY}" -f fqmul.sby pcpi
        COMMAND "${PQC_SBY}" -f fqmul.sby m
        COMMAND "${PQC_SBY}" -f fqmul.sby rvfi
        COMMAND "${PQC_SBY}" -f fqmul.sby rvfi_cover
        DEPENDS
            "${pqc_picorv32_source}"
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
            "${CMAKE_CURRENT_SOURCE_DIR}/formal/fqmul.sby"
            "${CMAKE_CURRENT_SOURCE_DIR}/formal/fqmul_properties.sv"
            "${CMAKE_CURRENT_SOURCE_DIR}/formal/rvfi_fqmul_monitor.sv"
        WORKING_DIRECTORY "${pqc_fqmul_formal_dir}"
        VERBATIM)

    set(
        pqc_cbmc_flags
        --bounds-check
        --pointer-check
        --signed-overflow-check
        --unsigned-overflow-check
        --conversion-check
        --undefined-shift-check
        --div-by-zero-check
        --unwinding-assertions
        --boolector
        --json-ui)
    list(JOIN pqc_cbmc_flags " " pqc_cbmc_flags_text)
    set(pqc_fqmul_cbmc_outputs)
    set(
        pqc_fqmul_cbmc_harnesses
        conversion|pqc_fqmul_conversion_harness|none
        fallback|pqc_fqmul_fallback_harness|none
        upper|pqc_fqmul_upper_harness|none
        wrapper|pqc_fqmul_wrapper_harness|none
        callsite|pqc_fqmul_callsite_harness|none
        butterfly|pqc_fqmul_butterfly_harness|--unwind_129
        butterfly-value|pqc_fqmul_butterfly_value_harness|none
        base|pqc_fqmul_base_harness|--unwind_5)
    foreach(pqc_harness IN LISTS pqc_fqmul_cbmc_harnesses)
        string(REPLACE "|" ";" pqc_parts "${pqc_harness}")
        list(GET pqc_parts 0 pqc_name)
        list(GET pqc_parts 1 pqc_function)
        list(GET pqc_parts 2 pqc_unwind)
        if(pqc_unwind STREQUAL "none")
            set(pqc_unwind)
        else()
            string(REPLACE "_" " " pqc_unwind "${pqc_unwind}")
        endif()
        set(pqc_output "${pqc_fqmul_formal_dir}/cbmc-${pqc_name}.json")
        add_custom_command(
            OUTPUT "${pqc_output}"
            COMMAND
                "${CMAKE_COMMAND}" -E env "PATH=${pqc_cad_bin}:$ENV{PATH}" /bin/sh -c
                "\"${PQC_CBMC}\" \"${PROJECT_SOURCE_DIR}/tests/fqmul_cbmc.c\" --function ${pqc_function} ${pqc_cbmc_flags_text} ${pqc_unwind} > \"${pqc_output}\""
            DEPENDS
                "${PROJECT_SOURCE_DIR}/tests/fqmul_cbmc.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/fqmul.h"
            VERBATIM)
        list(APPEND pqc_fqmul_cbmc_outputs "${pqc_output}")
    endforeach()
    add_custom_target(
        pqc-picorv32-fqmul-verify
        DEPENDS ${pqc_fqmul_formal_status} ${pqc_fqmul_cbmc_outputs})
endif()

if(PQC_POLY_PICORV32_SYNTHESIS)
    function(pqc_json_array output)
        set(result "[")
        set(separator "")
        foreach(value IN LISTS ARGN)
            string(REPLACE "\\" "\\\\" value "${value}")
            string(REPLACE "\"" "\\\"" value "${value}")
            string(APPEND result "${separator}\"${value}\"")
            set(separator ", ")
        endforeach()
        string(APPEND result "]")
        set(${output} "${result}" PARENT_SCOPE)
    endfunction()

    function(pqc_tool_version output executable argument)
        execute_process(
            COMMAND "${executable}" "${argument}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE value
            ERROR_VARIABLE error
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "failed to query tool version: ${executable} ${argument}")
        endif()
        if(value STREQUAL "")
            string(REPLACE "\r\n" "\n" lines "${error}")
            string(REPLACE "\r" "\n" lines "${lines}")
            string(REPLACE "\n" ";" lines "${lines}")
            list(FILTER lines EXCLUDE REGEX "^$")
            list(POP_BACK lines value)
        else()
            string(REGEX REPLACE "[\r\n].*" "" value "${value}")
        endif()
        string(REPLACE "\\" "\\\\" value "${value}")
        string(REPLACE "\"" "\\\"" value "${value}")
        set(${output} "${value}" PARENT_SCOPE)
    endfunction()

    pqc_tool_version(pqc_gcc_version "${PQC_RISCV_GCC}" --version)
    pqc_tool_version(pqc_binutils_version "${PQC_RISCV_LD}" --version)
    pqc_tool_version(pqc_verilator_version "${PQC_VERILATOR}" --version)
    pqc_tool_version(pqc_yosys_version "${PQC_YOSYS}" -V)
    pqc_tool_version(pqc_nextpnr_version "${PQC_NEXTPNR}" --version)
    pqc_tool_version(pqc_solver_version "${PQC_SOLVER}" --version)
    pqc_tool_version(pqc_cmake_version "${CMAKE_COMMAND}" --version)
    pqc_tool_version(pqc_ninja_version "${PQC_NINJA}" --version)
    pqc_tool_version(pqc_python_version "${PQC_PYTHON}" --version)

    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE pqc_repository_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    if(DEFINED ENV{PQC_POLY_CONTAINER_DIGEST} AND NOT "$ENV{PQC_POLY_CONTAINER_DIGEST}" STREQUAL "")
        set(pqc_container_digest "\"$ENV{PQC_POLY_CONTAINER_DIGEST}\"")
    else()
        set(pqc_container_digest null)
    endif()

    set(pqc_manifest_link_flags ${pqc_link_flags}
                                "-Wl,-Map,${pqc_target_dir}/baseline.map")
    list(TRANSFORM pqc_manifest_link_flags REPLACE
         "${pqc_firmware_dir}/link.ld" "targets/picorv32/firmware/link.ld")
    list(TRANSFORM pqc_manifest_link_flags REPLACE
         "${pqc_target_dir}/baseline.map" "baseline.map")
    pqc_json_array(pqc_compiler_flags_json ${pqc_c_flags})
    pqc_json_array(pqc_linker_flags_json ${pqc_manifest_link_flags})

    set(pqc_manifest "${pqc_results}/baseline-manifest.json")
    set(pqc_manifest_content "{\n")
    string(APPEND pqc_manifest_content "  \"repository_sha\": \"${pqc_repository_sha}\",\n")
    string(APPEND pqc_manifest_content "  \"picorv32_sha\": \"${PQC_POLY_PICORV32_SHA}\",\n")
    string(APPEND pqc_manifest_content "  \"riscv_toolchain_release\": \"${PQC_RISCV_RELEASE}\",\n")
    string(APPEND pqc_manifest_content "  \"oss_cad_suite_release\": \"${PQC_CAD_RELEASE}\",\n")
    string(APPEND pqc_manifest_content "  \"gcc_version\": \"${pqc_gcc_version}\",\n")
    string(APPEND pqc_manifest_content "  \"binutils_version\": \"${pqc_binutils_version}\",\n")
    string(APPEND pqc_manifest_content "  \"verilator_version\": \"${pqc_verilator_version}\",\n")
    string(APPEND pqc_manifest_content "  \"yosys_version\": \"${pqc_yosys_version}\",\n")
    string(APPEND pqc_manifest_content "  \"nextpnr_version\": \"${pqc_nextpnr_version}\",\n")
    string(APPEND pqc_manifest_content "  \"solver_version\": \"${pqc_solver_version}\",\n")
    string(APPEND pqc_manifest_content "  \"cmake_version\": \"${pqc_cmake_version}\",\n")
    string(APPEND pqc_manifest_content "  \"ninja_version\": \"${pqc_ninja_version}\",\n")
    string(APPEND pqc_manifest_content "  \"python_version\": \"${pqc_python_version}\",\n")
    string(APPEND pqc_manifest_content "  \"compiler_flags\": ${pqc_compiler_flags_json},\n")
    string(APPEND pqc_manifest_content "  \"linker_flags\": ${pqc_linker_flags_json},\n")
    string(APPEND pqc_manifest_content "  \"core_parameters\": [\"ENABLE_COUNTERS=1\", \"ENABLE_COUNTERS64=1\", \"ENABLE_REGS_16_31=1\", \"ENABLE_REGS_DUALPORT=1\", \"LATCHED_MEM_RDATA=0\", \"TWO_STAGE_SHIFT=1\", \"BARREL_SHIFTER=1\", \"TWO_CYCLE_COMPARE=0\", \"TWO_CYCLE_ALU=0\", \"COMPRESSED_ISA=1\", \"CATCH_MISALIGN=1\", \"CATCH_ILLINSN=1\", \"ENABLE_PCPI=1\", \"ENABLE_MUL=0\", \"ENABLE_FAST_MUL=0\", \"ENABLE_DIV=1\", \"ENABLE_IRQ=0\", \"ENABLE_IRQ_QREGS=0\", \"ENABLE_IRQ_TIMER=0\", \"ENABLE_TRACE=0\", \"REGS_INIT_ZERO=0\", \"PROGADDR_RESET=0x00000000\", \"PROGADDR_IRQ=0x00000010\", \"STACKADDR=0x00080000\"],\n")
    string(APPEND pqc_manifest_content "  \"fpga_part\": \"LFE5U-45F-6BG381C\",\n")
    string(APPEND pqc_manifest_content "  \"synthesis_seeds\": [1, 2, 3, 4, 5],\n")
    string(APPEND pqc_manifest_content "  \"source_archives\": [{\"name\": \"picorv32\", \"sha256\": \"${PQC_POLY_PICORV32_ARCHIVE_SHA256}\"}, {\"name\": \"riscv32-elf-ubuntu-22.04-gcc.tar.xz\", \"sha256\": \"ae36abbec394b29643154c1b4a1322e829937d04e82f41b47f9c27d3bd68e543\"}, {\"name\": \"oss-cad-suite-linux-x64-20260729.tgz\", \"sha256\": \"89ea1152ea84bc600f18cc685f721d534d1f018e09831662787865a3d79ce4aa\"}],\n")
    string(APPEND pqc_manifest_content "  \"container_digest\": ${pqc_container_digest}\n}\n")
    file(GENERATE OUTPUT "${pqc_manifest}" CONTENT "${pqc_manifest_content}")

    set(pqc_step1_sha e9b50a67dca6faafc8b8b2d1edcc79beff5eb99b)
    set(pqc_step1_dir "${pqc_target_dir}/step1-rtl")
    file(MAKE_DIRECTORY "${pqc_step1_dir}")
    foreach(pqc_step1_file IN ITEMS pqc_pcpi_mlkem.sv pqc_picorv32_core_top.sv)
        execute_process(
            COMMAND
                git show
                "${pqc_step1_sha}:targets/picorv32/rtl/${pqc_step1_file}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE pqc_step1_source
            COMMAND_ERROR_IS_FATAL ANY)
        file(
            CONFIGURE
            OUTPUT "${pqc_step1_dir}/${pqc_step1_file}"
            CONTENT "${pqc_step1_source}"
            @ONLY)
    endforeach()

    set(pqc_synthesis_result "${pqc_results}/baseline-synthesis.json")
    add_custom_command(
        OUTPUT "${pqc_synthesis_result}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
        COMMAND
            "${PQC_PYTHON}" "${pqc_synth_dir}/ecp5-50mhz.py" --yosys "${PQC_YOSYS}"
            --nextpnr "${PQC_NEXTPNR}" --ecppack "${PQC_ECPPACK}" --picorv32
            "${pqc_picorv32_source}" --pcpi "${pqc_step1_dir}/pqc_pcpi_mlkem.sv" --core
            "${pqc_step1_dir}/pqc_picorv32_core_top.sv" --script "${pqc_synth_dir}/core.ys"
            --work "${pqc_target_dir}/synthesis" --output "${pqc_synthesis_result}"
        DEPENDS
            "${pqc_picorv32_source}"
            "${pqc_step1_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_step1_dir}/pqc_picorv32_core_top.sv"
            "${pqc_synth_dir}/core.ys"
            "${pqc_synth_dir}/ecp5-50mhz.py"
        VERBATIM)
    add_custom_target(
        pqc-picorv32-synthesis
        DEPENDS "${pqc_synthesis_result}" "${pqc_manifest}")

    if(PQC_POLY_PICORV32_MLKEM)
        set(pqc_fqmul_synthesis_result "${pqc_results}/fqmul-synthesis.json")
        add_custom_command(
            OUTPUT "${pqc_fqmul_synthesis_result}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
            COMMAND
                "${PQC_PYTHON}" "${pqc_synth_dir}/ecp5-50mhz.py" --yosys "${PQC_YOSYS}"
                --nextpnr "${PQC_NEXTPNR}" --ecppack "${PQC_ECPPACK}" --picorv32
                "${pqc_picorv32_source}" --pcpi "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv" --core
                "${pqc_rtl_dir}/pqc_picorv32_core_top.sv" --script "${pqc_synth_dir}/core.ys"
                --work "${pqc_target_dir}/synthesis-fqmul" --output
                "${pqc_fqmul_synthesis_result}" --enable-fqmul
            DEPENDS
                "${pqc_picorv32_source}"
                "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
                "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
                "${pqc_synth_dir}/core.ys"
                "${pqc_synth_dir}/ecp5-50mhz.py"
            VERBATIM)
        add_custom_target(
            pqc-picorv32-fqmul-synthesis
            DEPENDS "${pqc_synthesis_result}" "${pqc_fqmul_synthesis_result}")
    endif()

    string(SUBSTRING "${pqc_repository_sha}" 0 8 pqc_repository_short)
    string(SUBSTRING "${PQC_POLY_PICORV32_SHA}" 0 8 pqc_picorv32_short)
    set(pqc_experiment_id "picorv32-step1-${pqc_repository_short}-${pqc_picorv32_short}")
    set(pqc_final_results "${PROJECT_SOURCE_DIR}/results/raw/${pqc_experiment_id}")
    add_custom_target(
        pqc-picorv32-finalize
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_final_results}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${pqc_manifest}"
                "${pqc_final_results}/baseline-manifest.json"
        COMMAND "${CMAKE_COMMAND}" -E copy "${pqc_results}/baseline-smoke.jsonl"
                "${pqc_final_results}/baseline-smoke.jsonl"
        COMMAND "${CMAKE_COMMAND}" -E copy "${pqc_stack_result}"
                "${pqc_final_results}/baseline-stack.json"
        COMMAND "${CMAKE_COMMAND}" -E copy "${pqc_size_result}"
                "${pqc_final_results}/baseline-code-size.json"
        COMMAND "${CMAKE_COMMAND}" -E copy "${pqc_synthesis_result}"
                "${pqc_final_results}/baseline-synthesis.json"
        COMMAND "${CMAKE_COMMAND}" -E copy "${baseline_dis}"
                "${pqc_final_results}/baseline.dis"
        COMMAND "${CMAKE_COMMAND}" -E copy "${pqc_target_dir}/baseline.map"
                "${pqc_final_results}/baseline.map"
        VERBATIM)
    add_dependencies(pqc-picorv32-finalize pqc-picorv32-sim pqc-picorv32-synthesis)
endif()

if(PQC_POLY_PICORV32_MLKEM AND PQC_POLY_PICORV32_SYNTHESIS AND
   PQC_POLY_PICORV32_FQMUL_VERIFY)
    set(
        PQC_POLY_STEP2_RESULTS_DIR
        "${PROJECT_SOURCE_DIR}/results/raw/picorv32-step2-3f13dce5-69d24e37"
        CACHE PATH "reviewed step 2 result directory")
    if(NOT IS_DIRECTORY "${PQC_POLY_STEP2_RESULTS_DIR}")
        message(FATAL_ERROR "reviewed Step 2 results are missing")
    endif()
    file(GLOB pqc_step2_records CONFIGURE_DEPENDS "${PQC_POLY_STEP2_RESULTS_DIR}/*")
    set(
        pqc_fqmul_final_results
        "${PROJECT_SOURCE_DIR}/results/raw/picorv32-step3-${pqc_repository_short}-${pqc_mlkem_native_short}")
    set(
        pqc_fqmul_result_files
        "${pqc_fqmul_final_results}/fqmul-formal.json"
        "${pqc_fqmul_final_results}/fqmul-disassembly.json"
        "${pqc_fqmul_final_results}/fqmul-candidates.json"
        "${pqc_fqmul_final_results}/fqmul-measurements.jsonl"
        "${pqc_fqmul_final_results}/fqmul-joint-winners.json"
        "${pqc_fqmul_final_results}/fqmul-staged-results.json"
        "${pqc_fqmul_final_results}/fqmul-synthesis.json"
        "${pqc_fqmul_final_results}/fqmul-final-comparison.json")
    set(pqc_software_winner_disassemblies)
    foreach(pqc_level IN ITEMS 512 768 1024)
        list(
            APPEND
            pqc_software_winner_disassemblies
            "${pqc_target_dir}/mlk${pqc_level}_ffuse2_ifuse2_rpair_bcachelate_xnone.dis")
    endforeach()
    add_custom_command(
        OUTPUT ${pqc_fqmul_result_files}
        COMMAND
            "$<TARGET_FILE:pqc-poly-mlkem>" --finalize-fqmul
            "${PROJECT_SOURCE_DIR}/examples/mlkem.json" "${PQC_POLY_STEP2_RESULTS_DIR}"
            "${pqc_mlkem_results}" "${pqc_results}" "${pqc_target_dir}"
            "${pqc_fqmul_formal_dir}" "${pqc_fqmul_final_results}"
        DEPENDS
            pqc-poly-mlkem
            ${pqc_fqmul_all_measurements}
            ${pqc_fqmul_formal_status}
            ${pqc_fqmul_cbmc_outputs}
            ${pqc_software_winner_disassemblies}
            ${pqc_step2_records}
            "${pqc_synthesis_result}"
            "${pqc_fqmul_synthesis_result}"
            "${PROJECT_SOURCE_DIR}/examples/mlkem.json"
        VERBATIM)
    add_custom_target(pqc-picorv32-fqmul-finalize DEPENDS ${pqc_fqmul_result_files})
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/red32.cmake")
