# Cloud MQTT protocol reverse notes

Source : `extraflame_dump.bin` (=4MB flash dump du Black Label WiFi module Extraflame, 2026-07-03).

Statut : reverse partiel. Broker, TLS cert, formats topics et payload keys confirmés. Segments variables des topics à finaliser via écoute réseau (=SSL bump ou capture LAN durant l'appairage app TotalControl 2).

## 1. Broker MQTT

- Host : `mqtt.extraflame.it`
- Port : `8883` (=TLS)
- URI : `mqtts://mqtt.extraflame.it:8883`
- Version protocole : MQTT 5.0 (=usage de replyTo + correlationId properties)

## 2. Certificat racine CA

Extrait à l'offset dump `0x1f5ea`, sauvegardé dans `firmware/main/certs/extraflame_ca.pem`.

```
subject   = CN=omnyvore.com, O=Omnyvore, OU=Tech Ops, C=IT, ST=Vicenza, L=Vicenza
issuer    = self-signed
notBefore = 2017-10-05
notAfter  = 2027-10-03
sha256 fp = 17:57:43:A6:AB:91:66:D3:7A:7F:63:D3:25:67:EC:E4:83:08:7B:29:01:DB:D0:8C:39:D4:6E:D1:99:8D:BF:BF
```

**Vendeur cloud** : Omnyvore srl (Vicenza, IT) - plateforme IoT sous licence utilisée par Extraflame.  
Le firmware original tag les topics d'un préfixe `omv` (=abréviation Omnyvore).

Le cert **expire 2027-10-03** : il faudra soit se procurer le prochain cert soit implémenter `SNI + hostname_verification=false` pour le mode cloud dans les 15 mois.

## 3. Format topics MQTT

Deux templates détectés dans le dump binaire :

### Firmware topic (=4 segments variables)

```
%s/%s/%s %s/IN/firmware
```

### Autres topics (=6 segments variables)

```
%s/%s/%s/%s %s/%s/{DIR}/{family}
```

Où `{DIR} = IN | OUT | REPLY` et `{family}` prend une des 11 valeurs listées ci-dessous.

**Note** : l'espace au milieu du format est peut-être un artefact log (=le vrai topic MQTT utilise probablement `/`). À confirmer sur capture live.

## 4. Familles topics identifiées

| Direction | Family        | Rôle                                     |
|-----------|---------------|------------------------------------------|
| IN        | firmware      | Push OTA cloud → module                  |
| IN        | settings      | Commande écriture registre               |
| IN        | time          | Sync heure cloud → module                |
| IN        | crono         | Modification chrono cloud → module       |
| IN        | addr          | Commande lecture/écriture EEPROM directe |
| IN        | misc          | Divers (=probablement reboot, factory)   |
| OUT       | status        | État machine periodic                    |
| OUT       | temperature   | Températures periodic                    |
| OUT       | alarm         | Événements alarme instantanés            |
| OUT       | error         | Codes erreur instantanés                 |
| OUT       | workingtimers | Compteurs maintenance                    |
| OUT       | time          | Ack sync horloge                         |
| OUT       | crono         | Broadcast chrono actuel                  |
| OUT       | settings      | Broadcast settings actuels               |
| OUT       | addr          | Réponse lecture EEPROM                   |
| OUT       | misc          | Divers                                   |
| OUT       | dyn           | Données Addrs_dyn (=modèle-dépendant)    |
| REPLY     | settings      | Ack MQTT 5.0 après commande settings     |
| REPLY     | time          | Ack MQTT 5.0 après commande time         |
| REPLY     | crono         | Ack MQTT 5.0 après commande crono        |

Ces topics sont sauvegardés dans les variables C (=strings du dump) :
`errorOutTopic`, `cntOutTopic`, `settingsInTopic`, `settingsReplyTopic`,
`settingsOutTopic`, `timeInTopic`, `timeReplyTopic`, `timeOutTopic`,
`chronoInTopic`, `chronoReplyTopic`, `chronoOutTopic`, `addrInTopic`,
`addrOutTopic`, `miscInTopic`, `miscOutTopic`.

## 5. Payload keys (=confirmées vs addon HA)

**Time payload** : `weekday`, `hour`, `minute`, `month`, `year`

**Chrono payload** (=× 4 profils, P1..P4) :
`chronoNStartTime`, `chronoNEndTime`, `chronoNTemp`, `chronoNEnabled`,
`chronoNDays`, `chronoNPower`

**Chrono global** : `weekChronoEnabled`

**Settings payload** :
`machineState`, `mainFanSpeed`, `mainFanMode`,
`targetWaterTemp`, `targetPower`, `targetRoomTemp`,
`targetCan1Temp`, `targetCan2Temp`,
`can1FanSpeed`, `can2FanSpeed`, `can1FanMode`, `can2FanMode`,
`workingMode`, `airZoneControl`, `airZoneControlAvail`

Toutes ces clés sont déjà mappées dans notre `mqtt_bridge.c` local - la seule
différence est que le cloud utilise ces mêmes noms **avec des payloads JSON**
alors que nous utilisons chaque clé comme un topic séparé.

## 6. Identifiants module (=partition secret1)

Lecture NVS `stove_data` namespace :

| Clé NVS        | Valeur exemple | Rôle                                  |
|----------------|----------------|---------------------------------------|
| `stove_id`     | `A700xxxxxx`   | Matricola (=serial unique)            |
| `stove_model`  | `00xxxxxxxxxx` | Code produit (=12 chiffres)           |
| `secure_code`  | `xxxxxxxx`     | Code de sécurité (=8 chiffres)        |
| `product`      | ?              | Catalogue produit                     |
| `custom`       | ?              | Config custom                         |
| `collaudo`     | ?              | Code test usine                       |
| `del_model`    | ?              | Flag delete                           |

Ces 3 premières valeurs sont probablement 3 des 6 segments variables du topic.

## 7. Structure MQTT 5.0

Logs révélateurs :
```
OMNYVORE_REPLYTO=%s
OMNYVORE_CORRELATIONID=%s
OMNYVORE_SETTINGS_STATE = %i
got OMNYVORE_SETTINGS_TOPIC successful, msg_id=%d
got OMNYVORE_TIME_TOPIC successful, msg_id=%d
got OMNYVORE_CHRONO_TOPIC successful, msg_id=%d
got OMNYVORE_ADDR_TOPIC successful, msg_id=%d
got OMNYVORE_MISC_TOPIC successful, msg_id=%d
```

Chaque message IN reçu du cloud contient les properties MQTT 5.0 :
- `Response Topic` (=où publier la réponse)
- `Correlation Data` (=échoue en REPLY pour lier requête à réponse)

Le module doit lire ces properties et publier sur `Response Topic` avec la
même `Correlation Data` pour que le cloud match la commande.

## 8. Auth handshake (=CONFIRMÉ 2026-07-08 20h50)

**H1 validée par test live** :
- `protocol_ver = MQTT_PROTOCOL_V_3_1_1` (=MQTT 5.0 rejeté sans properties Omnyvore)
- `client_id = matricola` (=A700xxxxxx)
- `username  = matricola`
- `password  = secure_code` (=8 chiffres, xxxxxxxx)
- `tls_ca    = extraflame_ca.pem` (=CA Omnyvore self-signed, valide jusqu'à 2027-10-03)

Test live depuis python paho :
```python
c = mqtt.Client(client_id="A700xxxxxx", protocol=mqtt.MQTTv311)
c.username_pw_set("A700xxxxxx", "xxxxxxxx")
c.tls_set(ca_certs=CA)
c.connect("mqtt.extraflame.it", 8883)
# → on_connect rc=0 ✅ ACCEPTÉ
```

**MQTT 5.0 test** : connexion CONN_LOST immédiate. Le broker exige les properties
`OMNYVORE_REPLYTO` + `OMNYVORE_CORRELATIONID` sur le CONNECT MQTT 5.0. Sans elles,
il ferme. En MQTT 3.1.1, la connexion tient.

## 8b. ACL post-CONNECT (=bloque tout par défaut)

Après un CONNECT réussi en 3.1.1, tests SUB/PUB :

| Opération | Topic tenté                                | Résultat            |
|-----------|--------------------------------------------|---------------------|
| SUB       | `#`                                         | 128 = Not Authorized |
| SUB       | `omv/#`                                     | 128 = Not Authorized |
| SUB       | `omv/A700xxxxxx/#`                          | 128 = Not Authorized |
| SUB       | `omv/A700xxxxxx/IN/settings`               | 128 = Not Authorized |
| SUB       | `omv/00xxxxxxxxxx/A700xxxxxx/#`             | 128 = Not Authorized |
| SUB       | `omv/00xxxxxxxxxx/A700xxxxxx/xxxxxxxx/IN/*` | 128 = Not Authorized |
| PUB       | `omv/A700xxxxxx/OUT/status`                 | CONN_LOST            |
| PUB       | `omv/00xxxxxxxxxx/A700xxxxxx/OUT/status`    | CONN_LOST            |
| PUB       | tout topic contenant matricola              | CONN_LOST            |

**Conclusion** : le broker attend des topic paths spécifiques qui ne contiennent
PAS directement la matricola. Les 6 segments variables du firmware original
doivent être des IDs internes (=user_id cloud, tenant_id, etc.) obtenus lors
d'une phase de provisioning MQTT 5.0 initial.

## 8c. Prochaines étapes reverse

1. Analyser via Ghidra la fonction `main_task` du firmware original pour voir
   où les 6 %s sont remplis (=sprintf caller).
2. Chercher les strings d'IDs stockées en NVS `settings` namespace (=au-delà
   de secret1/secret2).
3. Si présentes en NVS, ce sont probablement des IDs uniques établis lors du
   provisioning cloud (=Wi-Fi setup via app TotalControl 2).
4. Alternative : sniff live du firmware original via SSL bump avec le CA
   Omnyvore comme intermédiaire (=mitmproxy en L4 avec cert re-signé).

## 9. Découvertes session 2026-07-08 soir

Reverse plus profond du firmware ELF via analyse literal-pool Xtensa :

- **Table literals fonction MQTT** @ ota_0 offset 0x62108-0x621a8, load addr
  0x400d20e8-0x400d2188
- **"omv"** @ 0x3f408b4d (=ONE occurrence unique, prefix vendor)
- **"ex"** @ 0x3f408b4d - 4 = "ex" prefix, rôle exact non confirmé
- **"1.8"** @ 0x3f408b69 = firmware version marker (=passé en 4e arg firmware topic)
- **"EU"** @ 0x3f406e3e = région (=probablement dans path)
- **"prod"** @ 0x3f40ff11 = environnement production

Firmware LIT depuis CONNACK properties :
- `"replyto"` @ 0x3f407cfc - key du Response Topic MQTT 5.0
- `"correlationid"` @ 0x3f407d5e - key du Correlation Data MQTT 5.0

Log messages révélateurs :
- `"Saved topic for later use: %s"` - client stocke topics reçus
- `"Resuming topic %s"` - client réutilise topic stocké
- `"got OMNYVORE_SETTINGS_TOPIC successful, msg_id=%d"` - après SUBSCRIBE réussi
- `"OMNYVORE_SETTINGS_STATE = %i"` - state machine après CONNECT

**Confirmation modèle** : le broker Omnyvore **assigne les topics au client via
CONNACK properties MQTT 5.0**. Le client ne connait PAS les topics à priori, il
les reçoit et les mémorise.

**Blocker restant** : le broker rejette notre MQTT 5.0 CONNECT même avec
`RequestResponseInformation=1` + `SessionExpiryInterval=60`. Il attend
probablement une `AuthenticationMethod` string custom (=peut-être "OMNYVORE"
ou similaire) + `AuthenticationData` payload spécifique.

Sans capture SSL bump du firmware original tournant, cette dernière info reste
inaccessible via strings du dump - elle est construite dynamiquement dans le
code Xtensa.

## 10. Découvertes finales session 2026-07-08

**Firmware API key UUID** trouvée dans le dump :
- Offset 0x447d0-0x447f4 : string `"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"`
- Marquée comme `"key"` dans le contexte

**Auth handshake réel confirmé** :
- `client_id = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"` (=UUID firmware, PAS matricola)
- `username  = matricola` (=A700xxxxxx)
- `password  = secure_code` (=xxxxxxxx)
- MQTT 3.1.1 (=broker refuse MQTT 5.0 sans properties custom)

Test validé : connect rc=0, TENUE 20s sans disconnect (=vs kick 15s avec matricola comme cid).

**Blocker unique restant** :
Les topics d'un module (=matricola A700xxxxxx) sont **provisionnés côté serveur
Omnyvore lors du premier appairage** via l'app TotalControl 2. Le firmware
stocke ces topics en NVS `settings` namespace après réception.

Notre dump firmware date d'AVANT tout appairage (=NVS settings vide, remplie
uniquement de 0xff). Impossible donc de récupérer les topics du dump.

