include(FetchContent)

set(PQC_POLY_PICORV32_SHA "a473fc8fca393771d83b0ffcf0b14db3393339d8")
set(PQC_POLY_PICORV32_ARCHIVE_SHA256
    "050ba03d03eaacadb5953f3ba2218b49866c71d505c2476e49a0c0f5fe14e36c")

function(pqc_poly_fetch_picorv32)
    FetchContent_Declare(
        picorv32
        URL
            "https://codeload.github.com/YosysHQ/picorv32/tar.gz/${PQC_POLY_PICORV32_SHA}"
        URL_HASH "SHA256=${PQC_POLY_PICORV32_ARCHIVE_SHA256}"
        SOURCE_SUBDIR pqc_poly_no_subdir)
    FetchContent_MakeAvailable(picorv32)
    set(PQC_POLY_PICORV32_SOURCE_DIR
        "${picorv32_SOURCE_DIR}"
        PARENT_SCOPE)
endfunction()
