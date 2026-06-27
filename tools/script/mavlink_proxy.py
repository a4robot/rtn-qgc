#!/usr/bin/env python3
"""
MAVLink Proxy

This script acts as a bi-directional proxy between a Pixhawk connected via USB (serial)
and QGroundControl connected via UDP.

Dependencies:
    pip install pymavlink
"""

import argparse
import time
import sys
import os

# Enforce MAVLink 2 parsing and sending
os.environ["MAVLINK20"] = "1"

from pymavlink import mavutil

def main():
    parser = argparse.ArgumentParser(description="MAVLink Proxy: Forward data between USB Serial and UDP.")
    parser.add_argument("--serial", type=str, default="/dev/ttyUSB0", help="Serial port for Pixhawk (default: /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate for serial connection (default: 115200)")
    parser.add_argument("--udp-host", type=str, default="127.0.0.1", help="Target UDP host for QGC (default: 127.0.0.1)")
    parser.add_argument("--udp-port", type=int, default=15000, help="Target UDP port for QGC (default: 15000)")
    parser.add_argument("--source-system", type=int, default=255, help="MAVLink source system ID for the proxy (default: 255)")
    args = parser.parse_args()

    print(f"Starting MAVLink Proxy...")
    print(f"Connecting to Pixhawk on {args.serial} at {args.baud} baud...")

    try:
        serial_conn = mavutil.mavlink_connection(args.serial, baud=args.baud, source_system=args.source_system)
    except Exception as e:
        print(f"Failed to connect to serial port {args.serial}: {e}")
        sys.exit(1)

    udp_connection_string = f"udpout:{args.udp_host}:{args.udp_port}"
    print(f"Connecting to QGC on {udp_connection_string}...")

    try:
        udp_conn = mavutil.mavlink_connection(udp_connection_string, source_system=args.source_system)
    except Exception as e:
        print(f"Failed to create UDP connection {udp_connection_string}: {e}")
        sys.exit(1)

    print("Proxy active. Press Ctrl+C to stop.")

    messages_forwarded_to_udp = 0
    messages_forwarded_to_serial = 0
    last_print_time = time.time()

    try:
        while True:
            # Read from Serial (Pixhawk) -> Write to UDP (QGC)
            msg_serial = serial_conn.recv_msg()
            if msg_serial is not None and msg_serial.get_type() != 'BAD_DATA':
                udp_conn.write(msg_serial.get_msgbuf())
                messages_forwarded_to_udp += 1

            # Read from UDP (QGC) -> Write to Serial (Pixhawk)
            msg_udp = udp_conn.recv_msg()
            if msg_udp is not None and msg_udp.get_type() != 'BAD_DATA':
                serial_conn.write(msg_udp.get_msgbuf())
                messages_forwarded_to_serial += 1

            # Print stats every 5 seconds
            current_time = time.time()
            if current_time - last_print_time > 5.0:
                print(f"Stats: {messages_forwarded_to_udp} msgs sent to UDP, {messages_forwarded_to_serial} msgs sent to Serial")
                last_print_time = current_time

            # Yield slightly to prevent 100% CPU usage
            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\nStopping proxy...")
    finally:
        serial_conn.close()
        udp_conn.close()
        print("Connections closed.")

if __name__ == "__main__":
    main()
