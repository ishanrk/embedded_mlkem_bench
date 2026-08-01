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
