// {{titulo}} - plugin C (compilado pelo nucleo ck3_custom com o TinyCC dentro do jogo). Fala com o jogo so pela
// ck3_api (dll/ck3_api.h), por ids. Valores de script em ponto fixo: 100000 = 1.
#include <string.h>
#include "ck3_api.h"

static const ck3_api *A;
#define UM 100000

// {{prefixo}}_ping = <valor>: grava {{prefixo}}_resp = 2 * valor no escopo atual
static int on_effect(const char *name, uint32_t st, uint32_t id, int64_t value, void *ctx, uint32_t thread)
{
	if (!strcmp(name, "{{prefixo}}_ping")) {
		A->var_set((uint16_t)st, id, "{{prefixo}}_resp", value * 2, 0);
		A->log("{{prefixo}}_ping: escopo 0x%x/%u valor %.3f", st, id, value / (double)UM);
		return 1;
	}
	return 0;
}

// pedido pelo arquivo (python -m ck3ctl req "{{nome}} ola")
static int on_command(char **argv, int argc)
{
	if (argc < 1 || strcmp(argv[0], "{{nome}}"))
		return 0;
	A->log("{{nome}}: jogador=%u, %d argumento(s)", A->player(), argc - 1);
	return 1;
}

static void on_stop(void) {}

static const ck3_plugin PLUGIN = { CK3_API_VERSION, on_effect, on_command, on_stop };

const ck3_plugin *ck3_plugin_start(const ck3_api *api)
{
	A = api;
	return &PLUGIN;
}
