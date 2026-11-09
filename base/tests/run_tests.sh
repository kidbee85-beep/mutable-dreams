#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# fm1-base's tests: the platform's pieces on the host (encoder, TRS jack, storage, fastmath, the screen reader's
# announce.c, backup and restore, the screen mirror), the tools and web pages for them, the installer against a
# simulated FM-1, the update path against the template's package (tools/build.py template first), and the template in
# the simulator (tests/basic.txt).
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files (tools/fm1pkg_make.py)
OUT=template/build/tests
FAIL=0
mkdir -p "$OUT"
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run encoder sh -c "$CC -O2 -w -Ibsp/chip -Ibsp/boards/fm1 -o $OUT/encoder_test tests/encoder_test.c && $OUT/encoder_test"
run trs sh -c "$CC -O2 -w -Ibsp/chip -Ibsp/boards/fm1 -Idevice -o $OUT/trs_test tests/trs_test.c && $OUT/trs_test"
# USB audio around a render block (device/uac_mix.h) on felucca-bsp's usb.c: the recording without the computer's audio,
# the computer's at plat_usb_play_gain and MASTER, muted still read; and without usb_audio_out
run uac-mix sh -c "$CC -std=c99 -O2 -w -Ibsp/chip -Ibsp/boards/fm1 -o $OUT/uac_mix_test tests/uac_mix_test.c && \
    $OUT/uac_mix_test && $CC -std=c99 -O2 -w -Ibsp/chip -Ibsp/boards/fm1 -DTEST_NO_OUT -o $OUT/uac_mix_test_noout \
    tests/uac_mix_test.c && $OUT/uac_mix_test_noout"
# plat_lock / plat_unlock over a simulated interrupt enable: inside an interrupt they leave interrupts off
run lock sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Idevice -o $OUT/lock_test tests/lock_test.c && $OUT/lock_test"
# shim_irq_save / shim_irq_restore (the radio's) over a simulated interrupt enable: inside an interrupt they leave
# interrupts off
run shim_lock sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Idevice -o $OUT/shim_lock_test tests/shim_lock_test.c && $OUT/shim_lock_test"
# the RAM layouts as tools/build.py reads them: device/app.ld's fixed regions (the default), app_packed.ld's one
# region (ram_layout = "packed"), RAM code first, then data, bss, the pool
run layout "$PY" tests/layout_test.py
# the cache RAM (felucca-bsp's chip/fm1_cache_ram.h, fm1.toml cache_ram_ways) on a model of the cache's registers, tags and
# ways: 2, 4 and 7 ways set, tested and zeroed; 0 and 8 nothing; a cache that never idles, masks not taken, a stuck bit
# in each way: all put back, 0 bytes
run cache-ram sh -c "$CC -std=c11 -O1 -Wall -Wextra -Werror -Ibsp/chip -o $OUT/cache_ram_test bsp/tests/cache_ram_test.c && $OUT/cache_ram_test"
# the static stack check (tools/stack_depth.py, stack_ir.py): frames, the interrupts nested, indirect calls on the IR
run stack_depth "$PY" tests/stack_depth_test.py
# plat_console_* (device/console.c) over the CDC rings, the interrupt's side simulated; and without the console
run console sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Idevice -o $OUT/console_test tests/console_test.c && \
    $OUT/console_test && $CC -std=c99 -Wall -Wextra -Werror -O2 -Idevice -DFM1B_CDC=0 -o $OUT/console_test0 \
    tests/console_test.c && $OUT/console_test0"
run trs-stamp sh -c "$CC -O2 -w -DFELUCCA_MIDI_STAMP=1 -Ibsp/chip -Ibsp/boards/fm1 -Idevice -o $OUT/trs_test_stamp tests/trs_test.c && $OUT/trs_test_stamp"
# the MIDI-in stamps (FELUCCA_MIDI_STAMP): USB and the jack against a model of their registers on the TIMER5 schedule,
# each stamp against its message's landing (within +-0.05 ms; the poll's and the read's stamps beside it)
run midi-stamp sh -c "$CC -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function -Ibsp/chip -Ibsp/boards/fm1 -Idevice -o $OUT/midi_stamp_test tests/midi_stamp_test.c && $OUT/midi_stamp_test"
run storage sh -c "$CC -O2 -w -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
run storage-pool sh -c "$CC -O2 -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o $OUT/storage_pool_test \
    tests/storage_pool_test.c && $OUT/storage_pool_test"     # the pool (APP_STORE_POOL): the move, saves, cuts
