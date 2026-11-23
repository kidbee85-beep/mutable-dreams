#ifdef NDEBUG
#error "CPU2 host verification requires active assertions; NDEBUG is unsupported"
#endif
#include <cstdlib>
#ifndef MD_CPU2_CHECK
#define MD_CPU2_CHECK(condition) do { if(!(condition)) std::abort(); } while(0)
#endif

/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#include "mix_pipeline_host_backend.h"
#include <atomic>
#include <stdexcept>
static std::atomic<unsigned> mutations{0};
struct Context{int expected,observed;unsigned count;};
static void fp(void *p){auto &c=*static_cast<Context*>(p);c.observed=std::fegetround();MD_CPU2_CHECK(c.observed==c.expected);c.count++;}
static void partial(void*){mutations++;throw std::runtime_error("injected after state advancement");}
int main(){
 for(int round:{FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO}){
  MD_CPU2_CHECK(std::fesetround(round)==0);Context c{round,-1,0};const int accepted=md_mix_worker_submit(fp,&c);
  if(!accepted)fp(&c);MD_CPU2_CHECK(std::fesetround(FE_TONEAREST)==0);if(accepted)md_mix_worker_wait();MD_CPU2_CHECK(c.count==1&&c.observed==round&&std::fegetround()==FE_TONEAREST);
 }
 bool accepted=false,caught=false,blocked=false;
 try{accepted=md_mix_worker_submit(partial,nullptr);if(!accepted)partial(nullptr);else md_mix_worker_wait();}catch(const std::runtime_error&){caught=true;}
 MD_CPU2_CHECK(caught&&mutations==1);
 if(accepted){try{md_mix_worker_submit(partial,nullptr);}catch(const std::runtime_error&){blocked=true;}MD_CPU2_CHECK(blocked&&mutations==1);}
 std::printf("{\"fpu_modes\":4,\"one_execution\":true,\"accepted_partial_fault\":%s,\"mutations\":%u,\"replay_blocked_if_accepted\":%s}\n",accepted?"true":"false",mutations.load(),(!accepted||blocked)?"true":"false");
}
