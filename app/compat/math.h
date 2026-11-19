/* SPDX-License-Identifier: MIT
 * Freestanding single-precision math. Same adapter is used for DSP host/wasm tests. */
#pragma once
#include "fastmath.h"
#define M_PI 3.14159265358979323846f
#define fabsf fm_fabsf
#define floorf fm_floorf
#define sqrtf fm_sqrtf
#define sinf fm_sinf
#define cosf fm_cosf
#define tanf fm_tanf
#define expf fm_expf
#define exp2f fm_exp2f
#define logf fm_logf
#define log2f fm_log2f
#define powf fm_powf
#define tanhf fm_tanhf
#define isfinite(x) __builtin_isfinite(x)
static inline float ceilf(float x){return -fm_floorf(-x);}
static inline float fminf(float a,float b){return a<b?a:b;}
static inline float fmaxf(float a,float b){return a>b?a:b;}
/* The Mutable Instruments modules (clouds/warps/tides/marbles/grids) call the double forms fabs/cos/sin/sqrt/exp/log
 * on float data (an STM32 habit). The pi32v2 has no hardware double, so these run as single precision. */
static inline float fabs(float x){return fm_fabsf(x);}
static inline float cos(float x){return fm_cosf(x);}
static inline float sin(float x){return fm_sinf(x);}
static inline float sqrt(float x){return fm_sqrtf(x);}
static inline float exp(float x){return fm_expf(x);}
static inline float log(float x){return fm_logf(x);}
static inline float pow(float x,float y){return fm_powf(x,y);}
