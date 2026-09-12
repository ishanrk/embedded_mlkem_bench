set(
    pqc_c_flags
    # Every firmware uses the same bare-metal RV32 compiler settings.
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
set(
    pqc_mlkem_flags
    ${pqc_c_flags}
    -DMLK_CONFIG_NO_RANDOMIZED_API
    -DMLK_CONFIG_FILE=\"mlkem_config.h\")

function(build_bare_metal_runtime)
    # These two objects replace the operating system startup and C runtime.
    add_custom_command(
        OUTPUT "${pqc_target_dir}/crt0.o"
        COMMAND
            "${PQC_RISCV_GCC}" -march=rv32imc -mabi=ilp32 -ffreestanding
            -fno-pic -fno-pie -msmall-data-limit=0 -mstrict-align
            -mcmodel=medlow -Wall -Wextra -Werror -c
            "${pqc_firmware_dir}/crt0.S" -o "${pqc_target_dir}/crt0.o"
        DEPENDS "${pqc_firmware_dir}/crt0.S"
        VERBATIM)
    add_custom_command(
        OUTPUT "${pqc_target_dir}/runtime.o"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_c_flags} -I "${pqc_firmware_dir}" -c
            "${pqc_firmware_dir}/runtime.c" -o "${pqc_target_dir}/runtime.o"
        DEPENDS
            "${pqc_firmware_dir}/runtime.c"
            "${pqc_firmware_dir}/bench_mmio.h"
        VERBATIM)
endfunction()

function(prepare_fsri_keccak)
    # The pinned source has no rotate hook, so change only its macro line.
    file(MAKE_DIRECTORY "${pqc_mlkem_generated}")
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

    file(READ "${pqc_native_source}" pqc_native_text)
    string(REPLACE "#include \"src/fips202/keccakf1600.c\""
           "#include \"keccakf1600-fsri.c\"" pqc_native_output
           "${pqc_native_text}")
    if(pqc_native_output STREQUAL pqc_native_text)
        message(FATAL_ERROR "cannot find the pinned Keccak source include")
    endif()
    file(WRITE "${pqc_native_fsri}" "${pqc_native_output}")
endfunction()

function(build_mlkem_common level)
    set(bench "${pqc_target_dir}/mlkem-${level}-bench.o")
    add_custom_command(
        OUTPUT "${bench}"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
            "-DMLK_CONFIG_PARAMETER_SET=${level}" -I "${pqc_firmware_dir}"
            -I "${pqc_mlkem_dir}" -I "${pqc_mlkem_root}" -c
            "${pqc_firmware_dir}/mlkem_bench.c" -o "${bench}"
        DEPENDS
            "${pqc_firmware_dir}/mlkem_bench.c"
            "${pqc_firmware_dir}/bench_mmio.h"
            "${pqc_mlkem_dir}/mlkem_config.h"
            "${pqc_mlkem_root}/mlkem_native.h"
        VERBATIM)

    set(native "${pqc_target_dir}/mlkem-${level}-native.o")
    add_custom_command(
        OUTPUT "${native}"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_mlkem_flags}
            "-DMLK_CONFIG_PARAMETER_SET=${level}" -I "${pqc_mlkem_dir}" -I
            "${pqc_mlkem_root}" -c "${pqc_native_source}" -o "${native}"
        DEPENDS
            "${pqc_native_source}"
            "${pqc_mlkem_dir}/mlkem_config.h"
            "${pqc_mlkem_dir}/arith_backend.h"
        VERBATIM)

    set(native_fsri "${pqc_target_dir}/mlkem-${level}-native-fsri.o")
    add_custom_command(
        OUTPUT "${native_fsri}"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_mlkem_flags} -DPQC_USE_FSRI=1
            "-DMLK_CONFIG_PARAMETER_SET=${level}" -I "${pqc_mlkem_generated}"
            -I "${pqc_mlkem_dir}" -I "${pqc_mlkem_root}" -I
            "${pqc_mlkem_root}/src/fips202" -c "${pqc_native_fsri}" -o
            "${native_fsri}"
        DEPENDS
            "${pqc_native_fsri}"
            "${pqc_keccak_fsri}"
            "${pqc_mlkem_dir}/fsri.h"
            "${pqc_mlkem_dir}/mlkem_config.h"
            "${pqc_mlkem_dir}/arith_backend.h"
        VERBATIM)

    set(pqc_mlkem_bench "${bench}" PARENT_SCOPE)
    set(pqc_mlkem_native "${native}" PARENT_SCOPE)
    set(pqc_mlkem_native_fsri "${native_fsri}" PARENT_SCOPE)
endfunction()

