# Notification relay scripts (PC and Android)

These send notifications to the ESP32 OLED panel's `/notify` endpoint —
from a computer, or (via Termux) directly from an Android phone. All of
them just call the device's HTTP API, so you can also write your own
integration in any language — these are provided scripts, not the only way
to do it.

The device shows: **app name**, **title + message**, and a **file name**
line if you provide one (just the name, not the actual file — the OLED
can't display files, only text). Several notifications sent close together
are queued and shown one after another automatically, so nothing gets
silently dropped (up to 6 pending at once; if more pile up, the oldest
pending one is dropped to make room for the newest).

## 1. `notify.py` — the building block (all platforms)

Standalone sender, and also imported by the two relay scripts below.

```bash
python notify.py --device 192.168.1.42 \
  --app "Terminal" --title "Build finished" \
  --message "0 errors, 2 warnings" --file "build.log" --duration 8000
```

Only `--device` is required. Use this directly for anything you trigger
yourself: append it to the end of a long-running command
(`make && python notify.py --device 192.168.1.42 --title Done`), a cron
job, a CI pipeline step, a git hook, an Automator action, a Shortcuts
action, a Task Scheduler task — anything that can run a command.

## 2. Linux — `linux_dbus_relay.py` (automatic, reliable)

Forwards **every** desktop notification automatically and in real time, by
observing the standard `org.freedesktop.Notifications.Notify` D-Bus call
that literally every Linux notification goes through (Slack, Thunderbird,
Discord, package manager popups, etc.) — this is the same mechanism
`dbus-monitor` uses, so it's dependable across desktop environments
(GNOME, KDE, XFCE, dunst, mako...) and needs no special permission beyond
your normal user session.

**Install (Debian/Ubuntu):**
```bash
sudo apt install python3-dbus python3-gi
```
(On Fedora: `sudo dnf install python3-dbus python3-gobject`. Arch:
`sudo pacman -S python-dbus python-gobject`.)

**Run:**
```bash
python3 linux_dbus_relay.py --device 192.168.1.42
```

**Filter to specific apps** (recommended if you only care about a couple):
```bash
python3 linux_dbus_relay.py --device 192.168.1.42 --only-apps "Slack,Thunderbird"
```
or forward everything except some apps:
```bash
python3 linux_dbus_relay.py --device 192.168.1.42 --ignore-apps "Spotify"
```

**Run it persistently** with a systemd user service so it starts on login:
```ini
# ~/.config/systemd/user/oled-notify-relay.service
[Unit]
Description=ESP32 OLED notification relay

[Service]
ExecStart=/usr/bin/python3 %h/pc-notify/linux_dbus_relay.py --device 192.168.1.42
Restart=on-failure

[Install]
WantedBy=default.target
```
```bash
systemctl --user enable --now oled-notify-relay.service
```

## 3. Windows — `windows_toast_relay.py` (automatic, best-effort)

Polls Windows' built-in toast notification history (the same list Action
Center shows) and forwards new ones — covers most everyday apps (Telegram
Desktop, Discord, Outlook, Chrome, WhatsApp Desktop, etc.).

**Install:**
```powershell
pip install winsdk
```

**Run:**
```powershell
python windows_toast_relay.py --device 192.168.1.42
```

The first run pops a Windows permission prompt asking whether the script
can access your notifications — click **Allow**. You can review or revoke
this later under **Settings → Privacy & security → Notifications**.

**Honest caveat:** Windows' notification-listener API is less consistently
documented than Linux's D-Bus and has known quirks across Windows
versions — some notification styles or older apps may not show up. Treat
this as a genuinely useful best-effort relay, not a 100%-guaranteed capture
of everything.

**Run it persistently** with Task Scheduler: create a task triggered "At
log on", action = `pythonw.exe windows_toast_relay.py --device 192.168.1.42`
(use `pythonw.exe` instead of `python.exe` to run without a console
window).

## 4. Android — `android_termux_relay.py` (runs on the phone itself)

