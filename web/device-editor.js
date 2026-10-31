// SPDX-License-Identifier: MIT OR GPL-3.0-only
// One in-flight request, no automatic replay of a write after a lost acknowledgement.
(()=>{
const {PB,Link}=globalThis.DreamsBackup;
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const pack=b=>{let out=[];for(let i=0;i<b.length;i+=7){let mask=0,part=b.slice(i,i+7);part.forEach((v,k)=>mask|=(v>>7)<<k);out.push(mask,...part.map(v=>v&127));}return out;};
const unpack=b=>{let out=[];for(let i=0;i<b.length;){let mask=b[i++];for(let k=0;k<7&&i<b.length;k++)out.push(b[i++]|((mask>>k&1)<<7));}return Uint8Array.from(out);};
const words=b=>{const d=new DataView(b.buffer,b.byteOffset,b.byteLength);return Array.from({length:b.length/4},(_,i)=>d.getInt32(i*4,true));};
const u32=n=>[n&255,n>>>8&255,n>>>16&255,n>>>24&255];
class Device{
 constructor(link,id){this.link=link;this.id=id;this.seq=0;this.tail=Promise.resolve();this.pending=0;this.closed=false;}
 static async connect(access){
  const dev=await PB.find(access,(i,o)=>new Link(i,o));const d=new Device(dev.link,dev.id);
  try{if(dev.id==='FM-1_710010000')throw Error('Mutable Dreams 1.0 detected. The live editor requires 1.4. Back up your current work and install 1.4 first, or use browser-only mode.');const cap=words(await d.call(7));if(cap[0]!==1||cap[1]!==9||cap[2]!==2288)throw Error('This FM-1 needs the matching Mutable Dreams 1.4 firmware.');await d.remote([5,1]);return d;}
  catch(e){dev.link.close();throw e;}
 }
 exclusive(fn){
  if(this.closed||this.link.lost)return Promise.reject(Error('The FM-1 is disconnected.'));
  if(this.pending>=64)return Promise.reject(Error('The editor is busy. Wait for the current transfer.'));
  this.pending++;const p=this.tail.then(()=>{if(this.closed||this.link.lost)throw Error('The FM-1 is disconnected.');return fn();});
  this.tail=p.catch(()=>{}).finally(()=>this.pending--);return p;
 }
 async _call(c,p=[]){
  this.seq=(this.seq+1)&127;const head=[0xf0,0x7d,0x4d,0x44,c,this.seq];
  if(!this.link.send([...head,...p,0xf7]))throw Error('The FM-1 disconnected.');
  const end=Date.now()+3000;
  while(Date.now()<end){const r=await this.link.read(Math.max(1,end-Date.now()));if(!r)continue;
   if(head.some((v,i)=>r[i]!==v)||r.at(-1)!==0xf7||r.length<8)continue;
   await delay(18);if(r[6]){const e=Error(({1:'Invalid editor request.',2:'The command queue is full.',3:'This user slot is empty.',4:'Stop playback and use internal clock first. Wait for any backup to finish.',5:'This slot is occupied. Confirm replacement.',6:'Invalid or unsupported data. Nothing loaded.',7:'The bank could not be saved.',8:'Unsaved Biome: choose Keep, Save or Discard on the panel.'})[r[6]]||'Device rejected the request.');e.status=r[6];throw e;}
   return unpack(r.slice(7,-1));
  }
  throw Error('No reply from the FM-1. A timed-out write is not retried; read the device before trying again.');
 }
 call(c,p=[]){return this.exclusive(()=>this._call(c,p));}
 command(c,t,v){if(!Number.isInteger(c)||c<0||c>=124||(c>=99&&c<120)||t<0||t>5||v< -8192||v>8191)throw Error('Invalid parameter.');const z=v+8192;return this.call(2,[c,t,z&127,z>>7]);}
 ui(c,t,v){const z=v+8192;return this.call(10,[c,t,z&127,z>>7]);}
 remote(p){return this.exclusive(async()=>{
  const head=[0xf0,0x7d,0x4c,0x43,p[0]];if(!this.link.send([0xf0,0x7d,0x4c,0x43,...p,0xf7]))throw Error('FM-1 disconnected.');
  const end=Date.now()+3000;while(Date.now()<end){const r=await this.link.read(end-Date.now());if(r&&r.length===6&&head.every((v,i)=>r[i]===v)){await delay(18);return;}}
  throw Error('No reply to panel control.');
 });}
 async state(){const b=await this.call(9),w=words(b.slice(0,184));return {type:'state',pending:b.length>=220?words(b.slice(208,220))[0]:-1,kitMode:b.length>=220?words(b.slice(208,220))[1]:0,bankPending:b.length>=220?words(b.slice(208,220))[2]:-1,buttonGlow:b.length>=224?words(b.slice(220,224))[0]:0,page:w[0],track:w[1],playing:w[2],octave:w[3],help:w[4],motion:w[5],biome:w[6],edited:w[7],tempo:w[8],locked:w[9],freeze:w[10],bootSound:w[11],bankSlot:w[12],bankKind:w[13],clock:w[14],quantize:w[15],models:w.slice(16,22),tones:w.slice(22,28),muted:w.slice(28,34),knobs:w.slice(34,38),raw:w.slice(38,42),macro:w.slice(42,46),bankName:String.fromCharCode(...b.slice(184,208)).split('\0')[0]};}
 snapshot(){return this.exclusive(async()=>{const h=words(await this._call(8)),b=new Uint8Array(h[0]);if(h[0]!==2288)throw Error('Unsupported patch size.');for(let off=0;off<b.length;off+=224){const part=await this._call(5,[off&127,off>>7,h[2]]);if(part.length!==Math.min(224,b.length-off))throw Error('Incomplete patch.');b.set(part,off);}if(PB.crc32(b)!==(h[1]>>>0))throw Error('The patch failed its checksum.');return Array.from(b);});}
 upload(bytes){return this.exclusive(async()=>{
  if(!Array.isArray(bytes)||bytes.length!==2288||bytes.some(v=>!Number.isInteger(v)||v<0||v>255))throw Error('Load older patches in the browser first, then transfer the current patch.');
  const token=(await this._call(11,pack([...u32(bytes.length),...u32(PB.crc32(bytes))])))[0];
  try{for(let off=0;off<bytes.length;off+=196)await this._call(12,[token,off&127,off>>7,...pack(bytes.slice(off,off+196))]);await this._call(13,[token]);}
  catch(e){try{await this._call(14,[token]);}catch{}throw e;}
 });}
 bank(kind,slot,track,action,name=''){const b=Array.from({length:24},(_,i)=>i<23?(name.replace(/[^ -~]/g,'').charCodeAt(i)||0):0);return this.call(15,[kind,slot,track,action,...(action>=2?b:[])]);}
 backup(progress){return this.exclusive(()=>PB.backup(this.link,progress));}
 restore(bytes,event){PB.parse(bytes);return this.exclusive(async()=>{const code=await PB.restore(this.link,bytes,event);if(code)throw Error(PB.ERRORS[code]||`Restore failed (${code}).`);});}
 async close(){if(this.closed)return;try{await this.remote([1,0,0]);await this.remote([2,0,0,0,0]);await this.remote([4,1,32]);await this.remote([5,0]);}finally{this.closed=true;this.link.close();}}
}
globalThis.DreamsDevice={Device,pack,unpack,words};
})();
