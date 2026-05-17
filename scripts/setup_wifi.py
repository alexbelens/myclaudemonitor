#!/usr/bin/env python3
"""
setup_wifi.py — One-time WiFi credentials setup for Claude Monitor CYD.

Sends {"wifi_config":{"ssid":"...","pass":"..."}} via serial.
The CYD saves credentials to NVS flash and reboots.
After reboot it connects automatically and serves HTTP on claude-monitor.local

Usage:
    python3 scripts/setup_wifi.py --port /dev/cu.usbserial-11340 --ssid MyNet --password mypass
"""

import argparse
import json
import sys
import time
import serial


def main():
    parser = argparse.ArgumentParser(description="Configure WiFi on Claude Monitor CYD")
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/cu.usbserial-11340)")
    parser.add_argument("--ssid", required=True, help="WiFi network name")
    parser.add_argument("--password", default="", help="WiFi password (empty for open networks)")
    args = parser.parse_args()

    payload = json.dumps({"wifi_config": {"ssid": args.ssid, "pass": args.password}},
                         separators=(",", ":")) + "\n"

    print(f"Connecting to {args.port}...")
    try:
        ser = serial.Serial(args.port, 115200, timeout=3)
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)

    time.sleep(1)
    print(f"Sending WiFi config for '{args.ssid}'...")
    ser.write(payload.encode("utf-8"))

    print("Waiting for CYD response...")
    deadline = time.time() + 8
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if line:
            print(f"  CYD: {line}")
        if "Saved" in line or "Rebooting" in line:
            print("\nDone! CYD is rebooting and will connect to WiFi.")
            print("After ~5 seconds run: python3 scripts/bridge_wifi.py --plan pro")
            break
    else:
        print("\nNo confirmation received. Check the port and try again.")

    ser.close()


if __name__ == "__main__":
    main()
