// ck3_api: interface entre o nucleo generico (dll/ar_core.c) e os plugins de mod em fonte C (docs/arquitetura_dll.md,
// secao 5). O plugin nao le memoria do jogo: tudo passa por esta tabela, com ids do jogo (unidade, exercito, provincia,
// personagem) em vez de ponteiros. No jogo quem preenche e o nucleo; no simulador, o mundo simulado.
// Valores de script em ponto fixo: 100000 = 1. Tipos de escopo: 0x4 personagem, 0x8 provincia, 0x1b exercito.
#ifndef CK3_API_H
#define CK3_API_H
#pragma once
#include <stdint.h>

#define CK3_API_VERSION 7   // 2 (2026-10-07): game_files, file_read, file_free; 3: file_write; 4: county_attrition; 5: combat_info; 6: combat_hypo; 7: debug_mode
#define CK3_NONE 0xFFFFFFFFu   // id inexistente (exercito de unidade sem exercito etc.)

// (api 5) estado de um combate (combat_info)
typedef struct {
	uint32_t id, provincia;
	int lado_do_exercito, largura, contador, forcado;
	double vantagem, base, mult;
	uint32_t comandante[2];
	int rolagem[2], rmin[2], rmax[2];
	double lutando[2], total[2], v[2], v_cmd[2], v_mods[2];
} ck3_combat;

