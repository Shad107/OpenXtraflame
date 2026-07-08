# Target Black Label - Reflash du module Extraflame

⚠️ **ATTENTION - opération irréversible sans backup.**

Cette procédure remplace le firmware d'origine sur le module Extraflame Black Label T009_3 (=289€ TTC neuf) par OpenXtraflame. Sans backup, tu perds la fonctionnalité cloud Total Control 2.0 pour toujours (=jusqu'à ce que Extraflame publie un moyen de flash).

## Prérequis critiques

- ✅ **Dump du firmware original** effectué (=voir docs/DUMPING_ORIGINAL.md)
- ✅ **Backup .bin** stocké en lieu sûr (=au moins 2 copies)
- ✅ **Analyse Ghidra faite** pour valider GPIO mapping (=voir analysis/firmware-cartography.md section 15)

Si l'un de ces points est manquant, **NE FLASHE PAS Target Black Label**. Utilise plutôt Target External.

## GPIO mapping (=à VÉRIFIER via Ghidra)

Les valeurs suivantes sont inférées de conventions industrielles ESP32 et **doivent être vérifiées** avant flash. Un GPIO faux peut ne rien casser (=le firmware bootera mais LEDs off) mais peut aussi court-circuiter un pin utilisé par le hardware.

| Signal            | GPIO estimé | Vérification requise                    |
|-------------------|-------------|------------------------------------------|
| UART TX to stove  | GPIO 17     | Analyser rwms_master.c dans Ghidra       |
| UART RX to stove  | GPIO 16     | Analyser rwms_master.c dans Ghidra       |
| LED POWER (=vert) | GPIO 5      | À identifier via Ghidra ou test          |
| LED BLE           | GPIO 18     | À identifier via Ghidra ou test          |
| LED WI-FI         | GPIO 19     | À identifier via Ghidra ou test          |
| LED SERVER        | GPIO 21     | À identifier via Ghidra ou test          |
| RESET button      | GPIO 4      | À identifier via Ghidra ou test          |

## Procédure recommandée

### 1. Backup firmware original (=OBLIGATOIRE)

Voir docs/DUMPING_ORIGINAL.md pour la procédure complète.

```bash
esptool.py --port COM3 --baud 460800 --before no-reset --after no-reset \
    read_flash 0 0x400000 extraflame_original_backup.bin
```

Vérifie le hash :

```bash
sha256sum extraflame_original_backup.bin
```

### 2. Analyse Ghidra ota0.bin

Ouvre `analysis/partition_ota0.bin` dans Ghidra avec Xtensa architecture. Cherche :

- `uart_set_pin(SERIAL_UPGRADE_PORT, TX, RX, ...)` → valeurs numériques TX / RX
- `gpio_set_level(LED_XX, ...)` → GPIOs des 4 LEDs
- `gpio_get_level(RESET_BUTTON)` → GPIO du bouton reset

Note les valeurs et met à jour `firmware/main/hardware_config.h` section `TARGET_BLACKLABEL`.

### 3. Compile firmware Black Label

```bash
docker compose run --rm esp-idf idf.py -DTARGET=blacklabel build
```

Vérifie que la compilation passe sans warning majeur.

### 4. Flash

⚠️ **Point de non-retour.** Assure-toi de :
- Backup présent et vérifié
- GPIO mapping validé
- Chargeur 12V pour alimenter la carte

```bash
docker compose run --rm esp-idf idf.py -p /dev/ttyUSB0 -DTARGET=blacklabel flash monitor
```

### 5. Vérification post-flash

Monitor UART doit montrer :

```
I (xxx) MAIN: OpenXtraflame - build ...
I (xxx) MAIN: Board       : blacklabel-t009_3
I (xxx) MAIN: Target      : BLACKLABEL (reflash original)
I (xxx) MICRONOVA: Init UART1 TX=23 RX=5 @ 1200 baud 8N2 (inversé 0x24)
...
I (xxx) WIFI: SoftAP up : SSID='MyStove_XX:XX:XX:XX:XX:XX'
```

Le SSID conserve le préfixe `MyStove_` pour cohérence avec le manuel NAVEL PLUS.

### 6. Test des LEDs

Après boot, les 4 LEDs devraient s'allumer selon l'état :
- POWER : allumée en permanence
- BLE : allumée si mode AP (=provisioning)
- WI-FI : allumée quand connecté au Wi-Fi STA
- SERVER : allumée quand connecté au broker MQTT

Si une LED ne réagit pas, le GPIO estimé est probablement faux. Retour à l'étape 2 (Ghidra) pour identifier le bon.

### 7. Test bouton reset

Presser le bouton (=tactile switch chromé) 5 secondes devrait faire un reset factory (=effacer NVS, redémarrer en SoftAP).

Si le bouton ne fait rien, le GPIO est faux. Retour à Ghidra.

## Restauration si problème

Si l'ESP32 boot en boucle ou ne répond pas, restaure le firmware original :

```bash
esptool.py --port COM3 --baud 460800 write_flash 0x0 extraflame_original_backup.bin
```

Cette opération remet TOUT en état d'origine (=incluant secure_code, stove_model, NVS).

## Différences vs Target External

| Aspect            | Target External              | Target Black Label            |
|-------------------|------------------------------|-------------------------------|
| Matériel          | ESP32 spare + fils dupont    | Module Black Label 289€       |
| Risque            | Faible (=hardware séparé)    | Élevé (=peut briquer)         |
| Réversibilité     | Rien à restaurer             | Nécessite backup dump         |
| Coût              | ~15€                          | 0 (=module déjà acheté)       |
| LEDs status       | 1 LED onboard basique        | 4 LEDs Extraflame réutilisées |
| Bouton config     | Bouton BOOT dev board         | Bouton RESET Black Label      |
| Compatibilité HA  | Identique                    | Identique                     |
| Recommandation    | Débutants + testing          | Utilisateurs avancés          |

## Sécurité et légalité

- Ce reflash est effectué sur ton propre matériel dans un cadre personnel
- Ne redistribue pas les binaires modifiés dérivés d'Extraflame
- L'analyse de reverse engineering est légale en Europe pour interopérabilité (=Directive 2009/24/CE article 6)
- Ne partage pas ton dump firmware original (=contient tes credentials Extraflame)
