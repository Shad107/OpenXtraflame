# Photos hardware Extraflame Black Label T009_3

Photos prises pendant la session reverse engineering du 2026-07-03. Documente les étapes matérielles du dump firmware.

## Vue d'ensemble PCB

### 01_pcb_overview.jpg
Vue complète du PCB de la carte Extraflame Black Label une fois le boîtier ouvert. Montre l'emplacement général de :
- Module ESP32 (=centre)
- Jack DC alimentation 12V (=en haut)
- Connecteur SERIAL clip 4-pin (=gauche, vers poêle)
- Transformateur central
- Terminal blocks bleus (=CTI/PLC)

### 04_pcb_top_area_ce_marks.jpg
Zone haute du PCB avec marques CE, RoHS, UL 94V-0, date fabrication "25-22" (=semaine 25 de 2022).

## Module ESP32-WROOM-32

### 02_module_esp32_with_sticker.jpg
Module Wi-Fi Espressif ESP32-WROOM-32 avec sticker `002272728-000 C.Q.D33[0088]22/6394`. Le sticker cache partiellement le marquage officiel.

### 05_module_esp32_labels_readable.jpg
Module avec marquage complet lisible :
- `ESPRESSIF ESP32-WROOM-32`
- `FCC ID: 2AC7Z-ESPWROOM32`
- `IC: 21098-ESPWROOM32`
- `CMIIT ID: 2018DP2463`
- Fabricant : 乐鑫信息科技 (Shanghai) = Espressif Systems Shanghai
- Batch : `XX0H32`

### 06_module_cmb1_cmb2_view.jpg
Vue du module montrant les silkscreens `CMB1` et `CMB2` en dessous, ainsi que les castellated pads sur le côté droit.

### 08_module_ra3_resistors_view.jpg
Zoom sur RA3 (=résistor array `103` = 10kΩ), R14, R28, R13, R12 et le chip MEP (=probable régulateur/MOSFET).

### 15_module_top_view.jpg
Vue de dessus du module montrant l'orientation complète (=antenne en haut).

## Connecteurs

### 03_serial_4pin_connector_closeup.jpg
Macro sur le connecteur SERIAL 4-pin (=clip noir) sur le côté gauche du PCB. C'est là où le câble vers le poêle se branche.

### 07_cn5_3pin_header_macro.jpg
Header CN5 3-pin right-angle près du module ESP32. C'est le port UART debug factory. Triangle silkscreen indique pin 1 (=GND). Confirmé par jeng37 :
- Pin 1 (=triangle) : GND
- Pin 2 : RX module
- Pin 3 : TX module

⚠️ Sur l'unité d'Olivier, pin 2 (=RX) était NON connecté électriquement. Solution : utiliser la SERIAL 4-pin cable extrémité poêle débranchée comme alternative RX.

### 13_cn5_traces_pcb.jpg
Traces PCB visibles reliant les 3 pins CN5 aux castellated pads du module ESP32.

### 14_cn5_cn6_headers.jpg
Vue montrant CN5 (=3 pins) et CN6 (=8 pads through-hole) côte à côte à droite du module.

## Câble SERIAL (=vers poêle)

### 10_stove_ta_port_wires.jpg
Câble branché sur le port TA du POELE (=côté main board Micronova). Wires visibles : vert, marron, jaune.

### 11_stove_ta_port_labels.jpg
Label `TA` visible sur le boîtier du poêle où le câble entre. TA = Termostato Ambiente en italien.

### 12_serial_4pin_cable_colors.jpg
Zoom sur les 4 wires du câble SERIAL côté module :
- Pin 1 (=gauche) : 🟢 VERT = GND
- Pin 2 : 🟤 MARRON = TX module (=confirmé)
- Pin 3 : ⚪ BLANC = probable RX module
- Pin 4 (=droite) : 🟡 JAUNE = probable VCC +12V (=DANGER, ne pas toucher)

## Bouton et setup dump

### 09_reset_button_tactile_switch.jpg
Bouton poussoir tactile switch (=chromé avec plongeur noir) sur le PCB. Fonction RESET selon le manuel NAVEL PLUS :
- Press 5s = disconnect Wi-Fi + retour mode AP
- Press 2s = reconnect avec dernière config

### 16_module_with_dupont_wires.jpg
Setup dump firmware en cours - dupont wires (=bleu/jaune/orange) branchés sur CN5 pour communication avec CH340G USB-UART.

### 17_setup_uart_dupont.jpg
Vue rapprochée du setup UART final avec les 3 wires connectés au CH340G. Configuration validée pour dumper le firmware ESP32.

## Correspondance photos / documentation

| Photo                                | Doc                              |
|--------------------------------------|----------------------------------|
| 01, 04, 06                            | Vue PCB globale                  |
| 02, 05, 08, 15                        | Module ESP32-WROOM-32            |
| 03, 12                                | SERIAL 4-pin (=connecteur poêle) |
| 07, 13, 14                            | CN5 header debug UART            |
| 09                                    | Bouton RESET                     |
| 10, 11                                | Câblage côté poêle               |
| 16, 17                                | Setup dump firmware              |

Toutes ces photos servent de référence pour :
- Documentation reproduction (=quels connecteurs, où)
- Vérification pinout Target Blacklabel
- Support communauté HA (=après publication éventuelle)

⚠️ **Ces photos sont personnelles à Olivier.** Le sticker sur le module (=numéro série) est visible sur certaines photos. Avant publication publique, sanitiser (=flouter) le sticker et le QR code du module.
