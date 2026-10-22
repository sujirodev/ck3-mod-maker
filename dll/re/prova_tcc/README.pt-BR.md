Versao em ingles: [README.md](README.md)

# Prova do plugin em fonte C (TinyCC), 2026-10-04

Resultados em docs/arquitetura_dll.md, secao 7c.

## TinyCC usado
`https://repo.or.cz/tinycc.git`, commit `43c7708b85681a2fd4451c8a541af4494a8919b2` (versao 0.9.28rc). O
`win32/build-tcc.bat` nao rodou pelo terminal; compilado a mao, na pasta `win32`, com o GCC do WinLibs:

```
echo '#define TCC_VERSION "0.9.28rc"' > ../config.h
gcc -O2 -s -static -o tcc.exe ../tcc.c -DTCC_TARGET_PE -DTCC_TARGET_X86_64
gcc -O2 -c -o libtcc.o ../libtcc.c -DTCC_TARGET_PE -DTCC_TARGET_X86_64
mkdir include lib && cp ../include/*.h ../tcclib.h include/
# objetos do libtcc1.a: os mesmos do :make_lib do build-tcc.bat, com ./tcc.exe -B. -c <arquivo>
./tcc.exe -ar lib/libtcc1.a libtcc1.o crt1.o crt1w.o wincrt1.o wincrt1w.o dllcrt1.o dllmain.o winex.o chkstk.o alloca.o alloca-bt.o stdatomic.o atomic.o builtin.o
```

## Arquivos
| Arquivo | Papel |
|---|---|
| `ck3_api.h` | interface minima da prova |
| `host.c` + `plugin.c` + `bench.c` + `plugin_win.c` | prova fora do jogo (`gcc -O2 -o host.exe host.c libtcc.o -I<tinycc> -DTCC_TARGET_PE -DTCC_TARGET_X86_64`, `host.exe <pasta win32>`) |
| `core_prova.diff` | o que foi acrescentado ao `ar_core.c` para a prova no jogo (pedido `tcc <arquivo.c>`; `auto_raid_ping_livre` vai ao plugin) |
| `plugin_jogo.c` | plugin da prova no jogo |

Prova no jogo: o nucleo com o libtcc embutido ficou com 486 KB; arquivos de apoio do TinyCC (`include/` e
`lib/libtcc1.a`) em `C:\CK3\binaries\ck3`. Depois da prova o nucleo original foi restaurado e a pasta `ck3` apagada.
