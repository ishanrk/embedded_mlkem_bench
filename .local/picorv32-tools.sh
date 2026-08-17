#!/usr/bin/env sh

PQC_RISCV_TOOLCHAIN_ROOT=/home/ishan/.local/toolchains/riscv32-elf-2026.07.15
PQC_OSS_CAD_SUITE_ROOT=/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29

export PQC_RISCV_TOOLCHAIN_ROOT
export PQC_OSS_CAD_SUITE_ROOT
PATH="${PQC_RISCV_TOOLCHAIN_ROOT}/bin:${PQC_OSS_CAD_SUITE_ROOT}/bin:${PATH}"
export PATH
