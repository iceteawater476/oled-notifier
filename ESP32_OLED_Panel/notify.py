#!/usr/bin/env python3
"""
notify.py - send a notification to the ESP32 OLED panel from a PC.

Works standalone from the command line (Windows, macOS, or Linux), and is
also imported by the automatic relay scripts in this folder.

Command-line usage:
    python notify.py --device 192.168.1.42 --app "Terminal" \
        --title "Build finished" --message "0 errors, 2 warnings" \
        --file "build.log" --duration 8000

Only --device is required; everything else is optional. This is the
building block for wiring up *anything* that can run a command when
something happens: a completed download, a finished script, a cron job, a
git hook, a CI pipeline step, an Automator/Shortcuts action, etc. -- it
doesn't have to be tied to a real "notification" at all.
"""
import argparse
import urllib.request
import urllib.parse


def send_notification(device_ip, app="", title="", message="", file="", duration=6000):
    """Send one notification to the device. Returns True on success."""
    params = urllib.parse.urlencode({
        "app": app,
        "title": title,
        "message": message,
        "file": file,
        "duration": duration,
    })
    url = f"http://{device_ip}/notify?{params}"
    try:
        with urllib.request.urlopen(url, timeout=5) as resp:
            return resp.status == 200
    except Exception as e:
        print(f"[notify.py] Failed to reach device at {device_ip}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Send a notification to the ESP32 OLED panel")
    parser.add_argument("--device", required=True, help="Device IP address, e.g. 192.168.1.42")
    parser.add_argument("--app", default="", help="Source app / label, e.g. 'WhatsApp', 'Terminal'")
    parser.add_argument("--title", default="", help="Notification title")
    parser.add_argument("--message", default="", help="Notification body text")
    parser.add_argument("--file", default="", help="Attached file name, if any (name only, not the file itself)")
    parser.add_argument("--duration", type=int, default=6000, help="How long to show it, in ms (1000-30000)")
    args = parser.parse_args()

    ok = send_notification(args.device, args.app, args.title, args.message, args.file, args.duration)
    print("Sent." if ok else "Failed to send.")


if __name__ == "__main__":
    main()
