#!/usr/bin/env python3
"""Execute actual N=3/4 rendered save/reboot client: one mutation, bounded return."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
from test_nradio_management import build
from test_radio_settings_reconnect import NODE as ORIGINAL_NODE

def exact(source,old,new):
    assert source.count(old)==1,old
    return source.replace(old,new)

def harness():
    code=ORIGINAL_NODE
    code=exact(code,"const script=fs.readFileSync(process.argv[2],'utf8');", "const script=fs.readFileSync(process.argv[2],'utf8'),n=Number(process.argv[3]);")
    code=exact(code," const timers=new Map(),buttons=[{disabled:false}],inputs=[{name:'rf0',id:'rf0'},{name:'rf1',id:'rf1'}];", " const mask=mode==='recover-high'?(1<<(n-1)):mode==='recover-zero'?0:1|(1<<(n-1));\n const expected=Array.from({length:n},(_,i)=>({id:i,enabled:!!(mask&(1<<i))}));\n const wrong=expected.map(x=>({...x,enabled:x.id===n-1?!x.enabled:x.enabled}));\n const timers=new Map(),buttons=[{disabled:false}],inputs=expected.map(x=>({name:'rf'+x.id,id:'rf'+x.id}));")
    code=exact(code," const fields=mode==='network'?[['static','1'],['ip','192.0.2.44']]:[['radio_form','1'],['rf0','1']];", " const fields=[['radio_form','1'],...expected.filter(x=>x.enabled).map(x=>['rf'+x.id,'1'])];")
    code=exact(code,"if(options.method==='POST'){++saves;", "if(options.method==='POST'){assert.equal(new URL(url,'http://device').pathname,'/radios');const sent=new URLSearchParams(options.body);assert.equal(sent.get('radio_form'),'1');for(const x of expected)assert.equal(sent.has('rf'+x.id),x.enabled);++saves;")
    code=exact(code,"   if(polls===2)return {ok:true,json:async()=>[{id:0,enabled:false},{id:1,enabled:false}]};\n   ready=true;return {ok:true,json:async()=>[{id:0,enabled:true},{id:1,enabled:false}]};", "   if(mode==='wrong-mask'||polls<=4)return {ok:true,json:async()=>wrong};\n   ready=true;return {ok:true,json:async()=>expected};")
    code=exact(code,"if(mode==='recover'){assert(polls>=3);", "if(mode.startsWith('recover-')){assert(polls>=5);")
    code=exact(code,"['recover','unreachable','hung','save-fails','network']", "['recover-high','recover-zero','recover-sparse','wrong-mask','unreachable','hung','save-fails']")
    code=exact(code,"console.log('PASS '+mode+", "console.log('PASS N='+n+' '+mode+")
    return code

def main():
    with tempfile.TemporaryDirectory(prefix='nradio-reconnect-',dir=os.environ.get('TMPDIR')) as d:
        out=Path(d);fixture=out/'harness.cjs';fixture.write_text(harness())
        for n in (3,4):
            exe=build(out/str(n),n);html=out/f'root-{n}.html'
            subprocess.run([str(exe),'emit',str(html)],check=True)
            scripts=re.findall(r'<script>(.*?)</script>',html.read_text(),re.S)
            assert len(scripts)==1,'actual shared renderer script expected'
            client=out/f'client-{n}.js';client.write_text(scripts[0])
            subprocess.run(['node','--check',str(client)],check=True)
            command=['node',str(fixture),str(client),str(n)]
            print('RUN',' '.join(command),flush=True);subprocess.run(command,check=True,timeout=20)
    print('PASS 14 actual-rendered N=3/4 client scenarios')
if __name__=='__main__':main()
