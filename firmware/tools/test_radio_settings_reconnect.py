#!/usr/bin/env python3
"""Execute the rendered radio-save client under a deterministic offline browser port."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
FW=Path(__file__).resolve().parents[1]
NODE=r'''
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync(process.argv[2],'utf8');
async function scenario(mode){
 let clock=0,next=1,handler,finished=false,ready=false,polls=0,saves=0,navigation=null;
 const timers=new Map(),buttons=[{disabled:false}],inputs=[{name:'rf0',id:'rf0'},{name:'rf1',id:'rf1'}];
 const status={hidden:true,textContent:'',children:[],appendChild(x){this.children.push(x);return x;},append(...xs){this.children.push(...xs);}};
 const fields=mode==='network'?[['static','1'],['ip','192.0.2.44']]:[['radio_form','1'],['rf0','1']];
 const form={fields,dataset:{rebootSave:mode==='network'?'network':'radios'},action:mode==='network'?'http://device/network':'http://device/radios',addEventListener(n,f){assert.equal(n,'submit');handler=f;},querySelectorAll(q){return q==='button'?buttons:inputs;}};
 class Data{constructor(f){this.rows=f.fields;}has(k){return this.rows.some(x=>x[0]===k);}get(k){return this.rows.find(x=>x[0]===k)?.[1]??null;}[Symbol.iterator](){return this.rows[Symbol.iterator]();}}
 class Clock extends Date{static now(){return clock;}}
 const location={origin:'http://device',protocol:'http:',replace(url){if(mode!=='network')assert(ready,'radio-save client redirected before confirmed modem readiness');navigation=url;}};
 const context={console,FormData:Data,URLSearchParams,URL,AbortController,Date:Clock,location,
  document:{querySelectorAll(q){assert.equal(q,'form[data-reboot-save]');return [form];},getElementById(){return status;},createElement(tag){return {tag,href:'',textContent:''};}},
  setTimeout(cb,ms){const id=next++;timers.set(id,{at:clock+ms,cb});return id;},clearTimeout(id){timers.delete(id);},
  async fetch(url,options={}){
   if(options.method==='POST'){++saves;return {ok:mode!=='save-fails',status:mode==='save-fails'?500:200,text:async()=>mode==='save-fails'?'store failed':'{"status":"saved","rebooting":true}'};}
   ++polls;assert.equal(new URL(url,'http://device').pathname,'/api/radios');assert.equal(options.cache,'no-store');assert.equal(options.credentials,'same-origin');
   if(mode==='unreachable')throw new TypeError('offline');
   if(mode==='hung')return new Promise((_,reject)=>{assert(options.signal,'reconnect fetch needs a finite request deadline');options.signal.addEventListener('abort',()=>reject(new Error('AbortError')),{once:true});});
   if(polls===1)throw new TypeError('restarting');
   if(polls===2)return {ok:true,json:async()=>[{id:0,enabled:false},{id:1,enabled:false}]};
   ready=true;return {ok:true,json:async()=>[{id:0,enabled:true},{id:1,enabled:false}]};
  }};
 vm.runInNewContext(script,context);assert(handler,'real rendered script must register its submit handler');
 const pending=handler({preventDefault(){}}).then(()=>{finished=true;});
 for(let count=0;count<1000;++count){
  await new Promise(resolve=>setImmediate(resolve));
  if(finished&&timers.size===0)break;
  if(timers.size){const [id,t]=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];timers.delete(id);clock=t.at;t.cb();}
  else if(!finished)throw new Error('client promise stalled without a bounded timer');
 }
 await pending;assert(finished);assert.equal(saves,1,'reconnection must never repeat the mutation');
 if(mode==='recover'){assert(polls>=3);assert.equal(navigation,'http://device/');}
 else if(mode==='network'){assert.equal(navigation,'http://192.0.2.44/');assert.equal(polls,0);}
 else if(mode==='save-fails'){assert.equal(polls,0);assert.equal(navigation,null);assert(status.textContent.includes('Save failed'));assert.equal(buttons[0].disabled,false);}
 else {assert(polls>0);assert.equal(navigation,null);assert(clock>=60000&&clock<=120000,'bounded retry window required');assert(status.children.some(x=>x.href==='http://device/'),'timed-out reconnect must retain a manual destination');}
 console.log('PASS '+mode+': polls='+polls+' mutation_calls='+saves+' redirect='+navigation);
}
(async()=>{for(const mode of ['recover','unreachable','hung','save-fails','network'])await scenario(mode);})().catch(e=>{console.error(e);process.exitCode=1;});
'''
def main():
 with tempfile.TemporaryDirectory(prefix='radio-reconnect-',dir=os.environ.get('TMPDIR')) as d:
  out=Path(d);exe=out/'render';html=out/'root.html'
  subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(FW/'include'),str(FW/'src/webui_shared.cpp'),str(FW/'tests/webui_radio_endpoints_test.cpp'),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(html)],check=True,stdout=subprocess.DEVNULL)
  scripts=re.findall(r'<script>(.*?)</script>',html.read_text(),re.S)
  assert len(scripts)==1,'expected the actual shared save/reboot script'
  client=out/'client.js';client.write_text(scripts[0]);harness=out/'harness.cjs';harness.write_text(NODE)
  subprocess.run(['node','--check',str(client)],check=True)
  subprocess.run(['node',str(harness),str(client)],check=True,timeout=20)
if __name__=='__main__':main()
