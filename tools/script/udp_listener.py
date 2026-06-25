import socket

def start_server(host='0.0.0.0', port=2000):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind((host, port))
        print(f"Listening for UDP packets on {host}:{port}...")

        while True:
            data, addr = s.recvfrom(1024)
            if data:
                print(f"Received ({len(data)} bytes) from {addr}: {data.hex().upper()}")

if __name__ == "__main__":
    try:
        start_server()
    except KeyboardInterrupt:
        print("\nServer stopped.")
