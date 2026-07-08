> 📜 **Journal historique du reverse.** Ce document reflète ce qu'on pensait AU MOMENT
> de l'analyse. Certaines valeurs (adresses de registres, rôle maître/esclave, baud) ont été
> **corrigées depuis** par la validation sur le vrai poêle. Pour le protocole ACTUEL et validé,
> voir [`../docs/PROTOCOLE-MICRONOVA.md`](../docs/PROTOCOLE-MICRONOVA.md).

# Analyse Ghidra du firmware Extraflame

Mis à jour 2026-07-03 après installation locale et tests.

## Statut installation actuelle

**Ghidra 12.1.2 installé et fonctionnel** dans `~/opt/ghidra/`.

Points clés :
- JDK 21 pré-installé dans `~/opt/jdk21/`
- Ghidra 12.1.2 (juin 2026) = latest stable
- **Support Xtensa NATIF** (=nouveauté Ghidra 12+, plus besoin de plugin externe !)
- Sleigh files : `~/opt/ghidra/Ghidra/Processors/Xtensa/`
  - `xtensa_le.slaspec` (=little endian, ESP32)
  - `xtensa_be.slaspec` (=big endian)

## Lancement

### GUI

```bash
export JAVA_HOME=~/opt/jdk21
~/opt/ghidra/ghidraRun
```

### Headless (=CLI scripting)

```bash
export JAVA_HOME=~/opt/jdk21
~/opt/ghidra/support/analyzeHeadless \
    ~/opt/ghidra-projects openxflame \
    -import /home/user/Downloads/partition_ota0.bin \
    -processor "Xtensa:LE:32:default" \
    -loader BinaryLoader \
    -loader-baseAddr 0x400d0020
```

## Segments à mapper (=important !)

Depuis l'header ESP32 image :

| Segment | Vaddr        | Size    | Type                        |
|---------|--------------|---------|-----------------------------|
| 0       | 0x3f400020   | 336464  | drom (=constants + strings) |
| 1       | 0x3ffb0000   | 27772   | dram (=RW data)             |
| 2       | 0x40080000   | 28956   | iram (=fast code)           |
| 3       | 0x400d0020   | 690796  | irom (=code)                |
| 4       | 0x4008711c   | 57500   | iram2 (=text)               |
| 5       | 0x50000000   | 16      | rtc_slow_mem                |

Entry point : `0x40081470`

⚠️ **Limitation actuelle** : le loader BinaryLoader charge tout le fichier à UN SEUL address. Les cross-references entre segments ne sont pas résolues.

**Solutions weekend** :

Option A : GUI multi-segment load
1. Import le fichier
2. Dans le CodeBrowser, `Window > Memory Map`
3. Créer une région pour chaque segment avec bon vaddr
4. Import chaque segment de son file offset au vaddr correspondant

Option B : Custom loader ESP32
1. Cloner `github.com/tenable/esp32_image_parser` ou similaire
2. Générer un ELF proprement structuré
3. Importer l'ELF au lieu du .bin brut

Option C : Utiliser des projets publics
1. `github.com/mryndzionek/esp32_image_parser`
2. `github.com/tempesta-tech/tempesta-esp32`
3. Générer ELF → import Ghidra normal

## Script Java d'extraction fourni

Voir `tools/ghidra_scripts/ExtractGPIOs.java`.

Fonctionnement :
- Cherche les strings `gpio_set_level`, `uart_set_pin`, `LED_*`, etc.
- Trouve les références depuis le code
- Dump les 12 instructions précédentes (=probables `movi`/`l32r` avec les args)

Usage :

```bash
export JAVA_HOME=~/opt/jdk21
~/opt/ghidra/support/analyzeHeadless \
    ~/opt/ghidra-projects openxflame \
    -process partition_ota0.bin \
    -noanalysis \
    -scriptPath ~/opt/ghidra-scripts \
    -postScript ExtractGPIOs.java
```

Placer le fichier dans `~/opt/ghidra-scripts/`.

## Findings actuels sans multi-segment

Même avec le loading imparfait, on a récupéré via strings + objdump :

### Strings confirmées

```
gpio_set_level        → présente à 0x400d1fda
gpio_set_direction    → présente à 0x400d1fb4
uart_set_pin          → présente à 0x400d196c
uart_param_config     → présente à 0x400d1943
resetButton           → présente à 0x400deda2
ResetButton           → présente à 0x400d9fc7
```

### Fonctions custom Extraflame

```
../main/main.c
../main/http_server.c
../main/wifi_manager.c
../main/ota/Commands.c
../main/ota/SerialOTA2.c
../main/ota/WifiOta.c
../main/uart/rwms_master.c
../main/uart/serial.c
```

### Signatures de logs révélatrices

Trouvée dans strings :
```
"uart_set_pin(SERIAL_UPGRADE_PORT, SERIAL_UPGRADE_TX, SERIAL_UPGRADE_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)"
```

Les valeurs `SERIAL_UPGRADE_TX/RX` sont des `#define` constants dans le code. Leur valeur numérique est chargée par `movi` avant l'appel `uart_set_pin`. Ghidra GUI + multi-segment permet de les extraire.

