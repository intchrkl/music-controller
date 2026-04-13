import serial
import subprocess
import time

PORT = '/dev/tty.usbmodem206EF13320E03'
BAUD = 9600

ser = serial.Serial(PORT, BAUD, timeout=1)
last_lcd_payload = None

print("Sending LCD updates...\n")


def run_osascript(script: str) -> str:
    result = subprocess.run(
        ["osascript", "-e", script],
        capture_output=True,
        text=True
    )
    return result.stdout.strip()


def get_spotify_info():
    state = run_osascript('tell application "Spotify" to get player state as text')

    if state == "stopped" or state == "":
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


def send_lcd_payload(info):
    global last_lcd_payload

    state = info.get("state", "stopped")

    if state == "stopped":
        payload = "LCD|No track yet| \n"
    else:
        track = (info["track"] or "")
        artist = (info["artist"] or "")
        payload = f"LCD|{state}|{track}|{artist}\n"

    if payload != last_lcd_payload:
        ser.write(payload.encode("utf-8"))
        print("Sent payload:", payload.strip())
        last_lcd_payload = payload


while True:
    try:
        info = get_spotify_info()
        send_lcd_payload(info)
    except Exception as e:
        print(f"LCD update error: {e}")

    time.sleep(1)