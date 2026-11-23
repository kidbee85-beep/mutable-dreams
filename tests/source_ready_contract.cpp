/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * The actual C backend, with deterministic host platform calls. No DSP/device.
 * A nonzero application end timestamp never substitutes for platform join. */
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#ifdef NDEBUG
#error "Readiness contract needs assertions"
#endif
#ifndef MD_MIX_DEVICE
#define MD_MIX_DEVICE 1
#endif
#define MD_SOURCE_MIX_PIPELINE 1
static uint32_t clock_ticks=100,on=1,fences,wait_calls,run_calls,retire_calls;
static int accept=1,wait_rc;static bool platform_done=true,expect_zero_end;
uint32_t plat_ticks24(){return clock_ticks++;}
void plat_cpu2_sync(){fences++;}
uint32_t plat_cpu2_on(){return on;}
void plat_cpu2_retire(unsigned){retire_calls++;on=0;platform_done=true;}
int plat_cpu2_run(void(*)(uint32_t),uint32_t);
int plat_cpu2_wait_budget(uint32_t,uint32_t);
int16_t *plat_usb_ch(unsigned,unsigned*){return nullptr;}
int plat_cpu2_set(int){return 0;}
int plat_cpu2_gave_up(){return 0;}
uint32_t plat_cpu2_bad(){return 0;}
uint32_t plat_cpu2_stack(){return 0;}
void md_source_mix_stats(uint32_t *p){p[0]=p[1]=p[2]=0;}
#include "../app/cpu2_mix.c"
void md_mix_device_job(uint32_t){}
int md_mix_device_selftest(){return 288;}
void md_mix_device_recover(){}
int plat_cpu2_run(void(*)(uint32_t),uint32_t){
 run_calls++;assert(platform_done);assert(expect_zero_end&&mix2_job_end==0&&mix2_job_start==0&&mix2_job_bad==0);
 if(accept)platform_done=false;return accept;
}
int plat_cpu2_wait_budget(uint32_t,uint32_t span){
 wait_calls++;assert(!platform_done);assert(span==256u*24u);platform_done=true;return wait_rc;
}
int main(){
 unsigned char descriptor[8]={};expect_zero_end=true;mix2_cb_start=clock_ticks;
 assert(!md_mix_worker_ready_device()&&fences==1);
 mix2_job_end=123;assert(md_mix_worker_ready_device()); // An idle old timestamp grants nothing.
 assert(md_mix_worker_submit_device(descriptor));assert(mix2.accepted==1&&!platform_done);
 assert(!md_mix_worker_ready_device());md_mix_worker_job_begin();md_mix_worker_job_end();
 const auto completed=mix2.completed,w=wait_calls,r=retire_calls,submitted=run_calls;
 assert(md_mix_worker_ready_device());assert(!platform_done); // DSP end can precede platform completion.
 assert(mix2.completed==completed&&wait_calls==w&&retire_calls==r&&run_calls==submitted);
 assert(md_mix_worker_wait_device()==0&&platform_done&&wait_calls==1&&mix2.completed==1);
 assert(md_mix_worker_submit_device(descriptor));md_mix_worker_job_begin();
 clock_ticks=0;md_mix_worker_job_end();assert(!md_mix_worker_ready_device()); // Tick wrap false-negative.
 clock_ticks=120;assert(md_mix_worker_wait_device()==0&&mix2.completed==2);
 accept=0;mix2_job_end=321;assert(!md_mix_worker_submit_device(descriptor));assert(!md_mix_worker_ready_device()&&mix2.accepted==2&&mix2.refused==1);
 accept=1;assert(md_mix_worker_submit_device(descriptor));md_mix_worker_job_begin();md_mix_worker_job_end();assert(md_mix_worker_ready_device());
 wait_rc=-1;assert(md_mix_worker_wait_device()==-1);assert(mix2.completed==2&&mix2.failed==1&&mix2_abort&&retire_calls==1&&!on);
 uint32_t words[23]={};assert(mix2_read(words)==92&&words[0]==0x324d444du&&words[1]==2);
 std::puts("PASS actual cpu2_mix.c: reset before submit, sync/read hint, DSP-end before platform-done, tick-zero false-negative, refusal, mandatory256us join and fault retirement; no descriptor ownership from hint");
}