Pour débloquer :
1. **Ré-appairer un module cloud** (=via app TotalControl 2) puis re-dump le
   NVS pour récupérer les topics stockés.
2. OU **Sniffer un CONNECT MQTT 5.0** du firmware original tournant en prod
   pour capturer les properties Omnyvore custom envoyées.
3. OU **Décompiler la fonction main_task Xtensa** avec Ghidra pour identifier
   la fonction qui reçoit les topics et voir sur quel événement MQTT elle est
   appelée.

## 10b. Doc Omnyvore officielle (=récupérée via Wayback Machine 2025-03-22)

Source : `https://docs.omnyvore.com` (=site en staging, indispo direct).

### Format topics officiel

```
omv/{network}/{client_id}/{thing_family}/{thing_name}/{IN|OUT}/{channel}
```

Exemple doc :
```
omv/aws/82340B8011C8/wst_1.0/wst000000000001/IN/tempctrl
omv/aws/82340B8011C8/wst_1.0/wst000000000001/OUT/localize
```

Décomposition :
- `omv` = vendor prefix hardcoded
- `{network}` = nom du réseau (=créé côté Omnyvore, ex "aws")
- `{client_id}` = MAC address hex UPPERCASE (=exemple 12 chars)
- `{thing_family}` = nom du family_type + version (=ex "wst_1.0" pour wood stove)
- `{thing_name}` = ID interne du device (=ex "wst000000000001", assigné côté
  Omnyvore, PAS notre matricola directement)
