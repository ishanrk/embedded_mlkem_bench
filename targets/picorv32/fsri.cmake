if(NOT PQC_POLY_PICORV32_MLKEM)
    return()
endif()

set(pqc_picorv32_dir "${PROJECT_SOURCE_DIR}/targets/picorv32")
foreach(
    pqc_name
    IN ITEMS
        pqc_target_dir
        pqc_firmware_dir
        pqc_rtl_dir
        pqc_sim_dir
        pqc_synth_dir
        pqc_picorv32_source
        pqc_c_flags
        pqc_link_flags
        pqc_sim_cflags
        pqc_mlkem_root
        pqc_mlkem_generated
        pqc_mlkem_flags
        pqc_results)
    get_directory_property(
        ${pqc_name}
        DIRECTORY "${pqc_picorv32_dir}"
        DEFINITION ${pqc_name})
endforeach()

set(pqc_fsri_generated "${pqc_target_dir}/fsri-generated")
set(pqc_fsri_results "${pqc_target_dir}/fsri-results")
file(MAKE_DIRECTORY "${pqc_fsri_generated}")

set(pqc_fsri_keccak_source "${pqc_mlkem_root}/src/fips202/keccakf1600.c")
set(pqc_fsri_keccak "${pqc_fsri_generated}/keccakf1600-fsri.c")
file(READ "${pqc_fsri_keccak_source}" pqc_fsri_keccak_text)
set(
    pqc_fsri_keccak_macro
    "#define MLK_KECCAK_ROL(a, offset) (((a) << (offset)) ^ ((a) >> (64 - (offset))))")
string(
    REPLACE "${pqc_fsri_keccak_macro}" "#include \"fsri.h\""
            pqc_fsri_keccak_output "${pqc_fsri_keccak_text}")
if(pqc_fsri_keccak_output STREQUAL pqc_fsri_keccak_text)
    message(FATAL_ERROR "cannot replace pinned keccak rotate macro")
endif()
file(WRITE "${pqc_fsri_keccak}" "${pqc_fsri_keccak_output}")

set(pqc_fsri_scu_source "${pqc_mlkem_root}/mlkem_native.c")
set(pqc_fsri_scu "${pqc_fsri_generated}/mlkem-native-fsri.c")
file(READ "${pqc_fsri_scu_source}" pqc_fsri_scu_text)
string(
    REPLACE "#include \"src/fips202/keccakf1600.c\""
            "#include \"keccakf1600-fsri.c\"" pqc_fsri_scu_output
            "${pqc_fsri_scu_text}")
if(pqc_fsri_scu_output STREQUAL pqc_fsri_scu_text)
    message(FATAL_ERROR "cannot replace pinned keccak source")
endif()
file(WRITE "${pqc_fsri_scu}" "${pqc_fsri_scu_output}")

pqc_add_verilated(
    fsri
    pqc_picorv32_sim_top
    Vpqc_picorv32_sim_top
    "${pqc_sim_cflags} -DPQC_FSRI=1"
    -GENABLE_FSRI=1
    "${pqc_picorv32_source}"
    "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
    "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
    "${pqc_rtl_dir}/pqc_picorv32_sim_top.sv")

set(pqc_fsri_pcpi_dir "${pqc_target_dir}/fsri-pcpi-model")
set(pqc_fsri_pcpi_sim "${pqc_fsri_pcpi_dir}/Vpqc_pcpi_mlkem")
add_custom_command(
    OUTPUT "${pqc_fsri_pcpi_sim}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_fsri_pcpi_dir}"
    COMMAND
        "${PQC_VERILATOR}" --cc --exe --build -j 0 --Mdir "${pqc_fsri_pcpi_dir}"
        --top-module pqc_pcpi_mlkem --prefix Vpqc_pcpi_mlkem --Wno-fatal
        -CFLAGS "-std=c++20 -O3" -GENABLE_FSRI=1
        "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv" "${pqc_sim_dir}/fsri_pcpi.cpp"
    DEPENDS
        "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
        "${pqc_sim_dir}/fsri_pcpi.cpp"
    VERBATIM)
add_custom_target(
    pqc-picorv32-fsri-pcpi
    COMMAND "${pqc_fsri_pcpi_sim}"
    DEPENDS "${pqc_fsri_pcpi_sim}"
    VERBATIM)