# plat_headphones' debounce (device/plat_hp.c) over a simulated jack: a plug's bounce gives one change, 100 ms on
run hp sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Idevice -o $OUT/hp_test tests/hp_test.c && $OUT/hp_test"
run fastmath sh -c "$CC -O2 -w -o $OUT/fastmath_test tests/fastmath_test.c -lm && $OUT/fastmath_test"
run flash-ok sh -c "$CC -std=c99 -Wall -Wextra -Werror -o $OUT/flash_ok_test tests/flash_ok_test.c && $OUT/flash_ok_test"   # device/fm1_flash_ok.h: the flash parts the storage may use
run midi-rel sh -c "$CC -O2 -std=c99 -Wall -Wextra -Werror -o $OUT/midi_rel_test tests/midi_rel_test.c && $OUT/midi_rel_test"   # include/midi_rel.h
run boot-pack sh tests/boot_pack_test.sh               # the boot's LZMA load images (boot_pack = true)
# the screen reader's firmware side (device/announce.c, the one copy: fm1-reader's tools/sync_announce.sh copies it to
# the firmwares not on fm1-base): the protocol, the SAY frames, ann_later's settling
run announce sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o $OUT/announce_test tests/announce_test.c && \
    $OUT/announce_test"
# the half rendered aside (device/audio_commit.c) over a simulated audio DMA: copied only while its half is free
run audio-commit sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Idevice -o $OUT/audio_commit_test \
    tests/audio_commit_test.c && $OUT/audio_commit_test"
run installer "$PY" tests/install_test.py
# the installer page's script as make_site inlines it (fm1ports.js, fm1pkg.js, fm1ota.js in one module) parses: no name twice
run site-script "$PY" tests/site_script_test.py
# the installer's step 2 against felucca-bsp's real update loader on a pipe (bsp/tests/ldr_pipe.c): classic, fast,
# a loader from before the fast install, resumed after a power cut, a damaged table refused
[ -f bsp/tests/install_fast_test.py ] && run installer-fast-py env CC="$CC" FM1_INSTALL_TOOLS="$PWD/tools" \
    "$PY" bsp/tests/install_fast_test.py
# finding the FM-1 from Python (tools/fm1ports.py, as web/fm1ports.js) and the tools that use it, on a fake mido
run ports "$PY" tests/ports_test.py
# the screen reader's words file (tools/gen_words.py): made, and refused over the reader's limits
run gen-words "$PY" tests/gen_words_test.py
# firmware.json for firmware hubs (tools/firmware_json.py): the fields, the vocabularies, where it is written, the
# schema (docs/firmware.schema.json, when jsonschema is there)
run firmware-json "$PY" tests/firmware_json_test.py
# the build's check that a function is in the RAM code run, _rt_start.._rt_end wherever app.ld puts it (tools/ram_run.py)
run ram-run "$PY" tests/ram_run_test.py
# the check that nothing calls the chip ROM or uses its addresses (tools/romref.py): a constant kept as a number passes
run romref "$PY" tests/romref_test.py
# fm1.toml cpu2_stack (tools/build.py cpu2_stack_flags): the second core's job stack, its range and its stop message
run cpu2-stack "$PY" tests/cpu2_stack_test.py
# the check on an app's RAM code (tools/ramcode.py, plat.h PLAT_RAM_CODE): RAM -> RAM passes, into XIP fails
run ramcode "$PY" tests/ramcode_test.py
# the web installer's m-bootloader step (web/fm1ota.js installBoot) against a simulated FM-1, and finding the FM-1 by
# its port (unusual names, the loader under a new name, M-VAVE's Open firmware, resume's check, package checksums),
# and writing the resource region after it
if command -v node >/dev/null 2>&1; then
    run installer-boot node tests/installer_boot_test.mjs web/m-bootloader/fm1-uboot-1.2.fwsc
    run installer-ports node tests/installer_ports_test.mjs web/m-bootloader/fm1-uboot-1.2.fwsc
    run installer-res node tests/installer_res_test.mjs   # the resource region after an install (Updater.installRes)
    # FM1_STOCK_FWSC=<path to M-VAVE's FM-1_015.fwsc> adds the stock file's cases (that file is not in this repository)
    run checkufw node tests/checkufw_test.mjs web/m-bootloader/fm1-uboot-1.2.fwsc ${FM1_STOCK_FWSC:-}
    run installer-bluetooth node tests/installer_bluetooth_test.mjs
    # the handshake: a firmware that answers M-UPGRADE 3.11's query short and its whole identity to the one with 01
    run installer-hs node tests/installer_hs_test.mjs
    run installer-family-ports node tests/installer_family_ports_test.mjs
    run installer-foreign-loader node tests/installer_foreign_loader_test.mjs
    run installer-stock node tests/installer_stock_test.mjs
    # step 2 against felucca-bsp's real update loader on a pipe (bsp/tests/ldr_pipe.c): the fast install, the classic,
    # a loader from before it, a package without a Felucca loader, a power cut and the install resumed, a damaged one
    [ -f bsp/tests/ldr_pipe.c ] && run installer-fast env CC="$CC" node tests/installer_fast_test.mjs
