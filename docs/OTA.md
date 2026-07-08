# OTA Firmware Updates

OpenXtraflame supporte les mises à jour Over-The-Air (=OTA) via 3 mécanismes.

## Architecture

Le firmware utilise le mécanisme OTA standard ESP-IDF avec 2 slots :

```
┌─────────────────────────────────────────────┐
│  Partition otadata (=pointer)                │
│  → indique quel slot est actif               │
└─────────────────────────────────────────────┘
      │
      ├─ ota_0 (=1.5MB)  ← firmware A actif
      │                     ou fallback
      │
      └─ ota_1 (=1.5MB)  ← firmware B fallback
                            ou nouveau upload
```

**Rollback automatique** : si le nouveau firmware ne marque pas "app valid" dans les 30 secondes après boot (=via `esp_ota_mark_app_valid_cancel_rollback()`), le bootloader ESP-IDF revient automatiquement au slot précédent.

## Mécanisme 1 - Upload via Web UI

Sur la page config web, section OTA :

1. Cliquer "Choisir fichier" et sélectionner `OpenXtraflame.bin`
2. Cliquer "Upload"
3. Barre de progression
4. Module redémarre automatiquement
5. Nouveau firmware boot, marque comme valide après 30s

Endpoint : `POST /ota/upload` avec Content-Type multipart/form-data

## Mécanisme 2 - Pull depuis URL HTTP(S)

Publier sur MQTT :

```bash
mosquitto_pub -h 192.168.1.10 -u olivier -P xxx \
    -t 'extraflame/poele/ota/pull' \
    -m 'https://raw.githubusercontent.com/Shad107/OpenXtraflame/main/releases/v1.1.0/OpenXtraflame.bin'
```

Ou HTTP local :

```bash
curl -X POST http://192.168.X.X/ota/pull -d 'url=https://...'
```

Le module télécharge, vérifie signature (=si configurée), applique, reboot.

## Mécanisme 3 - Auto-check périodique

Configurable dans Web UI :

- URL manifest : `https://server.example/OpenXtraflame/manifest.json`
- Check interval : 24h par défaut
- Auto-apply : true / false

Format manifest :

```json
{
    "version": "1.2.0",
    "url": "https://server.example/OpenXtraflame/v1.2.0.bin",
    "sha256": "abc123...",
    "size": 1234567,
    "release_notes": "Bugfix Micronova timing"
}
```

## Signature et sécurité

### Signature RSA (=recommandé pour prod)

Générer clé RSA en dev :

```bash
openssl genrsa -out ota_signing_key.pem 3072
openssl rsa -in ota_signing_key.pem -pubout -out ota_signing_key.pub
```

Signer un firmware :

```bash
espsecure.py sign_data --version 2 --keyfile ota_signing_key.pem \
    -o OpenXtraflame_signed.bin OpenXtraflame.bin
```

Embed la clé publique dans le firmware via `sdkconfig` :

```
CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y
CONFIG_SECURE_BOOT_PUBLIC_KEY_EMBED=y
CONFIG_SECURE_BOOT_SIGNING_KEY="ota_signing_key.pem"
```

⚠️ Une fois signature activée, TOUS les futurs OTA doivent être signés avec la même clé.

### Auth token

Le endpoint `/ota/upload` peut être protégé par token :

```
POST /ota/upload
Authorization: Bearer <token>
```

Configurer le token via Web UI (=NVS).

## Fallback si nouveau firmware crashe

Le bootloader ESP-IDF détecte via watchdog si le firmware :
- Boot loop dans les 30s
- Panic
- Ne marque pas "app valid" avant timeout

Dans ce cas, bascule automatique sur l'ancien slot ota_0/ota_1.

Pour désactiver le fallback (=marquer immédiatement valide) :

```c
esp_ota_mark_app_valid_cancel_rollback();
```

## OTA du firmware POELE (=via UART)

Extraflame supporte aussi l'OTA du firmware du POELE lui-même via UART Micronova (=SerialOTA2.c reversé).

Notre OpenXtraflame peut relayer cette fonctionnalité :

```bash
# Push firmware poêle via MQTT
mosquitto_pub -t 'extraflame/poele/stove_ota/pull' \
    -m 'https://server/stove_firmware.bin'
```

Le firmware sera :
1. Téléchargé et stocké dans partition ota_stove (=192KB)
2. Transmis au poêle via UART Micronova (=protocole SerialOTA2)
3. Poêle applique et redémarre

⚠️ Fonctionnalité avancée, à ne pas confondre avec l'OTA du module Wi-Fi lui-même. Utile si Extraflame publie des nouveaux firmwares pour la carte de contrôle du poêle.

## Version tracking

Chaque firmware embed sa version via `esp_app_desc_t` :

```c
static const esp_app_desc_t app_desc = {
    .magic_word = 0xABCD5432,
    .version    = "1.0.0",
    .project_name = "OpenXtraflame",
    ...
};
```

Version visible via :
- Web UI : http://192.168.X.X/
- MQTT : `<prefix>/version`
- Log boot : `I (xxx) MAIN: Version 1.0.0`

## Test workflow OTA

1. Build v1.0.0 :
```bash
docker compose run --rm esp-idf idf.py -DTARGET=external build
cp build/OpenXtraflame.bin releases/v1.0.0.bin
```

2. Flash initial via USB :
```bash
docker compose run --rm esp-idf idf.py -p /dev/ttyUSB0 flash
```

3. Modifier code, bump version, rebuild :
```bash
# Édite CMakeLists.txt IDF_VER_APP="1.0.1"
docker compose run --rm esp-idf idf.py -DTARGET=external build
cp build/OpenXtraflame.bin releases/v1.0.1.bin
```

4. OTA depuis v1.0.0 vers v1.0.1 :
```bash
curl -X POST -F "firmware=@releases/v1.0.1.bin" \
    http://192.168.X.X/ota/upload
```

5. Vérifier version :
```bash
curl http://192.168.X.X/version
# {"version":"1.0.1", "slot":"ota_1", ...}
```

6. Rollback si problème :
```bash
curl -X POST http://192.168.X.X/ota/rollback
```

## GitHub Actions release automation

Workflow pour build + publish à chaque tag :

```yaml
# .github/workflows/release.yml
name: Release firmware
on:
  push:
    tags: ['v*']
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build with ESP-IDF
        run: |
          docker compose build
          docker compose run --rm esp-idf idf.py -DTARGET=external build
          docker compose run --rm esp-idf idf.py -DTARGET=blacklabel build
      - name: Sign
        run: espsecure.py sign_data --version 2 --keyfile ${{ secrets.SIGNING_KEY }} ...
      - name: Create release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            releases/OpenXtraflame-external.bin
            releases/OpenXtraflame-blacklabel.bin
            releases/manifest.json
```

## Roadmap OTA

- [x] Structure code (ota.c/h)
- [x] Partitions ota_0 / ota_1 configurées
- [ ] Handler POST /ota/upload dans web_ui.c
- [ ] Handler POST /ota/pull dans web_ui.c
- [ ] MQTT subscribe `<prefix>/ota/#`
- [ ] Progress via MQTT `<prefix>/ota/progress`
- [ ] Signature RSA implementation
- [ ] Auto-check manifest (=optionnel)
- [ ] GitHub Actions release automation
- [ ] Documentation utilisateur détaillée
