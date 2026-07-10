#!/bin/sh
set -eu

r=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
b=${1:-build/fsri}
case "$b" in
    /*) ;;
    *) b="$r/$b" ;;
esac

d="$b/targets/picorv32/fsri-formal"
p="$b/_deps/picorv32-src/picorv32.v"
s=$(command -v sby)

test -f "$p"
rm -rf "$d"
mkdir -p "$d"
cp "$p" "$d/picorv32.v"
cp "$r/targets/picorv32/rtl/pqc_pcpi_mlkem.sv" "$d/pqc_pcpi_mlkem.sv"
cp "$r/targets/picorv32/rtl/pqc_picorv32_core_top.sv" "$d/pqc_picorv32_core_top.sv"
cp "$r/targets/picorv32/formal/fsri.sby" "$d/fsri.sby"
cp "$r/targets/picorv32/formal/fsri_properties.sv" "$d/fsri_properties.sv"
cp "$r/targets/picorv32/formal/rvfi_fsri_monitor.sv" "$d/rvfi_fsri_monitor.sv"
cd "$d"
"$s" -f fsri.sby pcpi
"$s" -f fsri.sby rvfi
