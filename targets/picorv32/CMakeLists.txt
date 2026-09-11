set(PQC_RISCV_RELEASE "2026.07.15")
set(PQC_CAD_RELEASE "2026-07-29")

set(
    pqc_required_tools
    PQC_RISCV_GCC|riscv32-unknown-elf-gcc|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_RISCV_OBJCOPY|riscv32-unknown-elf-objcopy|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_RISCV_OBJDUMP|riscv32-unknown-elf-objdump|RISC-V_GNU_toolchain_${PQC_RISCV_RELEASE}
    PQC_VERILATOR|verilator|OSS_CAD_Suite_${PQC_CAD_RELEASE}
    PQC_NINJA|ninja|OSS_CAD_Suite_${PQC_CAD_RELEASE}
    PQC_PYTHON|python3|OSS_CAD_Suite_${PQC_CAD_RELEASE})

if(PQC_POLY_PICORV32_FORMAL)
    list(
        APPEND
        pqc_required_tools
        PQC_SBY|sby|OSS_CAD_Suite_${PQC_CAD_RELEASE}
        PQC_BOOLECTOR|boolector|OSS_CAD_Suite_${PQC_CAD_RELEASE})
endif()

if(PQC_POLY_PICORV32_SYNTHESIS)
    list(
        APPEND
        pqc_required_tools
        PQC_YOSYS|yosys|OSS_CAD_Suite_${PQC_CAD_RELEASE}
        PQC_NEXTPNR|nextpnr-ecp5|OSS_CAD_Suite_${PQC_CAD_RELEASE}
        PQC_ECPPACK|ecppack|OSS_CAD_Suite_${PQC_CAD_RELEASE})
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

include("${PROJECT_SOURCE_DIR}/cmake/FetchPinned.cmake")
pqc_poly_fetch_picorv32()

set(pqc_target_dir "${CMAKE_CURRENT_BINARY_DIR}")
set(pqc_firmware_dir "${CMAKE_CURRENT_SOURCE_DIR}/firmware")
set(pqc_mlkem_dir "${CMAKE_CURRENT_SOURCE_DIR}/mlkem")
set(pqc_rtl_dir "${CMAKE_CURRENT_SOURCE_DIR}/rtl")
set(pqc_sim_dir "${CMAKE_CURRENT_SOURCE_DIR}/sim")
set(pqc_formal_dir "${CMAKE_CURRENT_SOURCE_DIR}/formal")
set(pqc_synth_dir "${CMAKE_CURRENT_SOURCE_DIR}/synth")
set(pqc_picorv32_source "${PQC_POLY_PICORV32_SOURCE_DIR}/picorv32.v")
set(pqc_results "${pqc_target_dir}/results")

