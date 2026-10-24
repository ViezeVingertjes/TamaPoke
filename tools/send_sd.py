#!/usr/bin/env python3
"""Sends the packed sprites to the board's SD over USB.

  python3 tools/send_sd.py                  # sends tools/sdcard/mons/*.bin
  python3 tools/send_sd.py --port /dev/cu.usbmodem101   # or /dev/ttyACM0
  python3 tools/send_sd.py --ls             # lists what is on the SD
"""
import argparse
import glob
import os
import sys
import time
import serial

def find_port():
    ports = glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*')  # macOS, Linux
    if not ports:
        sys.exit("no encuentro la placa (/dev/cu.usbmodem* o /dev/ttyACM*)")
    return ports[0]

def wait_line(ser, expect, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        line = ser.readline().decode(errors='replace').strip()
        if not line:
            continue
        print(f"  placa: {line}")
        if line == expect:
            return True
        if line == 'ERR':
            return False
    return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port')
    ap.add_argument('--ls', action='store_true')
    args = ap.parse_args()

    port = args.port or find_port()
    print(f"puerto {port}")
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(1.5)
    ser.reset_input_buffer()

    if args.ls:
        ser.write(b'LS\n')
        wait_line(ser, 'DONE', 5)
        return

    files = sorted(glob.glob(os.path.join(os.path.dirname(__file__), 'sdcard', 'mons', '*.bin')))
    if not files:
        sys.exit("no hay .bin; ejecuta antes tools/pack_pmd.py")

    for path in files:
        size = os.path.getsize(path)
        name = f"mons/{os.path.basename(path)}"
        print(f"-> {name} ({size/1024:.0f} KB)")
        ser.write(f"PUT {name} {size}\n".encode())
        if not wait_line(ser, 'OK', 5):
            print("   la placa no acepto el PUT, sigo con el siguiente")
            continue
        t0 = time.time()
        ok = True
        with open(path, 'rb') as f:
            while chunk := f.read(2048):
                ser.write(chunk)
                # wait for the block's '#' ack
                ack = ''
                while ack not in ('#', 'ERR'):
                    ack = ser.readline().decode(errors='replace').strip()
                    if ack == '':
                        ok = False
                        break
                if not ok or ack == 'ERR':
                    ok = False
                    break
        if ok and wait_line(ser, 'DONE', 30):
            kbs = size / 1024 / max(0.01, time.time() - t0)
            print(f"   ok ({kbs:.0f} KB/s)")
        else:
            print("   fallo la transferencia")
    print("listo")

if __name__ == '__main__':
    main()
