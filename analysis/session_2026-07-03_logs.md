> 📜 **Journal historique du reverse.** Ce document reflète ce qu'on pensait AU MOMENT
> de l'analyse. Certaines valeurs (adresses de registres, rôle maître/esclave, baud) ont été
> **corrigées depuis** par la validation sur le vrai poêle. Pour le protocole ACTUEL et validé,
> voir [`../docs/PROTOCOLE-MICRONOVA.md`](../docs/PROTOCOLE-MICRONOVA.md).

# Session dump 2026-07-03 - logs bruts

Traces complètes de la session reverse engineering du module Extraflame Black Label.

## Hardware identifié

```
Carte PCB       : COD.T009_3 (=référence Extraflame)
Date fab        : semaine 25 de 2022 (=CE mark 25-22)
Module Wi-Fi    : Espressif ESP32-WROOM-32
FCC ID          : 2AC7Z-ESPWROOM32
IC              : 21098-ESPWROOM32
CMIIT ID        : 2018DP2463
Batch           : XX0H32
Sticker externe : 002272728-000 C.Q.D33[0088]22/6394
```

## Alim

```
Chargeur externe : 12V DC 500mA (=jack DC center-positive)
Voltage logique  : 3.3V (=ESP32-WROOM-32)
```

## Connecteurs (=Black Label)

```
SERIAL 4-pin clip (gauche PCB) - cable vers stove TA port :
  Pin 1 : 🟢 VERT   → GND
  Pin 2 : 🟤 MARRON → TX module (=confirmé, on lit boot log dessus)
  Pin 3 : ⚪ BLANC  → probable RX module (=à confirmer flash)
  Pin 4 : 🟡 JAUNE  → probable VCC (=+12V, DANGER)

CN5 3-pin right-angle (droit du module ESP32) :
  Pinout via jeng37 (=unité identique) :
  Pin 1 (=triangle ▲) : GND
  Pin 2 : RX module
  Pin 3 : TX module

  ⚠️ Sur unité Olivier : RX (=pin 2) était NON CONNECTÉ électriquement
  → Fallback : utiliser SERIAL 4-pin MARRON pour RX du CH340G
  → Ou souder direct sur castellated pad IO3 module

CN6 8-pin (=à droite de CN5) :
  Fonction : header expansion I/O (=à identifier)
  Non utilisé pour dump

Bouton poussoir noir (=milieu carte, tactile switch chromé) :
  Fonction : RESET (=connecté à EN du module ESP32)
  Confirmé par manuel NAVEL PLUS

Header 3-pin right-angle (=près SERIAL clip) :
  Fonction : à identifier
```

## Boot logs récupérés

### Normal boot (=SPI_FAST_FLASH_BOOT)

