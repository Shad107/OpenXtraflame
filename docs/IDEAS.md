# Ideas backlog

Idées à explorer plus tard, pas prioritaires pour la release initiale.

## Idée 1 - Fake cloud Omnyvore (=3e voie sans reflash)

**Proposée par Olivier le 2026-07-03 soir.**

### Concept

```
┌──────────────┐  DNS spoof   ┌──────────────────┐   MQTT HA   ┌────────────┐
│ Module       │  + TLS       │  fake-omnyvore   │   standard  │ Mosquitto  │
│ Extraflame   │ ───────────► │  (Docker/LXC)    │ ──────────► │  local     │
│ stock fw     │  fake broker │  - DNS override  │             │            │
│ (=zéro modif)│              │  - Cert présenté │             └────────────┘
└──────────────┘              │  - Topic bridge  │
                              └──────────────────┘
```

### Valeur

Une 3e option d'usage qui **ne demande PAS de reflash** :

| Approche | Difficulté | Public cible | Réversibilité |
|----------|-----------|--------------|---------------|
| TARGET_EXTERNAL (=ESP externe) | Facile | Bricoleurs Arduino | Débrancher |
| **Cloud proxy local** | Moyen | Homelabbers Docker | Retirer DNS override |
| TARGET_BLACKLABEL (=reflash) | Hard | Reversers avancés | esptool restore |

### Ce qu'on a déjà pour construire ça

Grâce au dump du 2026-07-03 :
- ✅ Endpoints Omnyvore exacts (=hostname MQTTS extraits)
- ✅ Topics IN/OUT/REPLY du protocole
- ✅ Format messages JSON attendus
- ✅ Certificat CA baked-in extrait du NVS
- ✅ secure_code + stove_model pour authentification

### Obstacle bloquant (=à re-vérifier)

⚠️ **User se souvient d'avoir tenté une approche TLS/cert et que ça n'a pas marché** (=session 2026-07-03).

Le certificat extrait du NVS est probablement le **CA racine Omnyvore privée** :
- On a la clé PUBLIQUE (=peut vérifier des certs signés par Omnyvore)
- On n'a PAS la clé PRIVÉE (=impossible de signer notre propre cert pour fake domain)
- Le module trust seulement les serveurs signés par cette CA → **fake broker rejeté au TLS handshake**

### Test à faire avant d'investir

```bash
cd /home/user/projects/openextraflame
openssl x509 -in analysis/omnyvore_ca.pem -text -noout | grep -E "Issuer|Subject"
```

- Si `Issuer == Subject` → CA auto-signée Omnyvore privée → **fake cloud impossible sans reflash**
- Si `Issuer == "R3 Let's Encrypt"` ou public → **fake cloud faisable en un weekend**

### Contournements théoriques si CA privée

1. **Patcher NVS** pour injecter notre propre CA → mais ça demande accès flash = autant reflasher
2. **MITM sur handshake TLS** pour forced downgrade → dépend de l'implémentation ESP-TLS
3. **Trouver un code path sans TLS** dans le firmware original (=peu probable)
4. **Rejoindre la meme filière** que Legobas/micronova2mqtt et utiliser le vrai cloud Agua IOT → mais on perd l'indépendance

### Décision

**Note l'idée, ne pas investir avant d'avoir vérifié le type de CA du certificat.**

Si CA privée : abandon, TARGET_BLACKLABEL reste la seule voie zero-cloud.
Si CA publique : ajouter `cloud-proxy/` au projet comme 3e approche.
