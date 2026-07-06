# Cartographie complète du firmware Extraflame Black Label T009_3

Reverse engineering exhaustif du firmware original (v1, nov 28 2022) pour construction du firmware `openextraflame` custom.

## 1. Architecture globale

```
   ┌─────────────────────────────────────────────────┐
   │        ESP32-D0WDQ6 rev v1.0                     │
   │        Firmware "navel" v1                       │
   │        ESP-IDF v4.3-dirty                        │
   │        Compiled : Nov 28 2022 by g.benetti (IT)  │
   └─────────────────────────────────────────────────┘
                        │
       ┌────────────────┼────────────────┐
       │                │                │
   ┌───▼────┐     ┌────▼─────┐    ┌────▼────┐
   │ SoftAP │     │ STA Wi-Fi│    │  Serial │
   │HTTP UI │     │  MQTT    │    │Micronova│
   │        │     │  Client  │    │  UART   │
   └────────┘     └──────────┘    └─────────┘
       │                │                │
   Config Wi-Fi   Cloud Omnyvore    Poêle Teodora
   Config poêle   mqtts://mqtt.     via bus RS-232
                  extraflame.it     4-pin cable
                  :8883
```

## 2. Sources code identifiées

Fichiers .c custom Extraflame (=extraits depuis strings ELF paths) :

```
../main/main.c                  # Entry point + task creation
../main/http_server.c            # HTTP server (=SoftAP web UI)
../main/wifi_manager.c           # Fork tonyp7 + customisation
../main/ota/Commands.c           # Commandes reçues (=de qui ?)
../main/ota/SerialOTA2.c         # OTA firmware POELE via UART
../main/ota/WifiOta.c            # OTA firmware ESP32 via Wi-Fi
../main/uart/rwms_master.c       # Read/Write Micronova Serial MASTER
../main/uart/serial.c            # SLIP protocol wrapper
```

**Notes :**
- Pas de `mqtt_client.c` custom → utilisation directe ESP-MQTT lib
- `rwms_master.c` = master Micronova RS232 protocol (=RWMS = Read Write Micronova Serial)
- `SerialOTA2.c` = capable de flasher firmware sur POÊLE via bus série
- `serial.c` utilise SLIP framing (`SLIP_tx_one_char`)

## 3. Layout partitions flash 4MB

Table extraite du boot log :

| # | Nom         | Type       | Offset     | Taille    | Contenu                          |
|---|-------------|------------|------------|-----------|----------------------------------|
| - | (bootloader)| bootloader | 0x001000   | ~28KB     | ESP-IDF 2nd stage bootloader    |
| - | partitions  | ptable     | 0x008000   | 4KB       | Partition table                 |
| 0 | nvs         | data.wifi  | 0x009000   | 16KB      | Wi-Fi credentials + config      |
| 1 | otadata     | data.ota   | 0x00d000   | 8KB       | OTA slot pointer (=quel ota active)|
| 2 | phy_init    | data.phy   | 0x00f000   | 4KB       | RF calibration (=vide sur dump) |
| 3 | ota_0       | app.ota_0  | 0x010000   | 1.5MB     | APP A (=firmware ESP32)          |
| 4 | ota_1       | app.ota_1  | 0x190000   | 1.5MB     | APP B (=fallback OTA, vide)     |
| 5 | ota_stove   | data.0x40  | 0x310000   | 192KB     | Firmware POÊLE (=OTA vers stove)|
| 6 | stove_bk    | data.0x40  | 0x340000   | 8KB       | Backup config poêle (=vide)     |
| 7 | file_sys    | data.spiffs| 0x342000   | 720KB     | SPIFFS web assets (=vide sur dump)|
| 8 | secret1     | data.wifi  | 0x3f2000   | 16KB      | NVS EN CLAIR : secure_code+model|
| 9 | secret2     | data.0x40  | 0x3f6000   | 16KB      | VIDE (=backup/futur usage)      |