function(build_mlkem_variant level k variant)
    set(backend "${pqc_target_dir}/mlkem-${level}-${variant}-backend.o")
    set(definition)
    if(variant STREQUAL "fqmul")
        set(definition -DPQC_USE_FQMUL=1)
    elseif(variant STREQUAL "red32")
        set(definition -DPQC_USE_RED32=1)
    endif()
    add_custom_command(
        OUTPUT "${backend}"
        COMMAND
            "${PQC_RISCV_GCC}" ${pqc_c_flags} "-DPQC_MLKEM_K=${k}"
            ${definition} -I "${pqc_mlkem_dir}" -c
            "${pqc_mlkem_dir}/fixed_backend.c" -o "${backend}"
        DEPENDS
            "${pqc_mlkem_dir}/fixed_backend.c"
            "${pqc_mlkem_dir}/fqmul.h"
            "${pqc_mlkem_dir}/red32.h"
        VERBATIM)

    if(variant STREQUAL "fsri")
        set(native "${pqc_mlkem_native_fsri}")
    else()
        set(native "${pqc_mlkem_native}")
    endif()

    set(name "${variant}-${level}")
    set(elf "${pqc_target_dir}/${name}.elf")
    set(hex "${pqc_target_dir}/${name}.hex")
    set(dis "${pqc_target_dir}/${name}.dis")
    add_custom_command(
        OUTPUT "${elf}" "${hex}" "${dis}"
        COMMAND
            "${PQC_RISCV_GCC}" "${pqc_target_dir}/crt0.o"
            "${pqc_target_dir}/runtime.o" "${pqc_mlkem_bench}" "${backend}"
            "${native}" ${pqc_link_flags}
            "-Wl,-Map,${pqc_target_dir}/${name}.map" -o "${elf}"
        COMMAND
            "${PQC_RISCV_OBJCOPY}" -O verilog --verilog-data-width=4
            "${elf}" "${hex}"
        COMMAND
            /bin/sh -c
            "\"${PQC_RISCV_OBJDUMP}\" -d -S \"${elf}\" > \"${dis}\""
        DEPENDS
            "${pqc_target_dir}/crt0.o"
            "${pqc_target_dir}/runtime.o"
            "${pqc_mlkem_bench}"
            "${backend}"
            "${native}"
            "${pqc_firmware_dir}/link.ld"
        VERBATIM)

    add_custom_target(
        "pqc-picorv32-firmware-${variant}-${level}"
        DEPENDS "${elf}" "${hex}" "${dis}")
    set(pqc_variant_outputs "${elf}" "${hex}" "${dis}" PARENT_SCOPE)
endfunction()

build_bare_metal_runtime()

set(pqc_mlkem_generated "${pqc_target_dir}/mlkem-fixed")
set(pqc_keccak_source "${pqc_mlkem_root}/src/fips202/keccakf1600.c")
set(pqc_keccak_fsri "${pqc_mlkem_generated}/keccakf1600-fsri.c")
set(pqc_native_source "${pqc_mlkem_root}/mlkem_native.c")
set(pqc_native_fsri "${pqc_mlkem_generated}/mlkem-native-fsri.c")
prepare_fsri_keccak()

set(pqc_baseline_firmware_outputs)
set(pqc_fqmul_firmware_outputs)
set(pqc_red32_firmware_outputs)
set(pqc_fsri_firmware_outputs)
set(pqc_firmware_outputs)

foreach(pqc_level IN ITEMS 512 768 1024)
    if(pqc_level EQUAL 512)
        set(pqc_k 2)
    elseif(pqc_level EQUAL 768)
        set(pqc_k 3)
    else()
        set(pqc_k 4)
    endif()

    build_mlkem_common("${pqc_level}")
    foreach(pqc_variant IN ITEMS baseline fqmul red32 fsri)
        build_mlkem_variant("${pqc_level}" "${pqc_k}" "${pqc_variant}")
        list(
            APPEND "pqc_${pqc_variant}_firmware_outputs"
            ${pqc_variant_outputs})
        list(APPEND pqc_firmware_outputs ${pqc_variant_outputs})
    endforeach()
endforeach()

add_custom_target(
    pqc-picorv32-firmware-baseline DEPENDS ${pqc_baseline_firmware_outputs})
add_custom_target(
    pqc-picorv32-firmware-fqmul DEPENDS ${pqc_fqmul_firmware_outputs})
add_custom_target(
    pqc-picorv32-firmware-red32 DEPENDS ${pqc_red32_firmware_outputs})
add_custom_target(
    pqc-picorv32-firmware-fsri DEPENDS ${pqc_fsri_firmware_outputs})
add_custom_target(pqc-picorv32-firmware DEPENDS ${pqc_firmware_outputs})
