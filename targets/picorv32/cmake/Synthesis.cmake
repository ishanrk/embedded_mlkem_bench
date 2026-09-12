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

if(PQC_POLY_PICORV32_MLKEM)
    set(pqc_summary "${pqc_results}/summary.json")
    add_custom_command(
        OUTPUT "${pqc_summary}"
        COMMAND
            "${PQC_PYTHON}" "${PROJECT_SOURCE_DIR}/scripts/results.py" --input
            "${pqc_results}" --output "${pqc_summary}"
        DEPENDS
            ${pqc_mlkem_results}
            ${pqc_synthesis_results}
            "${PROJECT_SOURCE_DIR}/scripts/results.py"
        VERBATIM)
    add_custom_target(pqc-picorv32-results DEPENDS "${pqc_summary}")
endif()
