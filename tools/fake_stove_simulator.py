#!/usr/bin/env python3
"""
Simule un poêle Extraflame en tant que master Micronova.

Utilisation :
  1. Flash openextraflame sur ESP32 spare (Target External)
  2. Connect USB-UART à GPIO 16 (=RX ESP32) et GPIO 17 (=TX ESP32)
  3. Lance ce script pour envoyer des commands read/write
  4. Observe les réponses du module

Ou avec QEMU :
  1. Lance QEMU avec pipe UART
  2. Connect ce script au pipe
  3. Voit la communication complète

Protocole Micronova (=à valider) :
  Read  : master envoie [0x00 addr], slave répond [value ~value]
  Write : master envoie [0x80|addr val ~val], slave répond [val ~val]
"""

import serial
import time
import argparse
import struct


# Registres Micronova (=de openextraflame micronova.h)
RAM_STOVE_STATUS = 0x21
RAM_TAMB         = 0x30
RAM_TH20         = 0x31
RAM_T_FUMI       = 0x32
RAM_POT_REALE    = 0x24
RAM_ACCENDI      = 0x50
RAM_SPEGNI       = 0x51


class MicronovaMaster:
    def __init__(self, port, baud=38400):
        self.ser = serial.Serial(port, baud, timeout=0.5,
                                 bytesize=8, parity='N', stopbits=1)
        print(f"[Master] Connected {port} @ {baud} 8N1")

    def read_ram(self, addr):
        """Send read request, expect [value, ~value] response"""
        self.ser.reset_input_buffer()
        req = bytes([0x00, addr])
        self.ser.write(req)
        self.ser.flush()
        resp = self.ser.read(2)
        if len(resp) != 2:
            print(f"[Master] READ 0x{addr:02x} : TIMEOUT")
            return None
        value, chk = resp[0], resp[1]
        expected_chk = (~value) & 0xff
        if chk != expected_chk:
            print(f"[Master] READ 0x{addr:02x} : BAD CHECKSUM ({value:02x} {chk:02x})")
        else:
            print(f"[Master] READ 0x{addr:02x} = 0x{value:02x} ({value})")
        return value

    def write_ram(self, addr, value):
        """Send write request, expect ACK [value, ~value]"""
        self.ser.reset_input_buffer()
        req = bytes([0x80 | addr, value, (~value) & 0xff])
        self.ser.write(req)
        self.ser.flush()
        resp = self.ser.read(2)
        if len(resp) != 2:
            print(f"[Master] WRITE 0x{addr:02x}=0x{value:02x} : TIMEOUT")
            return False
        if resp[0] != value or resp[1] != (~value) & 0xff:
            print(f"[Master] WRITE 0x{addr:02x}=0x{value:02x} : BAD ACK {resp.hex()}")
            return False
        print(f"[Master] WRITE 0x{addr:02x}=0x{value:02x} : ACK OK")
        return True

    def poll_loop(self, interval=1.0):
        """Loop periodically polling key registers"""
        print("\n=== Poll loop starting (Ctrl+C to stop) ===\n")
        try:
            while True:
                self.read_ram(RAM_STOVE_STATUS)
                self.read_ram(RAM_TAMB)
                self.read_ram(RAM_T_FUMI)
                self.read_ram(RAM_POT_REALE)
                time.sleep(interval)
        except KeyboardInterrupt:
            print("\n=== Stopped ===")

    def test_write(self, addr, value):
        """Test single write"""
        return self.write_ram(addr, value)

    def close(self):
        self.ser.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True, help='UART port (e.g. /dev/ttyUSB0)')
    p.add_argument('--baud', type=int, default=38400)
    p.add_argument('--mode', choices=['poll', 'read', 'write', 'test'], default='poll')
    p.add_argument('--addr', type=lambda x: int(x, 0), default=RAM_TAMB)
    p.add_argument('--value', type=lambda x: int(x, 0), default=0)
    p.add_argument('--interval', type=float, default=1.0)
    args = p.parse_args()

    m = MicronovaMaster(args.port, args.baud)
    try:
        if args.mode == 'poll':
            m.poll_loop(args.interval)
        elif args.mode == 'read':
            m.read_ram(args.addr)
        elif args.mode == 'write':
            m.write_ram(args.addr, args.value)
        elif args.mode == 'test':
            print("\n=== Test suite ===")
            m.read_ram(RAM_STOVE_STATUS)
            m.read_ram(RAM_TAMB)
            m.write_ram(RAM_TAMB, 22)
            m.read_ram(RAM_TAMB)
            m.write_ram(RAM_ACCENDI, 1)
            m.read_ram(RAM_STOVE_STATUS)
    finally:
        m.close()


if __name__ == '__main__':
    main()
