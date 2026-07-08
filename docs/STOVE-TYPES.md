# Types de poêle Extraflame supportés

Le firmware Extraflame Black Label v1.8 (=décembre 2022) contient des tables d'adresses EEPROM/RAM dynamiques (`Addrs_dyn`) pour **12 types de poêle** distincts, groupés en 4 familles.

## Enum interne (=firmware original)

```c
typedef enum {
    STOVE_TYPE_UNDEFINED  = 0,
    STOVE_TYPE_I_CALD     = 1,   // caldaia (=chaudière biomasse pleine)
    STOVE_TYPE_I_CANAL    = 2,   // canalizzato (=air canalisé)
    STOVE_TYPE_I_CANAL_2  = 3,
    STOVE_TYPE_I_CANAL_3  = 4,
    STOVE_TYPE_I_CANAL_4  = 5,
    STOVE_TYPE_I_IDRO     = 6,   // hydro (=échangeur eau intégré)
    STOVE_TYPE_I_IDRO_2   = 7,
    STOVE_TYPE_I_VENT     = 8,   // ventilato (=Teodora Evo confirmée)
    STOVE_TYPE_I_VENT_2   = 9,
    STOVE_TYPE_I_VENT_3   = 10,
    STOVE_TYPE_I_VENT_4   = 11,
    STOVE_TYPE_I_VENT_5   = 12,
} stove_type_t;
```

## Familles

| Famille | Sous-types | Caractéristique | Cas d'usage |
|---|---|---|---|
| **I_VENT** | 5 variantes (1-5) | Poêle ventilé simple (=air seul) | Chauffage direct pièce à vivre |
| **I_IDRO** | 2 variantes | Poêle avec échangeur eau intégré | Chauffage central compact |
| **I_CANAL** | 4 variantes | Poêle canalisé (=gaine air chaud) | Multi-pièces via gaine |
| **I_CALD** | 1 (pleine chaudière) | Chaudière biomasse pleine | Chaufferie principale |

## Adresses EEPROM par famille (extrait du reverse)

Le firmware original stocke 60 `Addrs_dyn` tables (=une par type × plusieurs sous-tables `RAM/EEPROM/chrono/custom`). Toutes extraites du dump à l'offset file `0x646e4`.

### I_VENT (=Teodora Evo, validé 2026-07-08)

| Symbole | Adresse | Valeur observée | Rôle |
|---|---|---|---|
| `EEPROM_SET_POWER_ADDR` | **0x7F** | 4 (=P.set utilisateur) | Puissance demandée (1-5), persistant |
| `EEPROM_SET_AMB_ADDR` | **0x7D** | 27 (=consigne °C) | Consigne ambiance (raw °C), persistant |
| `EEPROM_SET_TEMP_ADDR` | 0x3F | 28 | ? à valider |
| `EEPROM_SET_CHRONO1_POWER_ADDR` | 0x57 | 54 | ? |

### I_IDRO, I_CANAL, I_CALD

Adresses extraites du firmware mais **non-validées empiriquement** faute de poêle physique de ce type. Contributions bienvenues !

## Détection automatique (=Phase 3, à venir)

Le firmware original détecte le type via :
1. Lecture du champ `matricola` depuis la partition `secret1` (=stockée à la fabrication)
2. Regex sur le préfixe matricola pour identifier le sous-type
3. Sélection de la bonne table `Addrs_dyn`

Actuellement OpenXtraflame **hardcode `STOVE_TYPE_I_VENT`** (=validé sur Teodora Evo). Une PR pour ajouter la détection auto est bienvenue.

## Comment valider votre poêle

1. Flasher OpenXtraflame (=voir README.md)
2. Ouvrir le web UI : `http://<ip-module>/`
3. Vérifier la carte "Registres live"
4. Sur l'écran physique, changer P.set à 3 puis 5
5. Regarder si le registre `EEP_SET_POWER` bouge en direct
6. Si oui = votre poêle est bien I_VENT (=Teodora Evo compatible)
7. Sinon = testez les autres tables (à documenter dans une PR)