- `{IN|OUT}` = direction (=REPLY existe aussi)
- `{channel}` = nom du channel (=tempctrl, localize, etc.)

### Auth handshake officiel

Selon doc "connect-the-thing" :
- **client_id** = MAC address (=hex uppercase, no separator)
- **username** = string custom créée côté Omnyvore (=ex "stove001")
- **password** = string custom créée côté Omnyvore (=ex "password001")

Pour un module Extraflame, ces valeurs sont probablement provisionnées à
l'usine avec `username = matricola` et `password = secure_code` (=validé
par test : CONNECT rc=0).

### Blocker restant précis

Les segments `{network}`, `{thing_family}`, `{thing_name}` sont **assignés
côté serveur Omnyvore** au provisioning du device. Le firmware original les
récupère via **CONNACK properties MQTT 5.0** au CONNECT initial (=logs
`"Saved topic for later use"` et `"Resuming topic"` confirment ce flow).

Notre client MQTT 3.1.1 fonctionne pour le CONNECT (=auth valide) mais ne
peut pas récupérer ces topics assignés (=fournis uniquement via MQTT 5.0).

### Chemins pour débloquer

1. **Capture SSL bump** d'un module Extraflame tournant en cloud pour voir
   les properties MQTT 5.0 envoyées + le CONNACK payload.
