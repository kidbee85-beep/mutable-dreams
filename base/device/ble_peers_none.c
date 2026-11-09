/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* the devices one by one (plat.h plat_ble_connect_peer ..., an app with APP_BLE_PEERS): the own stack's central only */
#ifdef APP_BLE_PEERS
int plat_ble_connect_peer(const struct plat_ble_peer *p) { (void)p; return -1; }
int plat_ble_device_fail(uint32_t *code)
{
    if (code)
        *code = 0;
    return PLAT_BLE_CF_NONE;
}
uint32_t plat_ble_device_flags(void) { return 0; }
int plat_ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6]) { (void)irk, (void)addr; return 0; }
#endif