## Alternative avec Docker esp-idf

Sans avoir à setup Ghidra, on peut disassembler via l'outil ESP-IDF :

```bash
docker run --rm -v /home/user/Downloads:/work espressif/idf:v5.2.2 bash -c "
    xtensa-esp32-elf-objdump -D -b binary -m xtensa /work/partition_ota0.bin > /work/ota0_disasm.txt
"
```

Génère 871k lignes de disassembly. Grep pour patterns :

```bash
# Cherche les l32r qui chargent une addresse près d'un string
grep -B 5 "call.*gpio_set_level" ~/Downloads/ota0_disasm.txt | head -30
```

## Script Python analyze_ota0.py

Voir `tools/analyze_ota0.py`.

Fonctionnalités :
- Parse ESP32 image header + segments
- Extract strings avec adresses correctes
- Recherche patterns GPIO/UART/LED
- Extract 20 registres RAM_ Micronova
- Détecte 20 topics MQTT
- Comptage constants immédiats depuis disasm

Usage :

```bash
python3 tools/analyze_ota0.py partition_ota0.bin ota0_disasm.txt
```

Résultats déjà générés dans `analysis/tools/analysis_output.txt`.

## Questions restantes pour firmware complet

### GPIO Target Blacklabel

Nécessite multi-segment load Ghidra ou custom loader :

```c
// firmware/main/hardware_config.h TARGET_BLACKLABEL section
#define LED_POWER_PIN               GPIO_NUM_?  // TODO Ghidra
#define LED_BLE_PIN                 GPIO_NUM_?  // TODO Ghidra
#define LED_WIFI_PIN                GPIO_NUM_?  // TODO Ghidra
#define LED_SERVER_PIN              GPIO_NUM_?  // TODO Ghidra
#define CONFIG_BUTTON_PIN           GPIO_NUM_?  // TODO Ghidra
#define STOVE_UART_TX_PIN           GPIO_NUM_?  // TODO Ghidra
#define STOVE_UART_RX_PIN           GPIO_NUM_?  // TODO Ghidra
```

### Adresses HEX Micronova

Complèter `firmware/main/micronova.h` :

```c
typedef enum {
    MN_RAM_STOVE_STATUS      = 0x??,  // TODO Ghidra ou test empirique
    MN_RAM_ACCENDI           = 0x??,
    ...
} mn_ram_addr_t;
```

### HMAC MQTT

Cherche `mbedtls_md_hmac` + son caller pour comprendre la key derivation. Nécessaire pour Guardian Mode.

## Fichiers artefacts

- Ghidra binary : `~/opt/ghidra/`
- JDK 21 : `~/opt/jdk21/`
- Ghidra projects : `~/opt/ghidra-projects/openxflame/`
- Ghidra scripts custom : `~/opt/ghidra-scripts/`
- Script analyze Python : `tools/analyze_ota0.py`
- Script extract Java : `tools/ghidra_scripts/ExtractGPIOs.java`
- Disasm complet : `~/Downloads/ota0_disasm.txt` (=871k lignes)
- Output analyzer : `analysis/tools/analysis_output.txt`

## TODO weekend

- [ ] GUI Ghidra : `~/opt/ghidra/ghidraRun`
- [ ] Multi-segment load ota0.bin (=créer memory regions pour chaque vaddr)
- [ ] Auto-analyze avec cross-refs résolues
- [ ] Run ExtractGPIOs.java sur projet analysé
- [ ] Extraire GPIO exacts LEDs + bouton reset + UART
- [ ] Extraire adresses HEX registres RAM_ Micronova
- [ ] Mettre à jour `firmware/main/hardware_config.h`
- [ ] Mettre à jour `firmware/main/micronova.h`
- [ ] Recompile Target Blacklabel avec valeurs correctes

## Setup FINAL 2026-07-03 - ELF multi-segment fonctionnel

Progression finale de la session dump :

### 1. Génération ELF depuis image ESP32

Script `tools/make_esp32_elf.py` crée un ELF Xtensa avec les 6 segments mappés à leur vaddr propre :

```
Entry point: 0x40081470
Segments: 6
  .rodata          vaddr=0x3f400020 size=336464
  .dram            vaddr=0x3ffb0000 size=27772
  .iram            vaddr=0x40080000 size=28956
  .text            vaddr=0x400d0020 size=690796
  .iram            vaddr=0x4008711c size=57500
  .rtc_slow        vaddr=0x50000000 size=16
```

Usage :
```bash
python3 tools/make_esp32_elf.py ~/Downloads/partition_ota0.bin ~/Downloads/ota0.elf
```

Résultat : `ota0.elf` = 1.1MB, ELF 32-bit LSB, Xtensa.

### 2. Import Ghidra 12 headless

```bash
export JAVA_HOME=~/opt/jdk21
~/opt/ghidra/support/analyzeHeadless \
    ~/opt/ghidra-projects openxflame \
    -import ~/Downloads/ota0.elf \
    -processor "Xtensa:LE:32:default"
```

