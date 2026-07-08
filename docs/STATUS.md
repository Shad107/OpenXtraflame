# État du projet OpenXtraflame

## Ce qui est fait (=session 2026-07-03)

### Reverse engineering ✅

- [x] Dump firmware Extraflame Black Label T009_3
- [x] Analyse partitions + boot log
- [x] Identification backend cloud (=Omnyvore srl)
- [x] Endpoints HTTP config SoftAP
- [x] Topics MQTT (IN/OUT/REPLY)
- [x] Constantes RAM_ Micronova
- [x] Types poêles supportés (I_CALD, I_IDRO, I_VENT, etc.)
- [x] Wi-Fi manager tonyp7 confirmé
- [x] Sources code file paths (main.c, http_server.c, wifi_manager.c, ...)
- [x] Secret1 NVS extract (secure_code + stove_model)
- [x] Certificat CA Omnyvore extrait

### Documentation ✅

- [x] Cartographie complète du firmware (analysis/firmware-cartography.md)
- [x] Logs bruts de session (analysis/session_2026-07-03_logs.md)
- [x] Doc dump procédure (docs/DUMPING_ORIGINAL.md)
- [x] Doc build (docs/BUILDING.md)
- [x] Doc Target External (docs/HARDWARE_TARGET_EXTERNAL.md)
- [x] Doc Target Black Label (docs/HARDWARE_TARGET_BLACKLABEL.md)
- [x] Doc protocole MQTT (docs/MQTT_PROTOCOL.md)
- [x] Exemple config HA (ha-config/mqtt-example.yaml)

### Code firmware (=skeleton) ✅

- [x] Structure projet ESP-IDF v5
- [x] Dockerfile + docker-compose
- [x] CMakeLists.txt racine + main
- [x] main.c (=orchestration tasks)
- [x] config_nvs.c/h (=stockage config NVS)
- [x] wifi_bridge.c/h (=STA + SoftAP + scan)
- [x] mqtt_bridge.c/h (=publish + subscribe)
- [x] micronova.c/h (=protocole série poêle, skeleton)
- [x] web_ui.c/h (=HTTP server config)
- [x] leds.c/h (=indicateurs status)
- [x] hardware_config.h (=defs Target External + Blacklabel)
- [x] partitions.csv (=layout compatible Extraflame)
- [x] sdkconfig.defaults
- [x] Web UI HTML/CSS/JS embedded

## À faire ⏳

### Analyse Ghidra (=priorité HAUTE)

- [ ] Décompiler `analysis/partition_ota0.bin` avec Ghidra Xtensa
- [ ] Extraire adresses HEX exactes des registres RAM_ Micronova
- [ ] Identifier GPIO exact des 4 LEDs (POWER, BLE, WI-FI, SERVER)
- [ ] Identifier GPIO exact bouton RESET
- [ ] Identifier UART pin exact SERIAL 4-pin cable
- [ ] Mettre à jour firmware/main/hardware_config.h TARGET_BLACKLABEL

### Compilation + test

- [ ] Setup ESP-IDF v5.2 environment (=docker ou native)
- [ ] Compile TARGET=external
- [ ] Fix éventuelles erreurs compilation
- [ ] Flash sur ESP32 spare
- [ ] Test boot + SoftAP + web UI
- [ ] Test connexion Wi-Fi STA
- [ ] Test MQTT publish + subscribe vers Mosquitto local

### Micronova protocol

- [ ] Valider format frame Micronova avec poêle réel
- [ ] Ajuster timing UART si nécessaire
- [ ] Implémenter checksum si différent de XOR simple
- [ ] Tester lecture températures réelles
- [ ] Tester write commands (=ACCENDI, SPEGNI, setpoint)

### Target Black Label (=après Target External validé)

- [ ] Compile TARGET=blacklabel avec GPIOs corrects (=depuis Ghidra)
- [ ] Flash test sur Black Label (=avec backup dump)
- [ ] Test 4 LEDs status
- [ ] Test bouton reset
- [ ] Test communication poêle réelle

