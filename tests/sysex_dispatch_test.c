/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define FM1B_WIFI 0
#define APP_SYSEX
#define CL_REQUEST(p,n) 0
#define DS_REQUEST(p,n) 0
static uint8_t pending;
static unsigned inject, inject_on_empty, app_count, ota_count, ota_seen;
static int ota_frame_get(const uint8_t **p,uint32_t *n){
 if(!pending){pending=(uint8_t)inject_on_empty;inject_on_empty=0;return 0;}
 *p=&pending;*n=1;return 1;
}
static void ota_frame_done(void){pending=0;if(inject){pending=(uint8_t)inject;inject=0;}}
static int ann_frame(const uint8_t *p,uint32_t n,uint32_t now){(void)p;(void)n;(void)now;return 0;}
static int log_frame(const uint8_t *p,uint32_t n){(void)p;(void)n;return 0;}
static int remote_frame(const uint8_t *p,uint32_t n){(void)n;if(*p==2){app_count++;return 1;}return 0;}
static int app_sysex(const uint8_t *p,uint32_t n){(void)n;if(*p==1){app_count++;return 1;}return 0;}
static uint32_t ota_service(void){
 const uint8_t *p;uint32_t n;
 if(!ota_frame_get(&p,&n))return 0;
 ota_count++;ota_seen=*p;ota_frame_done();return ota_seen==3?7:0;
}
static void usb_cmd(uint32_t n){(void)n;}
#include "../base/device/sysex_dispatch.h"
int main(void){
 /* The old main loop peeked twice when its initial check returned empty. */
 pending=0;inject_on_empty=1;
 {
  const uint8_t *p;uint32_t n;
  if(ota_frame_get(&p,&n)&&app_sysex(p,n))ota_frame_done();
  else usb_cmd(ota_service());
 }
 assert(!app_count&&ota_count==1&&ota_seen==1&&!pending);
 puts("REPRODUCED: old main loop drops an editor request arriving after the empty peek");
 /* An application frame arrives in the ISR exactly when the previous owner releases its slot. */
 for(unsigned first=1;first<=2;first++)for(unsigned next=1;next<=3;next++){
  pending=(uint8_t)first;inject=next;app_count=ota_count=ota_seen=0;
  fm1_dispatch_sysex(0);assert(app_count==1&&ota_count==0&&pending==next);
  fm1_dispatch_sysex(1);assert(!pending);
  if(next==3)assert(app_count==1&&ota_count==1&&ota_seen==3);
  else assert(app_count==2&&ota_count==0);
 }
 /* A real ISR may enqueue the editor, panel or mirror request after the peek. */
 for(unsigned next=1;next<=3;next++){
  pending=0;inject_on_empty=next;app_count=ota_count=ota_seen=0;
  fm1_dispatch_sysex(2);assert(pending==next&&!app_count&&!ota_count);
  fm1_dispatch_sysex(3);assert(!pending);
  if(next==3)assert(!app_count&&ota_count==1&&ota_seen==3);
  else assert(app_count==1&&!ota_count);
 }
 /* Unknown traffic is released rather than wedging the single receive slot. */
 pending=4;inject=0;ota_count=0;fm1_dispatch_sysex(4);assert(!pending&&ota_count==1);
 puts("PASS: application/remote frames arriving at release survive; OTA identity route and unknown-frame cleanup intact");
 return 0;
}
