// Auto Raid - interface entre o carregador (winmm.dll, fixo) e o nucleo (auto_raid_core.dll, recarregavel).
#ifndef AR_HOST_H
#define AR_HOST_H
#include <stdint.h>

#define AR_API_VERSION 3

typedef struct ar_host {
	uint32_t version;
	uint8_t *base;                 // base do ck3.exe
	uint32_t main_thread;          // thread principal do jogo
	const char *dir;               // pasta da DLL, com barra no fim
	void (*log)(const char *fmt, ...);
	// envia um comando pelo despachante do jogo (passa pelo registro); so na thread principal
	uint8_t (*post)(void *cmd, int flags);
	// poe uma linha de pedido (o mesmo formato de auto_raid_cmd.txt) na fila da thread principal; qualquer thread
	void (*run_main)(const char *line);
} ar_host;

typedef struct ar_core {
	uint32_t version;
	// chamado em QUALQUER thread, antes do envio de cada comando do jogo
	void (*on_post)(void *cmd, int flags, uint32_t thread);
	// chamado na thread principal, com uma linha de pedido de auto_raid_cmd.txt
	void (*on_command)(const char *line);
	// chamado na thread em que o script roda, quando um efeito auto_raid_* e executado.
	// scope_type/scope_id: escopo atual (0x1b = exercito, id do exercito); value: argumento em ponto fixo (100000 = 1)
	void (*on_effect)(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
	                  uint32_t thread);
} ar_core;

// exportadas pelo nucleo
typedef const ar_core *(*ar_start_fn)(const ar_host *host);
typedef void (*ar_stop_fn)(void);

#endif
