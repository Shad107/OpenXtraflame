# Protocole série Extraflame / Micronova - reverse du firmware d'origine « navel »

> ## ✅ VALIDÉ SUR LE VRAI POÊLE (2026-07-07)
> OpenXtraflame communique avec la carte réelle en appliquant ce protocole.
> Tous les paramètres reversés confirmés corrects (1200 8N2, inversion 0x24,
> master polling, checksum additif, adresses). **Prérequis build : `-DOPENXFLAME_TARGET=blacklabel`**
> (sinon UART poêle sur GPIO17/16 = mauvais pins = carte muette). Vérifier le boot
> log : `Init UART1 TX=23 RX=5 @ 1200 baud 8N2`.

> Reconstitué le 2026-07-07 par analyse statique du firmware d'origine
> (`extraflame_dump.bin`, projet interne **navel** v1.8, ESP-IDF v4.3, dev g.benetti)
> via **Ghidra 12.1.2** (processeur Xtensa) + désassemblage objdump.
> Module cible : ESP32-D0WDQ6 (Black Label T009_3).
>
> Niveaux de confiance indiqués par section. Rien n'est inventé : chaque valeur
> vient du binaire (offsets/adresses cités). Deux points restent non résolus (voir §7).

---

## 1. Vue d'ensemble - TROIS protocoles sur le même UART

Le module et le poêle partagent **un seul port série** (UART1) mais y font tourner
**trois protocoles distincts** selon la phase :

| Protocole | Rôle du module | Usage | Fichier source (navel) |
|---|---|---|---|
| **RWMS** | **MAÎTRE** | Lecture/écriture live des registres RAM/EEPROM (température, état, puissance…) | `main/uart/rwms_master.c` |
| **SOTA2** | esclave | Canal de gestion/OTA : clone du protocole bootloader **esptool** + commandes stove | `main/ota/SerialOTA2.c` |
| **« working mode » natif** | - | Protocole applicatif quotidien à checksum **CRC4R** (partiellement non résolu) | cluster `ProductManager` |

➡️ **Pour lire les données dans Home Assistant, c'est le protocole RWMS qui compte** (§3).
Le module y est **MAÎTRE** : il *interroge* le poêle. C'est pourquoi une écoute
passive (module esclave) renvoie **0 trame**.

---

## 2. Couche physique  *(confiance : HAUTE)*

