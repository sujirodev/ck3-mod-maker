# C1 (docs/arquitetura_dll.md, secao 11.6): registro de configuracoes do motor (somente leitura)
from relib import *
import re

def col_class(vt):
    """nome da classe pelo RTTI (Complete Object Locator logo antes da vtable)"""
    o = rva2off(vt - 8)
    if o is None:
        return None
    col = struct.unpack_from('<Q', d, o)[0] - BASE
    co = rva2off(col)
    if co is None:
        return None
    sig, offs, cd, tdr = struct.unpack_from('<IIII', d, co)
    to = rva2off(tdr)
    if to is None:
        return None
    e = d.find(b'\0', to + 0x10)
    return d[to + 0x10:e].decode('latin1')

def lea_targets(f0, f1):
    out = []
    for ins in md.disasm(d[rva2off(f0):rva2off(f1)], f0):
        if ins.mnemonic in ('lea', 'mov') and 'rip +' in ins.op_str:
            m = re.search(r'rip \+ (0x[0-9a-f]+)', ins.op_str)
            out.append((ins.address, ins.mnemonic, ins.op_str.split(',')[0], ins.address + ins.size + int(m.group(1), 16)))
    return out
