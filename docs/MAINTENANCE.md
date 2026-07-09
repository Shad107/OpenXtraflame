# Onglet Maintenance

Documentation de l'onglet Maintenance ajouté en v0.1.0-rc2.

## Accès

Web UI locale du module → onglet **Poêle** → sous-onglet **🔧 Maintenance**.

## Sections

### 1. Auto-diagnostic combustion

Analyse en direct des métriques du poêle (=t_fumées, ratio puissances, alarmes actives) avec règles simples.

**Sévérités** :
- `critical` : action requise (=alarme active, coclea 3× sous factory)
- `warning` : à surveiller (=t_fumées hors plage, nettoyage brasero trop fréquent)
- `info` : constat sans urgence

**Limites** :
Ce diagnostic est **basé sur les valeurs live**, pas sur un historique. Il donne un aperçu instantané mais ne détecte pas le drift dans le temps ni les patterns saisonniers. Pour un diagnostic pertinent basé sur des moyennes 24h/7j/30j, utiliser le [companion service Brain](BRAIN.md) prévu en v0.2.

### 2. Compteurs maintenance

Deux compteurs suivis en NVS :

- **Service** : heures depuis dernière intervention. Seuil défaut 1500h.
- **Nettoyage brasero** : démarrages depuis dernier nettoyage. Seuil défaut 100.

Boutons **Reset** = snapshot des compteurs actuels comme "zéro" (=NVS persisté).

Exposés en MQTT :
- `sensor.<stove>_hours_since_service`
- `sensor.<stove>_hours_before_service`
- `sensor.<stove>_starts_since_cleaning`
- `sensor.<stove>_starts_before_cleaning`
- `button.<stove>_reset_service`
- `button.<stove>_reset_cleaning`

### 3. Paramètres techniciens (Pr01-Pr30)

Table live des 32 premiers registres EEPROM `0x40-0x5F` du menu UT04 Micronova.

**Colonnes** :
- **Pr** : numéro Micronova I023 aria (=Pr01→Pr30 approx)
- **EEPROM** : adresse hex physique (=0x40+N)
- **Label** : description italienne/française (=labels validés Micronova I023 aria)
- **Actuel** : valeur lue en direct via protocole
- **Factory** : valeur usine Micronova I023 aria (=référence)
- **Écart** : % vs factory (=vert <10%, orange <30%, rouge ≥30%)
- **Reco** : badge safety zone

**Safety zones** :

| Zone | Couleur | Description | Action UI |
|---|---|---|---|
| `SAFE` | 🟢 vert | Réglages user (=chrono, delta thermostat) | Modifiable directement |
| `COMBUSTION` | 🟠 orange | Affecte combustion (=coclea, aspiration) | Double confirmation + warning |
| `DANGER` | 🔴 rouge | Sécurité (=seuils surchauffe, timing) | NON modifiable via UI |

**Écriture** :
- Bouton ✏️ sur chaque ligne non-danger
- Prompt affiche valeur actuelle + factory + zone
- Warning conditionnel selon zone
- Confirmation double
- POST `/api/params/tech/write` avec `{"addr":<0x40-0x5F>, "value":<0-255>}`
- Le firmware queue le write via `mn_write_register()` (=opcode Micronova EEPROM 0xA0)
- Valeur confirmée au prochain cycle de poll (~15s)

