# Fase B, B1/B2: registro e anatomia de efeitos (somente leitura)
from relib import *
def vts(cls,nf=24):
    out=[]
    i=d.find(b'.?AV'+cls+b'@@\0')
    if i<0: return out
    td=off2rva(i-0x10)
    for m in re.finditer(re.escape(struct.pack('<I',td)),d):
        col=m.start()-0xC
        sig,offs,cd,tdr,cdr,selfr=struct.unpack_from('<IIIIII',d,col)
        if sig==1 and selfr==off2rva(col):
            for m2 in re.finditer(re.escape(struct.pack('<Q',BASE+selfr)),d):
                vt=m2.start()+8; fns=[]
                for k in range(nf):
                    f=struct.unpack_from('<Q',d,vt+8*k)[0]
                    if not (BASE+TVA<=f<BASE+TVA+TVS): break
                    fns.append(f-BASE)
                out.append((off2rva(vt),offs,fns))
    return out
def strrva(s):
    return [off2rva(m.start()+1) for m in re.finditer(re.escape(b'\0'+s+b'\0'),d)]
