import serial
import time
import random
import argparse

def encode_6bit_ascii(bit_string):
    # pad to multiple of 6
    if len(bit_string) % 6 != 0:
        bit_string += '0' * (6 - (len(bit_string) % 6))
    
    chars = []
    for i in range(0, len(bit_string), 6):
        val = int(bit_string[i:i+6], 2)
        if val < 40:
            chars.append(chr(val + 48))
        else:
            chars.append(chr(val + 56))
    return "".join(chars)

def calculate_checksum(sentence):
    checksum = 0
    for char in sentence:
        checksum ^= ord(char)
    return f"{checksum:02X}"

def generate_ais_type_1(mmsi, lat, lon, sog, cog, heading):
    """
    Generates an AIS Type 1 Message (Position Report)
    """
    msg_type = f"{1:06b}"
    repeat_ind = f"{0:02b}"
    mmsi_bin = f"{mmsi:030b}"
    nav_status = f"{15:04b}" # 15 = not defined
    rot = f"{0:08b}" # +0
    
    sog_knots = int(sog * 10)
    sog_bin = f"{sog_knots:010b}"
    
    pos_acc = f"{0:01b}"
    
    # Longitude in 1/10000 minute
    lon_min = int(lon * 600000)
    if lon_min < 0:
        lon_min = (1 << 28) + lon_min
    lon_bin = f"{lon_min:028b}"
    
    # Latitude in 1/10000 minute
    lat_min = int(lat * 600000)
    if lat_min < 0:
        lat_min = (1 << 27) + lat_min
    lat_bin = f"{lat_min:027b}"
    
    cog_val = int(cog * 10)
    cog_bin = f"{cog_val:012b}"
    
    true_heading = f"{heading:09b}"
    
    timestamp = f"{60:06b}" # 60 = not available
    
    maneuver = f"{0:02b}"
    spare = f"{0:03b}"
    raim = f"{0:01b}"
    radio = f"{0:019b}"
    
    payload = msg_type + repeat_ind + mmsi_bin + nav_status + rot + sog_bin + pos_acc + lon_bin + lat_bin + cog_bin + true_heading + timestamp + maneuver + spare + raim + radio
    
    payload_ascii = encode_6bit_ascii(payload)
    
    # NMEA format: !AIVDM,1,1,,A,<payload>,0*<checksum>
    sentence = f"AIVDM,1,1,,A,{payload_ascii},0"
    checksum = calculate_checksum(sentence)
    
    return f"!{sentence}*{checksum}"

class Vessel:
    def __init__(self, mmsi, lat, lon, heading):
        self.mmsi = mmsi
        self.lat = lat
        self.lon = lon
        self.heading = heading
        self.speed = random.uniform(2.0, 8.0) # knots

    def update(self):
        # Move slightly
        self.lat += random.uniform(-0.0001, 0.0001)
        self.lon += random.uniform(-0.0001, 0.0001)
        self.heading = int((self.heading + random.uniform(-5, 5)) % 360)
        self.speed = max(0.0, self.speed + random.uniform(-0.5, 0.5))

def main():
    parser = argparse.ArgumentParser(description="Mock AIS Data in Chao Phraya River")
    parser.add_argument('--port', type=str, required=True, help='Serial port to output data (e.g. /dev/ttyUSB0 or COM3)')
    parser.add_argument('--baudrate', type=int, default=38400, help='Serial baudrate (e.g. 38400, 115200)')
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=1)
    except Exception as e:
        print(f"Failed to open serial port {args.port}: {e}")
        return
    
    # Chao Phraya rough bounding area
    # Icon Siam area roughly 13.726, 100.510
    lat_center = 13.726
    lon_center = 100.510

    # Hardcoded 10 vessels, 1 Hz update rate
    count = 10
    hz = 1.0

    vessels = []
    for i in range(count):
        # Generate MMSI like 567000000 (Thailand MID is 567)
        mmsi = 567000000 + random.randint(1000, 9999)
        v_lat = lat_center + random.uniform(-0.02, 0.02)
        v_lon = lon_center + random.uniform(-0.01, 0.01)
        heading = random.randint(0, 359)
        vessels.append(Vessel(mmsi, v_lat, v_lon, heading))

    print(f"Starting AIS Mockup for {count} vessels in Chao Phraya River...")
    print(f"Sending to serial port {args.port} at {args.baudrate} baud...")

    try:
        while True:
            for v in vessels:
                v.update()
                aivdm_msg = generate_ais_type_1(
                    mmsi=v.mmsi,
                    lat=v.lat,
                    lon=v.lon,
                    sog=v.speed,
                    cog=v.heading,
                    heading=v.heading
                )
                ser.write((aivdm_msg + '\r\n').encode('utf-8'))
                # Optional: print(f"Sent: {aivdm_msg}")
            
            time.sleep(1.0 / hz)
    except KeyboardInterrupt:
        print("\nMockup stopped.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