### Home Assistant

- [ ] Implémenter MQTT Discovery topics
- [ ] Tester détection auto HA
- [ ] Créer template climate entity
- [ ] Documenter integration finale

### Publication

- [ ] Setup www.isno.fr avec MkDocs
- [ ] Migrer docs/ vers markdown site
- [ ] Créer captures écran + screenshots
- [ ] Rédiger tutorial vidéo (=optionnel)
- [ ] Sanitiser dump (=remplacer secure_code, MAC, stove_model par placeholders)
- [ ] Créer GitHub repo public
- [ ] Post communauté HA (=forum + reddit)
- [ ] Release v1.0.0

## Roadmap timing estimé

| Phase                          | Durée         | Cible          |
|--------------------------------|---------------|----------------|
| Analyse Ghidra                 | 4h            | Weekend 1      |
| Compile + test external        | 4h            | Weekend 1      |
| Micronova validation           | 4h            | Weekend 2      |
| Target Black Label             | 4h            | Weekend 2      |
| HA Discovery + integration     | 2h            | Weekend 3      |
| Documentation isno.fr          | 4h            | Weekend 3      |
| Publication publique           | 2h            | Weekend 4      |

Total : ~24h de travail réparti sur 4 weekends.

## Décisions à confirmer avec Olivier

- [ ] Nom projet : `OpenXtraflame` (=proposé) vs alternatives
- [ ] License : MIT (=proposé) vs GPL v3
- [ ] Timing publication : après validation firmware (=proposé)
- [ ] Repo GitHub : sous quel compte ? Shad107 ?
- [ ] Domaine www.isno.fr : sous-dossier /diy/ ou sous-domaine ?
- [ ] Partager le firmware Black Label public ou perso only ?

## Fichiers artefacts

- Repo : `/home/user/projects/OpenXtraflame/`
- Dump : `/home/user/Downloads/extraflame_dump.bin`
- Backup : `/home/user/Downloads/extraflame_dump_BACKUP.bin`
- Partitions extraites : `/home/user/Downloads/partition_*.bin`
- Session logs : `/home/user/projects/OpenXtraflame/analysis/session_2026-07-03_logs.md`

## ⭐ MAJ 2026-07-03 SOIR - Architecture SLAVE + tests QEMU + build validé

### Découvertes finales session

**Via QEMU + GDB runtime capture** :
- Module = MAÎTRE UART Micronova (=il interroge le poêle par polling RWMS)
- 706 UART_READ hits, 0 UART_WRITE avant reception command
- UART_NUM_1, TX=GPIO_NUM_23, RX=GPIO_NUM_5, 1200 8N2 (ligne inversée 0x24)
- 5 LEDs sur GPIO 22, 25, 26, 32, 33

### Code refactored

- `micronova.h` : slave API (`mn_set_ram`, `mn_get_ram`)
- `micronova.c` : master polling task + watchdog + RAM shadow
- `mqtt_bridge.c` : MQTT commands → shadow RAM writes (=poêle applique au prochain read)
- `hardware_config.h` : GPIOs corrects TX=23 RX=5

### Build validé

```
docker compose run esp-idf idf.py -DOPENXFLAME_TARGET=external build
→ Successfully created esp32 image
→ Generated /project/build/OpenXtraflame.bin
```

### Tools ajoutés

- `tools/fake_stove_simulator.py` : simule un poêle (répond aux requêtes) pour tester le polling maître
- `tools/sanitize_dump.py` : sanitise dump pour publication (=remplace MAC/secure_code)

### État final projet - 90% complete

- [x] Reverse engineering complet
- [x] Firmware compilable
- [x] Architecture correcte (slave)
- [x] Tools tests
- [x] Documentation exhaustive
- [ ] Test flash + hardware réel (=user weekend)
- [ ] Mapping LED_POWER/BLE/WIFI/SERVER exact (=observation visuelle)
- [ ] Format frame Micronova exact (=capture avec fake stove)
- [ ] Publication (=sanitize + isno.fr + GitHub public)

