/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C08000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver; an app's audio code, out of the XIP cache's way). (It was 24 KB, the last 8 KB
 * kept for RAM-run debug; LOOM-8's note starts took them, 2026-10-19.)
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * CACHE 0x01F28000..0x01F2F000: data-cache ways used as RAM (fm1.toml cache_ram_ways, felucca-bsp's fm1_cache_ram.h).
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table.
 * The default map, fixed regions; fm1.toml ram_layout = "packed" links with app_packed.ld instead (one SRAM budget). */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x9DFBC   /* the app slot (0x8DFBC) and 64 KB: the image before
                                                       * boot_pack packs it; build.py checks the slot */
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x8000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 0x7000     /* data-cache ways 0..6 as RAM: only with fm1.toml cache_ram_ways
                                                           * and its test passed (plat_cache_ram); the cache's otherwise */
}
SECTIONS
{
    /* JieLi's BT code and constants run from XIP, as in their SDK (sdk_ld_sfc.c); .volatile_ram_code
     * and the radio trim (*.rf_trim, CONFIG_RF_TRIM_CODE_AT_RAM in their demos) run from RAM: the trim
     * draws ~500 ms of high current, and fetching from the flash meanwhile corrupts what is read
     * (the crashes 0.7 s into every BT start, fm1-ble notes/03-05) */
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
              *(.bt_stack_code .ble_sm_code .hci_controller_code .ble_hci_code .ble_att_code .ble_db_code
                .classic_lmp_code .link_bulk_code .classic_rf_code .lmp_irq_code .ble_gatt_code .ble_ll_code
                .ble_rf_code .bt_rf_code .hci_interface_code .vendor_manager_code .device_manager_code)
              *(.*_const)
              . = ALIGN(4); _etext = .; } > XIP
    /* .c1_text: an app's second core's RAM loop (tools/build.py), copied and guarded with the rest; .app_ram: the
     * app's own RAM code (plat.h PLAT_RAM_CODE), the same, calling only RAM code (tools/ramcode.py checks) */
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) *(.volatile_ram_code) *(*.rf.text.cache.L2.rf_trim)
                  . = ALIGN(4); _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .;
                  _ar_start = .; *(.app_ram .app_ram.*) . = ALIGN(4); _ar_end = .;
                  _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    _app_xip_end = ORIGIN(XIP) + 0x8DFBC;            /* the app slot's end (the boot's bound on the packed load
                                                      * images: main_fm1.c) */
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.*_data) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) *(.*_bss) *(.bd_base) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    /* the cache RAM's objects (plat.h plat_cache_ram): zeroed by its test at the start, not by the boot; for an
     * object whose every user checks plat_cache_ram() first (without the test passed it is not RAM). Empty in
     * fm1-base itself; tools/build.py checks it fits the ways asked for (none: empty) */
    .cache_ram (NOLOAD) : { _cr_start = .; *(.cache_ram .cache_ram.*) . = ALIGN(4); _cr_end = .; } > CACHE
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
