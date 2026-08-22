#!/usr/bin/env python3
"""
windows_toast_relay.py - forwards Windows toast notifications to the
ESP32 OLED panel, using the built-in WinRT UserNotificationListener API.

HOW IT WORKS
Windows keeps a record of recent toast notifications (the pop-ups in the
bottom-right corner / Action Center) that any app can read *if you grant it
permission*. This script polls that list periodically and forwards new
ones. It covers most apps that show normal toast notifications (Telegram
Desktop, Discord, Outlook, Chrome, WhatsApp Desktop, etc.).

REQUIREMENTS
    pip install winsdk

FIRST RUN
The first time you run this, Windows will show a permission prompt asking
whether this app may access your notifications. You must click Allow.
(You can review/revoke this later under Settings > Privacy > Notifications.)

RUN
    python windows_toast_relay.py --device 192.168.1.42

CAVEATS -- please read
Unlike Linux's D-Bus (a documented, stable public interface), the Windows
notification-listener API is less consistently documented and has known
quirks across Windows versions/updates (some notification styles, or apps
using older notification mechanisms, may not appear). Treat this as
best-effort: a genuinely useful relay for most everyday apps, but not a
guaranteed-complete capture the way the Linux script is. If a particular
app's notifications don't show up, that's a Windows/app limitation, not
something this script can work around.
"""
import argparse
import asyncio
import sys

try:
    from winsdk.windows.ui.notifications.management import (
        UserNotificationListener,
        UserNotificationListenerAccessStatus,
    )
    from winsdk.windows.ui.notifications import NotificationKinds
except ImportError:
    print("Missing dependency. Run:\n  pip install winsdk", file=sys.stderr)
    sys.exit(1)

from notify import send_notification


def extract_text(user_notification):
    """Pull whatever title/body text elements exist on a toast notification."""
    texts = []
    try:
        binding = user_notification.notification.visual.bindings[0]
        for t in binding.get_text_elements():
            texts.append(t.text)
    except Exception:
        pass
    title = texts[0] if len(texts) > 0 else ""
    body = " ".join(texts[1:]) if len(texts) > 1 else ""
    return title, body


def extract_app_name(user_notification):
    try:
        return user_notification.app_info.display_info.display_name
    except Exception:
        return "Windows"


async def main_async(device, duration, poll_seconds):
    listener = UserNotificationListener.current
    status = await listener.request_access_async()
    if status != UserNotificationListenerAccessStatus.ALLOWED:
        print("Notification access was not granted. Re-run and click Allow when Windows prompts you.")
        return

    seen_ids = set()
    print(f"Watching for Windows notifications, forwarding to {device} ... Ctrl+C to stop.")
    while True:
        try:
            notifications = await listener.get_notifications_async(NotificationKinds.TOAST)
            for n in notifications:
                if n.id in seen_ids:
                    continue
                seen_ids.add(n.id)
                app_name = extract_app_name(n)
                title, body = extract_text(n)
                print(f"[{app_name}] {title}: {body}")
                send_notification(device, app=app_name, title=title, message=body, duration=duration)
            # Keep the seen-id cache from growing forever during a long session.
            if len(seen_ids) > 500:
                seen_ids.clear()
        except Exception as e:
            print(f"Error while polling notifications: {e}")
        await asyncio.sleep(poll_seconds)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", required=True, help="ESP32 device IP address")
    parser.add_argument("--duration", type=int, default=6000)
    parser.add_argument("--poll-seconds", type=float, default=2.0, help="How often to check for new notifications")
    args = parser.parse_args()
    asyncio.run(main_async(args.device, args.duration, args.poll_seconds))


if __name__ == "__main__":
    main()
