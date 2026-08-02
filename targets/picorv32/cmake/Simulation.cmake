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
    add_custom_target("pqc-picorv32-pcpi-${variant}" DEPENDS "${executable}")
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
    add_custom_target("pqc-picorv32-cpu-${variant}" DEPENDS "${executable}")
    set("pqc_cpu_${variant}" "${executable}" PARENT_SCOPE)
endfunction()

# One model per hardware setting makes disabled decoder tests explicit.
pqc_add_pcpi_model(baseline 0 "")
pqc_add_pcpi_model(fqmul 1 -GENABLE_FQMUL=1)
pqc_add_pcpi_model(red32 2 -GENABLE_RED32=1)
pqc_add_pcpi_model(fsri 3 -GENABLE_FSRI=1)
pqc_add_pcpi_model(dot2x 4 -GENABLE_DOT2X=1)
pqc_add_cpu_model(baseline "")
pqc_add_cpu_model(fqmul -GENABLE_FQMUL=1)
pqc_add_cpu_model(red32 -GENABLE_RED32=1)
pqc_add_cpu_model(fsri -GENABLE_FSRI=1)
pqc_add_cpu_model(dot2x -GENABLE_DOT2X=1)

add_custom_target(
    pqc-picorv32-pcpi-models
    DEPENDS
        "${pqc_pcpi_baseline}"
        "${pqc_pcpi_fqmul}"
        "${pqc_pcpi_red32}"
        "${pqc_pcpi_fsri}"
        "${pqc_pcpi_dot2x}")
add_custom_target(
    pqc-picorv32-cpu-models
    DEPENDS
        "${pqc_cpu_baseline}"
        "${pqc_cpu_fqmul}"
        "${pqc_cpu_red32}"
        "${pqc_cpu_fsri}"
        "${pqc_cpu_dot2x}")

set(pqc_sim_stamp "${pqc_results}/simulation.stamp")
add_custom_command(
    OUTPUT "${pqc_sim_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${pqc_results}"
    COMMAND "${pqc_pcpi_baseline}"
    COMMAND "${pqc_pcpi_fqmul}"
    COMMAND "${pqc_pcpi_red32}"
    COMMAND "${pqc_pcpi_fsri}"
    COMMAND "${pqc_pcpi_dot2x}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${pqc_sim_stamp}"
    DEPENDS
        "${pqc_pcpi_baseline}"
        "${pqc_pcpi_fqmul}"
        "${pqc_pcpi_red32}"
        "${pqc_pcpi_fsri}"
        "${pqc_pcpi_dot2x}"
    VERBATIM)
add_custom_target(pqc-picorv32-sim DEPENDS "${pqc_sim_stamp}")
