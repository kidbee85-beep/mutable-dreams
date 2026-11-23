/* Private FM-1 worker integration; no flash logging or allocation. */
#ifdef MD_MIX_DEVICE
extern void md_mix_device_job(uint32_t);
extern int md_mix_device_selftest(void);
extern void md_mix_device_recover(void);
unsigned md_mix_min_frames=32;
static struct {uint32_t accepted,completed,refused,failed,rejected,parity,parity_failed,max_job,max_wait,max_retire,serial,fault_mode;} mix2;
static volatile uint32_t mix2_job_start,mix2_job_end,mix2_job_bad,mix2_inject;
static uint32_t mix2_cb_start;static int mix2_abort,mix2_recover;
static uint32_t mix2_ticks(void){return plat_ticks24();}
static void mix2_retire(void){uint32_t t=mix2_ticks();plat_cpu2_retire(1);t=mix2_ticks()-t;if(t>mix2.max_retire)mix2.max_retire=t;}
void md_mix_worker_retire_device(void){mix2_retire();}
void md_mix_worker_test_begin(void){mix2_cb_start=mix2_ticks();}
void md_mix_worker_reject_device(void){mix2_job_bad=1;plat_cpu2_sync();}
void md_mix_worker_job_begin(void){mix2_job_start=mix2_ticks();plat_cpu2_sync();}
void md_mix_worker_job_end(void){mix2_job_end=mix2_ticks();plat_cpu2_sync();}
int md_mix_worker_failed_device(void){return mix2_abort;}
unsigned md_mix_worker_inject_device(void){return mix2_inject;}
int md_mix_worker_available_device(void){return plat_cpu2_on();}
/* Scheduling hint only: descriptor/output ownership still requires the real bounded join. */
int md_mix_worker_ready_device(void){plat_cpu2_sync();return mix2_job_end!=0;}
int md_mix_worker_submit_device(void *job){
 if(!plat_cpu2_on()){mix2.refused++;return 0;}
 mix2_job_bad=0;mix2_job_start=mix2_job_end=0;plat_cpu2_sync();
 if(!plat_cpu2_run(md_mix_device_job,(uint32_t)(uintptr_t)job)){mix2.refused++;return 0;}
 mix2.accepted++;return 1;
}
int md_mix_worker_wait_device(void){
 const uint32_t start=mix2_ticks(),elapsed=start-mix2_cb_start,limit=5000u*24u;
 const uint32_t left=elapsed<limit?limit-elapsed:0u,cap=256u*24u,span=left<cap?left:cap;
 int rc=plat_cpu2_wait_budget(start,span);plat_cpu2_sync();
 const uint32_t wait=mix2_ticks()-start;if(wait>mix2.max_wait)mix2.max_wait=wait;
 if(rc==0&&mix2_job_bad){mix2.rejected++;mix2_retire();rc=-1;}
 if(rc<0){plat_cpu2_retire(0);mix2.failed++;mix2.fault_mode=mix2_inject;mix2_inject=0;mix2_abort=mix2_recover=1;return -1;}
 const uint32_t ticks=mix2_job_end-mix2_job_start;if(ticks>mix2.max_job)mix2.max_job=ticks;
 mix2.completed++;return 0;
}
static int mix2_block_begin(void){int recover=mix2_recover;mix2_recover=0;mix2_abort=0;mix2_cb_start=mix2_ticks();return recover;}
static void mix2_zero(int32_t *pcm,unsigned n){
 memset(pcm,0,n*2*sizeof *pcm);
 for(unsigned offset=0;offset<n;){unsigned count=0;int16_t *p=plat_usb_ch(offset,&count);if(!p||!count)break;if(count>n-offset)count=n-offset;for(unsigned k=0;k<count;k++)memset(p+k*8,0,6*sizeof *p);offset+=count;}
}
static void mix2_init(void){
 memset(&mix2,0,sizeof mix2);mix2_cb_start=mix2_ticks();
 if(plat_cpu2_set(1)==0){int rc=md_mix_device_selftest();mix2.parity=rc>0?(uint32_t)rc:0;if(rc<=0){mix2.parity_failed++;mix2_retire();}}
 mix2_abort=mix2_recover=0;mix2_inject=0;
}
static unsigned mix2_read(uint32_t *w){
 const uint32_t v[]={0x324d444du,1,plat_cpu2_on(),(uint32_t)plat_cpu2_gave_up(),plat_cpu2_bad(),plat_cpu2_stack(),md_mix_min_frames,mix2.accepted,mix2.completed,mix2.refused,mix2.failed,mix2.rejected,mix2.parity,mix2.parity_failed,mix2.max_job,mix2.max_wait,mix2.max_retire,mix2.fault_mode,mix2_inject,(uint32_t)mix2_abort};memcpy(w,v,sizeof v);
#ifdef MD_SOURCE_MIX_PIPELINE
 w[1]=2;md_source_mix_stats(w+20);return sizeof v+3*sizeof(uint32_t);
#else
 return sizeof v;
#endif
}
#endif
