import serial
from datetime import datetime
from pynput.keyboard import Controller, Key
import subprocess
import time
import threading
import queue

keyboard = Controller()

debug = False

PORT = '/dev/tty.usbmodem206EF13320E02'
BAUD = 9600

ser = serial.Serial(PORT, BAUD, timeout=1)

print("Listening to Serial Monitor...\n")
if debug:
    print("DEBUG mode on")

# Queue for background metadata jobs
track_info_queue = queue.Queue()

def run_osascript(script: str) -> str:
    result = subprocess.run(
        ["osascript", "-e", script],
        capture_output=True,
        text=True
    )
    return result.stdout.strip()

def set_system_volume(vol: int):
    vol = max(0, min(100, vol))
    subprocess.run(
        ["osascript", "-e", f"set volume output volume {vol}"],
        capture_output=True,
        text=True
    )

def get_spotify_info():
    state = run_osascript('tell application "Spotify" to get player state as text')

    if state == "stopped":
        return {
            "state": "stopped",
            "track": None,
            "artist": None,
            "album": None,
            "position": None,
            "duration": None
        }

    track = run_osascript('tell application "Spotify" to get name of current track')
    artist = run_osascript('tell application "Spotify" to get artist of current track')
    album = run_osascript('tell application "Spotify" to get album of current track')
    position = run_osascript('tell application "Spotify" to get player position')
    duration = run_osascript('tell application "Spotify" to get duration of current track')

    return {
        "state": state,
        "track": track,
        "artist": artist,
        "album": album,
        "position": position,
        "duration": duration
    }

def format_time(seconds: float) -> str:
    total_seconds = int(seconds)
    minutes = total_seconds // 60
    secs = total_seconds % 60
    return f"{minutes}:{secs:02d}"

def print_current_track():
    info = get_spotify_info()

    if info["state"] == "stopped":
        print("Spotify is stopped")
        return

    position_sec = float(info["position"])
    duration_sec = int(info["duration"]) / 1000

    print(
        f"Current Track: {info['track']}\n"
        f"Artist: {info['artist']}\n"
        f"Album: {info['album']}\n"
        f"Length: {format_time(position_sec)} / {format_time(duration_sec)}\n"
    )

def track_info_worker():
    while True:
        command = track_info_queue.get()
        try:
            # small delay so Spotify has time to update after next/prev/playpause
            # time.sleep(0.2)
            print_current_track()
        except Exception as e:
            print(f"Track info error: {e}")
        finally:
            track_info_queue.task_done()

# Start background worker thread
worker = threading.Thread(target=track_info_worker, daemon=True)
worker.start()

while True:
    line = ser.readline().decode('utf-8').strip()

    if line:
        now = datetime.now()
        timestamp = now.strftime("%Y-%m-%d %H:%M:%S")
        print(f"{timestamp} Command from Arduino: {line}")

        # track_info = get_spotify_info()
        # lcd_payload = f"LCD|{track_info['track']}|{track_info['artist']}\n"
        # ser.write(lcd_payload.encode("utf-8"))

        if line.startswith("BTN"):
            if "PLAYPAUSE" in line:
                print("Received PlayPause")
                if not debug:
                    keyboard.tap(Key.media_play_pause)
                print("pynput: media_play_pause")
                track_info_queue.put("PLAYPAUSE")

            if "NEXT" in line:
                print("Received Next")
                if not debug:
                    keyboard.tap(Key.media_next)
                print("pynput: media_next")
                track_info_queue.put("NEXT")

            if "PREV" in line:
                print("Received Prev")
                if not debug:
                    keyboard.tap(Key.media_previous)
                print("pynput: media_previous")
                track_info_queue.put("PREV")

            if "MUTE" in line:
                print("Received Mute")
                if not debug:
                    keyboard.tap(Key.media_volume_mute)
                print("pynput: media_volume_mute")
        elif line.startswith("VOL"):
            _, vol_str = line.split("|", 1)
            vol = int(vol_str)
            if not debug: 
                set_system_volume(vol)
            print(f"Set volume to {vol}")

        print()