Android notifications live on the phone, so unlike the Linux/Windows
scripts above, this one runs *on the phone* — inside Termux, a terminal
app — rather than on a PC.

It needs the same underlying Android "Notification Access" permission that
Tasker/MacroDroid need (there's no way around that on Android, whichever
app reads notifications has to be granted it), but Termux:API is a small,
single-purpose, fully open-source app with no other feature surface —
worth considering if you'd rather not install a general-purpose automation
app just for this.

**Setup (all on the phone):**
1. Install **Termux** and **Termux:API** from F-Droid (not the Play Store
   — those builds are outdated):
   https://f-droid.org/packages/com.termux/ and
   https://f-droid.org/packages/com.termux.api/
2. In Termux: `pkg update && pkg install python termux-api`
3. Grant Termux:API notification access when prompted (or manually:
   Settings → Apps → Special access → Notification access → Termux:API →
   Allow).
4. Get `notify.py` and `android_termux_relay.py` onto the phone (e.g.
   `pkg install git` and clone this project, or copy the files in via
   `termux-setup-storage`).
5. Run:
   ```
   python android_termux_relay.py --device 192.168.1.42
   ```

Filter to specific apps the same way as the Linux script:
```
python android_termux_relay.py --device 192.168.1.42 --only-apps "whatsapp,telegram"
```

Android may kill Termux in the background under aggressive battery
optimization — exclude Termux from battery optimization (Settings → Apps →
Termux → Battery) if you want this running continuously rather than only
while Termux is in the foreground.

**If you'd rather have a proper installed app instead of a Termux script:**
that's also possible — a minimal Android app implementing
`NotificationListenerService` is a fairly small amount of Kotlin code that
does exactly this and nothing else (no automation-app feature surface at
all). It's a bigger lift (Android Studio, building/signing an APK) than
the script above, but if you want it, that's a reasonable next step to ask
for.

## 5. macOS — no automatic option, and that's an Apple limitation

There is **no supported public API** for a third-party script to read
other apps' Notification Center notifications on macOS — Apple locked this
down years ago (System Integrity Protection blocks reading the old
notifications database, and there's no replacement API for third parties).
This isn't a gap in these scripts; it genuinely isn't possible without
deep, fragile, unsupported hacks that break on every macOS update.

What *does* work reliably on macOS:

- **Manual/self-triggered**, same as any OS — wrap `notify.py` around a
  command, script, or Shortcuts/Automator action:
  ```bash
  long_running_command && python3 notify.py --device 192.168.1.42 \
    --app Terminal --title "Done" --message "Command finished"
  ```
- **Per-app automation hooks**, for apps that support them — e.g. **Mail.app
  rules** can run a shell script/AppleScript when new mail arrives; point
  that action at `notify.py --app Mail --title "%SUBJECT%" ...` (Mail rule
  actions can pass message details to a script).
- **Shortcuts app** (built into modern macOS) — build a Shortcut that runs
  a shell script action calling `notify.py`, and trigger it from a menu
  bar click, keyboard shortcut, or a Shortcuts personal automation (time of
  day, opening an app, etc. — same trigger types as iOS, not arbitrary
  notification reading).
- **Third-party automation tools** you may already use (Hazel, Keyboard
  Maestro, BetterTouchTool) often have their own trigger types (file
  added, app launched, hotkey) that can shell out to `notify.py` as an
  action — worth checking if you already have one of these installed.

## Why not a cloud service (IFTTT, Zapier, etc.)?

Those run on the internet and can't reach a private local IP like
`192.168.x.x` directly — the device would need to be exposed to the
internet (port forwarding / dynamic DNS), which isn't recommended given it
has no authentication. If you want cloud-triggered notifications, run one
of these relay scripts (or a tool like Home Assistant/Node-RED) on a
machine on your LAN, and have *that* call `/notify` — it can pull from
cloud sources (IMAP, RSS, webhooks it receives, etc.) itself.
