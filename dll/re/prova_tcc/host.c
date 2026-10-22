// proof: host with libtcc embedded (plays the role of the loader)
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>
#include "libtcc.h"
#include "ck3_api.h"
#include "bench.c"

static char tccdir[MAX_PATH];
static void hlog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("  [log] ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}
static int64_t last_var;
static int hset_var(void *ctx, const char *name, int64_t v) { last_var = v; printf("  [var] %s = %lld\n", name, (long long)v); return 1; }
static int heta(uint32_t unit, uint32_t prov) { return 12; }
static const ck3_api API = { 1, hlog, hset_var, heta };

static void on_err(void *o, const char *msg) { printf("  [tcc] %s\n", msg); }

static double now_ms(void)
{
	LARGE_INTEGER c, f;
	QueryPerformanceCounter(&c);
	QueryPerformanceFrequency(&f);
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

// compiles the plugin; NULL on failure (the caller keeps the previous one)
static TCCState *compile(const char *file, int nostdlib, const ck3_plugin **out, double *ms)
{
	double t0 = now_ms();
	TCCState *s = tcc_new();
	char p[MAX_PATH];
	tcc_set_lib_path(s, tccdir);
	tcc_set_error_func(s, NULL, on_err);
	snprintf(p, sizeof p, "%s/include", tccdir);
	tcc_add_sysinclude_path(s, p);
	if (nostdlib)
		tcc_set_options(s, "-nostdlib");
	tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
	if (tcc_add_file(s, file) < 0) {
		tcc_delete(s);
		return NULL;
	}
	if (nostdlib) {
		// only what the host allows: nothing from the Windows API
		tcc_add_symbol(s, "memset", memset);
		tcc_add_symbol(s, "memcpy", memcpy);
		tcc_add_symbol(s, "memmove", memmove);
		snprintf(p, sizeof p, "%s/lib/libtcc1.a", tccdir);
		tcc_add_file(s, p);
	}
	if (tcc_relocate(s) < 0) {
		tcc_delete(s);
		return NULL;
	}
	const ck3_plugin *(*start)(const ck3_api *) = tcc_get_symbol(s, "ck3_plugin_start");
	if (!start) {
		printf("  sem ck3_plugin_start\n");
		tcc_delete(s);
		return NULL;
	}
	*out = start(&API);
	*ms = now_ms() - t0;
	return s;
}

static void edit(const char *src, const char *dst, const char *from, const char *to)
{
	static char buf[65536];
	FILE *f = fopen(src, "rb");
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	char *at = strstr(buf, from);
	f = fopen(dst, "wb");
	fwrite(buf, 1, at - buf, f);
	fputs(to, f);
	fputs(at + strlen(from), f);
	fclose(f);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	snprintf(tccdir, sizeof tccdir, "%s", argv[1]);
	const ck3_plugin *pl = NULL, *pl2 = NULL;
	double ms;

	printf("1) compilar plugin.c em memoria (com a CRT)\n");
	TCCState *s = compile("plugin.c", 0, &pl, &ms);
	printf("   %s em %.1f ms\n", s ? "OK" : "FALHOU", ms);
	if (!s) return 1;
	pl->on_effect("auto_raid_ping", 0x1b, 42, 300000, NULL, 0);

	printf("2) compilar sem a biblioteca do sistema (-nostdlib, so a ck3_api)\n");
	TCCState *s2 = compile("plugin.c", 1, &pl2, &ms);
	printf("   %s em %.1f ms\n", s2 ? "OK" : "FALHOU", ms);
	if (s2) { pl2->on_effect("auto_raid_ping", 0x1b, 42, 100000, NULL, 0); tcc_delete(s2); }

	printf("3) recarga: VERSAO 1 -> 2\n");
	edit("plugin.c", "plugin_v2.c", "#define VERSAO 1", "#define VERSAO 2");
	TCCState *s3 = compile("plugin_v2.c", 1, &pl2, &ms);
	if (s3) {
		tcc_delete(s);   // discards the previous one only after the new one compiles
		s = s3;
		pl = pl2;
		printf("   trocado em %.1f ms\n", ms);
		pl->on_effect("auto_raid_ping", 0x1b, 42, 100000, NULL, 0);
	}

	printf("4) erro de compilacao: mantem o plugin anterior\n");
	edit("plugin.c", "plugin_erro.c", "return &PLUGIN;", "return &PLUGIN  /* falta o ponto e virgula */");
	TCCState *s4 = compile("plugin_erro.c", 1, &pl2, &ms);
	printf("   novo: %s; anterior segue respondendo:\n", s4 ? "compilou (inesperado)" : "recusado");
	pl->on_effect("auto_raid_ping", 0x1b, 42, 100000, NULL, 0);

	printf("5) desempenho: Dijkstra 120x120 x 40\n");
	double t0 = now_ms();
	int r1 = bench(40);
	double g = now_ms() - t0;
	t0 = now_ms();
	int r2 = pl->bench(40);
	double t = now_ms() - t0;
	printf("   gcc -O2: %.1f ms (%d)   tcc: %.1f ms (%d)   razao %.2fx\n", g, r1, t, r2, t / g);

	printf("6) compilar 20 vezes seguidas (tempo medio)\n");
	double sum = 0;
	for (int i = 0; i < 20; i++) {
		TCCState *x = compile("plugin.c", 1, &pl2, &ms);
		sum += ms;
		if (x) tcc_delete(x);
	}
	printf("   media %.1f ms\n", sum / 20);
	printf("7) plugin que tenta chamar ExitProcess sem a biblioteca do sistema\n");
	TCCState *s7 = compile("plugin_win.c", 1, &pl2, &ms);
	printf("   %s\n", s7 ? "COMPILOU (chamada liberada)" : "recusado");
	tcc_delete(s);
	return 0;
}
