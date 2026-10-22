// {{title}} - C plugin (compiled by the ck3_custom core with TinyCC inside the game). Talks to the game only through
// ck3_api (dll/ck3_api.h), by ids. Script values are fixed point: 100000 = 1.
#include <string.h>
#include "ck3_api.h"

static const ck3_api *A;
#define UM 100000

// {{prefix}}_ping = <value>: sets {{prefix}}_resp = 2 * value on the current scope
static int on_effect(const char *name, uint32_t st, uint32_t id, int64_t value, void *ctx, uint32_t thread)
{
	if (!strcmp(name, "{{prefix}}_ping")) {
		A->var_set((uint16_t)st, id, "{{prefix}}_resp", value * 2, 0);
		A->log("{{prefix}}_ping: scope 0x%x/%u value %.3f", st, id, value / (double)UM);
		return 1;
	}
	return 0;
}

// request through the file (uv run ck3ctl req "{{name}} hello")
static int on_command(char **argv, int argc)
{
	if (argc < 1 || strcmp(argv[0], "{{name}}"))
		return 0;
	A->log("{{name}}: player=%u, %d argument(s)", A->player(), argc - 1);
	return 1;
}

static void on_stop(void) {}

static const ck3_plugin PLUGIN = { CK3_API_VERSION, on_effect, on_command, on_stop };

const ck3_plugin *ck3_plugin_start(const ck3_api *api)
{
	A = api;
	return &PLUGIN;
}
