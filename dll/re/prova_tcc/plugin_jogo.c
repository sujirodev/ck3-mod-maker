// prova no jogo: plugin em C puro, compilado pelo nucleo com o libtcc
#include "../ck3_api.h"
#define VERSAO 1
static const ck3_api *api;
static void on_effect(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
                      uint32_t thread)
{
	uint32_t unit = (uint32_t)(value / 100000);
	int d = api->eta(unit, 7336);
	int ok = api->set_var(ctx, "ar_resp", (int64_t)d * 100000);
	api->log("plugin v%d: %s escopo=0x%x/%u unidade=%u -> eta ate 7336 = %d dias, ar_resp %s", VERSAO, name,
	         scope_type, scope_id, unit, d, ok ? "gravado" : "nao gravado");
}
static int nada(int n) { return n; }
static const ck3_plugin PLUGIN = { 1, on_effect, nada };
__declspec(dllexport) const ck3_plugin *ck3_plugin_start(const ck3_api *a)
{
	api = a;
	api->log("plugin v%d iniciado", VERSAO);
	return &PLUGIN;
}
