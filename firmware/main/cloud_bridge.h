/**
 * cloud_bridge - Extraflame cloud MQTT compatibility mode (BETA).
 *
 * Reproduit la connexion sortante du firmware original vers
 * `mqtts://mqtt.extraflame.it:8883` pour que l'app TotalControl 2 continue
 * à voir le poêle en parallèle du bridge MQTT local vers HA.
 *
 * Status : SCAFFOLDING. La connexion TLS est établie et les credentials
 * (=matricola + secure_code) sont envoyés. Les topics de publish et le
 * schema payload du cloud Omnyvore restent à reverse-engineer avant
 * que ce mode soit vraiment fonctionnel.
 *
 * Désactivé par défaut. Activation via `cfg->cloud_enabled = true`
 * (=onglet Avancé du Web UI).
 */
#pragma once

#include "esp_err.h"
#include "config_nvs.h"

/* Démarre le client cloud (=async, retourne immédiatement). Si le module
 * n'est pas TARGET_BLACKLABEL (=pas de matricola dispo), no-op. */
esp_err_t cloud_bridge_start(const app_config_t *cfg);

/* Arrête le client cloud (=graceful disconnect). */
esp_err_t cloud_bridge_stop(void);

/* Retourne true si actuellement connecté au broker Extraflame. */
bool cloud_bridge_connected(void);

/* Stats de debug pour l'endpoint HTTP /cloud/status */
typedef struct {
    bool     enabled;        /* cfg->cloud_enabled au démarrage */
    bool     started;        /* cloud_bridge_start() a été appelé */
    bool     connected;      /* état actuel du client MQTT */
    uint32_t connect_count;  /* nombre de MQTT_EVENT_CONNECTED cumulés */
    uint32_t error_count;    /* nombre de MQTT_EVENT_ERROR cumulés */
    int      last_error;     /* code erreur du dernier MQTT_EVENT_ERROR */
    char     last_error_str[80]; /* description texte du dernier event */
    char     broker_uri[64]; /* broker configuré */
    char     matricola[16];  /* client_id utilisé */
} cloud_bridge_stats_t;

void cloud_bridge_get_stats(cloud_bridge_stats_t *out);
