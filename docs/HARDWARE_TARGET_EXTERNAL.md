# Target External - Câblage ESP32 spare

Utilise un ESP32 supplémentaire connecté au bus série du poêle en parallèle du module Extraflame officiel.

## Matériel nécessaire

- 1x ESP32-WROOM-32 module ou dev board (=15€)
- Fils dupont F/F (=quelques cm)
- USB-UART pour flash (=CH340G 3.3V)
- Accès au bus série du poêle (=connecteur TA sur main board)

## Schéma câblage

```
   ┌──────────────────┐          ┌──────────────────┐
   │  Poêle           │          │  ESP32-WROOM-32  │
   │  Main board      │          │  (spare)         │
   │                  │          │                  │
   │  TA connector    │          │  GPIO 17  ────┐  │
   │  ┌──────┐        │          │  GPIO 16  ────┼┐ │
   │  │ TX   │───────┤          │  GND     ────┼┼┐│
   │  │ RX   │───────┼──────────┤  3V3 ── externe │
   │  │ GND  │───────┘          │                  │
   │  │ +12V │  (NOT USED)      │                  │
   │  └──────┘                    └──────────────────┘
   │                  │
   └──────────────────┘
```

## Pinout

| ESP32 pin  | Signal          | Cable poêle (=connecteur TA)              |
|------------|-----------------|-------------------------------------------|
| GPIO 17    | UART2 TX        | wire RX du poêle                          |
| GPIO 16    | UART2 RX        | wire TX du poêle                          |
| GND        | Ground          | wire GND du poêle                         |
| (VIN 5V ou 3V3) | Alim externe | NE PAS utiliser +12V du poêle direct !    |

⚠️ **NE PAS connecter le wire +12V (=jaune) du poêle à l'ESP32.** Alimente l'ESP32 séparément (=USB, alim 5V, batterie).

## Ordre de connexion

1. ⚠️ **DEBRANCHE le poêle du secteur** avant de bricoler
2. Ouvre le boitier électronique du poêle (=derrière/dessous selon modèle)
3. Repère le connecteur TA sur le main board Micronova
4. Note l'ordre des wires (=souvent silkscreen "GND TX RX +V" sur le PCB)
5. Fais tes soudures/dupont sans alimenter
6. Vérifie continuité si tu as un multimètre
7. Rebranche le poêle secteur

## Alimentation ESP32

Options :
- **Alim USB 5V** via micro-USB de la dev board (=le plus simple)
- **Alim externe 5V** via VIN pin (=si module WROOM-32 nu)
- **Alim 3V3 régulée** via 3V3 pin (=avancé)

⚠️ Si tu utilises l'USB de ta dev board, respecte la masse commune :
GND de l'ESP32 = GND du bus poêle = GND USB = potentiel commun.

## Test cablâge sans firmware

Avant de flasher openextraflame :

1. Alimente ESP32 seul (=sans le connecter au poêle)
2. Vérifie que ça boot (=LED onboard s'allume, blink patterns)
3. Utilise ESP-IDF example `hello_world` pour valider

Puis :

1. Éteins ESP32
2. Fais les connexions au poêle (=selon schéma ci-dessus)
3. Vérifie polarité GND
4. Rallume ESP32
5. Flash openextraflame Target External

## Log de test

Après flash, connecte-toi au serial monitor :

```
docker compose run --rm esp-idf idf.py -p /dev/ttyUSB0 monitor
```

Tu devrais voir :

```
I (xxx) MAIN: ===============================================
I (xxx) MAIN:   openextraflame - build Jul  3 2026 18:00:00
I (xxx) MAIN:   Board       : external-esp32
I (xxx) MAIN:   Target      : EXTERNAL (spare ESP32)
I (xxx) MICRONOVA: Init UART1 TX=17 RX=16 @ 38400 baud 8N1
I (xxx) MICRONOVA: Micronova task started
I (xxx) WIFI: SoftAP up : SSID='openextraflame_XXX'
I (xxx) WEB: Web UI listening on :80
```

Connecte-toi au SoftAP "openextraflame_XXXXXX" et va sur http://192.168.4.1/
pour configurer Wi-Fi + MQTT.

## Photos référence

À ajouter :
- Photo du connecteur TA du poêle
- Photo du câblage ESP32 spare
- Photo du module dans son boitier (=si tu en fais un)
