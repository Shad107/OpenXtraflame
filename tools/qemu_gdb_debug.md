# QEMU + GDB debug du firmware Extraflame

Setup pour extraction dynamique des valeurs GPIO/UART via debugging live.

## Prérequis

- Docker ESP-IDF v5.2.2 (=contient QEMU-Xtensa + xtensa-esp-elf-gdb)
- Dump firmware Extraflame `/home/user/Downloads/extraflame_dump.bin`
- Adresses fonctions ESP-IDF découvertes (=voir `extraflame_symbols.txt`)

## Approche

QEMU-Xtensa lance le firmware ESP32 en émulation. GDB attach permet de :
- Set breakpoints sur les fonctions ESP-IDF connues
- Lire registres à chaque hit
- Extraire les args réels (=GPIO nums pour LEDs, UART pins)

## Script principal

```bash
docker run --rm --name qemu-debug -v /home/user/Downloads:/w espressif/idf:v5.2.2 bash -c '
    . $IDF_PATH/export.sh > /dev/null 2>&1
    
    # Start QEMU avec GDB server + serial vers fichier
    qemu-system-xtensa \
        -serial file:/tmp/qemu.log \
        -machine esp32 \
        -drive file=/w/extraflame_dump.bin,if=mtd,format=raw \
        -global driver=timer.esp32.timg,property=wdt_disable,value=true \
        -gdb tcp::3333 -S -display none &
    QEMU_PID=$!
    sleep 3
    
    # Script GDB avec breakpoints sur les fonctions découvertes
    cat > /tmp/gdb.gdb << "GDBEOF"
target remote localhost:3333
set pagination off

# uart_set_pin - Extraflame wrapper à 0x400d8740
break *0x400d8740
commands
 silent
 printf "UART_SET_PIN: uart=%d tx=%d rx=%d rts=%d cts=%d\n", \
        ($a2 & 0xff), ($a3 & 0xff), ($a4 & 0xff), ($a5 & 0xff), ($a6 & 0xff)
 continue
end

# gpio_set_level - à 0x400d98f4
break *0x400d98f4
commands
 silent
 printf "GPIO_SET_LEVEL: gpio=%d level=%d\n", ($a2 & 0xff), ($a3 & 1)
 continue
end

# uart_param_config - à 0x400d88f4
break *0x400d88f4
commands
 silent
 printf "UART_PARAM_CONFIG: uart=%d cfg_ptr=0x%x\n", ($a2 & 0xff), $a3
 continue
end

c
GDBEOF
    
    # Attach GDB et laisser tourner 120s
    timeout 120 xtensa-esp-elf-gdb --batch -x /tmp/gdb.gdb > /tmp/gdb_out.txt 2>&1
    kill $QEMU_PID
    
    grep -E "UART_|GPIO_" /tmp/gdb_out.txt
'
```

## Ce qu'on capture

Les hits observés lors du premier test :

```
### gpio_set_level: gpio=0 level=... (=multiples calls)
```

**gpio=0** confirmé : LED POWER ou similaire est sur GPIO 0 (=cohérent avec design ESP32).

L'attribut "level" contient des valeurs bizarres car GDB lit les registres AVANT
l'`entry a1, N` qui rotate le window. Utiliser `& 0x1` pour extraire le vrai bit level.

## Weekend refinement

1. **Fix window rotation** : break APRÈS entry instruction
   ```
   break *0x400d98f6  # entry + 2 (=inside function frame)
   ```

2. **Capture SerialOta2 UART init** : cette fonction est appelée après 20s
   Augmenter timeout à 180s pour voir toutes les init UART.

3. **Trace CPU instructions** : `-d in_asm,cpu -D /tmp/trace.log`
   45M lignes générées en 30s. Filter par plage d'addresses spécifiques.

4. **Snapshot QEMU state** : `-monitor stdio`
   Permet de dumper la RAM, registres, à volonté sans breakpoints.

## Résultat attendu

Après refinement, on devrait avoir :

```
UART_SET_PIN: uart=1 tx=17 rx=16 rts=-1 cts=-1
UART_PARAM_CONFIG: uart=1 cfg_ptr=0x3ffb...
GPIO_SET_LEVEL: gpio=X level=1  # LED POWER
GPIO_SET_LEVEL: gpio=Y level=1  # LED WIFI
...
```

Ce qui donnerait directement les valeurs à mettre dans `hardware_config.h` :

```c
#define LED_POWER_PIN     GPIO_NUM_X
#define LED_WIFI_PIN      GPIO_NUM_Y
#define STOVE_UART_TX_PIN GPIO_NUM_17
#define STOVE_UART_RX_PIN GPIO_NUM_16
```

## État actuel (2026-07-03)

- ✅ QEMU boot du dump complet fonctionne
- ✅ GDB attach fonctionne (=breakpoints hit)
- ✅ Adresses ESP-IDF fonctions connues (=6 fonctions)
- ⚠️ Args lecture nécessite fix window rotation
- ⚠️ SerialOta2 UART pas encore capturée (=timing)

Weekend GUI Ghidra + GDB QEMU = combo qui débloquera tout.