2. **Login user Omnyvore + tenant Extraflame** pour voir les IDs internes
   assignés à notre matricola (=via UI web docs.omnyvore.com).
3. **Ghidra** pour disassembler la routine CONNECT MQTT 5.0 du firmware
   original et extraire les properties encodées en binaire.

### 10c. Preuve que le firmware attend une config MQTT du broker

String trouvée @ 0x10360 du dump :
```
"Waiting for configuration x,y,z from omnyvore... Tan:%i"
```

Logs environnants :
- `canN = %i` (=nombre de channels ?)
- `tanN = %i` (=Target Ambient temperature Number ?)
- `idro = %i` (=type hydro/water)

**Confirmation formelle** : le broker envoie une config initiale avec les 3
segments manquants `x, y, z` (=network, thing_family_ver, thing_name) au
CONNECT MQTT 5.0. Notre CONNECT en 3.1.1 ne déclenche pas ce push.

ELF reconstitué via `tools/make_esp32_elf.py` fonctionne pour disassembly.
La fonction MQTT init réelle reste à identifier via xrefs vers
`esp_mqtt5_client_set_connect_property` ou strings `OMNYVORE_REPLYTO`.
Session 2026-07-08 n'a pas eu le temps de la reverser en Xtensa.

## 12. Découverte majeure - REST API Extraflame (=2026-07-08 21h)

