# Dump du firmware Extraflame original

Procédure pour extraire le firmware du module Extraflame Black Label. **NÉCESSAIRE avant Target Black Label** pour permettre restauration en cas de problème.

Cette procédure est le résultat d'une session de reverse engineering de 8h documentée. Voir `analysis/session_2026-07-03_logs.md` pour la version complète.

## Photos référence

Voir `analysis/photos/` pour les 17 photos étape par étape prises pendant le dump initial. Chaque étape ci-dessous référence les photos utiles.

- Vue générale du PCB : [01_pcb_overview.jpg](../analysis/photos/01_pcb_overview.jpg)
- Module ESP32 : [05_module_esp32_labels_readable.jpg](../analysis/photos/05_module_esp32_labels_readable.jpg)
- Connecteur SERIAL 4-pin : [03_serial_4pin_connector_closeup.jpg](../analysis/photos/03_serial_4pin_connector_closeup.jpg)
- Câble 4 wires colorés : [12_serial_4pin_cable_colors.jpg](../analysis/photos/12_serial_4pin_cable_colors.jpg)
- CN5 header : [07_cn5_3pin_header_macro.jpg](../analysis/photos/07_cn5_3pin_header_macro.jpg)
- Setup dump : [17_setup_uart_dupont.jpg](../analysis/photos/17_setup_uart_dupont.jpg)

Voir aussi [../analysis/photos/README.md](../analysis/photos/README.md) pour catalogue complet.

## Matériel

- CH340G ou CP2102 en mode 3.3V (=**PAS 5V**)
- Fils dupont F/F
- Fer à souder mini (=recommandé pour shunt IO0)
- Chargeur 12V DC du module (=original Extraflame)

## Ouverture du module

Le module Black Label est dans un boîtier plastique noir avec 4 vis torx. Retirer les vis pour accéder au PCB `COD.T009_3`.

Identifier :
- **CN5** : header 3-pin près du module ESP32 (=UART debug)
- **SERIAL 4-pin clip** : câble vers poêle (=vert/marron/blanc/jaune)
- **Bouton noir** : reset système
- **Jack DC** : alim 12V

## Câblage USB-UART vers module

### Option A - via SERIAL 4-pin (=RX du module accessible ici)

Notre expérience a montré que le CN5 pin 2 (=supposé RX du module) était NC électriquement. Bypass en utilisant le cable SERIAL 4-pin extrémité côté poêle (=débranché du poêle) :

| CH340G | Cable SERIAL 4-pin (=extrémité poêle débranchée) |
|--------|--------------------------------------------------|
| TX     | wire BLANC (=RX module)                          |
| RX     | wire MARRON (=TX module)                         |
| GND    | wire VERT (=GND)                                 |

⚠️ **NE JAMAIS** connecter le wire JAUNE (=+12V du poêle).

### Option B - via CN5 (=si RX marche chez toi)

| CH340G | CN5 pin              |
|--------|----------------------|
| TX     | Pin 2 (=RX module)   |
| RX     | Pin 3 (=TX module)   |
| GND    | Pin 1 (=triangle ▲)  |

## Boot mode download

Pour dumper le flash, l'ESP32 doit être en download mode. Cela nécessite de tirer **IO0 à GND** pendant le boot.

### Position IO0 sur le module

IO0 = pin 25 du module ESP32-WROOM-32. Position physique :
- Côté DROIT du module (=celui où est CN5)
- **3ème castellated pad** depuis le COIN BAS-DROIT du module en remontant
- Pin 23 (=coin) = IO15, Pin 24 = IO2, Pin 25 = **IO0**

### Séquence power cycle avec shunt

1. Débranche le chargeur 12V
2. Positionne un fil temporaire : IO0 pad ↔ CN5 pin 1 (=GND)
3. Rebranche le chargeur 12V EN MAINTENANT le shunt
4. Attends 2-3 secondes
5. Retire le shunt

Confirmation en miniterm :

```
rst:0x1 (POWERON_RESET),boot:0x3 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))
waiting for download
```

Le module reste en download mode jusqu'à reset.

## esptool dump

Windows PowerShell / Linux bash :

```bash
python -m esptool --port COM3 --baud 115200 \
    --before no-reset --after no-reset chip_id
```

Doit retourner :

```
Chip type: ESP32-D0WDQ6 (revision v1.0)
Features: Wi-Fi, BT, Dual Core + LP Core, 240MHz
MAC: xx:xx:xx:xx:xx:xx
```

Vérifier efuses (=confirmer flash lisible) :

```bash
python -m espefuse --port COM3 --before no-reset summary
```

Chercher : `FLASH_CRYPT_CNT: 0` (=si != 0, flash chiffrée, dump illisible).

Dump complet :

```bash
python -m esptool --port COM3 --baud 460800 \
    --before no-reset --after no-reset \
    read_flash 0 0x400000 extraflame_original.bin
```

Prend ~2 minutes à 460800 baud.

## Vérification

```bash
# Taille attendue : 4194304 bytes
ls -la extraflame_original.bin

# Hash pour intégrité
sha256sum extraflame_original.bin

# Bootloader magic 0xE9 attendu à 0x1000
xxd -s 0x1000 -l 4 extraflame_original.bin
```

## Sauvegarde

**Faire 2 copies minimum** :

```bash
cp extraflame_original.bin extraflame_original_BACKUP.bin

# Copie sur NAS
scp extraflame_original.bin nas:backup/
```

⚠️ **Ne PARTAGE PAS ce fichier publiquement.** Il contient :
- Ta MAC address unique du module
- Ton `secure_code` (=code pairing Total Control 2.0)
- Ton `stove_model` (=numéro modèle Extraflame)

Ce sont des données personnelles.

## Restauration

Si Target Black Label ne fonctionne pas, restaure :

```bash
python -m esptool --port COM3 --baud 460800 \
    write_flash 0x0 extraflame_original.bin
```

**Nota** : le shunt IO0 n'est nécessaire QU'UNE FOIS par session (=cycle de power cycles). Une fois le module en download mode, il y reste tant qu'il n'est pas reset.

## Notes empiriques

Points de vigilance appris en session dump 2026-07-03 :

1. **CH340G 3.3V obligatoire** - le 5V TTL peut causer des états UART instables
2. **GND commun essentiel** - sans GND côté CH340G, les données sortent gribouillées
3. **Baud rate 115200** en download mode (=ROM ESP32 default)
4. **--before no-reset critique** - sans ce flag, esptool tente un DTR/RTS reset qui sort le module du download mode
5. **Le bouton RESET marche aussi** pour entrer download mode (=alternative au power cycle)
6. **Timing serré** - une fois en download mode, lance esptool rapidement (=~30s timeout)
