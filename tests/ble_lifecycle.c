/* Physical BLE MIDI writes are valid even before notification subscription.
 * A write-only sender must still generate a disconnect / release event. */
#define PLAT_HOST 1
#define FM1B_BLE_CENTRAL 1
#include "plat.h"
#include "btdev.h"
#include <assert.h>
#include <stdio.h>
static int linked, listening, dev;
int plat_ble_connected(void){return listening;}
int plat_ble_linked(void){return linked;}
int plat_ble_device(void){return dev;}
int plat_ble_state(void){return BLE_READY;}
int plat_ble_found(int i,char *n,int *r){(void)i;(void)n;(void)r;return 0;}
void plat_ble_scan(int on){(void)on;}
void plat_ble_disconnect(void){dev=BLE_DEV_OFF;}
int plat_ble_connect(int i){(void)i;return 0;}
#include "../base/lib/btdev.c"
int main(void){
 btdev_page(0);assert(btdev_event()==BTDEV_NONE);
 linked=1;btdev_page(0);assert(btdev_event()==BTDEV_COMPUTER);assert(btdev_event()==BTDEV_NONE);
 linked=0;btdev_page(0);assert(btdev_event()==BTDEV_COMPUTER_GONE);assert(btdev_event()==BTDEV_NONE);
 puts("PASS write-only BLE sender has a complete connected / gone lifecycle");
 linked=listening=1;btdev_page(0);assert(btdev_event()==BTDEV_COMPUTER);
 listening=0;btdev_page(0);assert(btdev_event()==BTDEV_NONE);
 linked=0;btdev_page(0);assert(btdev_event()==BTDEV_COMPUTER_GONE);
 puts("PASS notification unsubscribe alone does not disconnect an active write link");
 dev=BLE_DEV_READY;btdev_page(0);assert(btdev_event()==BTDEV_CONNECTED);
 dev=BLE_DEV_OFF;btdev_page(0);assert(btdev_event()==BTDEV_LOST);assert(btdev_event()==BTDEV_NONE);
 puts("PASS external peripheral lifecycle is preserved");
 return 0;
}
