// Auto Raid - interface between the loader (winmm.dll, fixed) and the core (auto_raid_core.dll, reloadable).
#ifndef AR_HOST_H
#define AR_HOST_H
#include <stdint.h>

#define AR_API_VERSION 3

typedef struct ar_host {
	uint32_t version;
	uint8_t *base;                 // ck3.exe base
	uint32_t main_thread;          // game main thread
	const char *dir;               // DLL folder, with a trailing slash
	void (*log)(const char *fmt, ...);
	// sends a command through the game dispatcher (goes through the registry); main thread only
	uint8_t (*post)(void *cmd, int flags);
	// puts a request line (same format as auto_raid_cmd.txt) on the main thread queue; any thread
	void (*run_main)(const char *line);
} ar_host;

typedef struct ar_core {
	uint32_t version;
	// called on ANY thread, before each game command is sent
	void (*on_post)(void *cmd, int flags, uint32_t thread);
	// called on the main thread, with a request line from auto_raid_cmd.txt
	void (*on_command)(const char *line);
	// called on the thread where the script runs, when an auto_raid_* effect is executed.
	// scope_type/scope_id: current scope (0x1b = army, army id); value: fixed-point argument (100000 = 1)
	void (*on_effect)(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
	                  uint32_t thread);
} ar_core;

// exported by the core
typedef const ar_core *(*ar_start_fn)(const ar_host *host);
typedef void (*ar_stop_fn)(void);

#endif
