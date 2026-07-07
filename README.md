# OpenXtraflame

**Firmware ESP32 open-source qui remplace la dépendance cloud du module Wi-Fi Extraflame Black Label par un contrôle 100 % local via MQTT / Home Assistant.**

[![Latest release](https://img.shields.io/github/v/release/Shad107/OpenXtraflame?include_prereleases&label=release)](https://github.com/Shad107/OpenXtraflame/releases)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.2.3-blue)](https://docs.espressif.com/projects/esp-idf/en/v5.2.3/)
[![License MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![Targets](https://img.shields.io/badge/targets-Black%20Label%20%7C%20M5Stack%20ATOM-orange)]()

Le poêle à granulés Extraflame Teodora Evo (et cousins Micronova : EdilKamin, LAMINOX, Freepoint, Karmek One) se pilote depuis l'app officielle **Total Control 2** via un broker MQTT propriétaire hébergé par [Omnyvore srl](https://omnyvore.com). OpenXtraflame remplace complètement cette couche cloud par un bridge MQTT local vers **Home Assistant** : le module d'origine reste installé, aucune modification visible, aucune dépendance externe.

📖 **Article complet du reverse engineering + guide pas à pas** : [www.isno.fr/projets/openxtraflame](https://www.isno.fr/projets/openxtraflame)

---

## ✨ Fonctionnalités

- 🔥 **Bridge Micronova UART maître** : interroge le poêle par polling RWMS à **1200 bauds 8N2, ligne inversée (0x24)**
- 📡 **Bridge MQTT local** vers Mosquitto / Home Assistant, sans passer par le cloud
- 🏠 **HA MQTT Discovery natif** : au premier `MQTT_EVENT_CONNECTED`, le module publie 11 topics retenus qui font apparaître automatiquement un device `Extraflame - OpenXtraflame Black Label` avec sensors T°/puissance/état, switch on/off, button reset, number setpoint et select puissance
- 🔎 **Détection auto du broker MQTT** : bouton "🔍 Détecter HA" qui tente `_mqtt._tcp`, `_home-assistant._tcp` puis `homeassistant.local` en mDNS et remplit host + port
- 🌐 **Web UI embarquée** : Dashboard, Wi-Fi, MQTT, Poêle, OTA, Debug, Avancé
- 📶 **Provisioning SoftAP + dual APSTA** : le SoftAP `openxtraflame-XXXX` reste up en permanence, never brick
- ⬆️ **OTA** upload direct depuis le navigateur OU pull depuis une URL HTTPS (=CA bundle Mozilla, barre de progression réelle via `esp_https_ota_perform` loop)
- 🔁 **Auto-wipe `phy_init` au changement de version firmware** : évite qu'un OTA hérite d'une calibration RF invalide
- 🛡️ **Rollback safety** : `esp_ota_mark_app_valid_cancel_rollback()` sur boot réussi, un firmware buggé rollback tout seul
- 💀 **Last Will Testament MQTT** : HA grise instantanément les entités si le module disparaît
- 🔒 **Protection anti-clobber** des credentials Wi-Fi/MQTT : un save depuis un onglet non lié n'écrase plus le password stocké
- 🔍 **Live log Micronova** dans l'onglet Debug (=64 dernières trames RX/TX, refresh 1 s)
- 💡 **Mapping des 4 LEDs** conservé (=POWER / WI-FI / SERVER / BLE), sémantique identique à l'usine

## 🎯 Cibles supportées

Une même codebase, deux cibles via `-DOPENXFLAME_TARGET=...` :

| Cible | Matériel | Cas d'usage |
|---|---|---|
| `external` | M5Stack ATOM Lite (=ESP32-PICO ~12 €) branché en parallèle du bus poêle | Validation / dev / installation qui garde le module Extraflame intact |
| `blacklabel` | Module Wi-Fi Extraflame Black Label T009_3 (=le vrai) | Remplacement complet du firmware d'origine, zéro modification visible |

## 🚀 Installation rapide (=Black Label)

Le premier flash est **filaire uniquement** (=via CH340G USB-UART). Ensuite tous les upgrades passent par OTA depuis le Web UI.

1. Télécharger le tarball complet depuis [releases](https://github.com/Shad107/OpenXtraflame/releases/latest) :
   `openxtraflame-vX.Y.Z-blacklabel.tar.gz`
2. Décompresser + brancher le CH340G (=voir le guide de câblage sur [isno.fr](https://www.isno.fr/projets/openxtraflame), il faut maintenir le pin `IO0` de l'ESP32 à `GND` pendant le reset)
3. Flasher :
   ```bash
   ./flash.sh /dev/ttyUSB0     # Linux
   .\flash.ps1 -Port COM3      # Windows PowerShell
   ```
4. Rebrancher le module dans le poêle, se connecter au SoftAP `openxtraflame-XXXXXX` (=open, mdp libre au premier boot), ouvrir `http://192.168.4.1/`, renseigner le SSID + le broker MQTT, sauvegarder + redémarrer.

Une fois en STA, le poêle apparaît dans Home Assistant via MQTT Discovery (=à venir en v0.2.x).

## 🏗️ Architecture

```
┌─────────────────┐        ┌─────────────────┐        ┌─────────────────┐
│                 │  UART  │                 │  MQTT  │                 │
│  Poêle          │1200 8N2│  ESP32 flashé   │  Wi-Fi │ Home Assistant  │
│  Extraflame     ├────────┤  OpenXtraflame  ├────────┤ + Mosquitto     │
│  (carte         │inversé │  (MAÎTRE RWMS   │  local │ (local only,    │
│   Micronova)    │  0x24  │   + RAM shadow) │        │  zero cloud)    │
└─────────────────┘        └─────────────────┘        └─────────────────┘
```

- Le **module est maître Micronova** : il *interroge* le poêle par polling (protocole RWMS, `[loc][addr]` → `[checksum][value]`, checksum additif). **Validé sur le vrai poêle : UART1, 1200 bauds 8N2, ligne inversée (masque `0x24`).** Détail complet dans [`docs/PROTOCOLE-MICRONOVA.md`](docs/PROTOCOLE-MICRONOVA.md).
- Le firmware maintient une **RAM shadow** des registres Micronova **standard** (STOVE_STATE `0x21`, temp fumées `0x3E`, temp ambiante `0x01`÷2, ...). Les commandes MQTT (allumer/éteindre = write à `0x21`, consignes) sont **poussées au poêle par le module maître**.

## 🔨 Build from source

Prérequis : Docker (=on utilise l'image officielle `espressif/idf:v5.2.3`).

```bash
docker run --rm -v $PWD/firmware:/project -w /project \
  espressif/idf:v5.2.3 \
  idf.py -DOPENXFLAME_TARGET=blacklabel build
```

Le binaire d'app sort dans `firmware/build/openextraflame.bin`. Pour le tarball complet (=à utiliser en flash filaire propre), joindre également :

- `firmware/build/bootloader/bootloader.bin`
- `firmware/build/partition_table/partition-table.bin`
- `firmware/build/ota_data_initial.bin`

Un exemple de script `flash.sh` / `flash.ps1` est fourni dans chaque tarball de release.

## 📁 Structure du projet

```
OpenXtraflame/
├── README.md                    ← ce fichier
├── LICENSE                       ← MIT
├── docker-compose.yml            ← env dev reproductible
├── Dockerfile.esp-idf            ← image ESP-IDF v5.2.3
├── firmware/
│   ├── CMakeLists.txt            ← PROJECT_VER + OPENXFLAME_TARGET
│   ├── partitions.csv            ← layout Black Label conservé
│   ├── sdkconfig.defaults
│   └── main/                     ← code C ESP-IDF
│       ├── main.c                ← boot banner + phy_init wipe on version change
│       ├── wifi_bridge.c         ← SoftAP + STA dual APSTA
│       ├── mqtt_bridge.c         ← publish state + subscribe cmds
│       ├── micronova.c           ← maître RWMS + RAM shadow + debug ring buffer
│       ├── web_ui.c              ← HTTPD + endpoints REST
│       ├── ota.c                 ← upload / pull URL / rollback
│       └── web/                  ← index.html + script.js + style.css (=embedded)
├── analysis/                     ← reverse engineering notes
├── ha-config/                    ← exemples HA YAML
└── tools/                        ← scripts helpers
```

## 🗺️ Roadmap

**v0.1.x : Public MVP** (=✅ livré) :

- [x] Reverse engineering complet du module Extraflame Black Label
- [x] Firmware Target External (=M5Stack ATOM Lite) validé
- [x] Firmware Target Black Label validé sur hardware réel
- [x] Web UI + provisioning + OTA + rollback safety
- [x] Auto phy_init wipe on version change
- [x] UI polie : toasts non bloquants, modal dialogs, placeholders passwords intelligents
- [x] Barre de progression OTA réelle via polling `/ota/status`

**v0.2.x : Home Assistant native** (=✅ livré) :

- [x] MQTT Discovery : 11 entités auto-provisionnées dans HA (=sensors, switch, button, number, select)
- [x] Last Will Testament sur `<prefix>/availability` : HA grise auto quand le module coupe
- [x] Auto-discovery du broker MQTT via mDNS (=fallbacks `_mqtt._tcp`, `_home-assistant._tcp`, `homeassistant.local`)
- [x] Bouton "🔍 Détecter HA" dans l'onglet MQTT du Web UI

**v0.3.x : Ecosystème** :

- [ ] Guardian mode : archive silencieuse des OTA officielles Extraflame (=filet si Omnyvore ferme)
- [ ] Support autres poêles Micronova (=EdilKamin, LAMINOX, Freepoint, Karmek One) via profils dispatcher par `stove_type`
- [ ] Publication sur ESP-IDF Component Registry pour permettre le fork facile

## 🤝 Contribuer

Les issues et PRs sont bienvenues. Ce projet est un side project, les réponses peuvent être lentes.

Pour un bug de firmware :
1. Ouvrir un moniteur série et coller les logs (=`python -m serial.tools.miniterm COM3 115200`)
2. Préciser la version installée (=visible dans le banner boot ou l'onglet OTA du Web UI)

## 📜 Licence

MIT : voir [LICENSE](LICENSE).

## ⚠️ Disclaimer

Reverse engineering effectué sur du matériel personnel dans un but éducatif et de préservation (=si Omnyvore ferme demain, mon poêle perd son pilotage à distance). Aucun code binaire dérivé du firmware Extraflame n'est distribué. L'utilisation de ce firmware sur ton matériel est à tes risques et périls. Extraflame ne fournit pas de support pour cette utilisation.

## 🙏 Crédits

- [philibertc/micronova_controller](https://github.com/philibertc/micronova_controller) et [Jorre05/micronova](https://github.com/Jorre05/micronova) pour avoir défriché le protocole Micronova côté ESP externe
- Espressif pour ESP-IDF et l'excellente doc sur l'ESP32
- La communauté Ghidra pour le support natif Xtensa qui a rendu la décompilation possible
- L'équipe Home Assistant pour l'intégration MQTT et son modèle Discovery