set(
    pqc_c_flags
    # every firmware uses the same bare metal RV32 compiler settings
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

# these two objects replace the operating system startup and C runtime
add_custom_command(
    OUTPUT "${pqc_target_dir}/crt0.o"
    COMMAND
        "${PQC_RISCV_GCC}" -march=rv32imc -mabi=ilp32 -ffreestanding -fno-pic
        -fno-pie -msmall-data-limit=0 -mstrict-align -mcmodel=medlow -Wall
        -Wextra -Werror -c "${pqc_firmware_dir}/crt0.S" -o
        "${pqc_target_dir}/crt0.o"
    DEPENDS "${pqc_firmware_dir}/crt0.S"
    VERBATIM)
add_custom_command(
    OUTPUT "${pqc_target_dir}/runtime.o"
    COMMAND
        "${PQC_RISCV_GCC}" ${pqc_c_flags} -I "${pqc_firmware_dir}" -c
        "${pqc_firmware_dir}/runtime.c" -o "${pqc_target_dir}/runtime.o"
    DEPENDS "${pqc_firmware_dir}/runtime.c" "${pqc_firmware_dir}/bench_mmio.h"
    VERBATIM)

# smoke firmware checks the shared CPU and multiplication path
set(pqc_smoke_object "${pqc_target_dir}/smoke.o")
set(pqc_smoke_elf "${pqc_target_dir}/smoke.elf")
set(pqc_smoke_hex "${pqc_target_dir}/smoke.hex")
set(pqc_smoke_dis "${pqc_target_dir}/smoke.dis")
add_custom_command(
    OUTPUT "${pqc_smoke_object}"
    COMMAND
        "${PQC_RISCV_GCC}" ${pqc_c_flags} -I "${pqc_firmware_dir}" -c
        "${pqc_firmware_dir}/smoke.c" -o "${pqc_smoke_object}"
    DEPENDS "${pqc_firmware_dir}/smoke.c" "${pqc_firmware_dir}/bench_mmio.h"
    VERBATIM)
add_custom_command(
    OUTPUT "${pqc_smoke_elf}" "${pqc_smoke_hex}" "${pqc_smoke_dis}"
    COMMAND
        "${PQC_RISCV_GCC}" "${pqc_target_dir}/crt0.o"
        "${pqc_target_dir}/runtime.o" "${pqc_smoke_object}" ${pqc_link_flags}
        "-Wl,-Map,${pqc_target_dir}/smoke.map" -o "${pqc_smoke_elf}"
    COMMAND
        "${PQC_RISCV_OBJCOPY}" -O verilog --verilog-data-width=4
        "${pqc_smoke_elf}" "${pqc_smoke_hex}"
    COMMAND
        /bin/sh -c
        "\"${PQC_RISCV_OBJDUMP}\" -d -S \"${pqc_smoke_elf}\" > \"${pqc_smoke_dis}\""
    DEPENDS
        "${pqc_target_dir}/crt0.o"
        "${pqc_target_dir}/runtime.o"
        "${pqc_smoke_object}"
        "${pqc_firmware_dir}/link.ld"
    VERBATIM)

function(pqc_add_pcpi_model variant index parameter)
    set(model_dir "${pqc_target_dir}/pcpi-${variant}")
    set(executable "${model_dir}/Vpqc_pcpi_mlkem")
    set(parameter_arg)
    if(NOT "${parameter}" STREQUAL "")
        set(parameter_arg "${parameter}")
    endif()
    add_custom_command(
        OUTPUT "${executable}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${model_dir}"
        COMMAND
            "${PQC_VERILATOR}" --cc --exe --build -j "${PQC_POLY_BUILD_JOBS}"
            --Mdir "${model_dir}" --top-module pqc_pcpi_mlkem --prefix
            Vpqc_pcpi_mlkem --Wno-fatal -CFLAGS
            "-std=c++20 -O3 -DPQC_TEST_VARIANT=${index}" ${parameter_arg}
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_sim_dir}/pcpi_test.cpp"
        DEPENDS
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_sim_dir}/pcpi_test.cpp"
        VERBATIM)
    set("pqc_pcpi_${variant}" "${executable}" PARENT_SCOPE)
endfunction()

function(pqc_add_cpu_model variant parameter)
    set(model_dir "${pqc_target_dir}/cpu-${variant}")
    set(executable "${model_dir}/Vpqc_picorv32_sim_top")
    set(parameter_arg)
    if(NOT "${parameter}" STREQUAL "")
        set(parameter_arg "${parameter}")
    endif()
    add_custom_command(
        OUTPUT "${executable}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${model_dir}"
        COMMAND
            "${PQC_VERILATOR}" --cc --exe --build -j "${PQC_POLY_BUILD_JOBS}"
            --Mdir "${model_dir}" --top-module pqc_picorv32_sim_top --prefix
            Vpqc_picorv32_sim_top --Wno-fatal -CFLAGS "-std=c++20 -O3"
            ${parameter_arg} "${pqc_picorv32_source}"
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
            "${pqc_rtl_dir}/pqc_picorv32_sim_top.sv"
            "${pqc_sim_dir}/firmware_sim.cpp"
        DEPENDS
            "${pqc_picorv32_source}"
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
            "${pqc_rtl_dir}/pqc_picorv32_sim_top.sv"
            "${pqc_sim_dir}/firmware_sim.cpp"
        VERBATIM)
    set("pqc_cpu_${variant}" "${executable}" PARENT_SCOPE)
endfunction()

# one model per hardware setting makes disabled decoder tests explicit
pqc_add_pcpi_model(baseline 0 "")
pqc_add_pcpi_model(fqmul 1 -GENABLE_FQMUL=1)
pqc_add_pcpi_model(red32 2 -GENABLE_RED32=1)
pqc_add_pcpi_model(fsri 3 -GENABLE_FSRI=1)
pqc_add_cpu_model(baseline "")
pqc_add_cpu_model(fqmul -GENABLE_FQMUL=1)
pqc_add_cpu_model(red32 -GENABLE_RED32=1)
pqc_add_cpu_model(fsri -GENABLE_FSRI=1)