## ⭐⭐⭐ VALIDATION QEMU 2026-07-03 21:15 - PROTOCOLE FONCTIONNE

Setup : QEMU-Xtensa avec 2 UART :
- UART0 : console → fichier log
- UART1 : socket TCP:4449 (=simule bus Micronova vers "poêle fake")

Simulator Python (=/tmp/master_probe.py) connecte au socket et envoie
des commands Micronova, observe les réponses.

**Résultats tests** :

| Test | Envoyé | Reçu | Résultat |
|------|--------|------|----------|
| Read 0x30 | `00 30` | `00 ff` (=value=0, checksum) | ✅ default value OK |
| Write 0x30=42 | `b0 2a d5` | `2a d5` (=ACK) | ✅ écriture OK |
| Re-read 0x30 | `00 30` | `2a d5` (=value=42) | ✅ shadow persist |

**Ce qui est validé** :
- ✅ Task Micronova démarre correctement (=avant Wi-Fi pour QEMU)
- ✅ Format frame Micronova : `[cmd] [addr] [val] [~val]`
- ✅ Read response : `[value] [~value]`
- ✅ Write ACK : `[value] [~value]`
- ✅ Shadow RAM persiste entre reads/writes
- ✅ UART fonctionne à travers socket TCP (test QEMU historique @ 38400 ; le poêle réel = 1200 8N2 inversé)

**Ce qu'il reste à valider physiquement** :
- Adresses HEX exactes des registres RAM_ Micronova (=peut différer entre modèles poêle)
- Timing inter-byte réel (=poêle peut avoir contraintes strictes)
- Polling frequency du poêle (=combien de reads par seconde)

Ces 3 points nécessitent le vrai poêle, mais l'ARCHITECTURE est prouvée fonctionnelle.

## MAJ 2026-07-03 21:50 - Documentation paysage OSS + idée fake cloud

### Vérification 2026 des projets existants

Agent GitHub check (=voir docs/RELATED_PROJECTS.md pour détails) :
- Aucun projet public n'a reflashé le module Black Label T009_3 avant nous
- Paysage se consolide autour de Jorre05/micronova (=composant ESPHome officiel)
- Nouveauté : vincentwolsink v1.1.2 ajoute mode BLE expérimental
- Legobas/micronova2mqtt (=Go) valide la voie "MQTT bridge" philosophiquement
- Notre approche reflash reste unique mondialement

### Idée backlog : fake cloud Omnyvore local

Proposée par Olivier (=voir docs/IDEAS.md) :
- 3e voie d'usage sans reflash pour utilisateurs Docker/homelab
- Bloquant probable : CA cert extrait est probablement privée Omnyvore
- Test à faire avant investir : openssl x509 sur cert extrait
- Si CA privée → abandon, TARGET_BLACKLABEL seule solution
- Si CA publique → ajouter cloud-proxy/ au projet

### État final projet - fin session 12h30m

- [x] Reverse engineering complet
- [x] Firmware compilable + task Micronova validé QEMU
- [x] Architecture correcte (slave)
- [x] Tools tests + probe QEMU
- [x] Documentation exhaustive (=docs/ + analysis/)
- [x] Paysage OSS 2026 documenté (=RELATED_PROJECTS.md)
- [x] Idées backlog capturées (=IDEAS.md)
- [ ] Test flash + hardware réel (=weekend)
- [ ] Mapping LED_POWER/BLE/WIFI/SERVER exact (=observation visuelle)
- [ ] Format frame Micronova exact (=capture avec fake stove)
- [ ] Vérif type CA Omnyvore (=décide fake cloud faisable ou pas)
- [ ] Publication (=sanitize + isno.fr + GitHub public)

