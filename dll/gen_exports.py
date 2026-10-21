# Gera exports.h e proxy.def a partir da DLL do sistema que sera repassada.
# Uso: python gen_exports.py winmm
import subprocess,sys,re
name=sys.argv[1]
out=subprocess.run(['objdump','-p',r'C:\Windows\System32\%s.dll'%name],capture_output=True,text=True).stdout
sec=out.split('[Ordinal/Name Pointer] Table')[1]
names=re.findall(r'^\s*\[\s*\d+\]\s+(?:\+base\[\s*\d+\]\s+[0-9a-f]+\s+)?(\w+)\s*$',sec,re.M)
names=sorted(set(names))
BS=chr(92); NL=chr(10)
open('exports.h','w',newline=NL).write('#define REAL_DLL "%s.dll"'%name+NL+'#define EXPORTS(X) '+BS+NL+(' '+BS+NL).join(chr(9)+'X(%s)'%n for n in names)+NL)
open('proxy.def','w',newline=NL).write('LIBRARY %s'%name+NL+'EXPORTS'+NL+''.join(chr(9)+n+NL for n in names))
print(name,len(names),'exports')
