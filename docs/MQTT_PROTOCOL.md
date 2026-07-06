# Protocole MQTT openextraflame

Le firmware openextraflame publie l'état du poêle sur MQTT et s'abonne aux commandes de contrôle. Compatible avec Home Assistant via MQTT Discovery (=optionnel).

## Configuration topic prefix

Le préfixe est configurable via l'UI web. Par défaut : `extraflame`.

Chaque poêle a aussi un nom (=stove_name), par défaut : `poele`.

Le préfixe complet est donc : `<mqtt_topic_prefix>/<stove_name>/`.

Exemple : `extraflame/poele/state`

## Topics publiés (=OUT)

### `<prefix>/state` (=publish périodique)

Toutes les `publish_interval_ms` (=défaut 5000ms).

```json
{
    "online": true,
    "state": 4,
    "power": 3,
    "alarm": 0,
    "t_ambient": 20.5,
    "t_water": 0,
    "t_smoke": 145.2,
    "setpoint": 21
}
```

Fields :
- `online` : true si UART Micronova répond
- `state` : état poêle (=0=off, 1=starting, ..., 8=alarm)
- `power` : niveau puissance actuel (=0-5 typique)
- `alarm` : code alarme (=0 si aucune)
- `t_ambient` : température ambiante (°C)
- `t_water` : température eau (=modèles idro seulement)
- `t_smoke` : température fumées (°C)
- `setpoint` : consigne actuelle (°C)

### `<prefix>/availability` (=LWT)

`online` ou `offline` (=Last Will Testament).

## Topics souscrits (=IN, commandes)

### `<prefix>/cmd/on`

Payload : ignoré. Envoie commande MN_RAM_ACCENDI au poêle.

### `<prefix>/cmd/off`

Payload : ignoré. Envoie MN_RAM_SPEGNI.

### `<prefix>/cmd/reset_alarm`

Payload : ignoré. Envoie MN_RAM_SBLOCCO.

### `<prefix>/cmd/setpoint`

Payload : integer °C, ex `21`. Envoie au MN_RAM_TAMB.

### `<prefix>/cmd/power`

Payload : integer 0-5, ex `3`. Envoie au MN_RAM_POT_REALE.

## HA MQTT Discovery

Si `ha_discovery_enabled=true` (=défaut), le firmware publie automatiquement sur les topics HA :

- `homeassistant/climate/<mac>/config` - entité climate
- `homeassistant/sensor/<mac>_temp_ambient/config` - sensor température
- `homeassistant/sensor/<mac>_temp_smoke/config` - sensor fumées
- `homeassistant/sensor/<mac>_alarm/config` - sensor alarme
- `homeassistant/switch/<mac>_power/config` - switch on/off

HA détecte automatiquement le poêle et l'ajoute comme device.

## Configuration Mosquitto local

Installation sur Debian/Ubuntu :

```bash
apt install mosquitto mosquitto-clients
systemctl enable --now mosquitto
```

Configuration user/password (=recommandé) :

```bash
mosquitto_passwd -c /etc/mosquitto/passwd olivier
```

Édite `/etc/mosquitto/conf.d/local.conf` :

```conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Restart :

```bash
systemctl restart mosquitto
```

## Configuration openextraflame

Dans l'UI web :

- MQTT Host : IP de ton Mosquitto (=192.168.1.10 par ex)
- MQTT Port : 1883
- User : olivier
- Password : (=celui défini par mosquitto_passwd)
- Topic prefix : extraflame
- Utiliser TLS : décoché (=LAN local)
- HA discovery : coché

## Test manuel avec mosquitto_sub/pub

Vérifier que le poêle publie :

```bash
mosquitto_sub -h 192.168.1.10 -u olivier -P xxx -t 'extraflame/#' -v
```

Envoyer une commande :

```bash
mosquitto_pub -h 192.168.1.10 -u olivier -P xxx -t 'extraflame/poele/cmd/on' -m ''
```

## Compat avec Extraflame cloud (=optionnel)

Le protocole cloud original utilise :

- Broker : `mqtts://mqtt.extraflame.it:8883`
- Cert CA : Omnyvore self-signed (=embedded)
- Auth : HMAC signed
- Topics : `%s/%s/%s/%s IN/... OUT/... REPLY/...` (=hiérarchie 4 niveaux)

openextraflame **ne reproduit PAS** ce protocole cloud. Il est destiné à un usage 100% local avec ton propre broker MQTT.

Si tu veux garder l'accès Total Control 2.0, utilise Target External (=le module Black Label reste actif avec son firmware d'origine, l'ESP32 spare fait le bridge local en parallèle).

## ⭐ MAJ 2026-07-03 SOIR - Découverte via QEMU + GDB

**Extraflame module = SLAVE UART, POELE = MASTER** :
- Le module ESP32 fait UNIQUEMENT des UART_READ (706 hits observés)
- Aucun UART_WRITE avant réception command du poêle
- Pattern : polling 1 byte / 100ms timeout à l'infini
- Buffer réception @ DRAM 0x3ffb7761

**Impact pour openextraflame** :
- micronova.c doit implémenter un slave listener, PAS un master polling
- Attend que le poêle envoie une command
- Répond avec les données demandées
- Le poêle initie TOUJOURS

**Correction architecture** :
```c
// Ancien design (=master polling)
for (;;) {
    mn_read_ram(MN_RAM_TAMB);  // faux : c'est le poêle qui appelle
    vTaskDelay(...);
}

// Nouveau design (=slave listener)
for (;;) {
    uart_read_bytes(port, &cmd, 1, portMAX_DELAY);
    handle_stove_command(cmd);
}
```
