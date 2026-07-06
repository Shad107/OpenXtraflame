# openextraflame

Custom firmware open-source pour les poêles à granulés Extraflame (et compatibles Micronova).

**Statut : ⚠️ EN DÉVELOPPEMENT PRIVÉ - PAS ENCORE PUBLIÉ**

## Objectif

Remplacer la dépendance cloud Omnyvore (`mqtt.extraflame.it:8883`) par un contrôle 100% local via MQTT vers Home Assistant / Mosquitto.

## Deux targets

### Target A : "External" (=recommandé, safe)

Firmware pour un ESP32 spare connecté en parallèle au bus série du poêle. Le module Extraflame Black Label reste INTACT.

- Matériel : ESP32-WROOM-32 spare + fils dupont
- Câblage : bus série poêle (=connecteur TA côté main board)
- Approche similaire à philibertc/micronova_controller
- Public/documenté sur www.isno.fr

### Target B : "Black Label Replacement" (=avancé)

Firmware qui REMPLACE celui d'origine sur le module Extraflame Black Label. Nécessite dump du firmware original en backup.

- Matériel : module Black Label 289€ Extraflame
- Réutilise pinout SERIAL 4-pin natif
- Restauration possible via dump firmware original
- Non-public, usage personnel uniquement

## Architecture

```
┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
│                 │  UART   │                 │  Wi-Fi  │                 │
│  Poêle          │38400 8N1│  ESP32 avec     │  MQTT   │ Home Assistant  │
│  Extraflame     ├─────────┤  openextraflame ├─────────┤ + Mosquitto     │
│  Teodora Evo    │Micronova│  firmware       │  local  │ (=local only)   │
│                 │  proto  │                 │         │                 │
└─────────────────┘         └─────────────────┘         └─────────────────┘
```

## Roadmap

- [x] Dump firmware original Extraflame Black Label
- [x] Analyse partitions + protocoles
- [x] Cartographie complète firmware
- [ ] Setup ESP-IDF v5.x + toolchain
- [ ] Skeleton code + build
- [ ] Wi-Fi manager fork tonyp7
- [ ] MQTT client vers Mosquitto local
- [ ] UART Micronova protocole
- [ ] Web UI SoftAP config
- [ ] Test sur ESP32 spare (=Target A)
- [ ] Test sur Black Label (=Target B, avec backup)
- [ ] Integration HA MQTT Discovery
- [ ] Documentation www.isno.fr
- [ ] Release publique GitHub

## Structure du projet

```
openextraflame/
├── README.md                    # ce fichier
├── LICENSE                       # MIT
├── docker-compose.yml            # env dev reproductible
├── Dockerfile.esp-idf            # container ESP-IDF v5
├── firmware/
│   ├── main/                     # code C ESP-IDF
│   ├── components/               # libs custom
│   ├── boards/
│   │   ├── external/             # config Target A
│   │   └── blacklabel/           # config Target B
│   └── build.sh
├── web/                          # HTML/CSS/JS pour SoftAP UI
├── docs/                         # documentation utilisateur
├── analysis/                     # reverse engineering notes
├── ha-config/                    # exemples HA yaml
└── tools/                        # scripts helpers
```

## Analyse du firmware Extraflame

Voir [analysis/firmware-cartography.md](analysis/firmware-cartography.md) pour reverse engineering complet.

Voir [analysis/session_2026-07-03_logs.md](analysis/session_2026-07-03_logs.md) pour logs bruts session dump.

## Licence

MIT (=voir LICENSE)

## Disclaimer

Ce projet est un travail de reverse engineering effectué sur du matériel personnel dans un but éducatif et pour usage personnel. Aucun code binaire dérivé d'Extraflame n'est distribué. L'utilisation de ce firmware sur votre matériel est à vos risques et périls. Extraflame ne fournit pas de support pour cette utilisation.
