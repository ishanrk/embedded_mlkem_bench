if(PQC_POLY_PICORV32_MLKEM)
    pqc_add_verilated(
        red32
        pqc_picorv32_sim_top
        Vpqc_picorv32_sim_top
        "${pqc_sim_cflags} -DPQC_RED32=1"
        -GENABLE_RED32=1
        "${pqc_picorv32_source}"
        "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
        "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
        "${pqc_rtl_dir}/pqc_picorv32_sim_top.sv")

    set(pqc_red32_pcpi_dir "${pqc_target_dir}/red32-pcpi-model")
    set(pqc_red32_pcpi_sim "${pqc_red32_pcpi_dir}/Vpqc_pcpi_mlkem")
    add_custom_command(
        OUTPUT "${pqc_red32_pcpi_sim}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_red32_pcpi_dir}"
        COMMAND
            "${PQC_VERILATOR}" --cc --exe --build -j 0 --Mdir "${pqc_red32_pcpi_dir}"
            --top-module pqc_pcpi_mlkem --prefix Vpqc_pcpi_mlkem --Wno-fatal
            -CFLAGS "-std=c++20 -O3" -GENABLE_RED32=1
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv" "${pqc_sim_dir}/red32_pcpi.cpp"
        DEPENDS
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_sim_dir}/red32_pcpi.cpp"
        VERBATIM)
    add_custom_target(
        pqc-picorv32-red32-pcpi
        COMMAND "${pqc_red32_pcpi_sim}"
        DEPENDS "${pqc_red32_pcpi_sim}"
        VERBATIM)

    set(pqc_red32_generated "${pqc_target_dir}/red32-generated")
    set(pqc_red32_results "${pqc_target_dir}/red32-results")
    set(pqc_red32_generated_files "${pqc_red32_generated}/red32-candidates.json")
    foreach(pqc_level IN ITEMS 512 768 1024)
        foreach(pqc_forward IN ITEMS stage fuse2)
            foreach(pqc_inverse IN ITEMS stage fuse2)
                foreach(pqc_reduction IN ITEMS each pair)
                    foreach(pqc_basemul IN ITEMS cachelate cacheeager directeager)
                        set(
                            pqc_plan_id
                            "mlk${pqc_level}_f${pqc_forward}_i${pqc_inverse}_r${pqc_reduction}_b${pqc_basemul}_xred32")
                        list(
                            APPEND
                            pqc_red32_generated_files
                            "${pqc_red32_generated}/backends/${pqc_plan_id}.c")
                    endforeach()
                endforeach()
            endforeach()
        endforeach()
    endforeach()

    add_custom_command(
        OUTPUT ${pqc_red32_generated_files}
        COMMAND
            "$<TARGET_FILE:pqc-poly-red32>" "${PROJECT_SOURCE_DIR}/examples/mlkem.json" -o
            "${pqc_red32_generated}"
        DEPENDS pqc-poly-red32 "${PROJECT_SOURCE_DIR}/examples/mlkem.json"
        VERBATIM)
    add_custom_target(pqc-picorv32-red32-generate DEPENDS ${pqc_red32_generated_files})

    set(pqc_red32_all_binaries)
    set(pqc_red32_all_measurements)
    foreach(pqc_level IN ITEMS 512 768 1024)
        set(pqc_level_red32)
        foreach(pqc_forward IN ITEMS stage fuse2)
            foreach(pqc_inverse IN ITEMS stage fuse2)
                foreach(pqc_reduction IN ITEMS each pair)
                    foreach(pqc_basemul IN ITEMS cachelate cacheeager directeager)
                        set(
                            pqc_plan_id
                            "mlk${pqc_level}_f${pqc_forward}_i${pqc_inverse}_r${pqc_reduction}_b${pqc_basemul}_xred32")
                        if(pqc_basemul STREQUAL directeager)
                            set(pqc_cache direct)
                        else()
                            set(pqc_cache cached)
                        endif()

                        set(pqc_source "${pqc_red32_generated}/backends/${pqc_plan_id}.c")
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
                                -DPQC_POLY_HAVE_MLK_RED32 -I
                                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem" -c "${pqc_source}" -o
                                "${pqc_object}"
                            DEPENDS
                                "${pqc_source}"
                                "${CMAKE_CURRENT_SOURCE_DIR}/mlkem/red32.h"
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

                        set(pqc_result "${pqc_red32_results}/${pqc_plan_id}-red32.jsonl")
                        set(pqc_stack_result "${pqc_red32_results}/${pqc_plan_id}-stack.json")
                        set(pqc_size_result "${pqc_red32_results}/${pqc_plan_id}-size.json")
                        add_custom_command(
                            OUTPUT "${pqc_result}" "${pqc_stack_result}" "${pqc_size_result}"
                            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_red32_results}"
                            COMMAND
                                "${red32_sim}" "+firmware=${pqc_hex}" --output "${pqc_result}"
                                --plan-id "${pqc_plan_id}" --level "${pqc_level}" --repeat-count 3
                                --stack-output "${pqc_stack_result}" --stack-usage
                                "${pqc_stack_usage}" --stack-root measured_complete --disassembly
                                "${pqc_dis}" --size-input "${pqc_size}" --size-output
                                "${pqc_size_result}"
                            DEPENDS
                                "${red32_sim}"
                                "${pqc_elf}"
                                "${pqc_hex}"
                                "${pqc_dis}"
                                "${pqc_size}"
                                "${pqc_stack_usage}"
                            VERBATIM)
                        list(APPEND pqc_red32_all_binaries "${pqc_elf}")
                        list(APPEND pqc_red32_all_measurements "${pqc_result}"
                             "${pqc_stack_result}" "${pqc_size_result}")
                        list(APPEND pqc_level_red32 "${pqc_result}")
                    endforeach()
                endforeach()
            endforeach()
        endforeach()

        set(pqc_level_output "${pqc_red32_results}/mlkem${pqc_level}-red32-measurements.jsonl")
        list(JOIN pqc_level_red32 " " pqc_level_inputs)
        add_custom_command(
            OUTPUT "${pqc_level_output}"
            COMMAND /bin/sh -c "cat ${pqc_level_inputs} > \"${pqc_level_output}\""
            DEPENDS ${pqc_level_red32}
            VERBATIM)
        list(APPEND pqc_red32_all_measurements "${pqc_level_output}")
    endforeach()

    add_custom_target(pqc-picorv32-red32-build DEPENDS ${pqc_red32_all_binaries})
    add_custom_target(pqc-picorv32-red32 DEPENDS ${pqc_red32_all_measurements})
    add_dependencies(pqc-picorv32-red32 pqc-picorv32-red32-pcpi)

    if(PQC_POLY_PICORV32_FQMUL_VERIFY)
        set(pqc_red32_formal_dir "${pqc_target_dir}/red32-formal")
        file(MAKE_DIRECTORY "${pqc_red32_formal_dir}")
        configure_file("${pqc_picorv32_source}" "${pqc_red32_formal_dir}/picorv32.v" COPYONLY)
        configure_file(
            "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
            "${pqc_red32_formal_dir}/pqc_pcpi_mlkem.sv"
            COPYONLY)
        configure_file(
            "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
            "${pqc_red32_formal_dir}/pqc_picorv32_core_top.sv"
            COPYONLY)
        foreach(pqc_formal_file IN ITEMS red32.sby red32_properties.sv rvfi_red32_monitor.sv)
            configure_file(
                "${CMAKE_CURRENT_SOURCE_DIR}/formal/${pqc_formal_file}"
                "${pqc_red32_formal_dir}/${pqc_formal_file}"
                COPYONLY)
        endforeach()
        set(
            pqc_red32_formal_status
            "${pqc_red32_formal_dir}/red32_pcpi/status"
            "${pqc_red32_formal_dir}/red32_noninterference/status"
            "${pqc_red32_formal_dir}/red32_rvfi/status"
            "${pqc_red32_formal_dir}/red32_rvfi_cover/status")
        add_custom_command(
            OUTPUT ${pqc_red32_formal_status}
            COMMAND "${PQC_SBY}" -f red32.sby pcpi
            COMMAND "${PQC_SBY}" -f red32.sby noninterference
            COMMAND "${PQC_SBY}" -f red32.sby rvfi
            COMMAND "${PQC_SBY}" -f red32.sby rvfi_cover
            DEPENDS
                "${pqc_picorv32_source}"
                "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
                "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
                "${CMAKE_CURRENT_SOURCE_DIR}/formal/red32.sby"
                "${CMAKE_CURRENT_SOURCE_DIR}/formal/red32_properties.sv"
                "${CMAKE_CURRENT_SOURCE_DIR}/formal/rvfi_red32_monitor.sv"
            WORKING_DIRECTORY "${pqc_red32_formal_dir}"
            VERBATIM)
        add_custom_target(pqc-picorv32-red32-verify DEPENDS ${pqc_red32_formal_status})
    endif()

    if(PQC_POLY_PICORV32_SYNTHESIS)
        set(pqc_red32_synthesis_result "${pqc_results}/red32-synthesis.json")
        add_custom_command(
            OUTPUT "${pqc_red32_synthesis_result}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
            COMMAND
                "${PQC_PYTHON}" "${pqc_synth_dir}/ecp5-50mhz.py" --yosys "${PQC_YOSYS}"
                --nextpnr "${PQC_NEXTPNR}" --ecppack "${PQC_ECPPACK}" --picorv32
                "${pqc_picorv32_source}" --pcpi "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv" --core
                "${pqc_rtl_dir}/pqc_picorv32_core_top.sv" --script "${pqc_synth_dir}/core.ys"
                --work "${pqc_target_dir}/synthesis-red32" --output
                "${pqc_red32_synthesis_result}" --enable-red32
            DEPENDS
                "${pqc_picorv32_source}"
                "${pqc_rtl_dir}/pqc_pcpi_mlkem.sv"
                "${pqc_rtl_dir}/pqc_picorv32_core_top.sv"
                "${pqc_synth_dir}/core.ys"
                "${pqc_synth_dir}/ecp5-50mhz.py"
            VERBATIM)
        add_custom_target(
            pqc-picorv32-red32-synthesis
            DEPENDS "${pqc_synthesis_result}" "${pqc_red32_synthesis_result}")
    endif()
endif()
