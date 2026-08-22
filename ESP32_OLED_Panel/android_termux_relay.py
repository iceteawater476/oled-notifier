#!/usr/bin/env python3
"""
android_termux_relay.py - forwards Android notifications to the ESP32 OLED
panel, running directly on the phone inside Termux -- no Tasker/MacroDroid
needed.

HOW IT WORKS
Unlike the Linux/Windows relays, this doesn't run on a PC -- Android
notifications live on the phone, so something has to poll them there.
Termux:API's `termux-notification-list` command returns the phone's
current notifications as JSON (package name, title, content text). This
script polls it periodically, tracks which notification IDs it's already
forwarded, and sends new ones to the device.

Why this instead of Tasker/MacroDroid: it needs the same underlying
Android "Notification Access" permission (there's no way around that on
Android, whatever app you use), but Termux:API is a small, single-purpose,
fully open-source app with no other feature surface -- unlike a general
automation app that can also do many other things once granted broad
permissions.

SETUP (all on the phone)
1. Install "Termux" and "Termux:API" -- from F-Droid, not the Play Store
   (the Play Store builds are outdated/unmaintained):
       https://f-droid.org/packages/com.termux/
       https://f-droid.org/packages/com.termux.api/
2. Open Termux and run:
       pkg update && pkg install python termux-api
3. Grant Termux:API "Notification access" when prompted (Settings ->
   Apps -> Special access -> Notification access -> Termux:API -> Allow).
4. Copy this file and notify.py onto the phone (e.g. via Termux's own
   `termux-setup-storage` + copying into ~/storage, or `pkg install git`
   and cloning wherever you're keeping this project).
5. Run:
       python android_termux_relay.py --device 192.168.1.42

Keep Termux running in the background (Android may kill it under
aggressive battery optimization -- exclude Termux from battery
optimization in Android's Settings > Apps > Termux > Battery, if you want
this running continuously).

Optional filters (matched against the notification's package name, e.g.
"com.whatsapp", so partial matches like "whatsapp" work fine):
    --only-apps "whatsapp,telegram"
    --ignore-apps "spotify"
"""
import argparse
import json
import subprocess
import sys
import time

from notify import send_notification


def get_notifications():
    try:
        result = subprocess.run(
            ["termux-notification-list"], capture_output=True, text=True, timeout=10
        )
        return json.loads(result.stdout) if result.stdout.strip() else []
    except FileNotFoundError:
        print(
            "termux-notification-list not found. Run: pkg install termux-api "
            "(and make sure the Termux:API app is installed too)",
            file=sys.stderr,
        )
        sys.exit(1)
    except Exception as e:
        print(f"Failed to read notifications: {e}")
        return []


def app_label(package_name):
    """Turn 'com.whatsapp' into something more readable, 'Whatsapp'."""
    if not package_name:
        return "Android"
    parts = [p for p in package_name.split(".") if p]
    return parts[-1].capitalize() if parts else package_name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", required=True, help="ESP32 device IP address")
    parser.add_argument("--duration", type=int, default=6000)
    parser.add_argument("--poll-seconds", type=float, default=2.0)
    parser.add_argument("--only-apps", default="", help="Comma-separated package-name fragments to forward (default: all)")
    parser.add_argument("--ignore-apps", default="", help="Comma-separated package-name fragments to skip")
    args = parser.parse_args()

    only = [a.strip().lower() for a in args.only_apps.split(",") if a.strip()]
    ignore = [a.strip().lower() for a in args.ignore_apps.split(",") if a.strip()]

    def should_forward(pkg):
        p = (pkg or "").lower()
        if only and not any(o in p for o in only):
            return False
        if any(i in p for i in ignore):
            return False
        return True

    seen_ids = set()
    print(f"Watching Android notifications, forwarding to {args.device} ... Ctrl+C to stop.")
    try:
        while True:
            for n in get_notifications():
                nid = n.get("id")
                if nid in seen_ids:
                    continue
                seen_ids.add(nid)

                pkg = n.get("packageName", "")
                if not should_forward(pkg):
                    continue

                title = n.get("title", "") or ""
                content = n.get("content", "") or ""
                app = app_label(pkg)
                print(f"[{app}] {title}: {content}")
                send_notification(args.device, app=app, title=title, message=content, duration=args.duration)

            if len(seen_ids) > 500:
                seen_ids.clear()
            time.sleep(args.poll_seconds)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
