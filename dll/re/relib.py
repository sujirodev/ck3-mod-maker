# Static reading of ck3.exe (read-only). Usage: from relib import *
import struct,re,bisect
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
EXE=r'C:\CK3\binaries\ck3.exe'
d=open(EXE,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]; optsz=struct.unpack_from('<H',d,pe+20)[0]
BASE=struct.unpack_from('<Q',d,pe+24+24)[0]
secs=[]
o=pe+24+optsz
for i in range(nsec):
    name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8)
    secs.append((name,va,vs,ro,rs)); o+=40
S={n:(va,vs,ro,rs) for n,va,vs,ro,rs in secs}
def off2rva(off):
    for n,va,vs,ro,rs in secs:
        if ro<=off<ro+rs: return va+off-ro
def rva2off(rva):
    for n,va,vs,ro,rs in secs:
        if va<=rva<va+rs: return ro+rva-va
TVA,TVS,TRO,TRS=S['.text']
# .pdata: function bounds
pva,pvs,pro,prs=S['.pdata']
funcs=[struct.unpack_from('<III',d,pro+i*12) for i in range(pvs//12)]
funcs=[f for f in funcs if f[0]]
starts=[f[0] for f in funcs]
def func_of(rva):
    i=bisect.bisect_right(starts,rva)-1
    if i>=0 and funcs[i][0]<=rva<funcs[i][1]: return funcs[i][0],funcs[i][1]
    return None
md=Cs(CS_ARCH_X86,CS_MODE_64)
def dis(rva,end=None,maxn=400):
    f=func_of(rva)
    if end is None: end=f[1] if f else rva+0x200
    out=[]
    for ins in md.disasm(d[rva2off(rva):rva2off(rva)+(end-rva)],rva):
        out.append('%08x  %-7s %s'%(ins.address,ins.mnemonic,ins.op_str))
        if len(out)>=maxn: break
    return '\n'.join(out)
def xref_rip(targets):
    """instructions with a RIP-relative disp32 pointing to the given RVAs (lea/mov/cmp...)"""
    targets=set(targets); res={t:[] for t in targets}
    text=d[TRO:TRO+TRS]
    for t in targets:
        pass
    # scans by plausible disp range: testing every offset is slow; uses the rip-relative ModRM pattern
    for m in re.finditer(rb'[\x48\x4c][\x8d\x8b\x89\x3b\x39][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',text,re.S):
        p=m.start(); disp=struct.unpack_from('<i',text,p+3)[0]
        t=TVA+p+7+disp
        if t in targets: res[t].append(TVA+p)
    return res
def xref_call(targets):
    targets=set(targets); res={t:[] for t in targets}
    text=d[TRO:TRO+TRS]
    for m in re.finditer(rb'[\xe8\xe9]',text):
        p=m.start()
        if p+5>len(text): break
        t=TVA+p+5+struct.unpack_from('<i',text,p+1)[0]
        if t in targets: res[t].append(TVA+p)
    return res
def cstr(rva,n=80):
    o=rva2off(rva); e=d.find(b'\0',o,o+n); return d[o:e if e>0 else o+n].decode('latin1')

def xref_any(targets):
    """Full scan of .text: any RIP-relative disp32 (end of the field = end of the instruction or +1..+4)
    that points to one of the RVAs. Returns {target: [rva of the disp field]}. Uses numpy."""
    import numpy as np
    text = np.frombuffer(d, dtype=np.uint8, count=TRS, offset=TRO)
    res = {t: [] for t in targets}
    n = TRS - 8
    pos = np.arange(n, dtype=np.int64)
    disp = (text[:n].astype(np.int64) | (text[1:n+1].astype(np.int64) << 8) |
            (text[2:n+2].astype(np.int64) << 16) | (text[3:n+3].astype(np.int64) << 24))
    disp = np.where(disp >= 2**31, disp - 2**32, disp)
    base_next = TVA + pos + 4
    for t in targets:
        for extra in (0, 1, 4):   # instructions with a 1- or 4-byte immediate after the disp
            hit = np.nonzero(base_next + extra + disp == t)[0]
            res[t] += [(int(TVA + h), extra) for h in hit]
    return res