set(pqc_fsri_crt0 "${pqc_target_dir}/fsri-crt0.o")
set(pqc_fsri_runtime "${pqc_target_dir}/fsri-runtime.o")
add_custom_command(
    OUTPUT "${pqc_fsri_crt0}"
    COMMAND
        "${PQC_RISCV_GCC}" -march=rv32imc -mabi=ilp32 -ffreestanding -fno-pic -fno-pie
        -msmall-data-limit=0 -mstrict-align -mcmodel=medlow -Wall -Wextra -Werror -c
        "${pqc_firmware_dir}/crt0.S" -o "${pqc_fsri_crt0}"
    DEPENDS "${pqc_firmware_dir}/crt0.S"
    VERBATIM)
add_custom_command(
    OUTPUT "${pqc_fsri_runtime}"
    BYPRODUCTS "${pqc_target_dir}/fsri-runtime.su"
    COMMAND
        "${PQC_RISCV_GCC}" ${pqc_c_flags} -I "${pqc_firmware_dir}" -c
        "${pqc_firmware_dir}/runtime.c" -o "${pqc_fsri_runtime}"
    DEPENDS "${pqc_firmware_dir}/runtime.c" "${pqc_firmware_dir}/bench_mmio.h"
    VERBATIM)

set(pqc_fsri_binaries)
set(pqc_fsri_measurements)
foreach(pqc_level IN ITEMS 512 768 1024)
    if(pqc_level EQUAL 512)
        set(pqc_k 2)
    elseif(pqc_level EQUAL 768)
        set(pqc_k 3)
    else()
        set(pqc_k 4)
    endif()
    math(EXPR pqc_scratch "${pqc_k} * 256")

    set(pqc_fsri_upstream "${pqc_target_dir}/mlkem-native-${pqc_level}-fsri.o")
    set(pqc_fsri_upstream_su "${pqc_target_dir}/mlkem-native-${pqc_level}-fsri.su")
    add_custom_command(
        OUTPUT "${pqc_fsri_upstream}"
        BYPRODUCTS "${pqc_fsri_upstream_su}"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
            "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" -I "${pqc_firmware_dir}" -I
            "${pqc_picorv32_dir}/mlkem" -I "${pqc_mlkem_root}" -I
            "${pqc_mlkem_root}/src/fips202" -c "${pqc_fsri_scu}" -o
            "${pqc_fsri_upstream}"
        DEPENDS
            "${pqc_fsri_scu}"
            "${pqc_fsri_keccak}"
            "${pqc_picorv32_dir}/mlkem/fsri.h"
            "${pqc_picorv32_dir}/mlkem/mlkem_config.h"
            "${pqc_picorv32_dir}/mlkem/arith_backend.h"
        VERBATIM)

    set(pqc_fsri_bench "${pqc_target_dir}/mlkem-bench-${pqc_level}-fsri.o")
    set(pqc_fsri_bench_su "${pqc_target_dir}/mlkem-bench-${pqc_level}-fsri.su")
    add_custom_command(
        OUTPUT "${pqc_fsri_bench}"
        BYPRODUCTS "${pqc_fsri_bench_su}"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
            "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" "-DPQC_MLKEM_K=${pqc_k}"
            "-DPQC_MLKEM_SCRATCH_BYTES=${pqc_scratch}" -I "${pqc_firmware_dir}" -I
            "${pqc_picorv32_dir}/mlkem" -I "${pqc_mlkem_root}" -c
            "${pqc_firmware_dir}/mlkem_bench.c" -o "${pqc_fsri_bench}"
        DEPENDS
            "${pqc_firmware_dir}/mlkem_bench.c"
            "${pqc_firmware_dir}/bench_mmio.h"
            "${pqc_picorv32_dir}/mlkem/mlkem_config.h"
            "${pqc_mlkem_root}/mlkem_native.h"
        VERBATIM)

    set(pqc_source_plan "mlk${pqc_level}_ffuse2_ifuse2_rpair_bcachelate_xnone")
    set(pqc_source "${pqc_mlkem_generated}/backends/${pqc_source_plan}.c")
    set(pqc_fsri_backend "${pqc_target_dir}/${pqc_source_plan}-fsri.o")
    set(pqc_fsri_backend_su "${pqc_target_dir}/${pqc_source_plan}-fsri.su")
    add_custom_command(
        OUTPUT "${pqc_fsri_backend}"
        BYPRODUCTS "${pqc_fsri_backend_su}"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_c_flags} -c "${pqc_source}" -o
            "${pqc_fsri_backend}"
        DEPENDS "${pqc_source}"
        VERBATIM)

    set(pqc_plan_id "mlk${pqc_level}_ffuse2_ifuse2_rpair_bcachelate_xfsri")
    set(pqc_elf "${pqc_target_dir}/${pqc_plan_id}.elf")
    set(pqc_hex "${pqc_target_dir}/${pqc_plan_id}.hex")
    set(pqc_dis "${pqc_target_dir}/${pqc_plan_id}.dis")
    set(pqc_size "${pqc_target_dir}/${pqc_plan_id}.size")
    set(pqc_stack_usage "${pqc_target_dir}/${pqc_plan_id}.stack-usage.su")
    add_custom_command(
        OUTPUT "${pqc_elf}" "${pqc_hex}" "${pqc_dis}" "${pqc_size}"
               "${pqc_stack_usage}"
        COMMAND
            "${PQC_RISCV_GCC}" "${pqc_fsri_crt0}" "${pqc_fsri_runtime}"
            "${pqc_fsri_bench}" "${pqc_fsri_backend}" "${pqc_fsri_upstream}"
            ${pqc_link_flags} "-Wl,-Map,${pqc_target_dir}/${pqc_plan_id}.map" -o
            "${pqc_elf}"
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
            "cat \"${pqc_target_dir}/fsri-runtime.su\" \"${pqc_fsri_upstream_su}\" \"${pqc_fsri_bench_su}\" \"${pqc_fsri_backend_su}\" > \"${pqc_stack_usage}\""
        DEPENDS
            "${pqc_fsri_crt0}"
            "${pqc_fsri_runtime}"
            "${pqc_fsri_bench}"
            "${pqc_fsri_backend}"
            "${pqc_fsri_upstream}"
            "${pqc_firmware_dir}/link.ld"
        VERBATIM)

    set(pqc_result "${pqc_fsri_results}/mlkem${pqc_level}-fsri-measurements.jsonl")
    set(pqc_stack_result "${pqc_fsri_results}/mlkem${pqc_level}-fsri-stack.json")
    set(pqc_size_result "${pqc_fsri_results}/mlkem${pqc_level}-fsri-size.json")
    add_custom_command(
        OUTPUT "${pqc_result}" "${pqc_stack_result}" "${pqc_size_result}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_fsri_results}"
        COMMAND
            "${fsri_sim}" "+firmware=${pqc_hex}" --output "${pqc_result}" --plan-id
            "${pqc_plan_id}" --level "${pqc_level}" --repeat-count 3 --stack-output
            "${pqc_stack_result}" --stack-usage "${pqc_stack_usage}" --stack-root
            measured_complete --disassembly "${pqc_dis}" --size-input "${pqc_size}"
            --size-output "${pqc_size_result}"
        DEPENDS
            "${fsri_sim}"
            "${pqc_elf}"
            "${pqc_hex}"
            "${pqc_dis}"
            "${pqc_size}"
            "${pqc_stack_usage}"
        VERBATIM)

    list(APPEND pqc_fsri_binaries "${pqc_elf}")
    list(APPEND pqc_fsri_measurements "${pqc_result}" "${pqc_stack_result}"
         "${pqc_size_result}")
