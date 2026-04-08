include(FetchContent)

set(PQC_POLY_PICORV32_SHA "a473fc8fca393771d83b0ffcf0b14db3393339d8")
set(PQC_POLY_PICORV32_ARCHIVE_SHA256
    "050ba03d03eaacadb5953f3ba2218b49866c71d505c2476e49a0c0f5fe14e36c")
set(PQC_POLY_MLKEM_NATIVE_SHA "69d24e37b8a04c6050ec55bc84a4228d7051bb4b")
set(PQC_POLY_MLKEM_NATIVE_ARCHIVE_SHA256
    "5f83af0a01fbed2c2d6cc370b56909f3b062728cff0ec9f310314707f13a1f3e")

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

function(pqc_poly_fetch_mlkem_native)
    FetchContent_Declare(
        mlkem_native
        URL
            "https://codeload.github.com/pq-code-package/mlkem-native/tar.gz/${PQC_POLY_MLKEM_NATIVE_SHA}"
        URL_HASH "SHA256=${PQC_POLY_MLKEM_NATIVE_ARCHIVE_SHA256}"
        SOURCE_SUBDIR pqc_poly_no_subdir)
    FetchContent_MakeAvailable(mlkem_native)
    set(PQC_POLY_MLKEM_NATIVE_SOURCE_DIR
        "${mlkem_native_SOURCE_DIR}"
        PARENT_SCOPE)
endfunction()
