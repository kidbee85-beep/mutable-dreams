#ifdef NDEBUG
#error "CPU2 host verification requires active assertions; NDEBUG is unsupported"
#endif
#include <cstdlib>
#ifndef MD_CPU2_CHECK
#define MD_CPU2_CHECK(condition) do { if(!(condition)) std::abort(); } while(0)
#endif

/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * CPU2 preparation: actual host concurrency, not a device speed benchmark.
 * Include before dreams.cpp in a standalone probe; no allocations in the DSP.
 */
#pragma once
#include <thread>
#include <cstring>
#include <cfenv>
#include <mutex>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <exception>
#include <chrono>
struct MixHostBackend {
 std::mutex mutex;std::condition_variable request,complete;
 bool pending=false,occupied=false,stop=false;void (*fn)(void*)=nullptr;void *arg=nullptr;
 std::fenv_t environment;std::exception_ptr error;unsigned long long submitted=0,finished=0,refused=0,waited=0;
 const char *mode=std::getenv("MD_MIX_MODE");std::thread worker;
 MixHostBackend():worker(&MixHostBackend::loop,this){}
 ~MixHostBackend(){
  {std::lock_guard<std::mutex> g(mutex);MD_CPU2_CHECK(!occupied);stop=true;}request.notify_one();worker.join();
  if(std::getenv("MD_MIX_REPORT"))std::fprintf(stderr,"MIX_BACKEND submitted=%llu finished=%llu refused=%llu waits=%llu\n",submitted,finished,refused,waited);
 }
 bool is(const char *s)const{return mode&&std::strcmp(mode,s)==0;}
 void loop(){
  for(;;){
   void (*job)(void*);void *context;std::fenv_t env;
   {std::unique_lock<std::mutex> l(mutex);request.wait(l,[&]{return pending||stop;});if(stop)return;job=fn;context=arg;env=environment;pending=false;}
   std::exception_ptr failure;
   try{MD_CPU2_CHECK(std::fesetenv(&env)==0);if(is("jitter")&&submitted%17==0)std::this_thread::sleep_for(std::chrono::microseconds(20));job(context);}
   catch(...){failure=std::current_exception();}
   {std::lock_guard<std::mutex> l(mutex);if(failure)error=failure;occupied=false;finished++;}complete.notify_one();
  }
 }
 int submit(void (*f)(void*),void *a){
  std::unique_lock<std::mutex> l(mutex);MD_CPU2_CHECK(!occupied);if(error)std::rethrow_exception(error);
  if(is("refuse")||(is("mixed")&&(submitted+refused)%3==0)){refused++;return 0;}
  submitted++;
  if(is("sync")){l.unlock();std::exception_ptr failure;try{f(a);}catch(...){failure=std::current_exception();}l.lock();if(failure)error=failure;finished++;return 1;}
  fn=f;arg=a;MD_CPU2_CHECK(std::fegetenv(&environment)==0);occupied=true;pending=!is("deferred");l.unlock();if(!is("deferred"))request.notify_one();return 1;
 }
 void wait(){
  std::unique_lock<std::mutex> l(mutex);waited++;
  if(is("deferred")&&occupied){
   auto f=fn;void *a=arg;std::fenv_t env=environment,caller;MD_CPU2_CHECK(std::fegetenv(&caller)==0);l.unlock();
   std::exception_ptr failure;try{MD_CPU2_CHECK(std::fesetenv(&env)==0);f(a);}catch(...){failure=std::current_exception();}
   MD_CPU2_CHECK(std::fesetenv(&caller)==0);l.lock();if(failure)error=failure;occupied=false;finished++;
  }
  complete.wait(l,[&]{return !occupied;});if(error)std::rethrow_exception(error);
 }
};
static MixHostBackend mix_host;
extern "C" int md_mix_worker_submit(void (*fn)(void*),void *arg){return mix_host.submit(fn,arg);}
extern "C" void md_mix_worker_wait(void){mix_host.wait();}
