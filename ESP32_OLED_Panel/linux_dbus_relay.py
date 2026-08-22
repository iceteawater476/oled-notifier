#!/usr/bin/env python3
"""
linux_dbus_relay.py - forwards every Linux desktop notification to the
ESP32 OLED panel automatically, in real time.

HOW IT WORKS
Every Linux desktop notification (from any app -- Thunderbird, Slack,
Discord, apt/dnf update popups, etc.) is delivered by calling the method
org.freedesktop.Notifications.Notify over your session D-Bus. This script
"eavesdrops" that one method call on the bus and forwards the app name,
summary (title), and body text to the device. This is the same mechanism
tools like dbus-monitor use, and it reliably captures essentially all
desktop notifications regardless of which notification daemon you use
(GNOME Shell, dunst, mako, xfce4-notifyd, etc.) -- no accessibility-style
permission prompt needed, since you're just observing your own session bus.

REQUIREMENTS (Debian/Ubuntu example -- adjust for your distro)
    sudo apt install python3-dbus python3-gi

RUN
    python3 linux_dbus_relay.py --device 192.168.1.42

Optional filters:
    --only-apps "Slack,Thunderbird"   only forward these apps (comma-separated, case-insensitive)
    --ignore-apps "spotify"           forward everything except these apps
    --duration 8000                   how long each shows on the OLED, in ms
"""
import argparse
import sys

try:
    import dbus
    from dbus.mainloop.glib import DBusGMainLoop
    from gi.repository import GLib
except ImportError:
    print("Missing dependencies. On Debian/Ubuntu run:\n"
          "  sudo apt install python3-dbus python3-gi", file=sys.stderr)
    sys.exit(1)

from notify import send_notification


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", required=True, help="ESP32 device IP address")
    parser.add_argument("--duration", type=int, default=6000)
    parser.add_argument("--only-apps", default="", help="Comma-separated app names to forward (default: all)")
    parser.add_argument("--ignore-apps", default="", help="Comma-separated app names to skip")
    args = parser.parse_args()

    only = {a.strip().lower() for a in args.only_apps.split(",") if a.strip()}
    ignore = {a.strip().lower() for a in args.ignore_apps.split(",") if a.strip()}

    def should_forward(app_name):
        name = (app_name or "").lower()
        if only and name not in only:
            return False
        if name in ignore:
            return False
        return True

    def on_message(bus, message):
        if message.get_member() != "Notify" or message.get_interface() != "org.freedesktop.Notifications":
            return
        try:
            fargs = message.get_args_list()
            app_name = str(fargs[0]) if len(fargs) > 0 else ""
            summary = str(fargs[3]) if len(fargs) > 3 else ""
            body = str(fargs[4]) if len(fargs) > 4 else ""
        except Exception as e:
            print(f"Could not parse notification: {e}")
            return

        if not should_forward(app_name):
            return

        print(f"[{app_name}] {summary}: {body}")
        send_notification(args.device, app=app_name, title=summary, message=body, duration=args.duration)

    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    # eavesdrop=true lets us observe method calls made *to* the notification
    # daemon, not just signals broadcast from it -- this is the standard,
    # documented way small utilities snoop on notifications.
    bus.add_match_string_non_blocking(
        "eavesdrop=true,interface='org.freedesktop.Notifications',member='Notify'"
    )
    bus.add_message_filter(on_message)

    print(f"Watching for Linux desktop notifications, forwarding to {args.device} ... Ctrl+C to stop.")
    try:
        GLib.MainLoop().run()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