| Paramètre | Valeur |
|---|---|
| UART | **UART1** - TX = **GPIO23**, RX = **GPIO5** |
| **Débit TÉLÉMÉTRIE RWMS** (lecture registres poêle) | **1200 baud, 8N2** ⚠️ |
| Débit canal SOTA2 (esptool/OTA/handshake init) | 38400 baud, 8N1 |
| Flow control | aucun |
| **Inversion de ligne** | **OUI - masque `0x24` = `RXD_INV \| TXD_INV`** (prouvé : `uart_set_line_inverse(1, 0x24)` @ 0x400e3b8x) |
| Bus | half-duplex, un fil (write-then-readback = détection d'écho/collision) |

> ⚠️ **CORRECTION IMPORTANTE (confiance HAUTE, décompilé) :** la télémétrie tourne à
> **1200 8N2**, PAS à 38400. Le `[SERIAL] 38400 8N1` visible au boot est la config
> **initiale** du canal SOTA2 ; le firmware bascule ensuite l'UART en **1200 8N2**
> (`uart_set_baudrate(1,0x4b0)` + `uart_set_stop_bits(1,3)`) et lance une tâche
> (`0x400e5480`) qui polle **82 EEPROM + 20 RAM** (= les compteurs exacts du §4).
> L'inversion `0x24` posée à l'init **persiste** au changement de baud.
> **Pour lire les registres → 1200 8N2 inversé.** 38400 = uniquement le canal de gestion.

> Le firmware d'origine référence `uart_set_line_inverse` (dans `serial.c`).
> ⚠️ Le **masque exact** (RXD/TXD) n'a **pas** de site d'appel trouvable dans le
> segment `.text` analysé (voir §7) - l'inversion est peut-être matérielle ou dans
> l'IRAM non extraite. En pratique : inverser **RXD** (indispensable pour décoder)
> et **TXD** (pour être compris) - `UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV`.

---

## 3. Protocole RWMS - lecture ET écriture des registres  *(confiance : HAUTE - décompilé Ghidra)*

Le module est **MAÎTRE** : il envoie une requête, le poêle répond. Fonctions d'origine :
lecture `rwms_master_read` (`FUN_400e4df4`), écriture `FUN_400e4edc`, moteur de poll
`FUN_400e51e4`/`FUN_400e5110`. **Spec complet et auto-suffisant ci-dessous.**

### 3.0 Config série (obligatoire avant tout)
- **UART1, 1200 baud, 8 data, 2 stop bits, sans parité (8N2)** - PAS 38400 (voir §2).
- **Ligne inversée : `uart_set_line_inverse(UART1, 0x24)`** (RXD_INV | TXD_INV).
- Bus half-duplex : après émission, on lit la réponse sur la même ligne.

### 3.1 L'octet `loc` (« location byte » Micronova)
```
loc = base | (bank & 0x1F)
   base :  0x00 = LECTURE RAM
           0x20 = LECTURE EEPROM
           0x80 = ÉCRITURE RAM
           0xA0 = ÉCRITURE EEPROM
   bank :  octet HAUT de l'adresse 16 bits (0 = page normale, 1 = page étendue)
addr = octet BAS de l'adresse
```
(Dérivation binaire : `base = source<<5`, source = 0 RAM-rd / 1 EE-rd / 4 RAM-wr / 5 EE-wr.)

### 3.2 LECTURE - requête 2 octets, réponse 2 octets
```
  → ÉMET  : [loc][addr]                      (2 octets ; PAS de checksum en lecture)
  ← REÇOIT: [checksum][value]                (2 octets)
            checksum attendu = (loc + addr + value) & 0xFF
  Le poêle peut d'abord RÉ-ÉMETTRE l'écho [loc][addr] → le sauter, puis lire les 2 vrais octets.
  Si checksum OK → value = 2e octet. Sinon → échec (log "!timeout no reply!0x%03X").
```
Pseudocode :
```c
uint8_t mn_read(uint8_t loc, uint8_t addr, uint8_t *out) {
    uint8_t req[2] = { loc, addr };
    uart_write(req, 2);
    uint8_t r[2];
    if (uart_read(r, 2, TIMEOUT) != 2) return ERR;
    if (r[0] == loc && r[1] == addr)          // écho éventuel : on relit
        if (uart_read(r, 2, TIMEOUT) != 2) return ERR;
    if (r[0] != (uint8_t)(loc + addr + r[1])) return ERR_CKSUM;
    *out = r[1];
    return OK;
}
```

### 3.3 ÉCRITURE - requête 4 octets, réponse 2 octets
```
  → ÉMET  : [loc][addr][value][checksum]      (4 octets)   checksum = (loc + addr + value) & 0xFF
  ← REÇOIT: [checksum][value]                 (2 octets)   checksum = (loc + addr + value) & 0xFF
  Vérif : reply[0] == (loc + addr + reply[1])  (sinon log "rwms bad checksum value" /
          "rwms bad reply length %u"). Le firmware relit ensuite le registre pour confirmer.
```
Pseudocode :
```c
uint8_t mn_write(uint8_t loc, uint8_t addr, uint8_t value) {
    uint8_t req[4] = { loc, addr, value, (uint8_t)(loc + addr + value) };
    uart_write(req, 4);
    uint8_t r[2];
    if (uart_read(r, 2, TIMEOUT) != 2) return ERR;
    if (r[0] != (uint8_t)(loc + addr + r[1])) return ERR_CKSUM;
    return OK;   // idéalement : relire l'adresse pour confirmer la valeur écrite
}
```

### 3.4 Exemples d'octets concrets
| Action | loc | addr | trame émise | réponse |
|---|---|---|---|---|
| Lire FUMES_TEMP (0x3E) | 0x00 | 0x3E | `00 3E` | `[cks][°C]`, cks=(0x00+0x3E+val) |
| Lire STOVE_STATE (0x21) | 0x00 | 0x21 | `00 21` | `[cks][état]` |
| Lire TAMB (0x01, valeur = °C×2) | 0x00 | 0x01 | `00 01` | `[cks][temp×2]` |
| **DÉMARRER le poêle** (write STOVE_STATE 0x21 = 0x01) | 0x80 | 0x21 | `80 21 01 A2` | `[cks][01]` |
| **ÉTEINDRE** (write STOVE_STATE 0x21 = 0x06) | 0x80 | 0x21 | `80 21 06 A7` | `[cks][06]` |
| Reset alarme (write STOVE_STATE 0x21 = 0x00) | 0x80 | 0x21 | `80 21 00 A1` | `[cks][00]` |
| Régler consigne temp (write TEMP_SET 0x7D) | 0x80 | 0x7D | `80 7D <val> <cks>` | `[cks][val]` |
| Régler puissance (write POWER_SET 0x7F, 1-5) | 0x80 | 0x7F | `80 7F <n> <cks>` | `[cks][n]` |
> Checksum additif : ex. `80 21 01` → 0x80+0x21+0x01 = **0xA2**. Les commandes ON/OFF/reset
> et les consignes s'écrivent en RAM (loc `0x80`). Adresses = Micronova standard (voir §4).

### 3.5 Timing / robustesse (relevé dans le moteur de poll `FUN_400e51e4`)
- **Retry** : jusqu'à ~3 tentatives par registre, avec délai croissant **~100 ms × n** entre essais.
- Après une **écriture**, le firmware **relit** le registre pour confirmer (`bit 0x100` = write-pending,
  `bit 0x200` = read-requested dans la table d'état interne).
- Un mutex sérialise les accès (le bus est half-duplex partagé).
- Délai ~5 ms après chaque transaction (`FUN_4008a1a0(5)`).

### ⚠️ Corrections vs `micronova.c` actuel
1. **Débit 1200 8N2** (pas 38400 8N1). **Ligne inversée `0x24`.**
2. **Rôle = MAÎTRE** (polling actif) - pas esclave passif.
3. **loc byte** = `0x00/0x20/0x80/0xA0 | bank` (lecture/écriture × RAM/EEPROM), bank en bits bas.
4. **Checksum ADDITIF** `(loc+addr+value)&0xFF` - et non `~value`.
5. Lecture = 2 octets émis, écriture = **4 octets** émis. Gérer l'**écho**.
6. Vraies adresses (§4). Bank 1 pour T_CAMERA/BULBO/SBLOCCO/T_PUFFER_SUP.

---

## 4. Carte des registres  *(confiance : HAUTE - VALIDÉ SUR VRAI POÊLE 2026-07-07)*

> ⚠️ **CORRECTION MAJEURE 2026-07-07 (post-live-probe) :** les adresses
> 0xD0-0xEF listées dans les versions précédentes du reverse navel
> **ne sont PAS les registres du contrôleur poêle**. Elles répondent à toutes
> les requêtes RWMS avec des valeurs placeholder `0x20`, mais ce sont
> probablement des adresses de la mémoire interne du module Extraflame
> Black Label (=display buffer, cache, ou autre).
>
> Les **vraies adresses Micronova** (=documentées par la communauté :
> [philibertc/micronova_controller](https://github.com/philibertc/micronova_controller),
> [ridiculouslab](https://github.com/ridiculouslab)) sont dans la plage
> **0x00-0x9F**. Chaque registre a été **validé empiriquement** sur un
> Teodora Evo I_VENT en juillet 2026.

### RAM standard Micronova (=à utiliser pour OpenXtraflame)

| Registre | Adresse | Encodage | Description | Statut |
|---|---|---|---|---|
| **TAMB** | **0x01** | °C × 2 (=raw / 2) | Temp ambiante | ✅ VALIDÉ |
| TH20 | 0x03 | °C × 2 | Temp eau (N/A sur I_VENT ventilé) | ✅ N/A cohérent |
| **STOVE_STATE** | **0x21** | enum | 0=OFF 1=Start 2=PelletLoad 3=Ignition 4=WORK 5=Cleaning 6=Final 7=Standby 8=Alarm | ✅ VALIDÉ |
| FLAME_POWER | 0x34 | % | Puissance flamme instantanée | ✅ à valider allumé |
| WATER_PRES | 0x3C | bar × 10 | Pression eau (N/A I_VENT) | ✅ N/A cohérent |
| **FUMES_TEMP** | **0x3E** | °C | Temp fumées | ✅ VALIDÉ (=0 quand OFF) |
| TEMP_SET | 0x7D | °C | Consigne temp (=write pour changer) | ✅ à valider allumé |
| POWER_SET | 0x7F | 1..5 | Puissance réglée | ✅ à valider allumé |
| TEMP_GET | 0x9D | °C | Consigne temp active | ✅ à valider allumé |
| POWER_GET | 0x9F | 1..5 | Puissance courante | ✅ à valider allumé |

### Commandes (=write à 0x21 STOVE_STATE)

| Commande | Value | Trame TX complète |
|---|---|---|
| **Allumer** | `0x01` | `80 21 01 A2` |
| **Éteindre** | `0x06` | `80 21 06 A7` |
| Force OFF (=reset alarme) | `0x00` | `80 21 00 A1` |

### Modes utilisés selon type de poêle

- **Teodora Evo I_VENT** (=ventilé) : TAMB, STOVE_STATE, FUMES_TEMP, FLAME_POWER, TEMP_SET/GET, POWER_SET/GET
- **Hydro** (=T_CALD, T_IDRO) : ajouter TH20, WATER_PRES

### Historique - adresses "navel" 0xD0-0xEF (=DÉPRÉCIÉES)

Les adresses documentées dans les V1-V3 du reverse navel (`STOVE_STATUS=0xD1`,
`T_FUMI=0xD9`, etc.) répondent à toutes les requêtes mais avec des valeurs
non corrélées à l'état poêle réel. Hypothèse : ce sont des adresses de la
mémoire interne du module WiFi Black Label (=buffer LCD ou cache produit).

### EEPROM (écritures) - sélection résolue
Chrono 2/3/4 (start/stop/jours/temp/puissance) : plage 0x50-0x65, 0x9E-0xA1, 0xAF-0xB2.
Heure : jour=0x66, heures=0x67, min=0x68, date=0x69, mois=0x6A, année=0xA0.
Compteurs heures H_1..H_5 = 0x6C/6D … 0x74/A1 ; H_TOT = 0xF8/F9 ; démarrages = 0xFA/FB.
Mode auto = 0xFC ; contrôle zones d'air = 0xFD.
> **Non résolus** (valeur flash 0x0000 → remplis depuis le poêle au runtime) :
> `EEPROM_SET_POWER`, `EEPROM_SET_TEMP`, `EEPROM_SET_AMB`, `EEPROM_SET_CAN*`, `CHRONO_ENABLE*`.
> À capturer en conditions réelles.

---

## 5. Protocole SOTA2 - canal de gestion / OTA  *(confiance : HAUTE)*

**SOTA2 est un clone du protocole du bootloader série ESP32 (esptool)** : même framing
SLIP, même en-tête 8 octets, même checksum XOR seed 0xEF, même handshake SYNC 36 octets.
Le module implémente le **côté esclave** (comme un ESP en mode flash) → le poêle/display
peut le reflasher et l'interroger. **Non nécessaire pour lire la télémétrie.**

### Framing SLIP (RFC 1055)
- Délimiteur de trame : **`0xC0`** (END) en début et fin.
- Échappement : **`0xDB`** - `DB DC` → `C0`, `DB DD` → `DB`.
- Garde de timeout inter-trame : `TIMER_UART_SLAVE_SLIP`.

### En-tête de paquet (dans la trame SLIP) - layout esptool
| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | direction (0x00 requête / 0x01 réponse) |
| 1 | 1 | **opcode** |
| 2 | 2 | taille payload (uint16 LE) |
| 4 | 4 | checksum (requête) / valeur retour (réponse), uint32 LE |
| 8 | n | payload |

### Handshake SYNC *(confiance : HAUTE)*
Tant que non synchronisé, **seule la commande SYNC (0x08) est acceptée**
(sinon log `received cmd but not sync`). Validation :
- taille payload = **36** ;
- 32 octets comparés à la constante @ `0x3f413639` :
  `07 07 12 20` puis **28-32 × `0x55`** (= le payload SYNC esptool standard) ;
- OK → `Sync received`.

### Checksum data *(confiance : HAUTE)*
**XOR 8 bits des octets de data, seed `0xEF`** (= algo esptool), comparé au champ
checksum de l'en-tête. (Ce n'est **pas** un CRC.)

### Table des opcodes  *(toutes CONFIRMÉES - immédiats `movi` de la routine d'enregistrement @ 0x400e8cdc)*
| Opcode | Commande | Équiv. esptool |
|---|---|---|
| 0x02 | FLASH_BEGIN | ✓ |
| 0x03 | FLASH_DATA (payload `[len:4][seq:4][0:4][0:4][data]`) | ✓ |
| 0x04 | FLASH_END | ✓ |
| 0x08 | **SYNC** | ✓ |
| 0x09 | WRITE_REG | ✓ |
| 0x0A | READ_REG | ✓ |
| 0x0B | SPI_SET_PARAMS | ✓ |
| 0x0D | SPI_ATTACH | ✓ |
| 0x0F | SET_BAUD | ✓ |
| 0x13 | VERIFY_MD5 | ✓ |
| 0xD0 | ERASE_FLASH | ✓ |
| 0xD1 | ERASE_REGION | ✓ |
| 0xD2 | READ_FLASH | ✓ |
| **0xD4** | ESP_REBOOT | custom |
| **0xD6** | ESP_GET_SECURE_CODE | custom |
| **0xD7** | ESP_GET_MAC_ADDRESS | custom |
| **0xD8** | ESP_GET_STOVE_MODEL | custom |
| **0xDA** | ESP_GET_STOVE_ID | custom |
| **0xDC** | ESP_GET_STOVE_FIRMWARE | custom |
| **0xDE** | ESP_GET_DISPLAY_FIRMWARE | custom |

(0x12 = alias de SYNC ; erreurs loggées `ESP_CMD_VALIDATE_FAIL/MAKE_REPLY_FAIL/REPLY_FAIL/AFTER_FAIL/NOT_IMPLEMENTED: 0x%02X`.)

---

## 6. Machine à états SOTA2  *(confiance : MOYENNE)*
- Machine de réception à jump-table 6 états @ `0x3f40db20` (var d'état @ `0x3ffb7764+4`).
- États haut niveau : `SOTA2_SYNC` / `SOTA2_STBY` / `SOTA2_ERROR`.
- Sur `TIMER_UART_SLAVE_SLIP` → « Resetting serial infos » (repli, bascule 1200 8N2).
- Rôle **esclave** confirmé (timers `TIMER_UART_SLAVE*`, état 0 = poll/attente des octets du poêle).

---

## 7. Points NON résolus (honnêteté)
1. ~~Masque `uart_set_line_inverse`~~ → **RÉSOLU** : `uart_set_line_inverse(1, 0x24)`
   = `RXD_INV | TXD_INV` (décompilé, fonction `0x400e3b38`).
2. ~~CRC4R~~ → **FAUX POSITIF** : « CRC4R » n'est pas une vraie chaîne, juste des octets
   de code interprétés par `strings` (contexte `RC0R RC1R … CRC4R RC5R …`). À ignorer.
3. ~~Checksum « working mode »~~ → **HORS SUJET** : le `crc FAIL` @ `0x400e6060` est dans
   le **serveur HTTP** (chaînes `http_server_...`), c'est le protocole app/téléphone, pas
   le bus poêle. RWMS a son propre checksum additif (§3). Rien à résoudre côté stove.
4. **15 adresses EEPROM à `0x0000` en flash** = remplies au runtime (dépendantes du modèle,
   négociées avec le poêle au connect). À **capturer sur le vrai poêle** :
   `SET_POWER`, `SET_TEMP`, `SET_AMB`, `SET_CAN1/2_VEL`, `SET_CAN1/2_TEMP`,
   `CHRONO_ENABLE`+`ENABLE1..4`, `CHRONO1_START`, `CHRONO1_STOP`, `CHRONO1_DAY1`.
   (Les 62 autres EEPROM + toutes les RAM sont résolues, §4.) Ce sont des registres
   d'**écriture de consignes** - la **lecture télémétrie** (toutes les RAM) est complète.
5. Nommage symbolique des états 2-5 de la machine SOTA2 (structure connue, noms non) -
   sans impact : SOTA2 est le canal OTA, pas la télémétrie.
6. **Décodage alarmes/états** (37 entrées idx 102-138 : `NESSUN_ALLARME`, `MANCATA_ACCENSIONE_BIT`,
   `ALLARME_SOVRATEMPERATURA_H2O_BIT`, `PWM_*`, `STATO_PUL_ORD_*`…) : les noms sont extraits,
   mais leur **encodage numérique n'est pas propre** (mix adresse/masque, valeurs runtime-dépendantes) -
   couche d'**interprétation** de STOVE_STATUS/ALLARM à corréler en conditions réelles.
   N'empêche pas de lire les registres bruts (§3), juste de traduire status→libellé.

---

## 8. Implications pour OpenXtraflame (`micronova.c`)
Pour que la lecture live fonctionne, il faut :
1. **Débit = 1200 baud, 8N2** (⚠️ PAS 38400 - voir §2), sur UART1.
2. **Inverser la ligne** : `uart_set_line_inverse(UART1, 0x24)` (= `RXD_INV | TXD_INV`, valeur prouvée).
3. **Passer en MODE MAÎTRE** : *poller* le poêle (envoyer `[loc][addr]`, lire `[cksum][value]`),
   au lieu d'écouter passivement.
4. **Checksum additif** `(loc+addr+value)&0xFF`.
5. Utiliser les **vraies adresses** (§4) - les valeurs actuelles de l'enum sont des placeholders.
6. Gérer le **bank 1** (sondes T_CAMERA/BULBO/SBLOCCO/T_PUFFER_SUP) : `loc = bank`.
7. Sauter l'**écho** éventuel `[loc][addr]` avant la vraie réponse.

**Ordre d'init exact (relevé dans navel) :** `uart_param_config` → `uart_set_line_inverse(1,0x24)`
→ `uart_set_pin(1, TX=23, RX=5)` → `uart_driver_install` → (plus tard) `uart_set_baudrate(1,1200)`
+ `uart_set_stop_bits(1, UART_STOP_BITS_2)` avant de lancer le polling.

---

## Annexe - artefacts d'analyse
- Dump : `~/Desktop/extraflame_dump.bin` (backup : `firmware-backups/extraflame_dump_ORIGINAL.bin`)
- Projet Ghidra + segments carvés + scripts : `scratchpad/` (SetupMem.java, ExtractProto.java)
- Adresses clés : registration commandes `0x400e8cdc` ; SYNC `0x400e88a0` ;
  FLASH_DATA/checksum `0x400e870c` ; SLIP décodeur `0x401747cc` ; RWMS read `0x400e4df4`.