typedef struct ck3_api {
	uint32_t version;
	uint32_t main_thread;                                 // thread principal do jogo
	void (*log)(const char *fmt, ...);                    // linha no log da DLL
	double (*clock_ms)(void);                             // relogio em milissegundos (para medir)
	int (*recording)(void);                               // gravacao ligada (pedido `gravar`)?
	void (*rec)(const char *fmt, ...);                    // linha JSON na gravacao (nada com ela parada)
	uint32_t (*player)(void);                             // personagem do jogador
	// unidades (exercitos no mapa)
	uint32_t (*unit_count)(void);                         // tamanho da tabela de unidades
	uint32_t (*unit_at)(uint32_t i);                      // id da unidade valida no indice i, 0 = nenhuma
	int (*unit_exists)(uint32_t unit);
	uint32_t (*unit_owner)(uint32_t unit);                // personagem dono
	uint32_t (*unit_army)(uint32_t unit);                 // exercito da unidade, CK3_NONE sem exercito valido
	uint32_t (*unit_location)(uint32_t unit);             // provincia atual
	int (*unit_path)(uint32_t unit, uint32_t *out, int max);   // caminho em curso; devolve o tamanho total
	int (*unit_relation)(uint32_t unit);                  // 0 minha, 1 aliada, 2 inimiga, 3 hostil, 4 neutra, 5 nenhuma
	// exercitos
	uint32_t (*army_unit)(uint32_t army);                 // unidade do exercito, 0 se nao existe
	int64_t (*army_loot)(uint32_t army);                  // saque carregado
	int64_t (*army_loot_cap)(uint32_t army);              // capacidade de saque
	int (*army_is_raid)(uint32_t army);                   // exercito de saque (modo raid)
	int (*army_raid)(uint32_t army, uint32_t *prov, int64_t *done);   // saque em curso: 1 e local/progresso
	// provincias
	uint32_t (*prov_count)(void);                         // ids validos: 1 .. prov_count()-1
	int (*prov_exists)(uint32_t prov);
	int (*prov_xy)(uint32_t prov, int *x, int *y);        // posicao no mapa (pixels)
	int (*prov_is_sea)(uint32_t prov);
	// primitivas do jogo (gravadas na gravacao com argumentos e resultado)
	int (*eta)(uint32_t unit, uint32_t prov, uint32_t *path, int *path_n, int path_max);   // dias, -1 sem caminho
	int (*eta_from)(uint32_t unit, uint32_t from, uint32_t prov);   // dias de `from` ate `prov` na velocidade da unidade
	double (*predict)(const uint32_t *ours, int n1, const uint32_t *theirs, int n2);   // chance de A vencer (exercitos)
	int (*can_raid)(uint32_t army, uint32_t prov);
	int64_t (*prov_loot)(uint32_t prov);                  // saque disponivel
	int (*raid_days)(uint32_t army, uint32_t prov, int64_t done, int64_t *total, int64_t *per_day);
	// variaveis de script no objeto (tipo de escopo, id); dias <= 0 = sem prazo. var_get: 1 se existe, tipo 1 = numero
	int (*var_get)(uint16_t st, uint32_t id, const char *name, int64_t *v, uint16_t *type);
	int (*var_set)(uint16_t st, uint32_t id, const char *name, int64_t v, int days);
	int (*var_set_scope)(uint16_t st, uint32_t id, const char *name, uint16_t vst, uint32_t vid);
	int (*var_del)(uint16_t st, uint32_t id, const char *name);
	// expressao de script (valor ou `{ if = { limit = { ... } add = 1 } }`) avaliada pelo jogo no escopo (st, id), com
	// o contexto `ctx` do efeito em execucao (root e o resto vem dele)
	int (*expr)(void *ctx, const char *text, uint16_t st, uint32_t id, int64_t *out);
	// opcoes da aba Mods (todas): nome no jogo (<mod>_<chave>) e valor
	int (*cfg_each)(void (*fn)(const char *name, double v, void *arg), void *arg);
	// linha de pedido (formato do auto_raid_cmd.txt: move, halt, stance, raid, split, disband, raise, pause, pan...)
	// para a fila da thread principal; qualquer thread
	void (*run_main)(const char *line);
	// arquivos .txt de uma pasta do jogo (ex.: "common/governments") como o jogo os ve com os mods ativos (jogo base e
	// mods na ordem do dlc_load.json; mesmo nome substitui; replace_path descarta os anteriores), por nome de arquivo:
	// caminhos completos separados por fim de linha em out. Devolve quantos, -1 se nao coube
	int (*game_files)(const char *rel_dir, char *out, int cap);
	char *(*file_read)(const char *path);                 // conteudo com 0 no fim, NULL se nao abriu; liberar com file_free
	void (*file_free)(char *p);
	// (api 3) grava o texto em ck3_custom/<nome> na pasta do jogo, inteiro (temporario + troca); com AR_INSTANCIA=<x>
	// o nome ganha _<x> antes da extensao. So nome de arquivo, sem pasta. Devolve 1 se gravou
	int (*file_write)(const char *name, const char *text, int len);
	// (api 4) baixas por atrito de condado ao passar da provincia `from` para a vizinha `to` (a caveira do caminho):
	// 0 = nao cobra, -1 = invalido. Funcoes do jogo, com o tamanho atual do exercito
	int (*county_attrition)(uint32_t army, uint32_t from, uint32_t to);
	// (api 4) caminho de `from` ate `to` pelo pathfinder do jogo, com a velocidade da unidade (provincias depois de
	// `from`); devolve o tamanho
	int (*path_from)(uint32_t unit, uint32_t from, uint32_t to, uint32_t *out, int max);
	// (api 5) estado do combate em que o exercito esta (docs/plano_re_batalha.md 6.1): 1 se ha combate. Lado 0 =
	// atacante. vantagem = base + v[0] - v[1]; v = rolagem + v_cmd (comandante, inclui os modificadores dele) + v_mods
	// (bloco do lado); mult = multiplicador de dano do lado com vantagem; rolagem sorteada entre rmin e rmax a cada
	// COMBAT_ROLL_DAYS
	int (*combat_info)(uint32_t army, ck3_combat *out);
	// (api 6) combate hipotetico entre o exercito atacante e o defensor na provincia: v (sem rolagem), v_cmd, v_mods,
	// comandante, rmin e rmax de cada lado pelas funcoes do jogo; a base (efeitos de combate: terreno, travessia,
	// suprimento...): base = efeitos do inicio (suprimento, reuniao das levas, fortaleza/fe, divida); terreno e travessia
	// ficam com quem chama
	int (*combat_hypo)(uint32_t atacante, uint32_t defensor, uint32_t provincia, ck3_combat *out);
	// (api 7) jogo aberto com -debug_mode? Sem ele (producao) o log dos plugins e descartado e os pedidos pelo arquivo
	// nao sao lidos
	int (*debug_mode)(void);
} ck3_api;

typedef struct ck3_plugin {
	uint32_t version;
	// efeito de script do mod; devolve 1 se tratou
	int (*on_effect)(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
	                 uint32_t thread);
	// pedido de auto_raid_cmd.txt que o nucleo nao conhece; devolve 1 se tratou
	int (*on_command)(char **argv, int argc);
	void (*on_stop)(void);   // antes de o plugin ser trocado ou descarregado
} ck3_plugin;

// definida pelo plugin
const ck3_plugin *ck3_plugin_start(const ck3_api *api);

#endif
