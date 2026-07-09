# Guardian Mode (=OTA Firmware Interceptor)

Idée : firmware OpenXtraflame qui coexiste avec le cloud Extraflame, intercepte les OTA firmwares officiels, les archive sans les appliquer.

## Statut : Target Blacklabel UNIQUEMENT, opt-in, désactivé par défaut

**Décision de design importante** :

Guardian Mode est disponible **UNIQUEMENT sur Target Blacklabel** (=reflash du module Extraflame officiel). Sur Target External (=ESP32 spare), le feature n'est PAS compilé.

Raison technique : Guardian Mode nécessite les credentials Extraflame que seul le module Black Label original possède :
- `secure_code` (=extrait du dump original)
- `stove_model` (=extrait du dump original)
- MAC address reconnue par Omnyvore
- Cert CA embedded pour valider TLS

Un ESP32 spare avec Target External n'a AUCUN de ces éléments. Toute tentative de connexion à Omnyvore serait rejetée par le serveur (=identity mismatch).

**Sur Target Blacklabel** :
- Guardian Mode existe dans le code
- Désactivé par défaut (=comportement 100% local safe)
- Opt-in via toggle Web UI
- Peut être activé/désactivé sans reflash

**Sur Target External** :
- Guardian Mode PAS compilé (=via `#ifdef TARGET_BLACKLABEL`)
- Section Guardian PAS visible dans Web UI
- Aucune option cloud dans la config

Raisons du design opt-in :
- Par défaut = comportement 100% local, offline, safe
- Utilisateur standard n'a pas besoin de cette complexité
- Considérations légales varient par pays et interprétation
- Certains utilisateurs veulent zero cloud (=complet air-gap)

## Concept

En mode Guardian, le module :
- Fonctionne normalement avec MQTT local (=Home Assistant)
- Se connecte AUSSI à `mqtt.extraflame.it:8883` (=Omnyvore cloud)
- Écoute topic `IN/firmware` (=notifications OTA)
- Télécharge les firmwares proposés depuis leurs URLs HTTPS
- Les archive dans partition `file_sys` (=720KB dispo) ou push vers HA storage
- **NE LES APPLIQUE PAS**
- Publish notification sur MQTT local

## Cas d'usage

1. **Firmware museum** : archive historique des versions Extraflame
2. **Diff analyse** : voir ce qu'Extraflame change à chaque release
3. **Detection features** : identifier nouvelles fonctionnalités
4. **Protection users** : rester sur version stable, refuser breaking updates
5. **Contribution reverse** : accélérer analyse communauté

## Architecture

```
                    ┌──────────────────┐
                    │  Omnyvore Cloud  │
                    │mqtt.extraflame.it│
                    └────────┬─────────┘
                             │ MQTT TLS
                             │ 8883
                             ▼
         ┌──────────────────────────────────┐
         │  ESP32 OpenXtraflame            │
         │  (Guardian mode enabled)         │
         │                                  │
         │  ├─ mqtt_local (=HA control)     │
         │  ├─ mqtt_omnyvore (=firmware)    │
         │  ├─ micronova UART (=stove)      │
         │  └─ guardian (=OTA interceptor)  │
         │                                  │
         │  Sur "IN/firmware" reçu :        │
         │  1. Parse URL + version          │
         │  2. Download bin                 │
         │  3. Save to file_sys             │
         │  4. Publish local MQTT           │
         │  5. Skip apply                   │
         └────────┬─────────────────────────┘
                  │
                  ▼
         ┌──────────────┐
         │ Home Assist  │
         │              │
         │ Notification │
         │ + Archive    │
         │ dans /media/ │
         └──────────────┘
```

## Requirements

- Reflash Black Label avec OpenXtraflame (=Target Blacklabel)
- Config Wi-Fi + MQTT local + credentials Omnyvore (=hérités du dump)
- 720KB file_sys partition pour buffer
- HA MQTT + storage folder

## Ce qu'on hérite du dump

