#!/usr/bin/env python3
"""
Sanitise un dump firmware Extraflame avant partage/illustration.

Remplace les données personnelles par des placeholders. AUCUNE valeur
personnelle n'est codée en dur : chaque secret à masquer est fourni en
argument, pour que ce script reste publiable tel quel.

- MAC address        (--mac      AA:BB:CC:DD:EE:FF)
- secure_code        (--secure-code   8 chiffres)
- stove_model        (--stove-model   code Extraflame)
- matricola / serial (--serial)
- N'importe quel motif texte additionnel (--redact "valeur", répétable)

Exemple :
    python3 sanitize_dump.py in.bin out.bin \\
        --mac AA:BB:CC:DD:EE:FF \\
        --secure-code 12345678 \\
        --stove-model 0000000000 \\
        --serial A000000000 \\
        --redact monMotDePasseWifi
"""

import argparse


def _replace(data: bytearray, real: bytes, fake: bytes, label: str) -> bytearray:
    if not real:
        return data
    n = data.count(real)
    if n:
        data = bytearray(data.replace(real, fake))
        print(f"Replaced {label} ({n} occurrence(s))")
    else:
        print(f"{label}: not found (skipped)")
    return data


def sanitize(inp, outp, args):
    with open(inp, 'rb') as f:
        data = bytearray(f.read())

    if args.mac:
        real_mac = bytes.fromhex(args.mac.replace(':', '').replace('-', ''))
        data = _replace(data, real_mac, bytes.fromhex('deadbeef0000'), 'MAC (binary)')
        for form in (args.mac.lower(), args.mac.upper()):
            data = _replace(data, form.encode(), b'DE:AD:BE:EF:00:00', 'MAC (ascii)')

    if args.secure_code:
        data = _replace(data, args.secure_code.encode(),
                        b'X' * len(args.secure_code), 'secure_code')
    if args.stove_model:
        data = _replace(data, args.stove_model.encode(),
                        b'Y' * len(args.stove_model), 'stove_model')
    if args.serial:
        data = _replace(data, args.serial.encode(),
                        b'S' * len(args.serial), 'serial/matricola')
    for r in args.redact or []:
        data = _replace(data, r.encode(), b'<REDACTED>', f'redact:{r[:4]}...')

    with open(outp, 'wb') as f:
        f.write(data)
    print(f"\nWrote sanitized dump to {outp}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('input')
    p.add_argument('output')
    p.add_argument('--mac')
    p.add_argument('--secure-code', dest='secure_code')
    p.add_argument('--stove-model', dest='stove_model')
    p.add_argument('--serial')
    p.add_argument('--redact', action='append', help='motif texte additionnel (repetable)')
    args = p.parse_args()
    sanitize(args.input, args.output, args)


if __name__ == '__main__':
    main()
