# Related projects - Micronova / Extraflame / Pellet Stove

État du paysage open-source vérifié le 3 juillet 2026, mis à jour 8 juillet 2026.

## Remerciements / crédits communauté

**openextraflame s'appuie explicitement sur le travail de reverse engineering
communautaire suivant** :

- **[philibertc/micronova_controller](https://github.com/philibertc/micronova_controller)** —
  référence historique du protocole série Micronova. Décryptage frames 8N1 + shadow RAM.
- **[Jorre05/micronova](https://github.com/Jorre05/micronova)** — composant ESPHome
  officiel Micronova, source pour opcodes + adresses standards.
- **[edenhaus/esphome-extraflame](https://github.com/edenhaus/esphome-extraflame)** —
  archivé 2026-01-08 mais premier à identifier les registres EEPROM I_VENT.
- **[vincentwolsink/home_assistant_micronova](https://github.com/vincentwolsink/home_assistant_micronova)** —
  intégration HA python, référence pour les commandes state (=ACCENDI/SPEGNI).
- **[Legobas/micronova2mqtt](https://github.com/Legobas/micronova2mqtt)** — bridge Go
  MQTT, valide philosophiquement l'approche "MQTT-first".
- **[morettigiorgio/micronova-C6](https://github.com/morettigiorgio/micronova-C6)** —
  PCB one-wire pour ESP32-C6, référence pour signal conditioning.

**openextraflame ajoute la couche manquante** : reflash direct du module Extraflame
Black Label T009_3 + reverse engineering du cloud MQTT Omnyvore. Aucun de ces projets
ne remplace le firmware d'origine du module Wi-Fi.

Si tu ne veux pas reflasher, **utilise plutôt un des projets ci-dessus** avec un
ESP32 externe cablé sur le bus série de ton poêle — c'est plus simple et safe.

## Approches existantes

Tous les projets publics tombent dans 3 catégories :

1. **ESP externe sur bus série Micronova** (=majorité)
2. **Intégration cloud Agua IOT / Total Control** (=passe par cloud tiers)
3. **Polling HTTP local du module WiNET** (=1 seul projet, module différent)

**Aucun projet public n'a reflashé le firmware du module Extraflame Black Label T009_3 avant openextraflame.**

## Projets inventoriés (juillet 2026)

### Micronova protocol - ESP externe

| Projet | Techno | Statut | Notes |
|--------|--------|--------|-------|
| philibertc/micronova_controller | ESP32 Arduino | Actif, 138 commits | Référence historique |
| Jorre05/micronova | ESPHome officiel | Actif | Composant Micronova officiel |
| edenhaus/esphome-extraflame | ESPHome | **Archivé 2026-01-08** | Redirige vers Jorre05 |
| morettigiorgio/micronova-C6 | ESP32-C6 one-wire | 2026-03-14 | PCB custom |
| r-hmn/micronova_stove_esphome | ESPHome + PCB | 2026-05-09 | PCB custom |

### Cloud bridge (Agua IOT)

| Projet | Techno | Statut | Notes |
|--------|--------|--------|-------|
| vincentwolsink/home_assistant_micronova_agua_iot | Python | v1.1.2 (2026-06-01) | **Nouveauté** : mode BLE expérimental |
| Legobas/micronova2mqtt | Go | 2026-06-26 | Bridge cloud → MQTT |

### HTTP local

| Projet | Techno | Statut | Notes |
|--------|--------|--------|-------|
| notarobot63/thermorossi-ha | Python | 2026-03-14 | Poll `/ajax/get-registers` sur module WiNET (=White Label, PAS Black Label) |

## Positionnement openextraflame

```
🎯 Deux niches vides occupées :

1. TARGET_BLACKLABEL - Reflash firmware original
   → 0 concurrents monde
   → Seule solution zero-hardware pour utilisateurs Black Label

2. Cloud proxy local (=idea, à explorer)
   → 0 concurrents Extraflame spécifique
   → Legobas montre que "Go + MQTT bridge" est validé philosophiquement
```

## Différences architecturales notables

- **Reflash direct** (=openextraflame) : ne dépend d'aucun cloud tiers, ni d'Extraflame, ni d'Agua IOT
- **Approches Agua IOT** : dépendance cloud tiers (=peut disparaître)
- **ESP externe** : dépend du bus série Micronova physiquement accessible

## Caveat

Vérification agent 2026-07-03 avec rate-limit GitHub API anonyme. Un post italien/allemand sur forum obscur pourrait avoir échappé. À re-vérifier avant publication publique.