else
    echo "  skip installer-boot (no node)"
fi
# backup and restore over USB (device/backup.c) on storage.c over a simulated flash, for a test app: the stream's
# frames, a restore refused or failed at every step changing nothing, a power cut at each write: the old set or the
# new, whole. Then tools/fm1_backup.py against a simulated FM-1 on that image, and the web page (web/backup) against
# the tool's own conversation and a simulated FM-1 in JS
run backup sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -o $OUT/backup_test tests/backup_test.c && \
    $OUT/backup_test $OUT/backup.bin"
run backup-pool sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -DBACKUP_TEST_POOL \
    -o $OUT/backup_pool_test tests/backup_test.c && $OUT/backup_pool_test"   # restore with a pooled store
run backup-appbuf sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -DBACKUP_TEST_POOL -DBACKUP_TEST_APPBUF \
    -o $OUT/backup_appbuf_test tests/backup_test.c && $OUT/backup_appbuf_test"   # the app's buffer lent (APP_BACKUP_BUF)
run backup-tool "$PY" tests/backup_tool_test.py $OUT/backup.bin
# fm1.toml lcd_panel: device/lcd.c's init table walked with and without the panel's setup, and the build's flag
run lcd-panel "$PY" tests/lcd_panel_test.py
if command -v node >/dev/null 2>&1; then
    run backup-page sh -c "$PY tests/backup_record.py $OUT/backup.bin $OUT/backup_transcript.json && \
        node tests/backup_page_test.mjs $OUT/backup_transcript.json"
else
    echo "  skip backup-page (no node)"