set(pqc_sim_stamp "${pqc_results}/simulation.stamp")
add_custom_command(
    OUTPUT "${pqc_sim_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
    COMMAND "${pqc_pcpi_baseline}"
    COMMAND "${pqc_pcpi_fqmul}"
    COMMAND "${pqc_pcpi_red32}"
    COMMAND "${pqc_pcpi_fsri}"
    COMMAND "${pqc_cpu_baseline}" "+firmware=${pqc_smoke_hex}" --smoke
    COMMAND "${CMAKE_COMMAND}" -E touch "${pqc_sim_stamp}"
    DEPENDS
        "${pqc_pcpi_baseline}"
        "${pqc_pcpi_fqmul}"
        "${pqc_pcpi_red32}"
        "${pqc_pcpi_fsri}"
        "${pqc_cpu_baseline}"
        "${pqc_smoke_hex}"
    VERBATIM)
add_custom_target(pqc-picorv32-sim DEPENDS "${pqc_sim_stamp}")

if(PQC_POLY_PICORV32_MLKEM)
    pqc_poly_fetch_mlkem_native()
    set(pqc_mlkem_root "${PQC_POLY_MLKEM_NATIVE_SOURCE_DIR}/mlkem")
    set(pqc_mlkem_generated "${pqc_target_dir}/mlkem-fixed")
    file(MAKE_DIRECTORY "${pqc_mlkem_generated}")

    # the pinned source has no rotate hook so this copy changes only its macro line
    set(pqc_keccak_source "${pqc_mlkem_root}/src/fips202/keccakf1600.c")
    set(pqc_keccak_fsri "${pqc_mlkem_generated}/keccakf1600-fsri.c")
    file(READ "${pqc_keccak_source}" pqc_keccak_text)
    set(
        pqc_keccak_macro
        "#define MLK_KECCAK_ROL(a, offset) (((a) << (offset)) ^ ((a) >> (64 - (offset))))")
    string(REPLACE "${pqc_keccak_macro}" "#include \"fsri.h\""
           pqc_keccak_output "${pqc_keccak_text}")
    if(pqc_keccak_output STREQUAL pqc_keccak_text)
        message(FATAL_ERROR "cannot find the pinned Keccak rotate macro")
    endif()
    file(WRITE "${pqc_keccak_fsri}" "${pqc_keccak_output}")

    set(pqc_native_source "${pqc_mlkem_root}/mlkem_native.c")
    set(pqc_native_fsri "${pqc_mlkem_generated}/mlkem-native-fsri.c")
    file(READ "${pqc_native_source}" pqc_native_text)
    string(REPLACE "#include \"src/fips202/keccakf1600.c\""
           "#include \"keccakf1600-fsri.c\"" pqc_native_output
           "${pqc_native_text}")
    if(pqc_native_output STREQUAL pqc_native_text)
        message(FATAL_ERROR "cannot find the pinned Keccak source include")
    endif()
    file(WRITE "${pqc_native_fsri}" "${pqc_native_output}")

    set(
        pqc_mlkem_flags
        ${pqc_c_flags}
        -DMLK_CONFIG_NO_RANDOMIZED_API
        -DMLK_CONFIG_FILE=\"mlkem_config.h\")
    set(pqc_mlkem_results)

    foreach(pqc_level IN ITEMS 512 768 1024)
        if(pqc_level EQUAL 512)
            set(pqc_k 2)
        elseif(pqc_level EQUAL 768)
            set(pqc_k 3)
        else()
            set(pqc_k 4)
        endif()

        set(pqc_bench "${pqc_target_dir}/mlkem-${pqc_level}-bench.o")
        add_custom_command(
            OUTPUT "${pqc_bench}"
            COMMAND
                "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
                "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" -I
                "${pqc_firmware_dir}" -I "${pqc_mlkem_dir}" -I
                "${pqc_mlkem_root}" -c "${pqc_firmware_dir}/mlkem_bench.c" -o
                "${pqc_bench}"
            DEPENDS
                "${pqc_firmware_dir}/mlkem_bench.c"
                "${pqc_firmware_dir}/bench_mmio.h"
                "${pqc_mlkem_dir}/mlkem_config.h"
                "${pqc_mlkem_root}/mlkem_native.h"
            VERBATIM)

        set(pqc_native "${pqc_target_dir}/mlkem-${pqc_level}-native.o")
        add_custom_command(
            OUTPUT "${pqc_native}"
            COMMAND
                "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
                "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" -I
                "${pqc_mlkem_dir}" -I "${pqc_mlkem_root}" -c
                "${pqc_native_source}" -o "${pqc_native}"
            DEPENDS
                "${pqc_native_source}"
                "${pqc_mlkem_dir}/mlkem_config.h"
                "${pqc_mlkem_dir}/arith_backend.h"
            VERBATIM)

        set(pqc_native_fsri_object
            "${pqc_target_dir}/mlkem-${pqc_level}-native-fsri.o")
        add_custom_command(
            OUTPUT "${pqc_native_fsri_object}"
            COMMAND
                "${PQC_RISCV_GCC}" ${pqc_mlkem_flags} -DPQC_USE_FSRI=1
                "-DMLK_CONFIG_PARAMETER_SET=${pqc_level}" -I
                "${pqc_mlkem_generated}" -I "${pqc_mlkem_dir}" -I
                "${pqc_mlkem_root}" -I "${pqc_mlkem_root}/src/fips202" -c
                "${pqc_native_fsri}" -o "${pqc_native_fsri_object}"
            DEPENDS
                "${pqc_native_fsri}"
                "${pqc_keccak_fsri}"
                "${pqc_mlkem_dir}/fsri.h"
                "${pqc_mlkem_dir}/mlkem_config.h"
                "${pqc_mlkem_dir}/arith_backend.h"
            VERBATIM)

        foreach(pqc_variant IN ITEMS baseline fqmul red32 fsri)
            set(pqc_backend "${pqc_target_dir}/mlkem-${pqc_level}-${pqc_variant}-backend.o")
            set(pqc_definition)
            if(pqc_variant STREQUAL "fqmul")
                set(pqc_definition -DPQC_USE_FQMUL=1)
            elseif(pqc_variant STREQUAL "red32")
                set(pqc_definition -DPQC_USE_RED32=1)
            endif()
            add_custom_command(
                OUTPUT "${pqc_backend}"
                COMMAND
                    "${PQC_RISCV_GCC}" ${pqc_c_flags} "-DPQC_MLKEM_K=${pqc_k}"
                    ${pqc_definition} -I "${pqc_mlkem_dir}" -c
                    "${pqc_mlkem_dir}/fixed_backend.c" -o "${pqc_backend}"
                DEPENDS
                    "${pqc_mlkem_dir}/fixed_backend.c"
                    "${pqc_mlkem_dir}/fqmul.h"
                    "${pqc_mlkem_dir}/red32.h"
                VERBATIM)

            set(pqc_name "${pqc_variant}-${pqc_level}")
            set(pqc_elf "${pqc_target_dir}/${pqc_name}.elf")
            set(pqc_hex "${pqc_target_dir}/${pqc_name}.hex")
            set(pqc_dis "${pqc_target_dir}/${pqc_name}.dis")
            if(pqc_variant STREQUAL "fsri")
                set(pqc_variant_native "${pqc_native_fsri_object}")
            else()
                set(pqc_variant_native "${pqc_native}")
            endif()
            add_custom_command(
                OUTPUT "${pqc_elf}" "${pqc_hex}" "${pqc_dis}"
                COMMAND
                    "${PQC_RISCV_GCC}" "${pqc_target_dir}/crt0.o"
                    "${pqc_target_dir}/runtime.o" "${pqc_bench}"
                    "${pqc_backend}" "${pqc_variant_native}" ${pqc_link_flags}
                    "-Wl,-Map,${pqc_target_dir}/${pqc_name}.map" -o "${pqc_elf}"
                COMMAND
                    "${PQC_RISCV_OBJCOPY}" -O verilog --verilog-data-width=4
                    "${pqc_elf}" "${pqc_hex}"
                COMMAND
                    /bin/sh -c
                    "\"${PQC_RISCV_OBJDUMP}\" -d -S \"${pqc_elf}\" > \"${pqc_dis}\""
                DEPENDS
                    "${pqc_target_dir}/crt0.o"
                    "${pqc_target_dir}/runtime.o"
                    "${pqc_bench}"
                    "${pqc_backend}"
                    "${pqc_variant_native}"
                    "${pqc_firmware_dir}/link.ld"
                VERBATIM)

            set(pqc_result "${pqc_results}/${pqc_name}.json")
            add_custom_command(
                OUTPUT "${pqc_result}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
                COMMAND
                    "${pqc_cpu_${pqc_variant}}" "+firmware=${pqc_hex}" --output
                    "${pqc_result}" --disassembly "${pqc_dis}" --variant
                    "${pqc_variant}" --level "${pqc_level}" --inputs 30
                DEPENDS
                    "${pqc_cpu_${pqc_variant}}"
                    "${pqc_elf}"
                    "${pqc_hex}"
                    "${pqc_dis}"
                VERBATIM)
            list(APPEND "pqc_${pqc_variant}_results" "${pqc_result}")
            list(APPEND pqc_mlkem_results "${pqc_result}")
        endforeach()
    endforeach()

    add_custom_target(pqc-picorv32-baseline DEPENDS ${pqc_baseline_results})
    add_custom_target(pqc-picorv32-fqmul DEPENDS ${pqc_fqmul_results})
    add_custom_target(pqc-picorv32-red32 DEPENDS ${pqc_red32_results})
    add_custom_target(pqc-picorv32-fsri DEPENDS ${pqc_fsri_results})
    add_custom_target(pqc-picorv32-mlkem DEPENDS ${pqc_mlkem_results})
endif()

if(PQC_POLY_PICORV32_FORMAL)
    set(pqc_formal_status)
    foreach(pqc_instruction IN ITEMS fqmul red32 fsri)
        set(pqc_work "${pqc_target_dir}/formal-${pqc_instruction}")
        file(MAKE_DIRECTORY "${pqc_work}")
        configure_file("${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
                       "${pqc_work}/pqc_pcpi_mlkem.sv" COPYONLY)
        configure_file("${pqc_formal_dir}/${pqc_instruction}_properties.sv"
                       "${pqc_work}/${pqc_instruction}_properties.sv" COPYONLY)
        configure_file("${pqc_formal_dir}/${pqc_instruction}.sby"
                       "${pqc_work}/${pqc_instruction}.sby" COPYONLY)
        set(pqc_status "${pqc_work}/${pqc_instruction}/status")
        add_custom_command(
            OUTPUT "${pqc_status}"
            COMMAND "${PQC_SBY}" -f "${pqc_instruction}.sby"
            DEPENDS
                "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
                "${pqc_formal_dir}/${pqc_instruction}_properties.sv"
                "${pqc_formal_dir}/${pqc_instruction}.sby"
            WORKING_DIRECTORY "${pqc_work}"
            VERBATIM)
        list(APPEND pqc_formal_status "${pqc_status}")
    endforeach()
    add_custom_target(pqc-picorv32-formal DEPENDS ${pqc_formal_status})
endif()

if(PQC_POLY_PICORV32_SYNTHESIS)
    set(pqc_synthesis_results)
    foreach(pqc_variant IN ITEMS baseline fqmul red32 fsri)
        set(pqc_option)
        if(NOT pqc_variant STREQUAL "baseline")
            set(pqc_option "--enable-${pqc_variant}")
        endif()
        set(pqc_result "${pqc_results}/${pqc_variant}-synthesis.json")
        add_custom_command(
            OUTPUT "${pqc_result}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
            COMMAND
                "${PQC_PYTHON}" "${pqc_synth_dir}/ecp5-50mhz.py" --yosys
                "${PQC_YOSYS}" --nextpnr "${PQC_NEXTPNR}" --ecppack
                "${PQC_ECPPACK}" --picorv32 "${pqc_picorv32_source}" --pcpi
                "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv" --core
                "${pqc_rtl_dir}/pqc_picorv32_core_top.sv" --script
                "${pqc_synth_dir}/core.ys" --work
                "${pqc_target_dir}/synthesis-${pqc_variant}" --output
                "${pqc_result}" ${pqc_option}
            DEPENDS
                "${pqc_picorv32_source}"
                "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
                "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
                "${pqc_synth_dir}/core.ys"
                "${pqc_synth_dir}/ecp5-50mhz.py"
            VERBATIM)
        list(APPEND pqc_synthesis_results "${pqc_result}")
    endforeach()
    add_custom_target(pqc-picorv32-synthesis DEPENDS ${pqc_synthesis_results})
endif()
