# desmontagem anotada (strings e destinos de dados): python re_show.py <rva> [max]
import sys,re as _re; sys.path.insert(0,'.')
from relib import *
a=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 400
f=func_of(a); end=f[1] if f else a+0x400
print('== func',[hex(x) for x in f] if f else None)
for ins in md.disasm(d[rva2off(a):rva2off(end)],a):
    s='%08x  %-7s %s'%(ins.address,ins.mnemonic,ins.op_str)
    m=_re.search(r'rip \+ 0x([0-9a-f]+)',ins.op_str)
    if m:
        t=ins.address+ins.size+int(m.group(1),16); s+='  ; ->'+hex(t)
        if rva2off(t) is not None:
            c=cstr(t,60)
            if len(c)>3 and all(32<=ord(ch)<127 for ch in c): s+=' "%s"'%c
    print(s); n-=1
    if n<=0: break