Le dump du firmware original contient :
- Cert CA Omnyvore embedded (=pour valider TLS mqtts://)
- secure_code (=identité module vers cloud)
- stove_model (=type poêle enregistré)

Notre OpenXtraflame en mode Guardian les réutilise pour maintenir la connexion cloud active. Extraflame ne voit pas de différence côté serveur.

## Implémentation code

```c
// firmware/main/guardian_mode.h

typedef struct {
    char     version[32];
    char     url[256];
    uint32_t size;
    char     sha256[65];
    uint64_t captured_at_ms;
    bool     downloaded;
    char     archive_path[128];
} captured_firmware_t;

esp_err_t guardian_start(const app_config_t *cfg);
esp_err_t guardian_on_omnyvore_message(const char *topic, const char *payload);
esp_err_t guardian_download_and_archive(const captured_firmware_t *fw);
esp_err_t guardian_list_captured(captured_firmware_t *out, int max);
```

## Configuration Web UI (=opt-in explicite)

L'utilisateur voit ce panneau uniquement s'il déroule la section "Advanced" :

```
┌─────────────────────────────────────────┐
│  ⚙️  Guardian Mode                      │
│  ─────────────────                      │
│  Par défaut désactivé (=comportement    │
│  100% local, aucune connexion cloud)     │
│                                          │
│  [☐] Enable Guardian Mode               │
│                                          │
│  ⚠️ ATTENTION - fonctionnalité avancée  │
│                                          │
│  Guardian Mode connecte votre module au  │
│  cloud Extraflame en écoute uniquement.  │
│  Aucune donnée utilisateur n'est trans   │
│  mise. Nécessite :                       │
│  → secure_code du module (=depuis dump)  │
│  → stove_model (=depuis dump)            │
│  → URL de votre serveur archive          │
│  → Compréhension des risques légaux      │
│                                          │
│  Archive server URL :                    │
│  [ https://archive.exemple.com/upload  ] │
│                                          │
│  API token (optional) :                  │
│  [ ***** ]                                │
│                                          │
│  Behavior when firmware captured :       │
│  [•] Archive only, never apply           │
│  [ ] Archive + notify (=manual apply)    │
│  [ ] Archive + auto-apply                │
│                                          │
│  [Save & Restart]                        │
└─────────────────────────────────────────┘
```

Nouvel écran config Guardian :

```
[✓] Guardian mode enabled
[✓] Also connect to Omnyvore cloud
[✓] Archive OTA firmwares
    URL fallback : http://192.168.1.10/OpenXtraflame/archive/
[✓] Notify HA when new firmware captured
[✗] Auto-diff with current version
```

## Configuration Home Assistant

```yaml
mqtt:
  sensor:
    - name: "Poele firmware captured"
      state_topic: "OpenXtraflame/poele/guardian/captured"
      value_template: "{{ value_json.version }}"
      json_attributes_topic: "OpenXtraflame/poele/guardian/captured"
      json_attributes_template: "{{ value_json | tojson }}"

notify:
  - platform: file
    name: firmware_log
    filename: /config/firmware_captures.log

automation:
  - alias: "Firmware Extraflame capturé"
    trigger:
      - platform: mqtt
        topic: "OpenXtraflame/poele/guardian/captured"
    action:
      - service: notify.mobile_app
        data:
          title: "Nouveau firmware Extraflame"
          message: "Version {{ trigger.payload_json.version }} archivée"
      - service: shell_command.download_firmware
        data:
          url: "{{ trigger.payload_json.url }}"
```

## Sécurité et ethique

⚠️ **Considérations légales** :
- Archive personnel : OK
- Redistribution publique : problématique (=copyright Extraflame)
- Sharing entre users : zone grise, éviter
- Analyse pour interop : légal en EU (=directive 2009/24/CE)

⚠️ **Considérations techniques** :
- Extraflame peut détecter le pattern "always downloading, never applying"
- Solution : appliquer occasionnellement pour rester crédible côté cloud
- Ou : rotate downloads pour ne pas trigger anomaly

## Étapes de dev

- [ ] Reverse HMAC key derivation (=Ghidra ota0.bin)
- [ ] Implémenter connect Omnyvore + auth HMAC
- [ ] Subscribe topics IN/firmware
- [ ] Parse payload notification format
- [ ] Download HTTPS handler
- [ ] Store in file_sys partition
- [ ] Push vers HA via MQTT + option HTTP upload
- [ ] Web UI Guardian config
- [ ] Test avec fake Omnyvore local d'abord (=Mosquitto)
- [ ] Test avec vraie connexion Omnyvore

## Nom du feature dans le firmware

Suggestions :
- "Guardian Mode"
- "OTA Interceptor"
- "Firmware Museum"
- "Extraflame Archive"
- "Cloud Watcher"

## Risques

1. **Comportement suspect détecté par Extraflame**
   - Solution : appliquer 1 firmware sur N pour paraître normal

2. **Rate limit ou ban IP**
   - Solution : espacer les downloads
   - Ne pas ré-download le même firmware

3. **Sanction contractuelle**
   - Le contrat Extraflame ne mentionne probablement pas ce cas
   - Reverse engineering pour interop = légal EU

4. **Cassure côté cloud**
   - Si Omnyvore change protocole ou tokens
   - Notre firmware doit gracefully degrader

## Architecture "own cloud" (=décision Olivier 2026-07-03)

Décision : le seul "cloud" utilisé sera **serveur perso Olivier**, pas Extraflame.

Extraflame cloud reste connecté SEULEMENT en écoute (=Guardian) pour capturer les OTAs.
Tout le reste (=archive, contrôle, notif, diff) = infra maison.

```
┌──────────────────────┐
│  Cloud Extraflame    │  ← module écoute (=Guardian)
│  mqtt.extraflame.it  │      pas de push data user
└──────────┬───────────┘
           │ pull firmware.bin
           ▼
┌──────────────────────┐
│  ESP32 OpenXtraflame│
└──────────┬───────────┘
           │ push firmware capturé
           ▼
┌──────────────────────┐
│  archive.isno.fr     │  ← 100% chez Olivier
│  (=LXC Gitea ou      │
│  Syno WebDAV)         │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│  HA + notif mobile   │
└──────────────────────┘
```

## Options stockage archive

**Option A - Syno WebDAV** (=simple)
- Path : \\<nas>\firmwares\ ou HTTP WebDAV
- Existant, pas de setup supplémentaire

**Option B - LXC Gitea** (=recommandé)
- Chaque firmware = git release avec tag
- Diff automatique entre versions
- Web UI native pour browse
- API webhook pour HA
- Setup ~30 min

**Option C - MinIO S3** (=avancé)
- Bucket S3-compatible self-hosted
- Standard API
- Versioning built-in

**Option D - Custom archive.isno.fr** (=devops fun)
- Node/Python endpoint
- Push HTTP + metadata custom
- UI browser custom

## "Delayed Release" bonus

Si Olivier veut être control freak :

- Module download depuis Omnyvore mais **PAS d'apply automatique**
- Push vers archive perso
- Olivier review diff strings via Gitea/UI
- Décide manuellement d'appliquer via `/ota/pull` from own server
- **Filter les updates comme un firewall** :
  - Refuser updates qui cassent features
  - Appliquer juste les patches sécurité
  - Stay on stable version

## Prochaine étape

Ghidra reverse de :
1. Comment le firmware reçoit notification OTA
2. Format exact du payload MQTT IN/firmware
3. Comment est calculé le HMAC signature

Une fois ces 3 points élucidés, implémentation Guardian = 8-10h dev.

## Alternative moins invasive

Si Guardian mode paraît trop risqué, on peut :

- Utiliser Target External (=ESP32 spare) qui SNIFF le trafic
- Écoute passive du bus série Micronova pour voir infos poêle
- Passive TLS sniff impossible (=chiffré)

Guardian mode nécessite Target Black Label (=reflash) pour intercepter au niveau MQTT.