```
entry 0x40080678
I (26) boot: ESP-IDF v4.3-dirty 2nd stage bootloader
I (27) boot: compile time 14:11:04
I (27) boot: chip revision: 1
I (30) boot_comm: chip revision: 1, min. bootloader chip revision: 0
I (37) boot.esp32: SPI Speed: 40MHz
I (41) boot.esp32: SPI Mode: DIO
I (46) boot.esp32: SPI Flash Size: 4MB
I (50) boot: Enabling RNG early entropy source...
I (56) boot: Partition Table:
I (59) boot: ## Label            Usage          Type ST Offset   Length
I (67) boot:  0 nvs              WiFi data        01 02 00009000 00004000
I (74) boot:  1 otadata          OTA data         01 00 0000d000 00002000
I (82) boot:  2 phy_init         RF data          01 01 0000f000 00001000
I (89) boot:  3 ota_0            OTA app          00 10 00010000 00180000
I (97) boot:  4 ota_1            OTA app          00 11 00190000 00180000
I (104) boot:  5 ota_stove        Unknown data     01 40 00310000 00030000
I (112) boot:  6 stove_bk         Unknown data     01 40 00340000 00002000
I (119) boot:  7 file_sys         Unknown data     01 82 00342000 000b0000
I (127) boot:  8 secret1          WiFi data        01 02 003f2000 00004000
I (134) boot:  9 secret2          Unknown data     01 40 003f6000 00004000
I (142) boot: End of partition table
I (146) boot_comm: chip revision: 1, min. application chip revision: 0
I (153) esp_image: segment 0: paddr=00010020 vaddr=3f400020 size=52250h (336464) map
I (289) esp_image: segment 1: paddr=00062278 vaddr=3ffb0000 size=06c7ch ( 27772) load
I (300) esp_image: segment 2: paddr=00068efc vaddr=40080000 size=0711ch ( 28956) load
I (313) esp_image: segment 3: paddr=00070020 vaddr=400d0020 size=a8a6ch (690796) map
I (574) esp_image: segment 4: paddr=00118a94 vaddr=4008711c size=0e09ch ( 57500) load
I (598) esp_image: segment 5: paddr=00126b38 vaddr=50000000 size=00010h (    16) load
I (611) boot: Loaded app from partition at offset 0x10000
I (611) boot: Disabling RNG early entropy source...
I (623) cpu_start: Pro cpu up.
I (623) cpu_start: Starting app cpu, entry point is 0x400813cc
I (0) cpu_start: App cpu up.
I (639) cpu_start: Pro cpu start user code
I (639) cpu_start: cpu freq: 160000000
I (639) cpu_start: Application information:
I (644) cpu_start: Project name:     navel
I (649) cpu_start: App version:      1
I (653) cpu_start: Compile time:     Nov 28 2022 16:05:12
I (659) cpu_start: ELF file SHA256:  5408e336c09e23ec...
I (665) cpu_start: ESP-IDF:          v4.3-dirty
I (671) heap_init: Initializing. RAM available for dynamic allocation:
I (678) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (684) heap_init: At 3FFBD6D8 len 00022928 (138 KiB): DRAM
I (690) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (696) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (703) heap_init: At 400951B8 len 0000AE48 (43 KiB): IRAM
I (710) spi_flash: detected chip: generic
I (714) spi_flash: flash io: dio
I (719) cpu_start: Starting scheduler on PRO CPU.
```

### App start - Wi-Fi + HTTP server

```
I (11205) phy_init: phy_version 4670,719f9f6,Feb 18 2021,17:07:07
I (11304) wifi:mode : sta (DE:AD:BE:EF:00:00) + softAP (DE:AD:BE:EF:00:01)
I (11305) wifi:enable tsf
I (11306) wifi:Total power save buffer number: 16
I (11307) wifi:Init max length of beacon: 752/752
I (11312) wifi:Init max length of beacon: 752/752
I (11317) WIFI: WIFI_EVENT_STA_START
I (11322) WIFI: WIFI_EVENT_AP_START
I (11324) WIFI: WIFI_EVENT_AP_START2
wifi_manager: starting softAP with ssid MyStove_DE:AD:BE:EF:00:00
wifi_manager: starting softAP with 20 MHz bandwidth
wifi_manager: starting softAP on channel 5
wifi_manager: softAP started, starting http_server
I (11347) http_server: asking to start http server
http_server: received start bit, starting server
got request to start http server
ip = 0.0.0.0,port = 80
HTTP Server listening...
start http server: success
I (11433) WIFI: request to wifi scan..
I (13880) WIFI: WIFI_EVENT_SCAN_DONE
ap_num = 11
```

### Download boot (=avec shunt IO0)

```
rst:0x1 (POWERON_RESET),boot:0x3 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))
waiting for download
```

### HTTP server debug (=custom serveur en série)

```
------------------- start session ask-reply --------------------
---got msg---
msg[0] = 0x50
msg[1] = 0x4F
msg[2-232]="ST /api/status HTTP/1.1
User-Agent: Mozilla/5.0 (Windows NT; Windows NT 10.0; fr-FR) WindowsPowerShell/5.1.26100.8655
Content-Type: application/x-www-form-urlencoded
Host: 192.168.1.1
Content-Length: 0
Connection: Keep-Alive
"
msg[233] = 0x0D
msg[234] = 0x0A

I (1368174) http_server: http_server_netconn_serve: got line = POST /api/status HTTP/1.1
I (1368184) http_server: /unknow served
```

## Esptool chip_id output

```
python -m esptool --port COM3 --baud 115200 --before no-reset --after no-reset chip_id

esptool v5.3.0
Note: Pre-connection option "no-reset" was selected.
Connected to ESP32 on COM3:
Chip type:          ESP32-D0WDQ6 (revision v1.0)
Features:           Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
Crystal frequency:  40MHz
MAC:                DE:AD:BE:EF:00:00

Stub flasher running.

Warning: ESP32 has no chip ID. Reading MAC address instead.
MAC:                DE:AD:BE:EF:00:00

Staying in bootloader.
```

