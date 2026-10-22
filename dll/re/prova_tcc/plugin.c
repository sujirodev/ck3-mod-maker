// proof: mod plugin in plain C, compiled in memory by the loader
#include "ck3_api.h"
#include "bench.c"
#define VERSAO 1
static const ck3_api *api;

static int iguais(const char *a, const char *b)
{
	while (*a && *a == *b) a++, b++;
	return *a == *b;
}

static void on_effect(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
                      uint32_t thread)
{
	// GCC extensions used in ar_core.c
	struct { uint32_t prov; int days; } cand[2] = { { 1, 5 }, { 2, 3 } };
	__typeof__(cand[0]) t = cand[0];
	cand[0] = cand[1];
	cand[1] = t;
	_Alignas(16) uint8_t buf[32];
	struct { const char *p; uint32_t len; } __attribute__((aligned(16))) txt = { name, 3 };
	int dest = ({ int x = api->eta(scope_id, 7336); x > 0 ? x : 0; });
	buf[0] = (uint8_t)dest;
	if (iguais(name, "auto_raid_ping")) {
		api->set_var(ctx, "ar_resp", value * 2 * VERSAO);
		api->log("plugin v%d: %s valor=%lld eta=%d troca=%u alinhado=%d", VERSAO, name, (long long)value, dest,
		         cand[0].prov, (int)(((uintptr_t)buf | (uintptr_t)&txt) & 15));
	}
}

static const ck3_plugin PLUGIN = { 1, on_effect, bench };

__declspec(dllexport) const ck3_plugin *ck3_plugin_start(const ck3_api *a)
{
	api = a;
	api->log("plugin v%d iniciado", VERSAO);
	return &PLUGIN;
}
