/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Original three-mode hand drum. Circular-membrane mode ratios are acoustic
 * reference values, not copied instrument code. Existing per-voice states are
 * reused; no sample bank, heap allocation or additional delay buffer. */
static void skin_render(Voice &v,const md_track &t,float f,float tone,float harm,float shape,bool trigger,float *out,unsigned n){
 float x[3],y[3],co[3],si[3],decay[3];
 const float ratio[3]={1.f,1.59334f,2.13555f};
 float seconds=.035f+.62f*unit(t.decay)*unit(t.decay);
 const float weight[3]={.78f-.20f*harm,.18f+.35f*harm,.04f+.30f*harm*harm};
 for(int j=0;j<3;j++){
  x[j]=v.choir_phase[j];y[j]=v.choir_frequency[j];
  float hz=f*ratio[j],r=1.f/(1.f+1.f/(MD_RATE*seconds*(j==0?1.f:j==1?.42f:.23f)));
  co[j]=r*plaits::Sine(.25f+fminf(.45f,hz));si[j]=r*plaits::Sine(fminf(.45f,hz));
  if(trigger){float q=y[j]-v.velocity*weight[j]*partial_gain(hz);y[j]=q/(1.f+.12f*fabsf(q));}
  decay[j]=hz<.45f?1.f:0.f;
 }
 float burst_decay=1.f/(1.f+1.f/(MD_RATE*(.0015f+.008f*shape)));
 float burst=(.015f+.12f*tone)*(.25f+.75f*harm),noise_lp=v.noise_lp,excite=v.excite;
 for(unsigned k=0;k<n;k++){
  float body=0;
  for(int j=0;j<3;j++){
   float nx=co[j]*x[j]-si[j]*y[j];y[j]=si[j]*x[j]+co[j]*y[j];x[j]=nx;
   body+=nx*decay[j];
  }
  // A short, gently low-passed skin contact sits above the body, never a
  // continuous hiss. It remains responsive to touch without a hard click.
  noise_lp+=(noise()-noise_lp)*(.12f+.55f*tone);excite*=burst_decay;
  out[k]=.65f*body+noise_lp*excite*burst*v.velocity;
 }
 float energy=0;for(int j=0;j<3;j++){v.choir_phase[j]=x[j];v.choir_frequency[j]=y[j];energy+=fabsf(x[j])+fabsf(y[j]);}
 v.noise_lp=noise_lp;v.excite=excite;v.env=fminf(1.f,energy*.5f);
 if(energy<1e-6f&&excite<1e-6f)v.active=0;
}
