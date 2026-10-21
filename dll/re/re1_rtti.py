import struct,re,sys,hashlib
d=open(r'C:\CK3\binaries\ck3.exe','rb').read()
print('sha256',hashlib.sha256(d).hexdigest(),'size',len(d))
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]; optsz=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<Q',d,pe+24+24)[0]
print('imagebase',hex(base))
secs=[]
o=pe+24+optsz
for i in range(nsec):
    name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8)
    secs.append((name,va,vs,ro,rs)); print(name,hex(va),hex(vs),hex(ro),hex(rs)); o+=40
def off2rva(off):
    for n,va,vs,ro,rs in secs:
        if ro<=off<ro+rs: return va+off-ro
def rva2off(rva):
    for n,va,vs,ro,rs in secs:
        if va<=rva<va+max(vs,rs): return ro+rva-va
def vtables(cls):
    out=[]
    name=b'.?AV'+cls+b'@@\0'
    i=d.find(name)
    if i<0: print(cls,'no rtti'); return out
    td=off2rva(i-0x10)
    for m in re.finditer(re.escape(struct.pack('<I',td)),d):
        col=m.start()-0xC
        sig,offs,cd,tdr,cdr,selfr=struct.unpack_from('<IIIIII',d,col)
        if sig==1 and selfr==off2rva(col):
            colva=base+selfr
            for m2 in re.finditer(re.escape(struct.pack('<Q',colva)),d):
                vt=m2.start()+8
                fns=[]
                for k in range(12):
                    f=struct.unpack_from('<Q',d,vt+8*k)[0]
                    if not (base<f<base+0x10000000): break
                    fns.append(hex(f-base))
                out.append((hex(off2rva(vt)),offs,fns))
    print(cls.decode(),'TD rva',hex(td),out)
    return out
for c in [b'CMoveUnitCommand',b'CToggleCompanionAICommand',b'CSetCompanionAISettingsCommand',b'CRaidIntentCommand',b'CCommandDispatcher',b'CCommand']:
    vtables(c)
for m in re.finditer(rb'\.\?AV[A-Za-z0-9_@$?]*(MoveUnit|UnitOrder|HaltUnit|Halt)[A-Za-z0-9_@$?]*@@',d):
    print(m.group().decode())
for s in [b'commanddispatcher.cpp',b'move_unit\0',b'unit_order_type\0',b'gathering_army\0']:
    for m in re.finditer(re.escape(s),d): print(s,hex(off2rva(m.start()) or 0))