endforeach()

add_custom_target(pqc-picorv32-fsri-build DEPENDS ${pqc_fsri_binaries})
add_dependencies(pqc-picorv32-fsri-build pqc-picorv32-mlkem-generate)
add_custom_target(pqc-picorv32-fsri DEPENDS ${pqc_fsri_measurements})
add_dependencies(
    pqc-picorv32-fsri
    pqc-picorv32-fsri-pcpi
    pqc-picorv32-mlkem-generate)

if(PQC_POLY_PICORV32_SYNTHESIS)
    set(pqc_fsri_synthesis "${pqc_results}/fsri-synthesis.json")
    add_custom_command(
        OUTPUT "${pqc_fsri_synthesis}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
        COMMAND
            "${PQC_PYTHON}" "${pqc_synth_dir}/ecp5-50mhz.py" --yosys "${PQC_YOSYS}"
            --nextpnr "${PQC_NEXTPNR}" --ecppack "${PQC_ECPPACK}" --picorv32
            "${pqc_picorv32_source}" --pcpi "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv" --core
            "${pqc_rtl_dir}/pqc_picorv32_core_top.sv" --script "${pqc_synth_dir}/core.ys"
            --work "${pqc_target_dir}/synthesis-fsri" --output "${pqc_fsri_synthesis}"
            --enable-fsri
        DEPENDS
            "${pqc_picorv32_source}"
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
            "${pqc_synth_dir}/core.ys"
            "${pqc_synth_dir}/ecp5-50mhz.py"
        VERBATIM)
    add_custom_target(pqc-picorv32-fsri-synthesis DEPENDS "${pqc_fsri_synthesis}")
    add_dependencies(pqc-picorv32-fsri-synthesis pqc-picorv32-synthesis)
endif()