fi
PKG=template/build/template.fwsc
if [ -f "$PKG" ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test $PKG web/m-bootloader/fm1-uboot-1.2.fwsc"
    run update-loader sh -c "head -c 60000 template/build/template.bin > $OUT/old_app.bin && \
        $PY tools/fm1pkg_make.py $OUT/old_app.bin template/build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $PKG"
else
    echo "  skip update path (no $PKG: tools/build.py template)"
fi
run host-build "$PY" tools/build.py template --host
# the screen mirrored over USB (device/mirror.c; the fonts: the template's build/gen): its pixel code on drawn bands,
# rows not sent twice, drops and redraws, the input reports, off, the remote; then the viewer's own decoder
# (web/mirror) on a session's stream against the device's screen, and tools/fm1_mirror.py making a PNG of it
run mirror sh -c "mkdir -p $OUT/mirror && $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 \
    -Itemplate/build/gen -o $OUT/mirror_test tests/mirror_test.c && $OUT/mirror_test --out $OUT/mirror"
if command -v node >/dev/null 2>&1; then
    run mirror-viewer node tests/mirror_test.mjs $OUT/mirror
    # the page finding the FM-1 (web/fm1ports.js): by name, the pick, a HELLO answer on another port; never other gear
    run mirror-ports node tests/mirror_ports_test.mjs
else
    echo "  skip mirror-viewer, mirror-ports (no node)"
fi
run mirror-png "$PY" tools/fm1_mirror.py $OUT/mirror.png --from $OUT/mirror/stream.syx
# the trimmed fonts (fm1.toml font_trim) draw what the full ones draw, pixel for pixel (every face with lower case)
run font-trim sh -c "mkdir -p $OUT/font/full $OUT/font/trim && \
    FM1_FONT_FULL='T XS S B M L' $PY tools/gen_font.py $OUT/font/full/felucca_font.h && \
    FM1_FONT_FULL='T XS S B M L' FM1_FONT_TRIM=1 $PY tools/gen_font.py $OUT/font/trim/felucca_font.h && \
    $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I$OUT/font/full -o $OUT/font/full_test tests/font_trim_test.c && \
    $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I$OUT/font/trim -o $OUT/font/trim_test tests/font_trim_test.c && \
    $OUT/font/full_test $OUT/font/full.bin && $OUT/font/trim_test $OUT/font/trim.bin && cmp $OUT/font/full.bin $OUT/font/trim.bin"
# the packed fonts (fm1.toml font_pack: the trimmed glyphs' alphas Huffman-coded) too, pixel for pixel
run font-pack sh -c "mkdir -p $OUT/font/full $OUT/font/pack && \
    FM1_FONT_FULL='T XS S B M L' $PY tools/gen_font.py $OUT/font/full/felucca_font.h && \
    FM1_FONT_FULL='T XS S B M L' FM1_FONT_TRIM=1 FM1_FONT_PACK=1 $PY tools/gen_font.py $OUT/font/pack/felucca_font.h && \
    $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I$OUT/font/full -o $OUT/font/full_test tests/font_trim_test.c && \
    $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I$OUT/font/pack -o $OUT/font/pack_test tests/font_trim_test.c && \
    $OUT/font/full_test $OUT/font/full.bin && $OUT/font/pack_test $OUT/font/pack.bin && cmp $OUT/font/full.bin $OUT/font/pack.bin"
# the packed fonts drawn through the decoded-glyph cache (app.h APP_FONT_CACHE, device/gfx.c font_cached) too: a cache
# that forgets all the time (the largest glyph's bytes, 16 slots) and a roomy one
run font-cache sh -c "mkdir -p $OUT/font/full $OUT/font/pack && \
    FM1_FONT_FULL='T XS S B M L' $PY tools/gen_font.py $OUT/font/full/felucca_font.h && \
    FM1_FONT_FULL='T XS S B M L' FM1_FONT_TRIM=1 FM1_FONT_PACK=1 $PY tools/gen_font.py $OUT/font/pack/felucca_font.h && \
    $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I$OUT/font/full -o $OUT/font/full_test tests/font_trim_test.c && \
    $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -DAPP_FONT_CACHE=FONT_BOX_MAX -DAPP_FONT_CACHE_BITS=4 \
        -I$OUT/font/pack -o $OUT/font/cache_small_test tests/font_trim_test.c && \
    $CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -DAPP_FONT_CACHE=6144u -DAPP_FONT_CACHE_BITS=8 \
        -I$OUT/font/pack -o $OUT/font/cache_test tests/font_trim_test.c && \
    $OUT/font/full_test $OUT/font/full.bin && $OUT/font/cache_small_test $OUT/font/cache_small.bin && \
    $OUT/font/cache_test $OUT/font/cache.bin && cmp $OUT/font/full.bin $OUT/font/cache_small.bin && \
    cmp $OUT/font/full.bin $OUT/font/cache.bin"
run template template/build/host/template_host template/tests/basic.txt template/build/host
# Bluetooth audio's calls (plat_bta_*) on the host simulator's stand-in, from a test app's buttons
run bta sh -c "$PY tools/build.py tests/bta_app --host && tests/bta_app/build/host/bta_test_host tests/bta_app/tests/bta.txt tests/bta_app/build/host"
# the same with the buffers lent by the app (fm1.toml bt_audio_mem = "app", plat_bta_mem): on only with the block lent, refused back while on
run bta-mem sh -c "$PY tools/build.py tests/bta_mem_app --host && tests/bta_mem_app/build/host/bta_mem_test_host tests/bta_mem_app/tests/bta.txt tests/bta_mem_app/build/host"
# the headphone jack: the script's jack in / out in the simulator (tests/hp.txt)
run headphones template/build/host/template_host tests/hp.txt template/build/host
# the own Bluetooth stack (bt/, Optimist's: ble_stack = "own"): its link layer, host, pairing, BLE-MIDI, central, the
# radio driver against a fake engine, FM-1 to FM-1, and BR/EDR (A2DP) pieces; UBSan, and ASan where its runtime runs
if sh tests/bt/run.sh; then :; else FAIL=1; fi
# the template as an .rfm1 (tools/build.py --rfm1: the host simulator in WebAssembly for host apps), driven as a host
# app drives it: the descriptor, the screen, a chord, the reader over SysEx, the flash image booted from again
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    rm -f template/build/rfm1/*.rfm1
    run rfm1 sh -c "$PY tools/build.py template --rfm1 && node tests/rfm1_test.mjs template/build/rfm1/*.rfm1"
else
    echo "  skip rfm1 (no emcc or node)"
fi
# the simulated data area: a file put at its offset in each window, read back there by plat_data (tests/data.txt)
run host-data sh -c "printf '\\022\\064\\126\\170' > $OUT/data4.bin && \
    template/build/host/template_host tests/data.txt template/build/host"
# plat_data_write clears bits in place, as the device's NOR (tests/data_bits.txt; plat.h's promise)
run host-data-bits template/build/host/template_host tests/data_bits.txt template/build/host
# an app with APP_BATTERY_RAW (plat_battery_raw) in the host simulator: tests/battery_app
run battery-app sh -c "$PY tools/build.py tests/battery_app --host && \
    tests/battery_app/build/host/battest_host tests/battery_app/tests/battery.txt tests/battery_app/build/host"
# an app with APP_CPU_KHZ (plat_cpu_khz) in the host simulator: tests/cpukhz_app
run cpukhz-app sh -c "$PY tools/build.py tests/cpukhz_app --host && \
    tests/cpukhz_app/build/host/khztest_host tests/cpukhz_app/tests/cpukhz.txt tests/cpukhz_app/build/host"
# plat_core on both cores (cpu2 = true) in the host simulator: tests/core_app
run core-app sh -c "$PY tools/build.py tests/core_app --host && \
    tests/core_app/build/host/coretest_host tests/core_app/tests/core.txt tests/core_app/build/host"
# SysEx between the computer and an app (APP_SYSEX, plat_sysex_send) in the host simulator: tests/sysex_app
run sysex-app sh -c "$PY tools/build.py tests/sysex_app --host && \
    tests/sysex_app/build/host/sxtest_host tests/sysex_app/tests/sysex.txt tests/sysex_app/build/host"
# the dim lights at any brightness (APP_DIM_LEVELS, plat_dim_ns) in the host simulator: tests/dim_app
run dim-app sh -c "$PY tools/build.py tests/dim_app --host && \
    tests/dim_app/build/host/dimtest_host tests/dim_app/tests/dim.txt tests/dim_app/build/host"
# the browser emulator (tools/build.py --web, emcc): the template's worklet and app.wasm under node (boot, screen, a
# key, MIDI in and out); its optional parts' build (fm1.toml emu); then each part's own test, tests/emu_NAME_test.mjs,
# which builds what it needs (the template is left built as it is: resample, the part on without asking)
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    export PYTHON="$PY"
    run emu-build "$PY" tools/build.py template --web
    run emu node tests/emu_test.mjs template/build/emu
    run emu-parts "$PY" tests/emu_parts_test.py
    for t in tests/emu_*_test.mjs; do
        [ -e "$t" ] || continue
        run "$(basename "$t" _test.mjs)" node "$t"
    done
    run emu-plain "$PY" tools/build.py template --web
else
    echo "  skip emu (no emcc or node)"
fi
# and in the browser emulator (web/emu/emu_web.c web_sx_in, web_sx_out_*), its app.wasm under node
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    run emu-sysex sh -c "$PY tools/build.py tests/sysex_app --web && node tests/sysex_web_test.mjs tests/sysex_app/build/emu"
else
    echo "  skip emu-sysex (no emcc or node)"
fi
# the backup in the host simulator (backup, expect value) on a small app with backup = true: tests/backup_app (the
# emulator's part "backup" has its own test, emu_backup_test.mjs, on the same app)
run backup-app sh -c "$PY tools/build.py tests/backup_app --host && mkdir -p tests/backup_app/build/out && \
    tests/backup_app/build/host/bktest_host tests/backup_app/tests/backup.txt tests/backup_app/build/out"
# an app with APP_BLE_PEERS (its own list: the scan's reports, plat_ble_connect_peer) in the host simulator
run peers-app sh -c "$PY tools/build.py tests/peers_app --host && \
    tests/peers_app/build/host/peertest_host tests/peers_app/tests/peers.txt tests/peers_app/build/host"
# the fourth window on request (xdata2 = true, FM1B_XDATA2): the template's host build with it (kept apart, the plain
# build put back after), the window the app's, the store's sectors in it refused (tests/data_xdata2.txt)
run host-xdata2 sh -c "FM1B_XDATA2=1 $PY tools/build.py template --host >/dev/null && \
    cp template/build/host/template_host $OUT/template_host_xdata2 && $PY tools/build.py template --host >/dev/null && \
    $OUT/template_host_xdata2 tests/data_xdata2.txt template/build/host"
# the whole flash as a 1 MiB image (flash_dump / flash_load, FM1B_FLASH): out of the simulator, read and saved again by
# device/storage.c (tests/flash_image_test.c), back into a new run, and a power-on with it; the round trip byte for byte
H=template/build/host
run host-flash-dump sh -c "printf '\\022\\064\\126\\170' > $OUT/data4.bin && $H/template_host tests/flash_dump.txt $H"
run flash-image sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -o $OUT/flash_image_test \
    tests/flash_image_test.c && $OUT/flash_image_test $H/flash.bin $H/flash2.bin"
run host-flash-load sh -c "$H/template_host tests/flash_load.txt $H && \
    FM1B_FLASH=$H/flash2.bin $H/template_host tests/flash_boot.txt $H && \
    cmp $H/flash.bin $H/flash3.bin"
# the USB port as a MIDI host (usb_host, plat_usb_host, plat_usb_host_name) in the host simulator's model of it: tests/usbh_app
run usbh-app sh -c "$PY tools/build.py tests/usbh_app --host && \
    tests/usbh_app/build/host/usbhtest_host tests/usbh_app/tests/usbh.txt tests/usbh_app/build/host"
# the crash history in flash (device/crashlog.c): the ring against a simulated NOR (filled past both sectors, a power
# cut at every program and in an erase, the boot's dedup across restarts and power-offs, a save cut short not tried
# again, the frames, the clear); then in the template's simulator (crash, restart, power, the request's frames and
# "end N", the clear, a power-on with the dumped flash) read through tools/fm1_remote.py's parser and its --elf symbols
run crashlog sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -o $OUT/crashlog_test tests/crashlog_test.c && \
    $OUT/crashlog_test"
run host-crashlog sh -c "mkdir -p $OUT/crashlog && $H/template_host tests/crashlog.txt $OUT/crashlog && \
    $PY tests/crashlog_host_test.py $H/template_host $OUT/crashlog"
# the debug stats in flash (device/dbgstats.c): the ring against a simulated NOR (the switch, the 5-minute snapshots,
# the wrap's erase and write-back, a power cut at every program and inside the erase and the write-back, a staged update
# refused, the quiet moment and the forced write, the crash's snapshot, the frames, the clear; nothing written outside
# 0xE4000..0xE4EFF); then in the template's simulator (26 simulated minutes, a restart, a crash, a power-off, a staged
# update, a power-on with the dumped flash) read through tools/fm1_remote.py's parser
run dbgstats sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -o $OUT/dbgstats_test tests/dbgstats_test.c && \
    $OUT/dbgstats_test"
# the second core's stack room (plat_stack_left(PLAT_STK_C1)) on a host model of felucca-bsp's fm1_cpu1.h: -1 when CPU1 never started
run stack-c1 sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -Itests/stack_c1 -o $OUT/stack_c1_test tests/stack_c1_test.c && \
    $OUT/stack_c1_test"
run host-dbgstats sh -c "mkdir -p $OUT/dbgstats && $H/template_host tests/dbgstats.txt $OUT/dbgstats && \
    $PY tests/dbgstats_host_test.py $H/template_host $OUT/dbgstats"
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
