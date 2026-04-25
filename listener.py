import serial
import subprocess
import time
from serial.tools import list_ports

BAUD = 115200
RECONNECT_DELAY = 2.0
POLL_INTERVAL = 1.0

# Put your most likely ports first
PREFERRED_PORTS = [
    "/dev/tty.usbmodemECDA3B600B7C3",   # RX
    "/dev/tty.usbmodem3C8427C3FDF03",   # TX
    "/dev/cu.usbmodemECDA3B600B7C3",
    "/dev/cu.usbmodem3C8427C3FDF03",
]

last_lcd_payload = None
last_vol_payload = None
ser = None


def run_osascript(script: str) -> str:
    result = subprocess.run(
        ["osascript", "-e", script],
        capture_output=True,
        text=True
    )
    return result.stdout.strip()


def get_spotify_info():
    state = run_osascript('tell application "Spotify" to get player state as text')

    if state == "" or state == "stopped":
        return {
            "state": "stopped",
            "track": None,
            "artist": None,
        }

    track = run_osascript('tell application "Spotify" to get name of current track')
    artist = run_osascript('tell application "Spotify" to get artist of current track')

    return {
        "state": state,
        "track": track,
        "artist": artist,
    }


def get_system_volume():
    out = run_osascript('output volume of (get volume settings)')
    try:
        vol = int(out)
    except ValueError:
        vol = 0

    return max(0, min(100, vol))


def clean_field(text: str) -> str:
    return (text or "").replace("\n", " ").replace("|", "/")


def build_lcd_payload(info):
    state = info.get("state", "stopped")

    if state == "stopped":
        return "LCD|No track yet| \n"

    track = clean_field(info.get("track"))
    artist = clean_field(info.get("artist"))
    return f"LCD|{track}|{artist}\n"


def build_vol_payload(vol: int):
    return f"VOL|{vol}\n"

# Returns a list of possible serial ports to try
def candidate_ports():
    found = [p.device for p in list_ports.comports()]

    ordered = []
    seen = set()

    for port in PREFERRED_PORTS + found:
        if port not in seen:
            seen.add(port)
            ordered.append(port)

    ordered.sort(key=lambda p: (
        0 if "usbmodem" in p else 1,
        0 if p.startswith("/dev/cu.") else 1,
        p
    ))

    return ordered

# Tries to open a specific serial port
def try_open_port(port: str):
    try:
        s = serial.Serial(port, BAUD, timeout=1, write_timeout=1)
        time.sleep(2.0)  # give board time to reset after opening serial
        print(f"Connected to {port}")
        return s
    except Exception as e:
        print(f"Could not open {port}: {e}")
        return None

# Attempts to connect to any available serial port
def connect_serial():
    ports = candidate_ports()
    print("Trying ports:")
    for p in ports:
        print(" ", p)

    for port in ports:
        s = try_open_port(port)
        if s is not None:
            return s

    raise RuntimeError("Could not connect to TX or RX on any serial port.")


def ensure_connected():
    global ser
    if ser is not None and ser.is_open:
        return

    ser = connect_serial()


def safe_write(payload: str):
    global ser
    try:
        ensure_connected()
        ser.write(payload.encode("utf-8"))
        ser.flush()
    except Exception as e:
        print(f"Serial write failed: {e}")
        try:
            if ser is not None:
                ser.close()
        except Exception:
            pass
        ser = None
        raise


def send_lcd_payload(info):
    global last_lcd_payload
    payload = build_lcd_payload(info)

    if payload != last_lcd_payload:
        safe_write(payload)
        print("Sent LCD:", payload.strip())
        last_lcd_payload = payload


def send_volume_payload(vol):
    global last_vol_payload
    payload = build_vol_payload(vol)

    if payload != last_vol_payload:
        safe_write(payload)
        print("Sent VOL:", payload.strip())
        last_vol_payload = payload


def main():
    global ser

    while True:
        try:
            ensure_connected()

            info = get_spotify_info()
            vol = get_system_volume()

            send_lcd_payload(info)
            send_volume_payload(vol)

            time.sleep(POLL_INTERVAL)

        except KeyboardInterrupt:
            print("\nStopping.")
            break

        except Exception as e:
            print(f"Update loop error: {e}")
            try:
                if ser is not None:
                    ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(RECONNECT_DELAY)


if __name__ == "__main__":
    main()