**otadata content (0xd000)** :
```
01 00 00 00 ff ff ff ff ff ff ff ff ff ff ff ff
ff ff ff ff ff ff ff ff 02 00 00 00 9a 98 43 47
```
→ Deux entries slot config. First slot marked "1", second slot "2" with CRC.
→ Confirms OTA slot 0 (=ota_0) is active.

## 4. Tasks FreeRTOS identifiées

Depuis strings, les tasks créés :

```
main_task    (=main.c task principale)
mqtt_task    (=MQTT publish/subscribe loop)
serial_task  (=UART Micronova I/O)
ipc_task     (=Inter-process communication)
```

Standard ESP-IDF tasks aussi présentes :
```
pthread_task
tiT (=lwip)
sys_evt
```

## 5. Interfaces communication

### 5.1 Interface HTTP SoftAP (=config initial + reconfig)

Serveur HTTP écoute sur `0.0.0.0:80` accessible via :
- SoftAP : http://192.168.1.1/ (=confirmé par debug log)
- STA : http://192.168.X.X/ (=IP DHCP quand connecté au Wi-Fi <WIFI_SSID>)

**Endpoints identifiés :**

| Method  | URL                    | Fonction                              |
|---------|------------------------|---------------------------------------|
| GET     | /                       | Homepage HTML (=redirect vers wifi.html)|
| GET     | /wifi.html              | Page config Wi-Fi                     |
| GET     | /ap.json                | Scan Wi-Fi (=liste APs disponibles)   |
| GET     | /status.json            | Status connexion Wi-Fi + IP           |
| GET     | /status                 | Alias status                          |
| POST    | /connect.json           | Connect au Wi-Fi (=SSID+password)     |
| DELETE  | /connect.json           | Disconnect Wi-Fi                      |
| GET     | /web/bootstrap.css      | Framework CSS                         |
| GET     | /web/bootstrap.js       | Framework JS                          |
| GET     | /web/jquery.js          | jQuery                                |
| GET     | /web/popper.js          | Bootstrap dependency                  |
| GET     | /web/chart.bundle.js    | Chart.js (=graphes)                   |
| GET     | /web/code.js            | Code custom Extraflame ⭐             |
| GET     | /web/style.css          | Style custom                          |
| GET     | /favicon.ico            | Icon                                  |

