#!/usr/bin/env python3
"""
Convert an ESP32 raw firmware binary into an ELF file with proper segment layout.
Usage: python3 make_esp32_elf.py partition_ota0.bin ota0.elf
"""

import sys
import struct
from makeelf.elf import ELF, ELFCLASS, ELFDATA, EM, ET


def parse_esp32_bin(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, seg_count, _, _ = struct.unpack('<BBBB', data[:4])
    entry = struct.unpack('<I', data[4:8])[0]
    assert magic == 0xE9, f'bad magic: 0x{magic:02x}'
    off = 24  # skip header
    segments = []
    for _ in range(seg_count):
        vaddr, size = struct.unpack('<II', data[off:off+8])
        segments.append({'vaddr': vaddr, 'size': size, 'data': data[off+8:off+8+size]})
        off += 8 + size
    return entry, segments


def name_for_segment(vaddr):
    """Return conventional ESP32 section name for a virtual address."""
    if 0x3F400000 <= vaddr <= 0x3F800000:
        return '.rodata'
    if 0x3FFA0000 <= vaddr <= 0x40000000:
        return '.dram'
    if 0x40080000 <= vaddr <= 0x400A0000:
        return '.iram'
    if 0x400D0000 <= vaddr <= 0x40400000:
        return '.text'
    if 0x50000000 <= vaddr <= 0x50002000:
        return '.rtc_slow'
    return f'.seg_{vaddr:08x}'


def main():
    if len(sys.argv) < 3:
        print('Usage: python3 make_esp32_elf.py input.bin output.elf')
        sys.exit(1)
    inp, out = sys.argv[1], sys.argv[2]

    entry, segments = parse_esp32_bin(inp)
    print(f'Entry point: 0x{entry:08x}')
    print(f'Segments: {len(segments)}')

    # Create ELF for Xtensa little-endian
    elf = ELF(e_class=ELFCLASS.ELFCLASS32,
              e_data=ELFDATA.ELFDATA2LSB,
              e_machine=EM.EM_XTENSA,
              e_type=ET.ET_EXEC)
    elf.Elf.Ehdr.e_entry = entry

    for seg in segments:
        name = name_for_segment(seg['vaddr'])
        # Skip empty or rtc mem
        if seg['size'] == 0:
            continue
        print(f'  Adding {name:16s} vaddr=0x{seg["vaddr"]:08x} size={seg["size"]}')
        section_idx = elf.append_section(name, bytes(seg['data']), seg['vaddr'])
        # Also add a PROGBITS load segment
        try:
            elf.append_segment(section_idx, addr=seg['vaddr'], mem_size=seg['size'])
        except Exception as e:
            print(f'    warn: {e}')

    with open(out, 'wb') as f:
        f.write(bytes(elf))
    print(f'Wrote {out}')


if __name__ == '__main__':
    main()
