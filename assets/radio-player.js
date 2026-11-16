/* Public listener only. No DJ credentials or control of the broadcast. */
(() => {
 'use strict';
 const STREAM='https://panel.radiocast.net/listen/music-for-streams-radio-192/radio.mp3';
 const NOW='https://panel.radiocast.net/api/nowplaying/music-for-streams-radio-192';
 const OWNER='mutable-dreams-audio-focus-v1', PREF='mutable-dreams-radio-v1';
 const id=globalThis.crypto?.randomUUID?.() || Math.random().toString(36).slice(2);
 const read=(key,fallback)=>{try{return JSON.parse(localStorage.getItem(key))||fallback;}catch{return fallback;}};
 const write=(key,value)=>{try{localStorage.setItem(key,JSON.stringify(value));}catch{}};
 const prefs=read(PREF,{volume:.32,paused:false});
 const audio=document.getElementById('radio-audio') || new Audio(); audio.preload='none'; audio.src=STREAM;
 audio.volume=Number.isFinite(prefs.volume)?Math.max(0,Math.min(1,prefs.volume)):.32;
 let transport=false, owned=false, pending=0, destroyed=false, managedPlay=false, internalPauseEvents=0;
 let channel;try{channel=new BroadcastChannel('mutable-dreams-audio-focus-v1');}catch{}
 const panel=document.createElement('aside');panel.className='md-radio';panel.setAttribute('aria-label','Mutable Dreams FM radio player');
 panel.innerHTML='<div class="md-radio-top"><span class="md-radio-sprout" aria-hidden="true">❧</span><div><a href="radio.html">Mutable Dreams FM</a><span class="md-radio-title">A garden of generative music</span></div><button class="md-radio-toggle" type="button" aria-label="Play Mutable Dreams FM">Listen</button><button class="md-radio-fold" type="button" aria-label="Minimise radio player" aria-expanded="true">−</button></div><div class="md-radio-bottom"><p class="md-radio-status" role="status">Connecting to the public radio…</p><label>Volume <input aria-label="Radio volume" type="range" min="0" max="100" value="'+Math.round(audio.volume*100)+'"></label></div><p class="md-radio-directories"><a href="https://tunein.com/radio/Mutable-Dreams-FM-s364904/" aria-label="Listen to Mutable Dreams FM on TuneIn">Listen on TuneIn ↗</a></p>';
 document.body.append(panel); document.body.classList.add('has-md-radio');
 const status=panel.querySelector('.md-radio-status'),toggle=panel.querySelector('.md-radio-toggle');
 const save=()=>write(PREF,{volume:audio.volume,paused:prefs.paused});
 const lease=()=>read(OWNER,null);
 const remoteInstrument=()=>{const f=lease();return f&&f.id!==id&&f.kind==='instrument'&&f.until>Date.now();};
 function announce(kind){const f={id,kind,until:Date.now()+45000};write(OWNER,f);channel?.postMessage(f);}
 function paint(message){status.textContent=message;toggle.textContent=audio.paused?'Listen':'Pause';toggle.setAttribute('aria-label',audio.paused?'Play Mutable Dreams FM':'Pause Mutable Dreams FM');panel.classList.toggle('md-radio-playing',!audio.paused);}
 function pause(message,manual=false){++pending;if(!audio.paused)internalPauseEvents++;audio.pause();owned=false;releaseFocus('radio');if(manual){prefs.paused=true;save();}paint(message);}
 function claimInstrument(){pause('Radio paused for your instrument. Press Listen to return.');announce('instrument');}
 function releaseFocus(kind){const f=lease();if(f?.id===id&&(!kind||f.kind===kind))try{localStorage.removeItem(OWNER);}catch{}}
 function instrumentState(playing){transport=!!playing;if(transport)claimInstrument();else if(lease()?.id===id&&lease()?.kind==='instrument')releaseFocus();}
 async function play(manual=false){
  if(transport||remoteInstrument()){paint('Stop the instrument before listening to the radio.');return false;}
  if(!manual&&prefs.paused){paint('Radio paused. Press Listen whenever you like.');return false;}
  const current=lease();if(!manual&&current&&current.id!==id&&current.until>Date.now()){paint('Radio is already active in another Mutable Dreams tab.');return false;}
  const request=++pending;announce('radio');
  try{
   managedPlay=true;await audio.play();managedPlay=false;
   if(request!==pending||destroyed){pause('Radio paused for the current audio source.');return false;}
   owned=true;prefs.paused=false;save();announce('radio');paint('Live radio · independent of the browser instrument');return true;
  }catch(error){
   managedPlay=false;
   if(request!==pending)return false;
   releaseFocus();
   paint(error?.name==='NotAllowedError'?'Press Listen to start the radio. Your browser needs one click.':'Radio connection unavailable. Press Listen to try again.');return false;
  }
 }
 toggle.addEventListener('click',()=>audio.paused?play(true):pause('Radio paused. Press Listen to return.',true));
 panel.querySelector('input').addEventListener('input',event=>{audio.volume=+event.target.value/100;save();});
 panel.querySelector('.md-radio-fold').addEventListener('click',event=>{const folded=panel.classList.toggle('md-radio-folded');event.currentTarget.textContent=folded?'+':'−';event.currentTarget.setAttribute('aria-expanded',String(!folded));event.currentTarget.setAttribute('aria-label',folded?'Expand radio player':'Minimise radio player');});
 const focus=message=>{if(message&&message.id!==id&&message.until>Date.now()&&lease()?.id===message.id)pause(message.kind==='instrument'?'Radio paused for the instrument in another tab.':'Radio is playing in another Mutable Dreams tab.');};
 if(channel)channel.onmessage=event=>focus(event.data);
 window.addEventListener('storage',event=>{if(event.key===OWNER)try{focus(JSON.parse(event.newValue));}catch{}});
 // Capture before the instrument's own handler starts sound, including the real FM-1 surface.
 document.addEventListener('pointerdown',event=>{if(event.target.closest('#power,#play,#dream-start,#device-connect,#fm1-surface [data-hw],#fm1-surface [data-key],#fm1-surface .fm1-key,[data-key],.engine-select,.tone-select,#macros input'))claimInstrument();},true);
 document.addEventListener('click',event=>{if(event.target.closest('#power,#play,#dream-start,#device-connect'))claimInstrument();},true);
 document.addEventListener('play',event=>{if(event.target!==audio)claimInstrument();},true);
 window.addEventListener('blur',()=>{if(document.activeElement?.tagName==='IFRAME')claimInstrument();});
 audio.addEventListener('play',()=>{if(managedPlay)return;if(transport||remoteInstrument()){pause('Stop the instrument before listening to the radio.');return;}owned=true;prefs.paused=false;save();announce('radio');paint('Live radio · independent of the browser instrument');});
 audio.addEventListener('pause',()=>{if(internalPauseEvents){internalPauseEvents--;return;}owned=false;releaseFocus();prefs.paused=true;save();paint('Radio paused. Press Listen to return.');});
 audio.addEventListener('error',()=>pause('Radio connection unavailable. Press Listen to try again.'));
 audio.addEventListener('waiting',()=>{if(!audio.paused)paint('Buffering the public radio…');});
 audio.addEventListener('playing',()=>{if(owned)paint('Live radio · independent of the browser instrument');});
 const heartbeat=setInterval(()=>{if(transport)announce('instrument');else if(owned&&!audio.paused)announce('radio');},15000);
 async function nowPlaying(){try{const response=await fetch(NOW,{cache:'no-store'});if(!response.ok)return;const data=await response.json();const song=data?.now_playing?.song;panel.querySelector('.md-radio-title').textContent=song?.title||song?.text||'A garden of generative music';}catch{}}
 nowPlaying();const metadata=setInterval(nowPlaying,30000);
 window.addEventListener('pagehide',event=>{pause('Radio paused.');releaseFocus();if(!event.persisted){destroyed=true;clearInterval(heartbeat);clearInterval(metadata);channel?.close();}});
 window.addEventListener('pageshow',event=>{if(event.persisted){destroyed=false;paint('Radio paused. Press Listen to return.');}});
 window.MutableDreamsRadio=Object.freeze({claimInstrument,instrumentState,play:()=>play(true),pause:()=>pause('Radio paused.',true),get paused(){return audio.paused;}});
 play(false);
})();

