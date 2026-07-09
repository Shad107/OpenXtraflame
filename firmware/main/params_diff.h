#pragma once
#include <stdint.h>
#include <stdbool.h>

#define PARAMS_DIFF_BASE   0x40
#define PARAMS_DIFF_COUNT  0x40

void params_diff_init(void);
/* Prend un snapshot des 64 valeurs actuelles = référence t0 */
void params_diff_snapshot(void);
/* Retourne JSON [{addr, before, after}] des addresses qui ont bougé depuis snapshot */
char *params_diff_json(void);
/* Nombre d'adresses qui ont bougé depuis le dernier snapshot */
uint8_t params_diff_count(void);

/* Watcher passif : appelé à chaque cycle poll. Détecte toute mutation
 * spontanée sur la zone tech et l'enregistre dans le ring buffer. */
void params_diff_watcher_tick(void);
/* Ring buffer 32 dernières mutations (=addr, before, after, timestamp) */
char *params_diff_watcher_dump_json(void);
