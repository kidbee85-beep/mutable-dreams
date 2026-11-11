/* SPDX-License-Identifier: MPL-2.0
 * Mutable Dreams: one owner for each pending USB SysEx frame.
 * The USB interrupt can enqueue a frame after an empty peek or after release.
 * Only call the updater while the already inspected frame is still pending.
 */
static void fm1_dispatch_sysex(uint32_t now)
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n))
        return;
    if (ann_frame(p, n, now) || log_frame(p, n) || CL_REQUEST(p, n)
        || DS_REQUEST(p, n) || remote_frame(p, n)
#if FM1B_WIFI
        || wifi_frame(p, n)
#endif
#if FM1B_BT_AUDIO
        || bta_frame(p, n)
#endif
#if FM1B_BLE_OWN && FM1B_BLE_CENTRAL
        || dev_frame(p, n)
#endif
#ifdef APP_SYSEX
        || app_sysex(p, n)
#endif
    )
        ota_frame_done();
    else
        usb_cmd(ota_service()); /* The same still-pending frame; never its successor. */
}
