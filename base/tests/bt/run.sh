#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0
# Copyright (C) 2026 Henri David (hdavid), Optimist (github.com/w0ts/optimist); fm1-base's runner: Charles Vestal
# The Bluetooth stack's host tests (bt/: Optimist's own stack, docs/bt/BLE-STACK.md, BT-CLASSIC.md), built with ASan and
# UBSan where the compiler has them. Run from tests/run_tests.sh, or alone:  sh tests/bt/run.sh
set -u
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
OUT="${OUT:-template/build/tests/bt}"
FAIL=0
mkdir -p "$OUT"
echo 'int main(void) { return 0; }' > "$OUT/san_probe.c"
san_runs() {   # the sanitizers in $1 build and run (macOS 27's ASan runtime hangs before main: then UBSan alone)
    $CC $1 -o "$OUT/san_probe" "$OUT/san_probe.c" 2>/dev/null || return 1
    "$OUT/san_probe" & p=$!
    i=0
    while kill -0 $p 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
    kill -0 $p 2>/dev/null && { kill $p; wait $p 2>/dev/null; return 1; }
    wait $p
}
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
san_runs "$SAN" || SAN="-fsanitize=undefined -fno-sanitize-recover=all"
san_runs "$SAN" || SAN=""
T=tests/bt
t() {   # t NAME SOURCE [FLAGS...]: build SOURCE with FLAGS as NAME and run it
    name="$1"; src="$2"; shift 2
    if $CC -Wextra $SAN "$@" -o "$OUT/$name" "$T/$src" > "$OUT/$name.log" 2>&1 && "$OUT/$name" >> "$OUT/$name.log" 2>&1; then
        echo "  ok   bt/$name"
    else
        echo "  FAIL bt/$name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
t ble_prim ble_prim_test.c
t ble_time ble_time_test.c
t ble_stack ble_stack_test.c
# the app's stream (plat_ble_stream, device/bt_own_stream.h: a screen mirrored) merged with the MIDI queue
t ble_stack_stream ble_stack_test.c -DBLE_STREAM_TEST=1
t ble_stack_enc ble_stack_test.c -DBLE_LL_ENC=1
t ble_stack_smp ble_stack_test.c -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1
t ble_driver ble_driver_test.c -I$T/ble_fake
t ble_driver_pair ble_driver_test.c -I$T/ble_fake -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1 -DBLE_MIDI_NEED_ENC=1
t ble_driver_nodiag ble_driver_test.c -I$T/ble_fake -DBLE_DIAG=0
t ble_driver_pair_nodiag ble_driver_test.c -I$T/ble_fake -DBLE_DIAG=0 -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1 -DBLE_MIDI_NEED_ENC=1
t ble_stack_central ble_stack_test.c -DBLE_CENTRAL=1 -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1
t ble_driver_central ble_driver_test.c -I$T/ble_fake -DBLE_CENTRAL=1
t ble_scan_driver ble_scan_driver_test.c -I$T/ble_fake
t ble_scan_driver_nodiag ble_scan_driver_test.c -I$T/ble_fake -DBLE_DIAG=0
t ble_scan ble_scan_test.c
t ble_central ble_central_test.c
t ble_central_min ble_central_test.c -DBLE_CENTRAL=3
t ble_central_min_bond ble_central_test.c -DBLE_CENTRAL=3 -DFELUCCA_BLE_BOND=1
t ble_stack_central_min ble_stack_test.c -DBLE_CENTRAL=3
t ble_central_driver ble_central_driver_test.c -I$T/ble_fake
t ble_central_driver_nodiag ble_central_driver_test.c -I$T/ble_fake -DBLE_DIAG=0
t ble_midi ble_midi_test.c
t bt_coex bt_coex_test.c
t bt_inq bt_inq_test.c
t bt_crypto bt_crypto_test.c
t bt_ssp bt_ssp_test.c
t bt_lmp bt_lmp_test.c
t bt_afh bt_afh_test.c
t bt_l2cap bt_l2cap_test.c
t bt_sdps bt_sdps_test.c
t bt_avrcp bt_avrcp_test.c
t bt_avdtp bt_avdtp_test.c
t bt_sbc bt_sbc_test.c -O2 -w -I$T/ref/sbc_dec $T/ref/sbc_dec/*.c -lm
# FM-1 to FM-1: two whole stacks, units built apart
F2F=1
unit() {   # unit OBJ FLAGS...
    o="$1"; shift
    $CC -Wextra $SAN -c "$@" -o "$OUT/f2f_$o.o" $T/ble_f2f_unit.c > "$OUT/f2f_$o.log" 2>&1 || { F2F=0; tail -5 "$OUT/f2f_$o.log"; }
}
unit a -DF2F_U=a
unit b -DF2F_U=b
unit c -DF2F_U=c -DBLE_MIDI_NEED_ENC=1
unit a3 -DF2F_U=a -DBLE_CENTRAL=3
if [ $F2F = 1 ]; then
    t ble_f2f ble_f2f_test.c "$OUT/f2f_a.o" "$OUT/f2f_b.o" "$OUT/f2f_c.o"
    t ble_f2f_l3 ble_f2f_test.c -DF2F_A_LEVEL=3 "$OUT/f2f_a3.o" "$OUT/f2f_b.o" "$OUT/f2f_c.o"
else
    echo "  FAIL bt/ble_f2f (its units did not build)"; FAIL=1
fi
exit $FAIL
