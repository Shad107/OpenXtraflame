# Related projects - Micronova / Extraflame / Pellet Stove

État du paysage open-source vérifié le 3 juillet 2026.

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