## Espefuse summary complet (=SECURITE OUVERTE)

```
python -m espefuse --port COM3 --before no-reset summary

Calibration fuses:
ADC_VREF (BLOCK0)                                  True ADC reference voltage                         = 1156 R/W (0b01000)

Config fuses:
WR_DIS (BLOCK0)                                    Efuse write disable mask                           = 0 R/W (0x0000)
RD_DIS (BLOCK0)                                    Disable reading from BlOCK1-3                      = 0 R/W (0x0)
DISABLE_APP_CPU (BLOCK0)                                                                              = False
DISABLE_BT (BLOCK0)                                                                                    = False
CHIP_CPU_FREQ_RATED (BLOCK0)                                                                          = True
CODING_SCHEME (BLOCK0)                             Efuse variable block length scheme = NONE (BLK1-3 len=256 bits)
CONSOLE_DEBUG_DISABLE (BLOCK0)                     Disable ROM BASIC interpreter fallback = True

Flash fuses:
FLASH_CRYPT_CNT (BLOCK0)                           Flash encryption is enabled if this field has an odd number of bits set = 0 ✅
FLASH_CRYPT_CONFIG (BLOCK0)                        Flash encryption config (key tweak bits) = 0

Identity fuses:
CHIP_VER_REV1 (BLOCK0)                             bit is set to 1 for rev1 silicon = True
WAFER_VERSION_MAJOR (BLOCK0)                       = 1

Jtag fuses:
JTAG_DISABLE (BLOCK0)                              Disable JTAG = False ✅

Mac fuses:
MAC (BLOCK0)                                       MAC address = DE:AD:BE:EF:00:00 (CRC 0x26 OK)

Security fuses:
UART_DOWNLOAD_DIS (BLOCK0)                         Disable UART download mode = False ✅
ABS_DONE_0 (BLOCK0)                                Secure boot V1 is enabled for bootloader image = False ✅
ABS_DONE_1 (BLOCK0)                                Secure boot V2 is enabled for bootloader image = False ✅
DISABLE_DL_ENCRYPT (BLOCK0)                        Disable flash encryption in UART bootloader = False
DISABLE_DL_DECRYPT (BLOCK0)                        Disable flash decryption in UART bootloader = False
KEY_STATUS (BLOCK0)                                Usage of efuse block 3 (reserved) = False
SECURE_VERSION (BLOCK3)                            Secure version for anti-rollback = 0
BLOCK1 (BLOCK1)                                    Flash encryption key = 00...00 (=vide)
BLOCK2 (BLOCK2)                                    Security boot key = 00...00 (=vide)
BLOCK3 (BLOCK3)                                    Variable Block 3 = 00...00 (=vide)
```

## Dump commande

```
python -m esptool --port COM3 --baud 460800 --before no-reset --after no-reset read-flash 0 0x400000 C:\Users\<you>\Desktop\extraflame_dump.bin

Read 4194304 bytes from 0x00000000 in 100.1 seconds (335.3 kbit/s)
```

## Fichier dump

```
Path (Windows) : C:\Users\<you>\Desktop\extraflame_dump.bin
Path (Linux)   : /home/user/Downloads/extraflame_dump.bin
Backup         : /home/user/Downloads/extraflame_dump_BACKUP.bin
Size           : 4 194 304 bytes (=4 MB exact)
SHA256         : d64ff741e179ff8fa6ec365fd227973037f17ab5c5edbcd53e5568e65cc8c897
```

## Partitions extraites

```
partition_ota0.bin         1572864 bytes  (=1.5MB)  offset 0x010000
partition_ota_stove.bin     196608 bytes  (=192KB)  offset 0x310000
partition_file_sys.bin      720896 bytes  (=720KB)  offset 0x342000 (=VIDE 0xFF)
partition_secret1.bin        16384 bytes  (=16KB)   offset 0x3f2000
partition_secret2.bin        16384 bytes  (=16KB)   offset 0x3f6000 (=VIDE 0xFF)
```

## Secret1 - NVS EN CLAIR (=info sensible)

```
Keys/Values extraits :
  settings    = "0"
  del_model   = "0"
  product     = "0"
  secure_code = "XXXXXXXX"     ← 🔑 code registration/pairing 8 chiffres
  stove_model = "YYYYYYYYYY"   ← 🔑 modèle Teodora Evo code Extraflame
  custom2     = "1"
```

