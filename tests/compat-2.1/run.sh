#!/bin/sh
# 2.1 <-> 2.2 storage compatibility on the host simulator. No hardware.
# Usage: tests/compat-2.1/run.sh HOST_2_1 HOST_2_2 OUTDIR
set -u
H21=$1 H22=$2 OUT=$3; D=$(cd "$(dirname "$0")" && pwd); mkdir -p "$OUT"; rc=0
step(){ host=$1 script=$2 flash=$3
 sed "s|OUT/|$OUT/|" "$D/$script" > "$OUT/$script"
 echo "=== $script"; if [ -n "$flash" ]; then FM1B_FLASH="$OUT/$flash" "$host" "$OUT/$script" "$OUT"; else "$host" "$OUT/$script" "$OUT"; fi
 r=$?; echo "rc=$r"; [ $r -eq 0 ] || rc=1; }
step "$H21" 1-write-2.1.txt ""
step "$H22" 2-update-2.2.txt a-2.1.bin
step "$H21" 3-return-2.1.txt b-2.2.bin
step "$H22" 4-again-2.2.txt c-2.1.bin
step "$H22" 5-backup-2.2.txt b-2.2.bin
step "$H21" 6-restore-2.1.txt ""
step "$H22" 7-after-restore-2.2.txt d-2.1.bin
echo "compat rc=$rc"; exit $rc
