// proof: minimal interface between the core and the plugin compiled by libtcc
#include <stdint.h>
typedef struct ck3_api {
	uint32_t version;
	void (*log)(const char *fmt, ...);
	int (*set_var)(void *ctx, const char *name, int64_t fixed);
	int (*eta)(uint32_t unit, uint32_t prov);
} ck3_api;
typedef struct ck3_plugin {
	uint32_t version;
	void (*on_effect)(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
	                  uint32_t thread);
	int (*bench)(int n);
} ck3_plugin;
