# Build openextraflame

## Prérequis

- Docker + Docker Compose OU installation manuelle ESP-IDF v5.2+
- USB-UART adaptateur (=CH340G, CP2102, FT232) 3.3V logic
- ESP32 dev board OU module Extraflame Black Label

## Build via Docker (=recommandé)

Le container `Dockerfile.esp-idf` contient déjà tout l'environnement Espressif.

### 1. Setup

```bash
cd ~/projects/openextraflame
docker compose build
```

### 2. Configurer la cible ESP32

```bash
docker compose run --rm esp-idf idf.py set-target esp32
```

### 3. Compiler pour Target External (=ESP32 spare)

```bash
docker compose run --rm esp-idf idf.py -DTARGET=external build
```

Le firmware est produit dans `firmware/build/openextraflame.bin`.

### 4. Compiler pour Target Black Label (=reflash original)

```bash
docker compose run --rm esp-idf idf.py -DTARGET=blacklabel build
```

⚠️ **Ne flashe PAS avant d'avoir un backup du firmware original !**

### 5. Flasher

Configuration du device USB dans `docker-compose.yml` (défaut : /dev/ttyUSB0).

```bash
docker compose run --rm esp-idf idf.py -p /dev/ttyUSB0 flash monitor
```

### 6. Monitor série uniquement

```bash
docker compose run --rm esp-idf idf.py -p /dev/ttyUSB0 monitor
```

Ctrl+] pour sortir.

## Build sans Docker (=install ESP-IDF native)

Voir doc officielle Espressif : https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

```bash
cd ~/esp/esp-idf
. ./export.sh
cd ~/projects/openextraflame/firmware
idf.py -DTARGET=external build
```

## Structure des artefacts build

```
firmware/build/
├── bootloader/bootloader.bin       # 0x1000
├── partition_table/partition-table.bin  # 0x8000
└── openextraflame.bin              # 0x10000 (=ota_0)
```

## Flash offsets (=référence)

| Fichier                           | Offset      | Notes                    |
|-----------------------------------|-------------|--------------------------|
| bootloader.bin                    | 0x1000      | ESP-IDF 2nd stage        |
| partition-table.bin               | 0x8000      | Table 4KB                |
| openextraflame.bin                | 0x10000     | ota_0                    |

## Flash rapide via esptool (=manuel)

```bash
esptool.py --port /dev/ttyUSB0 --baud 460800 write_flash \
    0x1000 bootloader.bin \
    0x8000 partition-table.bin \
    0x10000 openextraflame.bin
```

## Restauration firmware Extraflame original (=si nécessaire)

Si tu as un dump du firmware original :

```bash
esptool.py --port /dev/ttyUSB0 --baud 460800 write_flash \
    0x0 /path/to/extraflame_dump.bin
```

Le dump est un flash complet 4MB, il restaure TOUT dans son état d'origine.
