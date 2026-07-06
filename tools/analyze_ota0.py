#!/usr/bin/env python3
"""
Analyse le firmware ota0.bin de l'ESP32 Extraflame pour extraire :
- Segments et layout mémoire
- Références cross entre strings et fonctions
- Patterns GPIO / UART / MQTT

Usage :
    python3 analyze_ota0.py path/to/partition_ota0.bin
"""

import struct
import sys
import re
from collections import Counter

def parse_esp32_image(path):
    with open(path, 'rb') as f:
        data = f.read()

    # ESP32 image header : magic(1) segcount(1) mode(1) speed_size(1) entry(4) [+16 extended]
    magic, seg_count, spi_mode, spi_speed_size = struct.unpack('<BBBB', data[:4])
    entry = struct.unpack('<I', data[4:8])[0]
    assert magic == 0xE9, f'bad magic: 0x{magic:02x}'
    print(f'Magic          : 0x{magic:02x}')
    print(f'Segments       : {seg_count}')
    print(f'Entry point    : 0x{entry:08x}')
    print()

    off = 24  # skip 8+16
    segments = []
    for i in range(seg_count):
        vaddr, size = struct.unpack('<II', data[off:off+8])
        segments.append({
            'idx': i,
            'vaddr': vaddr,
            'size': size,
            'file_off': off + 8,
            'data': data[off+8:off+8+size],
        })
        print(f'Seg {i}: vaddr=0x{vaddr:08x} size={size:>7d} file_off=0x{off+8:x}')
        off += 8 + size
    print()
    return segments


def extract_strings_with_context(seg, min_len=6):
    """Extract printable strings from a segment with their addresses"""
    text = seg['data'].decode('latin-1', errors='ignore')
    strings = []
    current = ''
    start = 0
    for i, c in enumerate(text):
        if 32 <= ord(c) < 127:
            if not current:
                start = i
            current += c
        else:
            if len(current) >= min_len:
                addr = seg['vaddr'] + start
                strings.append((addr, current))
            current = ''
    if len(current) >= min_len:
        addr = seg['vaddr'] + start
        strings.append((addr, current))
    return strings


def find_gpio_patterns(segments):
    """Look for GPIO/UART/LED patterns in strings + rodata references"""
    print('=== GPIO/UART/LED patterns ===')
    all_strings = []
    for seg in segments:
        strings = extract_strings_with_context(seg)
        all_strings.extend(strings)

    patterns = [
        (r'led[_ ]?(power|ble|wifi|server|status)', 'LED'),
        (r'gpio[_ ]?set[_ ]?level', 'gpio_set_level'),
        (r'gpio[_ ]?config', 'gpio_config'),
        (r'uart[_ ]?set[_ ]?pin', 'uart_set_pin'),
        (r'reset[_ ]?button', 'reset_button'),
    ]
    for pat, name in patterns:
        matches = [(a, s) for a, s in all_strings if re.search(pat, s, re.IGNORECASE)]
        for addr, s in matches[:5]:
            print(f'  [{name}] 0x{addr:08x}: {s!r}')
    print()


def find_micronova_regs(segments):
    """Extract RAM_ and EPR_ Micronova register constants"""
    print('=== Micronova RAM/EPR constants ===')
    all_strings = []
    for seg in segments:
        all_strings.extend(extract_strings_with_context(seg))

    ram_matches = sorted(set(s for _, s in all_strings if re.match(r'^RAM_[A-Z0-9_]+$', s)))
    epr_matches = sorted(set(s for _, s in all_strings if re.match(r'^EPR_[A-Z0-9_]+$', s)))

    print(f'RAM_ registers found : {len(ram_matches)}')
    for r in ram_matches:
        print(f'  {r}')
    print()
    print(f'EPR_ registers found : {len(epr_matches)}')
    for r in epr_matches:
        print(f'  {r}')
    print()


def find_endpoints(segments):
    """Find HTTP endpoints + MQTT topics"""
    print('=== HTTP endpoints & MQTT topics ===')
    all_strings = []
    for seg in segments:
        all_strings.extend(extract_strings_with_context(seg))

    http_ep = sorted(set(s for _, s in all_strings
                        if re.match(r'^(GET|POST|PUT|DELETE) /', s)
                        or re.match(r'^/[a-z_]+', s) and len(s) < 40))
    for e in http_ep[:30]:
        print(f'  {e}')
    print()

    topics = sorted(set(s for _, s in all_strings
                       if re.search(r'IN/|OUT/|REPLY/', s)))
    print(f'MQTT topics : {len(topics)}')
    for t in topics[:20]:
        print(f'  {t}')
    print()


def find_urls_and_creds(segments):
    """Find URLs, hostnames, potential credentials"""
    print('=== URLs, hostnames, mentions ===')
    all_strings = []
    for seg in segments:
        all_strings.extend(extract_strings_with_context(seg))

    for pat, name in [
        (r'https?://', 'URL'),
        (r'\.extraflame\.', 'extraflame'),
        (r'\.omnyvore\.', 'omnyvore'),
        (r'mqtt', 'MQTT'),
    ]:
        matches = sorted(set(s for _, s in all_strings if re.search(pat, s, re.IGNORECASE)))
        for m in matches[:10]:
            print(f'  [{name}] {m}')
    print()


def find_hex_constants_near_ram(disasm_path):
    """Parse disassembly to find hex constants used near string references"""
    if not disasm_path:
        return
    print('=== Hex constants patterns (=from disasm) ===')
    try:
        with open(disasm_path) as f:
            lines = f.readlines()
        # Look for movi.n a, N patterns (=immediate load, common for GPIO/register values)
        movi_pattern = re.compile(r'movi(?:\.n)?\s+a\d+,\s*0?x?([0-9a-f]+)', re.IGNORECASE)
        values = Counter()
        for line in lines[:200000]:  # limit
            m = movi_pattern.search(line)
            if m:
                v = int(m.group(1), 16 if 'x' in m.group(0) else 10)
                if 0 <= v <= 255:  # likely GPIO/reg values
                    values[v] += 1
        print('Top movi constants (=potential GPIO/reg values):')
        for v, c in values.most_common(20):
            print(f'  0x{v:02x} ({v:3d}) : {c} occurrences')
    except FileNotFoundError:
        print(f'  {disasm_path} not found, skipping')
    print()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    disasm = sys.argv[2] if len(sys.argv) > 2 else None

    segments = parse_esp32_image(path)
    find_gpio_patterns(segments)
    find_micronova_regs(segments)
    find_endpoints(segments)
    find_urls_and_creds(segments)
    if disasm:
        find_hex_constants_near_ram(disasm)


if __name__ == '__main__':
    main()