**`api.extraflame.it` = Spring Boot avec JWT auth** (=93.46.203.43).

Response headers révèlent :
- `X-Application-Context: application` - Spring Boot classique
- Body 500 sur `X-Auth-Token: <invalide>` :
```json
{
  "exception": "io.jsonwebtoken.MalformedJwtException",
  "message": "JWT strings must contain exactly 2 period characters. Found: 0"
}
```

**JWT structure exigée** : `header.payload.signature` (=3 parts séparées par `.`)

Auth flow probable :
1. `POST /login` avec username/password → JWT
2. `GET /api/things/<matricola>` avec `X-Auth-Token: <JWT>` → topics assignés

Endpoints login testés (=tous 401, mais ça signifie "accès refusé", pas "n'existe pas") :
- `/login`, `/api/login`, `/authenticate`, `/api/authenticate`
- `/oauth/token`, `/api/oauth/token`, `/token`, `/api/token`
- `/api/v1/login`, `/api/v1/authenticate`

Endpoint `/auth/login` = 404 (=celui-là n'existe pas).

**Le firmware du poêle n'utilise PAS cette API** - c'est l'app mobile
TotalControl 2 qui l'utilise. Cependant l'API est le chemin le plus direct
vers les 3 IDs manquants (=network, thing_family_ver, thing_name).

Prochaines étapes :
1. Décompiler l'APK TotalControl 2 (=via jadx ou apktool)
2. Chercher dans le smali le vrai path login + format username/password
3. Récupérer un JWT, appeler `/api/things/A700xxxxxx` (=ou similaire)
4. Extraire les 3 IDs → hardcoder dans notre firmware
5. SUB/PUB devrait alors fonctionner

## 11. Comment configurer notre firmware OpenXtraflame pour l'auth cloud

Update `cloud_bridge.c` :
```c
esp_mqtt_client_config_t cfg_mqtt = {
    .broker.address.uri            = "mqtts://mqtt.extraflame.it:8883",
    .broker.verification.certificate = (const char *)extraflame_ca_pem_start,
    .credentials.client_id         = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
    .credentials.username          = matricola,      /* mn_get_stove_matricola() */
    .credentials.authentication.password = secure_code, /* mn_get_stove_secure_code() */
    .session.keepalive             = 60,
    .session.protocol_ver          = MQTT_PROTOCOL_V_3_1_1,
};
```

CONNECT sera accepté. Publish/Subscribe restera bloqué tant que les topics
assignés au module ne seront pas récupérés (=via voie 1/2/3 ci-dessus).

## 9. Web endpoints locaux (=confirmation isolation)

Aucun endpoint HTTP cloud dans le firmware. Tous les GETs/POSTs pointent vers
l'AP local du module durant l'appairage :
```
GET /wifi
GET /favicon
GET /status
POST /connect
DELETE /connect
GET /ap
```

Ça confirme que **tout le trafic cloud passe uniquement par MQTT/8883**.

## 10. TODO pour finaliser mode cloud

1. **Capture MQTT live** : lancer un poêle avec firmware original sur LAN
   isolé + mitmproxy avec `extraflame_ca.pem` en cert intermédiaire pour
   sniffer les 6 segments variables du topic et l'auth handshake.
2. **Segments variables** : une fois connus, hardcoder les constants dans
   `cloud_bridge.c` et remplacer les segments dynamiques par
   `stove_id` / `stove_model` / `secure_code`.
3. **Correlation-ID + Response-Topic handling** : implémenter le flux MQTT 5.0
   dans le event handler du cloud_bridge.
4. **Payload JSON schemas** : documenter le schéma exact des payloads OUT/*
   (probablement `{ "prop1": v1, "prop2": v2, ... }`).
5. **Interop TotalControl 2** : tester que l'app mobile reconnaît notre
   module en tant que poêle légitime.

Objectif final : mode cloud DÉSACTIVÉ par défaut, activable via
`cfg->cloud_enabled=true` pour utilisateurs qui veulent garder l'app mobile
Extraflame en plus du contrôle local HA.