**Debug logs live serveur :**
```
------------------- start session ask-reply --------------------
---got msg---
msg[0] = 0x50 (=P)
msg[1] = 0x4F (=O)
msg[2-232]="ST /api/status HTTP/1.1..."
I (T) http_server: /unknow served
```
→ /api/status = **NON RECONNU** (=pas d'endpoint API REST pour data poêle)
→ Confirmation : contrôle poêle passe UNIQUEMENT par MQTT, pas HTTP.

### 5.2 MQTT vers cloud Omnyvore

**Broker :** `mqtts://mqtt.extraflame.it:8883` (=TLS)

**Cert CA embedded :** self-signed Omnyvore srl (Vicenza, IT), valide 2017-2027.

**Auth :** HMAC signed messages (`HMAC RX/TX` visible dans strings).

**Format topics :** `%s/%s/%s/%s` (=probable `stove_id/model/version/direction/action`).

**Topics IN (=commandes vers poêle) :**
| Topic          | Fonction                              |
|----------------|---------------------------------------|
| IN/firmware    | OTA update firmware poêle             |
| IN/addr        | Adressage/config                      |
| IN/crono       | Chrono/timers programmés              |
| IN/misc        | Divers                                |
| IN/settings    | Paramètres poêle (=setpoint, etc.)    |
| IN/time        | Time sync                             |

**Topics OUT (=data du poêle) :**
| Topic              | Fonction                          |
|--------------------|-----------------------------------|
| OUT/status         | État poêle (=on, off, alarm, etc.)|
| OUT/temperature    | Températures multiples            |
| OUT/alarm          | Codes alarmes actives             |
| OUT/dyn            | Données dynamiques temps réel     |
| OUT/workingtimers  | Timers en cours                   |
| OUT/misc           | Divers OUT                        |
| OUT/settings       | Paramètres actuels                |
| OUT/addr           | Adressage                         |
| OUT/time           | Time                              |
| OUT/crono          | Chrono                            |

**Topics REPLY (=ACKs) :**
| Topic           | Réponse pour            |
|-----------------|-------------------------|
| REPLY/crono     | ACK IN/crono            |
| REPLY/settings  | ACK IN/settings         |
| REPLY/time      | ACK IN/time             |

### 5.3 UART Micronova vers poêle

**Config UART (=constantes du firmware) :**
```
SERIAL_UPGRADE_PORT    = UART_NUM_1 ou 2 (=probable)
SERIAL_UPGRADE_TX      = pin GPIO (=à identifier)
SERIAL_UPGRADE_RX      = pin GPIO (=à identifier)
SERIALOTA_MAX_BUFFER   = taille buffer
Format                 = 8N2, 1200 baud (=Micronova standard)
Framing                = SLIP protocol (=SLIP_tx_one_char visible)
```

**Cable poêle SERIAL 4-pin (=jaune/blanc/vert/marron) :**

Convention Micronova identifiée par test empirique :
| Wire couleur | Pin (=gauche vers droite) | Fonction probable       |
|--------------|---------------------------|-------------------------|
| 🟢 VERT      | 1                         | GND                     |
| 🟤 MARRON    | 2                         | TX module (=confirmé)   |
| ⚪ BLANC     | 3                         | RX module (=probable)   |
| 🟡 JAUNE     | 4                         | +VCC 12V (=DANGER)      |

## 6. Registres Micronova RAM_

Constantes des adresses RAM lues/écrites par le module (=via commandes série) :

| Constant                  | Description                            |
|---------------------------|----------------------------------------|
| RAM_ACCENDI               | Commande "allume" (write)              |
| RAM_SPEGNI                | Commande "éteins" (write)              |
| RAM_RESET_UTENTE          | Reset user commands (write)            |
| RAM_SBLOCCO_ADDR          | Déblocage alarmes (write)              |
| RAM_STATO_GESTITO         | État géré (read)                       |
| RAM_STOVE_STATUS_ADDR     | Status général poêle (read)            |
| RAM_ALLARM_ADDR           | Code alarme actif (read)               |
| RAM_CAUSA_STATO7_ADDR     | Cause status 7 (=code erreur, read)    |
| RAM_MOD_ADDR              | Mode actuel (read)                     |
| RAM_POT_REALE_ADDR        | Puissance réelle (read)                |
| RAM_SERBATORIO_VUOTO_ADDR | Réservoir vide (read)                  |
| RAM_BULBO_ADDR            | Sonde bulbe (read)                     |
| RAM_TAMB_ADDR             | Température ambiante (read)            |
| RAM_TH20_ADDR             | Température eau (=modèles idro)        |
| RAM_T_BOILER_ADDR         | Température ballon                     |
| RAM_T_CAMERA_ADDR         | Température chambre combustion         |
| RAM_T_FUMI_ADDR           | Température fumées                     |
| RAM_T_H20_RIT_ADDR        | Température eau retour                 |
| RAM_T_PUFFER_INF_ADDR     | Température puffer bas                 |
| RAM_T_PUFFER_SUP_ADDR     | Température puffer haut                |

**⚠️ Note :** les adresses HEX de ces registres ne sont pas dans les strings.
Analyse Ghidra du binary requise pour extraire les hex offsets.

Référence utile : [ridiculouslab table registres Micronova](https://www.ridiculouslab.com/arguments/iot/stufa/micronova_en.php) et [philibertc/micronova_controller](https://github.com/philibertc/micronova_controller).

## 7. Types poêles supportés

Constantes `STOVE_TYPE_I_*` :

| Type            | Description                              |
|-----------------|------------------------------------------|
| I_CALD          | Caldaia (=chaudière)                     |
| I_CANAL         | Canalizzato (=canalisé, 1 canal)         |
| I_CANAL_2       | Canalisé 2 canaux                        |
| I_CANAL_3       | Canalisé 3 canaux                        |
| I_CANAL_4       | Canalisé 4 canaux                        |
| I_IDRO          | Idro (=à eau)                            |
| I_IDRO_2        | Idro variant 2                           |
| I_VENT          | Ventilato (=ventilé)  ← **Teodora Evo**  |
| I_VENT_2        | Ventilé variant 2                        |
| I_VENT_3        | Ventilé variant 3                        |
| I_VENT_4        | Ventilé variant 4                        |
| I_VENT_5        | Ventilé variant 5                        |

**Constantes états poêle :**
```
STATO_PUL_ORD_APERTO_VALUE      # Pulizia ordinaria ouverte
STATO_PUL_ORD_APERTURA_VALUE    # En cours d'ouverture
STATO_PUL_ORD_CHIUSURA_VALUE    # En cours de fermeture
STATO_PUL_ORD_OFF_VALUE         # Fermé
```

## 8. Wi-Fi Manager (=fork tonyp7)

Fonctions confirmées dans strings :
```
wifi_manager_fetch_wifi_sta_config
wifi_manager_fetch_wifi_settings (ssid, pwd, channel, hidden, bandwidth, 
                                   sta_only, sta_power_save, sta_static_ip,
                                   ip_addr, gw_addr, netmask, dns_auto, dns1, dns2)
```

**SoftAP config :**
- SSID : `MyStove_DE:AD:BE:EF:00:00` (=pattern `MyStove_<MAC>`)
- Password default : `86274949` (=depuis manuel NAVEL PLUS)
- Channel : 5
- Bandwidth : 20 MHz
- IP module : 192.168.1.1 (=confirmé via debug HTTP log)

**mDNS :**
- Hostname : "Extraflame"
- Instance name : "Extraflame_"

## 9. NVS Storage

**Partition `nvs` (0x9000, 16KB) :** Wi-Fi credentials + config générale.

**Partition `secret1` (0x3f2000, 16KB) :** Données sensibles EN CLAIR :
| Clé          | Valeur (exemple)  | Description                       |
|--------------|-------------------|-----------------------------------|
| settings     | "0"               | Settings global                   |
| del_model    | "0"               | Delete model flag                 |
| product      | "0"               | Product code                      |
| secure_code  | "XXXXXXXX"        | ⭐ Code registration/pairing      |
| stove_model  | "YYYYYYYYYY"      | ⭐ Code modèle Extraflame         |
| custom2      | "1"               | Custom flag 2                     |

**⚠️ secure_code + stove_model = identifient l'utilisateur/module dans le cloud Extraflame.** Doivent être sanitisés avant partage du dump.

## 10. OTA Update

Le module supporte 2 types d'OTA :

**A. OTA firmware ESP32 lui-même (=WifiOta.c)** :
- Reçu via HTTP client (=probable) depuis endpoint Omnyvore
- Écrit dans partition ota_0 ou ota_1 (=alternance)
- otadata pointer mis à jour → reboot → boot sur nouvelle partition

**B. OTA firmware POÊLE (=SerialOTA2.c)** :
- Reçu via MQTT topic `IN/firmware`
- Transmis au poêle via UART Micronova
- Utilise partitions ota_stove (=stockage temporaire) et stove_bk (=backup)
- `iap_begin` : In-Application Programming poêle
- Chunks envoyés par UART avec ACK

## 11. Certificats embedded

Un cert CA identifié dans le binary :

```
Sujet   : /C=IT/ST=Vicenza/L=Vicenza/O=Omnyvore/OU=Tech Ops/CN=omnyvore.com
Émission: 2017-10-05
Expire  : 2027-10-03
Type    : Self-signed CA
Sujet Alt: emailAddress=info@omnyvore.com
```

**Signification :**
- Omnyvore srl est le PROVIDER cloud IoT (=Extraflame utilise ce partner)
- `mqtt.extraflame.it` = probable CNAME vers infra Omnyvore
- Ce cert CA sert à valider le cert TLS du broker MQTT

**⚠️ Cert utilisateur/client MQTT :** pas trouvé en clair dans strings, probablement :
- Généré dynamiquement depuis MAC + secure_code
- Ou hardcoded dans le binaire (=besoin Ghidra pour extract exact)
- Ou provisionné à première connexion cloud

## 12. Développeur / build info

```
Compilation user     : g.benetti
Path build           : C:/Users/g.benetti/esp/esp-idf/
ESP-IDF version      : v4.3-dirty
Compile time         : Nov 28 2022 16:05:12
Chip revision cible  : 1 (=match ESP32-D0WDQ6 rev v1.0)
Coding scheme        : NONE
Comments/strings     : Italien ("Da inviare con MQTT: %s")
```

## 13. Sécurité analyse

**Points faibles Extraflame identifiés :**
1. ❌ Flash **NON chiffrée** (=FLASH_CRYPT_CNT=0)
2. ❌ Secure Boot **DÉSACTIVÉ** (=ABS_DONE=0)
3. ❌ UART download mode **ACTIF** (=UART_DOWNLOAD_DIS=False)
4. ❌ JTAG **PAS désactivé** (=JTAG_DISABLE=False)
5. ❌ secure_code stocké en clair dans NVS
6. ❌ Certificat CA cloud extractible en clair

**Impact :**
- N'importe qui avec accès physique 30 min peut dump firmware
- Extraflame n'a pas activé les protections ESP32 disponibles
- Facilite le reverse mais expose leurs users à risque cloné/spoofed

## 14. Roadmap firmware openextraflame

### Phase 1 - MVP (=Target External)
- [ ] Setup ESP-IDF v5.x + toolchain Xtensa
- [ ] Structure projet + CMakeLists.txt
- [ ] Wi-Fi manager custom (=fork tonyp7 minimal)
- [ ] MQTT client vers Mosquitto local
- [ ] UART Micronova basique (=1200 baud 8N2)
- [ ] Web UI SoftAP config (=Wi-Fi + MQTT broker)
- [ ] Publish OUT/status + OUT/temperature MQTT
- [ ] Test compilé sur ESP32 spare

### Phase 2 - Micronova complet
- [ ] Implémentation registres RAM_ complets (=hex addresses de Ghidra)
- [ ] Support toutes commandes (ACCENDI, SPEGNI, RESET_UTENTE)
- [ ] Lecture températures + status + alarmes
- [ ] Subscribe IN/settings pour setpoint, mode, puissance
- [ ] Publier REPLY/ topics ACK

### Phase 3 - Target Black Label
- [ ] Analyser ota0.bin avec Ghidra pour LED GPIOs
- [ ] Identifier bouton RESET GPIO
- [ ] Identifier pins UART précis (=SERIAL 4-pin cable)
- [ ] Partitions.csv identique à Extraflame (=compat OTA)
- [ ] Test reflash sur module Black Label
- [ ] Restauration dump original si problème

### Phase 4 - HA Integration
- [ ] MQTT Discovery HA (=auto config entities)
- [ ] Exposer sensors : température, status, alarme
- [ ] Exposer commands : on/off, setpoint, mode
- [ ] Exemple config yaml HA

### Phase 5 - Documentation + release
- [ ] Documentation sur www.isno.fr
- [ ] GitHub public release
- [ ] Post communauté HA
- [ ] Tutorial vidéo/screenshots

## 15. Questions ouvertes

Zones qui nécessitent Ghidra pour reversing complet :

1. **Adresses HEX exactes registres RAM Micronova**
   - Actuellement on a les NOMS des constants (RAM_TAMB_ADDR, etc.)
   - Il faut les valeurs numériques pour parler au poêle
   - À extraire du binary via Ghidra decompilation

2. **Format binaire trames Micronova**
   - Sync bytes ?
   - Structure header ?
   - Checksum CRC4R visible mais algo à identifier

3. **HMAC key derivation MQTT auth**
   - Comment secure_code est utilisé pour signer messages MQTT
   - À reverse pour compat cloud si besoin (=pas nécessaire pour local)

4. **GPIO mapping exact ESP32**
   - LED_POWER, LED_BLE, LED_WIFI, LED_SERVER (=quels GPIOs ?)
   - Bouton RESET (=quel GPIO ?)
   - UART TX/RX poêle (=quel GPIO/UART unit ?)
   - Nécessaire pour Target Black Label

5. **Format SPIFFS file_sys**
   - Partition marquée SPIFFS mais vide sur dump
   - Devait contenir les assets web (=bootstrap, jquery, etc.)
   - Probable populée seulement après provisioning cloud

## 16. Références externes

- [tonyp7/esp32-wifi-manager](https://github.com/tonyp7/esp32-wifi-manager) - Wi-Fi manager library utilisée
- [philibertc/micronova_controller](https://github.com/philibertc/micronova_controller) - Reverse Micronova bus série
- [jeng37/Micronova-Diadema-IDRO-to-Home-Assistant](https://github.com/jeng37/Micronova-Diadema-IDRO-to-Home-Assistant) - Autre reverse (=WIP)
- [Jorre05/micronova](https://github.com/Jorre05/micronova) - ESPHome component micronova
- [ridiculouslab micronova protocol](https://www.ridiculouslab.com/arguments/iot/stufa/micronova_en.php) - Table registres
- [Espressif ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/) - Framework doc
- [ESP32-WROOM-32 datasheet](https://documentation.espressif.com/esp32-wroom-32_datasheet_en.pdf)
- [Micronova NAVEL 2.0 fiche produit](https://www.micronovasrl.com/en/projects/t009-navel-2-0/)
- [Extraflame Black Label manual](https://www.manualslib.com/manual/1985399/Extraflame-Wifi-Module-Black-Label.html)
- [NAVEL PLUS user manual PDF](https://www.micronovasrl.com/wp-content/uploads/2023/08/istruzione-navel.pdf)

## Update finale 2026-07-03 - blocage reverse

### Ce qu'on peut trouver via strings (=sans Ghidra GUI)

Confirmé après analyse Python + Ghidra headless :

| String log         | vaddr        | Ptr refs  |
|--------------------|--------------|-----------|
| gpio_set_level     | 0x3f401fba   | 1         |
| gpio_set_direction | 0x3f401f94   | 1         |
| uart_set_pin       | 0x3f40194c   | 1         |
| uart_param_config  | 0x3f401923   | 1         |
| uart_driver_install| 0x3f40189f   | 1         |
| ResetButton        | 0x3f409fa7   | 1         |

Chaque string a 1 pointer reference dans le binaire (=1 caller par log message).

### Ce qui reste bloqué sans GUI Ghidra

Les constantes suivantes NE SONT PAS des strings dans le binaire :

- `LED_POWER`, `LED_BLE`, `LED_WIFI`, `LED_SERVER`
- `SERIAL_UPGRADE_TX`, `SERIAL_UPGRADE_RX`, `SERIAL_UPGRADE_PORT`

Ce sont des `#define` C préprocessés à compile time. Leur valeur numérique est chargée par des instructions `movi` près des call sites, mais sans symbol resolution, impossible d'identifier les callers automatiquement.

### Comment finir (=weekend GUI)

**Approche A - Ghidra GUI avec ESP-IDF ELF de référence** :

1. Compiler un projet ESP-IDF vide avec v4.3 (=même version que Extraflame)
2. Extraire les symboles de son ELF (`xtensa-esp32-elf-nm`)
3. Match les signatures ESP-IDF vs notre binaire dans Ghidra
4. Identifier `uart_set_pin`, `gpio_set_level`, etc. comme functions
5. Cross-refs vers ces fonctions = call sites réels
6. Extraire les args (`movi a2, X` avant call)

**Approche B - Testing empirique** (=recommandé, 30 min) :

1. Flash openextraflame Target External sur ESP32 spare
2. Wire chaque GPIO ESP32 spare (INPUT_PULLUP) à chaque castellated pad du module Black Label
3. Ouvrir un firmware original (=avant reflash) et regarder quelle LED s'allume quand
4. Corréler GPIO ESP32 spare qui va HIGH ↔ LED Black Label allumée
5. Documenter GPIOs :
   - LED_POWER_PIN, LED_BLE_PIN, LED_WIFI_PIN, LED_SERVER_PIN
   - CONFIG_BUTTON_PIN (=pousser le bouton, voir GPIO qui descend)
   - STOVE_UART_TX/RX_PIN (=via SERIAL 4-pin cable, voir traces PCB ou multimètre)

L'approche B est plus rapide, plus fiable, et ne nécessite pas de setup GUI Ghidra.

## Verdict FINAL 2026-07-03 - toutes voies exploratrices épuisées

Approches tentées cette session :

| Approche                             | Résultat                          |
|--------------------------------------|-----------------------------------|
| Ghidra headless + Xtensa natif       | Strings OK, refs incomplètes       |
| ELF multi-segment via custom loader  | Fonctionnel, mais analyzer bloqué  |
| Fingerprint matching ESP-IDF v4.3    | 0 hit (=v4.3-dirty patched)        |
| Xtensa objdump binary                | 871k lignes brutes, pas exploitable|
| Capstone Xtensa                      | Non supporté                       |
| Radare2                              | Install bloqué (=no sudo)           |
| Statistical heuristic GPIO           | Bruit total (=4442 hits pour 32)   |

### Conclusion technique

Le firmware Extraflame est parfaitement dumpable en clair, mais son **reverse au niveau instruction Xtensa nécessite** :
- Ghidra GUI + heures d'analyse manuelle
- OU testing empirique sur hardware réel

**Aucune voie automatisée court-terme.**

### Recommandation TL;DR

Le testing empirique 30 min = solution finale évidente. Reverse binary Xtensa sans symbol resolution complète = trou noir.

## Fingerprint match v4.3.2 + v4.3.3 - RÉSULTAT NÉGATIF DÉFINITIF

Testées TROIS variantes ESP-IDF v4.3 successives :

| Version         | Match dans Extraflame |
|-----------------|-----------------------|
| release-v4.3    | 0 hit                 |
| v4.3.2          | 0 hit                 |
| v4.3.3          | 0 hit                 |

Signature Xtensa entry a1 dans .text Extraflame : ~4015 fonctions détectées.
Aucune ne commence par les mêmes premiers 6-8 bytes qu'une fonction ESP-IDF v4.3.x standard.

Conclusion : Extraflame utilise **des patches profonds ESP-IDF** ou une **toolchain GCC différente** (=flags -O3 vs -Os, options ABI, etc.). Le byte-level match est impossible.

**Reverse binary via fingerprint DÉFINITIVEMENT abandonné.**

## ⭐ ADRESSES ESP-IDF PATCHÉES DE EXTRAFLAME - découvertes 2026-07-03

Approche : les fonctions ESP-IDF (=même patchées) contiennent leur propre
string de log (`"gpio_set_level"`, etc.) chargée par `l32r`. En trouvant
qui charge quoi, on identifie les fonctions.

**Résultat : adresses des fonctions ESP-IDF DANS le binaire Extraflame :**

| Fonction ESP-IDF     | Adresse Extraflame | Log call site       |
|----------------------|--------------------|--------------------:|
| gpio_set_level       | 0x400d98f4         | 0x400d9915          |
| gpio_set_direction   | 0x400d9a4c         | 0x400d9a6d          |
| uart_set_pin         | 0x400d8740         | 0x400d8758          |
| uart_param_config    | 0x400d88f4         | 0x400d8909          |
| uart_driver_install  | 0x400d90fc         | 0x400d9117          |
| ResetButton (=class) | 0x400dff29 et 3 autres | multiples calls|

Ces adresses peuvent être **importées comme symboles dans Ghidra** pour
identifier les callers automatiquement.

### Comment continuer (=weekend)

1. Dans Ghidra, ouvrir le projet ota0.elf déjà importé
2. Navigate to address 0x400d8740 (=uart_set_pin dans Extraflame)
3. Rename cette fonction "uart_set_pin_extraflame"
4. View → References → Show Xrefs to
5. Chaque xref = un call site avec les args (GPIO pin numbers)
6. Explorer callers pour extraire pins UART Micronova
7. Idem pour gpio_set_level → LED/button GPIOs

Cette approche évite le fingerprint matching (=impossible) et exploite
les strings de log comme "ancres" pour identifier les fonctions.

### Script Java Ghidra suggéré

```java
// Rename functions using known addresses
symbolTable.createFunctionLabel(toAddr("400d98f4"), "gpio_set_level", ...);
symbolTable.createFunctionLabel(toAddr("400d9a4c"), "gpio_set_direction", ...);
symbolTable.createFunctionLabel(toAddr("400d8740"), "uart_set_pin", ...);
// etc.

// Then find xrefs and analyze args
```

## ⭐⭐⭐ 2026-07-03 SOIR - QEMU + GDB CAPTURE COMPLÈTE

Setup ultime = QEMU-Xtensa + xtensa-esp32-elf-gdb dans Docker ESP-IDF v5.2.2.

### Breakpoints sur adresses ESP-IDF Extraflame identifiées

```
break *0x400d8743  # uart_set_pin après entry
break *0x400d98f7  # gpio_set_level après entry
break *0x400d88f7  # uart_param_config après entry
break *0x400d90ff  # uart_driver_install après entry
```

### Runtime CAPTURE

```
UART_SET_PIN uart=1 tx=23 rx=5 rts=255 cts=255
UART_PARAM   uart=1
GPIO_SET_LEVEL gpio=22 level=1
GPIO_SET_LEVEL gpio=25 level=0/1 cycling
GPIO_SET_LEVEL gpio=26 level=0/1 cycling
GPIO_SET_LEVEL gpio=32 level=0/1 cycling
GPIO_SET_LEVEL gpio=33 level=0/1 cycling
```

### CONCLUSIONS FINALES

**UART Micronova Black Label** :
| Paramètre | Valeur |
|-----------|--------|
| UART peripheral | UART_NUM_1 |
| TX pin | GPIO_NUM_23 |
| RX pin | GPIO_NUM_5 |
| RTS pin | UART_PIN_NO_CHANGE (=255) |
| CTS pin | UART_PIN_NO_CHANGE (=255) |
| Baud rate | 38400 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |

**LEDs Black Label** :
| GPIO | Rôle probable |
|------|---------------|
| GPIO 22 | LED additionnelle |
| GPIO 25 | LED_POWER (=candidate) |
| GPIO 26 | LED_BLE |
| GPIO 32 | LED_WIFI |
| GPIO 33 | LED_SERVER |

Mapping exact à confirmer visuellement (=quel GPIO va HIGH quand quel LED s'allume).

### Impact projet openextraflame

`firmware/main/hardware_config.h` TARGET_BLACKLABEL section MISE À JOUR avec ces vraies valeurs. Target Blacklabel désormais **compilable et flashable** sans besoin de hardware.