**Sources labels** :
- [Micronova I023 aria LED Manual](https://www.sercatec.com/wp-content/uploads/2021/10/MANUAL-AIRE-LED.pdf)
- [Extraflame Technical Manual 00227596](https://www.manualslib.com/manual/3000756/Extraflame-00227596.html)

### 4. Historique alarmes

Ring buffer 20 dernières alarmes, persisté NVS.

**Colonnes** :
- Début (=timestamp unix formaté)
- Fin (=timestamp ou "-" si active)
- Durée (=minutes)
- Code (=hex du bit d'alarme)
- Type (=label français depuis dictionnaire de bits)

**Bits d'alarme** (=décomposition du registre `RAM_ALLARM`) :

| Bit | Hex | Label français |
|---|---|---|
| 0 | 0x01 | Sonde fumées défectueuse |
| 1 | 0x02 | Fumées trop chaudes |
| 2 | 0x04 | Fumées court-circuit |
| 3 | 0x08 | Aspirateur défectueux |
| 4 | 0x10 | Échec allumage |
| 5 | 0x20 | Perte de flamme |
| 6 | 0x40 | Dépression insuffisante |
| 7 | 0x80 | Commande vis sans fin |

Exposé MQTT :
- `sensor.<stove>_history_alarms` (=state = count, attrs = events)

### 5. Recovery

Bouton **Rollback firmware** = bascule vers le slot OTA précédent + reboot.

Utile si :
- Nouveau firmware pose problème (=UI cassée, boot loop non détecté)
- Regression fonctionnelle après OTA

Le firmware précédent reste stocké dans le slot OTA inactif jusqu'à la prochaine OTA.

Exposé MQTT :
- `button.<stove>_rollback_firmware`

## API HTTP

| Endpoint | Méthode | Description |
|---|---|---|
| `/api/params/tech` | GET | Dump 32 registres EEPROM 0x40-0x5F |
| `/api/params/tech/write` | POST | Écrit un registre (=body JSON `{addr, value}`) |
| `/api/params/tech/snapshot` | POST | Baseline t0 pour reverse UT04 live |
| `/api/params/tech/diff` | GET | Registres qui ont bougé depuis baseline |
| `/api/eeprom/snapshot` | GET | Dump complet EEPROM 0x00-0xFF |
| `/api/history/alarms` | GET | Ring buffer alarmes JSON |
| `/api/maint/reset_service` | POST | Zéro le compteur service |
| `/api/maint/reset_cleaning` | POST | Zéro le compteur nettoyage brasero |
| `/ota/rollback` | POST | Rollback vers slot OTA précédent |

## MQTT topics

Prefix `<mqtt_prefix>/<stove_name>/`.

**Read (=publish firmware → HA)** :
- `state` → JSON complet (=t_ambient, t_fumées, alarm_code, compteurs, etc.)
- `history_alarms` → `{"events":[...], "count":N}` (=retained qos1)
- `params_tech` → `{"params":[{pr,addr,val,factory,div}], "divergent_count":N}` (=retained qos1)
- `combustion_diag` → `{"diagnostics":[...], "severity":"ok|info|warning|critical"}` (=retained qos1)

**Write (=subscribe HA → firmware)** :
- `cmd/reset_service` → reset compteur service
- `cmd/reset_cleaning` → reset compteur nettoyage
- `cmd/rollback_firmware` → rollback slot OTA précédent

## Codes accès menu tech écran poêle

Pour modifier les Pr directement depuis l'écran du poêle (=sans passer par l'UI) :

**Séquence Extraflame** :
```
P6 (=régler horloge)
P2 x N → écran "TECHNIC SET"
P6
Maintenir P4 → saisir A0 (Extraflame overlay) ou A9 (Micronova natif)
P6 pour confirmer
"GENERAL DATA"
P2 → naviguer sections
P6 → entrer dans section
P4/P5 → modifier valeur
P2 → suivant / mémoriser
P3 → retour
P1 → sortir
```

**Codes secrets** :
- `A0` / `A9` : accès menu technicien
- `77` : reset alarmes / heures partielles / conduit / déblocage
- `01` : recharge defaults usine

Sources :
- Manuel Extraflame 00227596 (=structure menu)
- Manuel Micronova I023 aria LED (=labels + defaults)

## Limitations connues

- **Diagnostic live only** : pas de moyenne 24h/7j (=à venir via [Brain](BRAIN.md))
- **Labels Pr** : mapping I023 aria supposé linéaire à partir de 0x40. Décalage possible sur certains registres (=à valider live diff quand accès physique)
- **Zone DANGER** non modifiable : sécurités surchauffe, timing bougie, seuils allumage
- **Watcher passif désactivé** : capture des mutations spontanées en zone tech désactivée depuis brick 2026-07-09. À refonter avec throttle + bounded NVS