Résultat :
- Auto-analyze OK
- Strings résolues à leurs vraies adresses (=0x3f4xxxxx)
- Base analyzers passés :
  - Basic Constant Reference
  - Data Reference
  - Disassemble Entry Points
  - ELF Scalar Operand References
  - Subroutine References

### 3. État actuel des refs

Les strings sont trouvées mais les cross-refs code → string ne sont pas toutes créées automatiquement. Les strings comme `gpio_set_level` sont utilisées comme arguments de `ESP_LOG` (=printf-style), donc leur ref est un chargement `l32r` dans le caller.

**GUI weekend** :
- Sélectionner une string dans Symbol Tree
- View → References → Show References to Address
- Ou : ScriptManager → run "Constant Propagation Analyzer" en mode Aggressive
- Ou : lancer le "Aggressive Instruction Finder"

### 4. Ce qui reste à faire weekend

Ordre efficace :
1. Ouvrir Ghidra GUI : `~/opt/ghidra/ghidraRun`
2. Open project openxflame
3. Open ota0.elf
4. Analysis → Auto Analyze → cocher "Aggressive Instruction Finder"
5. Rerun ExtractGPIOs.java depuis Script Manager
6. Explorer les callers de gpio_set_level / uart_set_pin
7. Noter les valeurs immédiates chargées avant les calls
8. Update firmware/main/hardware_config.h avec vraies valeurs

### 5. Alternative empirique 

Plus rapide que reverse Ghidra pur pour identifier GPIOs :

```
1. Flash OpenXtraflame Target External sur ESP32 spare
2. Wire ESP32 spare GND à module Black Label GND
3. Wire un GPIO ESP32 spare (=en INPUT_PULLUP) sur chaque GPIO
   du module Black Label un par un
4. Toggle LEDs Black Label via l'application (=ou en le laissant tourner)
5. Voir quel GPIO ESP32 spare voit "HIGH" quand LED s'allume
6. Résultat en 30 min
```

Cette méthode ne nécessite pas de reverse et donne des résultats certains.

## Solution 1 tentée 2026-07-03 - RÉSULTAT NÉGATIF

Compilation d'un projet ESP-IDF v4.3 minimal avec appels aux fonctions cibles pour extraction des symboles :

```bash
docker run --rm -v /tmp/idf_ref:/project espressif/idf:release-v4.3 bash -c "
    . \$IDF_PATH/export.sh
    idf.py build
"
```

ELF produit : `analysis/tools/idf43_reference.elf` (=4.1MB, 2381 symbols)
Symbols dump : `analysis/tools/idf43_symbols.txt`

Adresses cibles ESP-IDF v4.3 vanilla :
```
gpio_set_level      @ 0x400d60b0
gpio_config         @ 0x400d63d8
gpio_set_direction  @ 0x400d62ec
uart_set_pin        @ 0x400d4b54
uart_param_config   @ 0x400d4e7c
uart_driver_install @ 0x400d556c
```

### Fingerprint match tentée

Test recherche des premiers 8-24 bytes de chaque fonction de la référence dans le binaire Extraflame :

**Résultat : 0 match** même à 8 bytes de fingerprint.

### Pourquoi ça échoue

- Boot log Extraflame indique `ESP-IDF v4.3-dirty`
- "dirty" = Extraflame a patché la SDK ESP-IDF
- Modifications cascade dans tout le binaire (=literal pools, PC-relative offsets)
- Byte-identical impossible

### Ce qui pourrait marcher

- **Ghidra Function ID avec fuzzy matching** : GUI, weekend
- **BinDiff** ou **Diaphora** : compare structuralement 2 binaires
- **Testing empirique** sur ESP32 spare : reste le plus rapide (30 min)

### Décision

Approche B (=testing empirique) définitivement recommandée. Solution 1 abandonnée.

## Radare2 install final 2026-07-03

Installed via sudo : `radare2 5.9.8` (=distro debian)

### Commandes utiles

```bash
# Ouvrir référence ELF pour comparer aux Extraflame
r2 -A /home/user/Downloads/idf43_reference.elf
> afl~gpio_set_level   # trouve gpio_set_level dans référence

# Disasm bytes de Extraflame avec rasm2
python3 -c "
data = open('/home/user/Downloads/partition_ota0.bin','rb').read()
print(data[OFFSET:OFFSET+SIZE].hex())
" | rasm2 -a xtensa -b 32 -d $(cat)
```

### Limitations rencontrées

- r2 warns "Cannot find asm.parser for xtensa.pseudo" (=extension manquante)
- Analyse aaa très lente sur ELF ESP32 (=timeout 2 min)
- Xtensa arch OK pour disasm ligne par ligne (rasm2)
- Autoanalyzer r2 pas idéal pour Xtensa ESP32

### Conclusion tools

Radare2 + Ghidra fonctionnent mais **le reverse binaire Xtensa niveau instruction reste laborieux** :
- Sans symboles ESP-IDF matching : trou noir
- ESP-IDF v4.3-dirty (patched) : fingerprint match impossible
- Xtensa disasm OK mais résolution manuelle nécessaire

**Chemin de moindre résistance = testing empirique 30 min.**
