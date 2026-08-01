set(PQC_RISCV_RELEASE "2026.07.15")
set(PQC_CAD_RELEASE "2026-07-29")

set(pqc_target_dir "${CMAKE_CURRENT_BINARY_DIR}")
set(pqc_firmware_dir "${CMAKE_CURRENT_SOURCE_DIR}/firmware")
set(pqc_mlkem_dir "${CMAKE_CURRENT_SOURCE_DIR}/mlkem")
set(pqc_rtl_dir "${CMAKE_CURRENT_SOURCE_DIR}/rtl")
set(pqc_sim_dir "${CMAKE_CURRENT_SOURCE_DIR}/sim")
set(pqc_formal_dir "${CMAKE_CURRENT_SOURCE_DIR}/formal")
set(pqc_synth_dir "${CMAKE_CURRENT_SOURCE_DIR}/synth")
set(pqc_results "${pqc_target_dir}/results")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tools.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/FetchPinned.cmake")

if(PQC_POLY_PICORV32_MLKEM OR PQC_POLY_PICORV32_SYNTHESIS)
    pqc_poly_fetch_picorv32()
    set(pqc_picorv32_source "${PQC_POLY_PICORV32_SOURCE_DIR}/picorv32.v")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Simulation.cmake")

if(PQC_POLY_PICORV32_MLKEM)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MlkemBench.cmake")
endif()

if(PQC_POLY_PICORV32_FORMAL)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Formal.cmake")
endif()

if(PQC_POLY_PICORV32_SYNTHESIS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Synthesis.cmake")
endif()
