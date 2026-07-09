# openxtraflame-brain

Companion service (=roadmap v0.2) qui apprend le comportement de ton poêle sur du long-terme et propose des ajustements actionnables.

## Pourquoi un service séparé ?

Le firmware ESP32 embarqué ne peut PAS faire de vraie analyse historique :
- 4MB flash, ~200KB heap → pas de place pour stocker plusieurs mois de metrics
- Pas de fs long-terme fiable
- Diagnostic firmware = live only, non pertinent (=T fumées 32°C ≠ mauvaise combustion, poêle éteint)

Un service séparé permet :
- Historique long-terme (=1 an+ dans SQLite)
- Compute non contraint (=EMA sur 7j/30j, drift detection, patterns saisonniers)
- Cross-ref avec météo / présence / heures via HA API
- UI riche pour propositions + confirmation
- Rollback intelligent si une modif Pr empire

Le **firmware reste stable** = zéro risque de brick lié à l'analyse.

## Architecture

```
┌──────────────┐    MQTT     ┌───────────────────┐    HA API    ┌──────────┐
│              │─────────────▶                   │──────────────▶          │
│ OpenXtraflame│             │  openxtraflame-   │              │ Home     │
│ ESP32        │             │  brain (Docker)   │              │ Assistant│
│              │◀────────────│                   │◀─────────────│          │
│              │  HTTP write │  SQLite + FastAPI │  météo, etc. │          │
└──────────────┘  Pr01-Pr30  └───────────────────┘              └──────────┘
                                     │
                                     │ REST + UI
                                     ▼
                              ┌─────────────┐
                              │  User web   │
                              │  browser    │
                              └─────────────┘
```

## Stack technique

```
Image: ghcr.io/shad107/openxtraflame-brain:latest
  Multi-arch: linux/amd64, linux/arm64, linux/arm/v7

Containers dépendances: aucune (=SQLite embedded)

Composants:
  - FastAPI + Uvicorn   → API REST + UI wizard (:8080)
  - Jinja2 templates    → UI onboarding + dashboard
  - paho-mqtt           → subscribe openxtraflame/#
  - SQLite embedded     → persist metrics + config
  - APScheduler         → compute EMA/drift toutes les 5 min
  - httpx               → HA API + firmware ESP32 REST
  - Pydantic            → validation input UI

Volumes:
  /app/data
    ├── config.yaml     → broker MQTT + HA URL/token + seuils
    ├── history.db      → SQLite metrics horodatés
    └── logs/           → applog

Ports:
  8080  → UI + REST API
```

## Modèle SQLite

```sql
-- Metrics horodatés (=insert toutes les 30s)
CREATE TABLE metrics (
    ts             INTEGER PRIMARY KEY,   -- unix timestamp
    t_ambient      REAL,
    t_smoke        REAL,
    state          INTEGER,               -- 0=off, 1=start, ...
    power_level    INTEGER,               -- 1-5
    power_real     INTEGER,
    modulation     INTEGER,
    alarm_code     INTEGER,
    tremie_vide    INTEGER,               -- 0/1
    hours_total    INTEGER,
    starts_total   INTEGER
);
CREATE INDEX idx_metrics_ts ON metrics(ts);

-- Paramètres Pr01-Pr30 horodatés (=chaque changement détecté)
CREATE TABLE params_history (
    ts             INTEGER,
    addr           INTEGER,               -- 0x40-0x5F
    value          INTEGER,               -- 0-255
    PRIMARY KEY (ts, addr)
);

-- Propositions générées + résultat après application
CREATE TABLE proposals (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_created     INTEGER,
    ts_applied     INTEGER NULL,
    ts_reverted    INTEGER NULL,
    addr           INTEGER,
    val_before     INTEGER,
    val_after      INTEGER,
    reason         TEXT,                  -- diagnostic déclencheur
    result         TEXT NULL              -- observé après 24h
);

-- Config (=1 ligne, upsert)
CREATE TABLE config (
    key            TEXT PRIMARY KEY,
    value          TEXT
);
```

## UI wizard onboarding

**Étape 1 — Bienvenue**
```
OpenXtraflame-Brain
Companion service d'analyse et recommandations pour ton poêle.

Ce wizard va configurer:
- Ta connexion MQTT au poêle
- L'intégration Home Assistant
- La collecte de metrics dans une base locale

[ Commencer → ]
```

**Étape 2 — MQTT broker**
```
Auto-détection... trouvé:
  ✓ Broker MQTT sur homeassistant.local:1883

Confirmer ou modifier:
  Host       [homeassistant.local ]
  Port       [1883                ]
  Username   [                    ]
  Password   [                    ]
  Prefix     [openxtraflame       ]

[ Tester ]  → ✓ Connecté, 12 topics openxtraflame trouvés

[ Suivant → ]
```

**Étape 3 — Home Assistant (=optionnel)**
```
Home Assistant permet de:
- Récupérer météo pour corrélations
- Publier propositions en notifications
- Auto-import package sensors REST

URL HA    [http://homeassistant.local:8123]
Token     [                                ]  ← long-lived access token

[ Tester ]  → ✓ Connecté, sensor.poele_online détecté

[ Auto-import package HA ] [ Skip ] [ Suivant → ]
```

**Étape 4 — Ready**
```
✓ Tout est configuré !

Le service va maintenant collecter des metrics pendant 7 jours avant
de proposer les premiers ajustements. Tu peux consulter le dashboard
en attendant.

[ Aller au dashboard → ]
```

## Phase learning

**Ingestion** : subscribe MQTT `openxtraflame/<stove>/state`, `params_tech`, `history_alarms`. Insert dans `metrics` toutes les 30s (=throttle vs le publish firmware toutes les 2s).