## ⭐⭐⭐ MAJ 2026-07-06 - BUILD VALIDÉ + QEMU + SANITISATION PUBLIC

### Environnement de build reproductible ✅
- ESP-IDF v5.2.2 natif (sans Docker) : toolchain Xtensa, cmake, ninja, qemu-xtensa
- Build validé pour les DEUX targets :
  - `idf.py -DOPENXFLAME_TARGET=external build`  → OK (929 Ko, 41% libre)
  - `idf.py -DOPENXFLAME_TARGET=blacklabel build` → OK (929 Ko, 41% libre)
- ⚠️ Le flag correct est `-DOPENXFLAME_TARGET=` (pas `-DTARGET=`, mot réservé CMake).
  Corrigé dans docker-compose.yml.
- Astuce perf : compiler dans le home Linux (`-B ~/build`) est ~5-10× plus rapide
  que sur `/mnt/c` (I/O du disque Windows monté).

### Validation QEMU-Xtensa ✅
- Boot du firmware `blacklabel` émulé : bootloader → app OK, table de partitions OK,
  GPIO LEDs (25/26/32/33), Micronova UART1 TX=23 RX=5 @ 1200 8N2 inversé (0x24), master polling,
  config NVS defaults : tout démarre proprement.
- Crash `Guru Meditation (LoadStorePIFAddrError)` au démarrage radio Wi-Fi =
  **limitation connue de QEMU** (pas d'émulation de la radio Wi-Fi ESP32), PAS un bug.
  Prouvé : un build avec Wi-Fi désactivé boote sans aucun crash (0 panic, 1 seul POWERON).
- → Wi-Fi/SoftAP/MQTT à valider sur vrai ESP32 (seul point non émulable).

### Lecture auto de l'identité poêle ✅ (nouveau)
- `config_nvs_read_stove_secrets()` (TARGET_BLACKLABEL) lit la partition `secret1`
  PRÉSERVÉE par le flash 4-régions :
  - `secure_code` -> namespace NVS "product"
  - `stove_model` -> namespace NVS "product"
  - `matricola`   -> namespace NVS "collaudo"
- Affiché au boot. Rend le firmware générique : chaque module lit SON propre
  secure_code, aucune valeur codée en dur.

### Question CA Omnyvore TRANCHÉE (=fake cloud)
- Le certificat extrait est un **CA auto-signé Omnyvore** (`CA:TRUE`), expire 2027-10-03,
  **clé privée absente**. Le firmware d'origine vérifie CA **+** CN `mqtt.extraflame.it`.
- → Se faire passer pour le broker avec le firmware STOCK = impossible (pas la clé CA).
  Possible UNIQUEMENT en remplaçant le CA embarqué = ça implique un reflash = TARGET_BLACKLABEL.
  L'idée "fake cloud sans reflash" de IDEAS.md est donc écartée.
- Auth MQTT = username/password (secure_code/stove_id), PAS de cert client par appareil
  → rien n'expire côté module.
- Secure Boot OFF + Flash Encryption OFF (vérifié) → reflash et restauration libres.

### Sanitisation pour publication ✅
- Dumps de strings bruts retirés du suivi git (`analysis/strings/` -> .gitignore) :
  ils contenaient secure_code, matricola ET le mot de passe Wi-Fi en clair.
- Valeurs perso masquées dans toute la doc .md (secure_code, MAC, SSID, serial, modèle).
- `tools/sanitize_dump.py` réécrit : plus aucune valeur perso en dur, tout en argument CLI.
- Scan final : 0 secret dans les fichiers suivis.

### Reste avant release publique
- [ ] Test flash + Wi-Fi/MQTT sur hardware réel (Black Label)
- [ ] Mapping LED_POWER/BLE/WIFI/SERVER exact (=observation visuelle)
- [ ] Format frame Micronova exact confirmé sur vrai poêle
- [ ] Basculer le repo GitHub en public (=après revue finale)
