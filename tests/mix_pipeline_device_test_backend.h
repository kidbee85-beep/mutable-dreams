#ifdef NDEBUG
#error "Device verification requires active assertions"
#endif
#include "mix_pipeline_host_backend.h"
#include <atomic>
#include <chrono>
extern "C" void md_mix_device_job_host(void*);
static std::atomic<int> device_bad{0},device_aborted{0},device_on{1};
static thread_local int device_core=0;
static std::atomic<unsigned> device_injection{0};
static void (*device_partial_hook)(void*);
static void device_entry(void *p){device_core=1;if(device_partial_hook){device_partial_hook(p);device_bad=1;}else md_mix_device_job_host(p);device_core=0;}
extern "C" {
unsigned md_mix_min_frames=32;
uint32_t plat_ticks24(void){return (uint32_t)(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()*24/1000);}
uint32_t plat_core(void){return device_core;}
void plat_cpu2_sync(void){std::atomic_thread_fence(std::memory_order_seq_cst);}
int md_mix_worker_available_device(void){return device_on;}
int md_mix_worker_ready_device(void){return 0;} // Conservative host hint, independent of ownership.
void md_mix_worker_reject_device(void){device_bad=1;}
void md_mix_worker_retire_device(void){device_on=0;}
void md_mix_worker_test_begin(void){}
void md_mix_worker_job_begin(void){}
void md_mix_worker_job_end(void){}
unsigned md_mix_worker_inject_device(void){return device_injection;}
int md_mix_worker_failed_device(void){return device_aborted;}
int md_mix_worker_submit_device(void *p){if(!device_on)return 0;device_bad=0;return md_mix_worker_submit(device_entry,p);}
int md_mix_worker_wait_device(void){md_mix_worker_wait();if(device_bad){device_on=0;device_aborted=1;return -1;}return 0;}
}
