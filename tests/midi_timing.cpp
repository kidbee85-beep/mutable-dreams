/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * F-2: MIDI played at its frame inside a callback's block, and a steady tempo
 * from an external clock, kept out of the saved project (F-6). */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static float a[512],b[512];
static void block(float *lr){md_render(lr,128);md_render(lr+256,128);}
static void quiet_rack(){
 md_init();assert(md_command(MD_SCENE,0,MD_BLANK_BIOME));float t[2];md_render(t,1);
 assert(md_command(MD_MODEL,0,MD_MELLOW));assert(md_command(MD_LEVEL,0,800));assert(md_command(MD_ATTACK,0,0));
 assert(md_command(MD_MUTE,0,0));md_render(t,1);
}
int main(){
 // A note placed at frame 100 of a block: the block equals a silent one up to
 // frame 100 and sounds from there on (its first two 128-frame renders too).
 quiet_rack();md_block_begin();md_midi_at(0x90,60,100,0,100);block(a);
 quiet_rack();md_block_begin();block(b);
 for(unsigned k=0;k<100;k++)assert(a[2*k]==b[2*k]&&a[2*k+1]==b[2*k+1]);
 bool sounds=false;for(unsigned k=100;k<256;k++)sounds|=a[2*k]!=b[2*k];
 assert(sounds);
 // Frame 0 is at once, as md_midi.
 quiet_rack();md_block_begin();md_midi_at(0x90,60,100,0,0);block(a);
 quiet_rack();md_midi(0x90,60,100,0);block(b);
 assert(!memcmp(a,b,sizeof a));
 // External clock at 120 BPM (918.75 frames a pulse), placed as the app does:
 // each pulse at its frame in its block. The tempo settles on 120 and holds.
 md_init();float t[2];md_render(t,1);const int project_tempo=md_state()->tempo;assert(project_tempo!=120);
 assert(md_command(MD_CLOCK_SOURCE,0,1));md_render(t,1);
 md_block_begin();md_midi_at(0xfa,0,0,0,0);md_render(t,1);
 double next=1;unsigned pulses=0,changes=0;int tempo=-1;uint32_t steps0=0;
 for(unsigned blk=0;blk<4000;blk++){
  md_block_begin();
  const double start=blk*256.0;
  while(next<start+256){md_midi_at(0xf8,0,0,0,(unsigned)(next-start));next+=918.75;pulses++;}
  block(a);
  if(pulses>48){if(tempo>=0&&md_tempo_now()!=tempo)changes++;tempo=md_tempo_now();}
  if(pulses==48)steps0=md_status()->step;
 }
 assert(md_playing()&&tempo==120&&!changes);
 // F-6: the project keeps its own tempo (an unlocked pattern still moves its
 // seed while playing); switching the clock off times the steps by it again.
 assert(md_state()->tempo==project_tempo);
 assert(md_command(MD_CLOCK_SOURCE,0,0));md_render(t,1);assert(md_tempo_now()==project_tempo);
 const uint32_t steps=md_status()->step-steps0;
 assert(steps+2>=(pulses-48)/6&&steps<=(pulses-48)/6+2);
 // Two pulses inside one block both start their steps.
 printf("PASS midi timing: a note at its frame inside the block, frame 0 at once; %u clock pulses at 120 BPM held 120 with no change, %u steps; the project tempo %d kept.\n",pulses,steps,project_tempo);
 return 0;
}