**Compute** — job APScheduler toutes les 5 min :

```python
# EMA multi-échelle
ema_1h    = compute_ema(metrics, window=3600)
ema_24h   = compute_ema(metrics, window=86400)
ema_7d    = compute_ema(metrics, window=604800)
ema_30d   = compute_ema(metrics, window=2592000)

# Drift detection
drift_smoke = (ema_7d.t_smoke / ema_30d.t_smoke - 1) * 100
if abs(drift_smoke) > 10:
    trigger_proposal(...)

# Ratio puissances (=indicateur saturation poêle)
p5_ratio = ema_7d.hours_p5 / ema_7d.hours_total
if p5_ratio > 0.35:
    trigger_proposal(...)

# Fréquence démarrages (=cycles courts)
starts_per_day = ema_7d.starts_total / 7
if starts_per_day > 5:
    trigger_proposal(...)
```

**Baseline stove-specific** — après 7 jours de learning, on considère les moyennes normales pour CE poêle dans CE contexte (=isolation, taille pièce, pellets utilisés). Les alertes se déclenchent sur écart à cette baseline, pas sur valeurs absolues génériques.

## Phase propositions

Chaque diagnostic peut générer une **proposition** stockée en DB :

```json
{
  "id": 42,
  "ts_created": 1720608000,
  "addr": 71,             // 0x47 = Pr08 Coclea P3
  "val_before": 9,
  "val_after": 12,
  "reason": "T fumées EMA-7j +14% vs baseline. Suggestion: augmenter coclea Pr08 pour compenser encrassement probable.",
  "confidence": 0.72,
  "safety": "combustion"
}
```

**UI dashboard** affiche les propositions actives :

```
┌──────────────────────────────────────────────────────────┐
│  💡 Proposition #42 (=confiance 72%)                     │
│                                                          │
│  Pr08 Coclea puissance 3 : passer de 9 → 12              │
│                                                          │
│  Raison: T fumées moyenne 7j est de 215°C, vs baseline   │
│  188°C (=+14%). Signal probable d'encrassement conduit   │
│  ou de coclea sous-alimentée. Augmenter la coclea de     │
│  0.3s permet de compenser.                               │
│                                                          │
│  Zone: 🟠 COMBUSTION - modif surveillée 24h              │
│                                                          │
│  [ Appliquer ]  [ Reporter ]  [ Ignorer ]                │
└──────────────────────────────────────────────────────────┘
```

**Sur clic Appliquer** :
1. POST `http://<esp32>/api/params/tech/write {addr, value}`
2. Update `proposals.ts_applied`
3. Watch metrics pendant 24h
4. Si drift EMPIRE (=T fumées +20% vs baseline) → **auto-rollback** vers `val_before` + notification HA
5. Si drift AMÉLIORÉ → mark `result="improved"` + apprentissage baseline

## Package HA auto-généré

Sur wizard étape 3 (=avec token HA), le service peut créer via API :

```yaml
# packages/openxtraflame_brain.yaml
rest:
  - resource: http://openxtraflame-brain:8080/api/status
    scan_interval: 60
    sensor:
      - name: "Poêle Brain baseline age"
        value_template: "{{ value_json.baseline_days }}"
      - name: "Poêle Brain propositions actives"
        value_template: "{{ value_json.proposals_active }}"

sensor:
  - platform: rest
    resource: http://openxtraflame-brain:8080/api/diagnostic
    name: "Poêle diagnostic historique"
    json_attributes:
      - severity
      - diagnostics
      - drift_smoke_7d
      - p5_ratio

automation:
  - alias: "Notif proposition Poêle Brain"
    trigger:
      platform: state
      entity_id: sensor.poele_brain_propositions_actives
    condition: "{{ trigger.to_state.state | int > 0 }}"
    action:
      service: notify.mobile_app_ton_pixel
      data:
        message: "{{ states('sensor.poele_diagnostic_historique') }}"
        data:
          url: "http://openxtraflame-brain:8080/"
```

## Sécurité writes Pr

Le service N'ÉCRIT JAMAIS directement dans le firmware :
- Chaque proposition passe par un clic user explicite
- Zone `DANGER` : jamais proposée en auto (=uniquement lecture)
- Zone `COMBUSTION` : proposition + double confirmation UI + watch 24h
- Auto-rollback si drift empire (=fail-safe)

Le firmware garde son endpoint `/api/params/tech/write` avec validation :
- Refuse `addr < 0x40 || addr > 0x5F` (=hors zone tech)
- Refuse `value < 0 || value > 255`

## Roadmap

- **v0.1** : MVP wizard + collecte MQTT + dashboard metrics + 3 diagnostics data-driven simples
- **v0.2** : 10+ règles diagnostics + auto-rollback + package HA auto-gen
- **v0.3** : Cross-ref météo HA + patterns saisonniers + prédictions conso
- **v0.4** : Modèle ML léger (=régression sur T fumées prédite vs observée) pour détection anomalies fines
- **v1.0** : Multi-poêle support (=un service = plusieurs stoves) + partage anonymisé baselines communauté

## Installation prévue

```bash
docker run -d \
  --name openxtraflame-brain \
  -p 8080:8080 \
  -v poele-data:/app/data \
  --restart unless-stopped \
  ghcr.io/shad107/openxtraflame-brain:latest

# Puis ouvrir http://<host>:8080 → wizard
```

Compatible :
- Docker Desktop / Docker Engine
- Portainer
- Synology DSM Container Manager
- TrueNAS Scale
- Raspberry Pi (=arm64)
- Home Assistant OS via addon (=à venir)
