Portuguese version: [README.pt-BR.md](README.pt-BR.md)

# Proof of the C source plugin (TinyCC), 2026-10-04

Results in docs/arquitetura_dll.md, section 7c.

## TinyCC used
`https://repo.or.cz/tinycc.git`, commit `43c7708b85681a2fd4451c8a541af4494a8919b2` (version 0.9.28rc).
`win32/build-tcc.bat` did not run from the terminal; built by hand, in the `win32` folder, with WinLibs GCC:

```
echo '#define TCC_VERSION "0.9.28rc"' > ../config.h
gcc -O2 -s -static -o tcc.exe ../tcc.c -DTCC_TARGET_PE -DTCC_TARGET_X86_64
gcc -O2 -c -o libtcc.o ../libtcc.c -DTCC_TARGET_PE -DTCC_TARGET_X86_64
mkdir include lib && cp ../include/*.h ../tcclib.h include/
# objetos do libtcc1.a: os mesmos do :make_lib do build-tcc.bat, com ./tcc.exe -B. -c <arquivo>
./tcc.exe -ar lib/libtcc1.a libtcc1.o crt1.o crt1w.o wincrt1.o wincrt1w.o dllcrt1.o dllmain.o winex.o chkstk.o alloca.o alloca-bt.o stdatomic.o atomic.o builtin.o
```

## Files
| File | Role |
|---|---|
| `ck3_api.h` | minimal interface for the proof |
| `host.c` + `plugin.c` + `bench.c` + `plugin_win.c` | proof outside the game (`gcc -O2 -o host.exe host.c libtcc.o -I<tinycc> -DTCC_TARGET_PE -DTCC_TARGET_X86_64`, `host.exe <pasta win32>`) |
| `core_prova.diff` | what was added to `ar_core.c` for the in-game proof (request `tcc <arquivo.c>`; `auto_raid_ping_livre` goes to the plugin) |
| `plugin_jogo.c` | plugin for the in-game proof |

In-game proof: the core with embedded libtcc came to 486 KB; TinyCC support files (`include/` and
`lib/libtcc1.a`) in `C:\CK3\binaries\ck3`. After the proof the original core was restored and the `ck3` folder deleted.