## Backend cloud

```
Provider           : Omnyvore srl (=Vicenza, Italie)
MQTT broker        : mqtts://mqtt.extraflame.it:8883
Cert CA embedded   : Omnyvore self-signed (2017-2027)
Info contact       : info@omnyvore.com
Auth               : HMAC signed messages
```

## HTTP Endpoints (=SoftAP config)

```
GET /
GET /wifi.html
GET /ap.json          (=scan Wi-Fi)
GET /status.json      (=status connexion)
POST /connect.json    (=connect au Wi-Fi)
DELETE /connect.json  (=disconnect)
GET /web/bootstrap.css
GET /web/bootstrap.js
GET /web/chart.bundle.js
GET /web/code.js      ⭐ code custom Extraflame
GET /web/jquery.js
GET /web/popper.js
GET /web/style.css
```

## MQTT Topics reversés

```
Format hiérarchique : %s/%s/%s/%s (=probable stove_id/model/version/direction/action)

IN topics (=commandes vers poêle) :
  IN/firmware
  IN/addr
  IN/crono
  IN/misc
  IN/settings
  IN/time

OUT topics (=données du poêle) :
  OUT/status
  OUT/temperature
  OUT/alarm
  OUT/dyn         (=données dynamiques)
  OUT/workingtimers
  OUT/misc
  OUT/settings
  OUT/addr
  OUT/time
  OUT/crono

REPLY topics :
  REPLY/crono
  REPLY/settings
  REPLY/time
```

## Constants Micronova identifiées (=registres RAM)

```
RAM_ACCENDI              (=allume)
RAM_ALLARM_ADDR          (=adresse alarme)
RAM_BULBO_ADDR           (=adresse bulbe = sonde temp)
RAM_CAUSA_STATO7_ADDR    (=cause status 7 = code alarme)
RAM_MOD_ADDR             (=mode)
RAM_POT_REALE_ADDR       (=puissance réelle)
RAM_RESET_UTENTE         (=reset utilisateur)
RAM_SBLOCCO_ADDR         (=déblocage)
RAM_SERBATORIO_VUOTO_ADDR (=réservoir vide)
RAM_SPEGNI               (=éteindre)
RAM_STATO_GESTITO        (=état géré)
RAM_STOVE_STATUS_ADDR    (=status poêle)
RAM_TAMB_ADDR            (=température ambiante)
RAM_TH20_ADDR            (=température eau)
RAM_T_BOILER_ADDR        (=température ballon)
RAM_T_CAMERA_ADDR        (=température chambre combustion)
RAM_T_FUMI_ADDR          (=température fumées)
RAM_T_H20_RIT_ADDR       (=température eau retour)
RAM_T_PUFFER_INF_ADDR    (=température puffer bas)
RAM_T_PUFFER_SUP_ADDR    (=température puffer haut)
```

## Stove types supportés (=Extraflame Black Label firmware)

```
STOVE_TYPE_I_CALD        (=chaudière/caldaia)
STOVE_TYPE_I_CANAL       (=canalisé)
STOVE_TYPE_I_CANAL_2
STOVE_TYPE_I_CANAL_3
STOVE_TYPE_I_CANAL_4
STOVE_TYPE_I_IDRO        (=hydro / à eau)
STOVE_TYPE_I_IDRO_2
STOVE_TYPE_I_VENT        (=ventilé) ← Teodora Evo probable
STOVE_TYPE_I_VENT_2
STOVE_TYPE_I_VENT_3
STOVE_TYPE_I_VENT_4
STOVE_TYPE_I_VENT_5
```

## Dev identity (=leak paths)

```
Compilation user : g.benetti (=Italien, Extraflame ou Omnyvore)
Path             : C:/Users/g.benetti/esp/esp-idf/
ESP-IDF version  : v4.3-dirty
Comments code    : italien ("Da inviare con MQTT: %s")
```

## Firmware technique

```
App name          : navel
App version       : 1
Compile time      : Nov 28 2022 16:05:12
ELF SHA256        : 5408e336c09e23ec... (=partial)
Framework         : ESP-IDF v4.3-dirty
Wi-Fi manager     : tonyp7 library (=fork)
MQTT client       : ESP-MQTT
File system       : SPIFFS (=probable, file_sys partition)
mDNS name         : Extraflame (=hostname + instance)
```
