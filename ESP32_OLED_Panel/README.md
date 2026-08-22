# ESP32 + 0.96" OLED Multi-Function Panel

A single Arduino sketch that turns an ESP32 30-pin board and a 0.96" SSD1306
I2C OLED into a small desk display: clock (with a second alternate face),
text (static or scrolling, in a selectable size, paginated automatically if
it's long, supporting both ASCII and Unicode), images, a simple looping
"video", auto-sized QR codes, an editable Pomodoro timer with a completed-
cycle count, a stopwatch, and pop-up notifications — all controlled from a
web page hosted by the ESP32 itself. WiFi setup is done by scanning a QR
code shown on the OLED the first time it boots. The onboard BOOT button
doubles as a physical control, and a buzzer gives audible feedback.

## Wiring

| Part | ESP32 pin |
|---|---|
| OLED VCC | 3V3 |
| OLED GND | GND |
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| Buzzer (+) | GPIO25 |
| Buzzer (-) | GND |

No extra button needs wiring — the sketch uses the **BOOT button already on
your ESP32 dev board** (GPIO0).

> **Note on GPIO0 / BOOT:** this pin also controls entering the ESP32's
> flashing mode, but that only matters at power-on/reset. Once the sketch is
> running, pressing BOOT is completely safe and behaves like a normal
> button. Just don't hold it down while plugging in power or pressing
> reset, or the board will go into bootloader mode instead of starting
> normally.

### About the buzzer

Most small buzzers bundled with ESP32 starter kits are **passive** — they
have no built-in oscillator and need a driven square wave at an audible
frequency to make sound, which is what the sketch generates in software
(`buzzerTone()`). This is the default (`BUZZER_IS_ACTIVE = false`).

If yours is an **active** buzzer instead (it beeps on its own the instant
you apply power — usually has "ACTIVE" printed on the case, and you can't
see a coil through the little hole on top), set:
```cpp
const bool BUZZER_IS_ACTIVE = true;
```
near the top of the sketch. The device also plays a short self-test beep
right at boot so you can confirm the wiring works — remove that line in
`setup()` if you don't want it.

## Libraries to install (Arduino IDE → Tools → Manage Libraries)

Search for these **exact names** and install:

- **Adafruit GFX Library** (by Adafruit)
- **Adafruit SSD1306** (by Adafruit)
- **QRCode** (by **Richard Moore**) — if the search shows more than one
  result named "QRCode", make sure you pick the one whose author is
  Richard Moore. Source: https://github.com/ricmoo/QRCode
- **U8g2_for_Adafruit_GFX** (by **olikraus** / Oliver Kraus) — adds
  Unicode/UTF-8 text rendering on top of the display driver you already
  have; it does not replace Adafruit_SSD1306, it just adds extra fonts.

You also need the **ESP32 board package** installed via
Tools → Board → Boards Manager → search "esp32" → install the one by
**Espressif Systems**. This automatically provides `WiFi`, `WebServer`,
`DNSServer`, `Preferences`, and `SPIFFS` — nothing else to install for those.

## Flashing

1. Open `ESP32_OLED_Panel.ino` in the Arduino IDE. You do **not** need to
   edit any WiFi credentials in the code — setup happens over WiFi via QR
   code (see below).
2. Board: **ESP32 Dev Module** (or your specific board), default partition
   scheme is fine (it includes a SPIFFS partition).
3. Upload. Some ESP32 boards need you to hold the **BOOT** button while the
   IDE says "Connecting...".
4. Open the Serial Monitor at **115200 baud** to watch progress (optional).

## First-time WiFi setup (QR code)

1. On first boot (or any time it has no saved WiFi network), the OLED shows
   a QR code captioned **"Scan to setup WiFi"**.
2. Scan it with your phone's camera. This is a standard WiFi-join QR code —
   your phone will prompt you to connect to the device's own temporary
   hotspot, **ESP32-OLED-Setup** (password `12345678`).
3. Once connected, most phones automatically pop up a "Sign in to network"
   captive-portal page. If it doesn't appear, open a browser and go to
   `http://192.168.4.1/`.
4. On that page, tap **Scan for networks**, pick your home WiFi from the
   list (or type the name manually), enter its password, and tap
   **Connect**.
5. The device saves the network, restarts, and joins your home WiFi. The
   setup hotspot disappears once that happens.
6. From then on, the device boots straight into normal operation and is
   reachable at its new IP address on your home network — shown briefly on
   the OLED at boot, and printed in the Serial Monitor.

To re-run setup later (e.g. moving to a new WiFi network), open the web
page's **Setup** tab and tap **Forget WiFi network**.

## QR code legibility

QR codes are now auto-sized: the device picks the smallest QR "version"
(the standard QR size grades) that fits whatever text you give it, instead
of always using a fixed large version. Less data means fewer modules
(squares), and fewer modules means each one gets drawn bigger and is much
easier for a phone camera to resolve on a 128x64 screen. This applies to
the WiFi setup QR, the "connect to website" QR, and any custom QR you
generate — a typical local URL like `http://192.168.1.42/` now renders at
roughly double the module size compared to before. Very long text (over
~271 characters) gets truncated to keep the code decodable.

## Using the web page

- **Clock** — set your timezone as a GMT offset in hours, tap Apply. Time
  syncs automatically over NTP once connected.
- **Text** — type anything, ASCII or Unicode; toggle scrolling on/off,
  adjust scroll speed, and pick a text size. If the text doesn't fit on
  screen at once (in non-scrolling mode), it now **pages through** the
  content automatically every few seconds instead of being cut off, with a
  small page-count indicator in the corner.
- **Image** / **Video** — convert photos to monochrome bitmaps and either
  show one or loop through several as a simple animation.
- **QR Code** — type a URL or text; auto-sized for legibility as above.
- **Pomodoro** — configurable work/break/long-break durations and cycle
  count, with a running total of completed pomodoros, audible tones on
  phase changes, and BOOT-button pause/resume/start.
- **Stopwatch** — start / stop / lap / reset, with up to 10 laps recorded.
  Works from the web page or the BOOT button.
- **Notify** — send a test pop-up notification, and see the endpoint
  contract for hooking up your own triggers (details below).
- **Setup** — shows a "connect" QR code on demand, and lets you forget the
  saved WiFi network.

All settings persist across reboots (text, QR content, Pomodoro config and
completed count, timezone, saved WiFi network).

## Notifications — how to trigger them

The device exposes a simple endpoint that works with either `GET` or `POST`:

```
GET  /notify?app=<text>&title=<text>&message=<text>&file=<text>&duration=<ms, optional>
POST /notify   (Content-Type: application/x-www-form-urlencoded, same fields)
```

All fields are optional strings; `duration` is 1000–30000 ms, default 6000.
`app` is the source app/label (e.g. "WhatsApp", "Terminal"), `file` is a
file *name* only (e.g. "report.pdf") — the OLED can't display an actual
file, just its name, so a download-complete or attachment-received
notification can still tell you what arrived.

When it receives a request, the device shows a bordered box with the app
name, title + message, and file name (if given) on top of whatever is
currently showing (clock, pomodoro, etc.), beeps twice, and automatically
returns to the previous screen after `duration` milliseconds.
**Notifications sent close together are queued** (up to 6 pending) and
shown one after another rather than overwriting/losing each other — if
more than 6 pile up, the oldest pending one is dropped to make room for the
newest, so you always see the most recent activity. The web page's Notify
tab has a "Send test notification" form (with all the fields above) plus a
live "pending in queue" counter so you can see this working.

**For automatic PC → OLED notification forwarding** (not just manual
testing), see the **`pc-notify/`** folder next to this sketch — it has a
shared sender script plus real relays: one for Linux (via D-Bus, forwards
essentially all desktop notifications reliably) and one for Windows (via
the built-in toast notification API, best-effort), along with a clear
explanation of macOS's limitations and the workarounds that do work there.

No microcontroller can read another device's notification tray directly,
so getting your phone's *own* notifications onto the OLED always means
something has to actively call this endpoint. How invasive that is depends
entirely on what you use to do the calling — here are the options roughly
**from least to most invasive**:

### 1. Zero-permission, one-tap triggers (no app install, no special access)

Since `/notify` now accepts plain `GET` requests, anything that can open a
URL can trigger it — no automation app, no permissions dialog at all:

- **NFC tag:** write the URL below to a cheap NFC sticker (any "NFC Tools"
  style app can do this) and stick it on your desk. Tap your phone on it to
  fire a notification. Both iOS and Android read NFC tags natively.
- **Home Screen icon:** in Safari or Chrome, open the URL, then
  "Add to Home Screen" — you get a one-tap icon, no app required.
- **Browser bookmark / bookmarklet** — same idea, from a bookmarks bar.
- **iOS Shortcuts "Open URL" action**, triggerable from the Shortcuts
  widget, an Action Button, or a Siri phrase ("Hey Siri, desk note") — this
  only needs the Shortcuts app, which is Apple's own and needs no special
  permission grant.

Example URL (fill in your device's IP and, ideally, URL-encode spaces as
`%20` or `+`):
```
http://<device-ip>/notify?title=Desk&message=Hello!&duration=5000
```

This covers "I want to trigger it myself with one tap" — it just can't
react automatically to things happening on your phone, since nothing is
watching in the background.

### 2. iOS Shortcuts Personal Automations (built-in, no extra permission)

Open Shortcuts → Automation → "+" and pick a trigger: time of day,
arriving/leaving a location, opening a specific app, connecting to a
charger, a Focus mode changing, an NFC tag, etc. Add action **Get Contents
of URL** → `http://<device-ip>/notify` → Method `GET` or `POST` with
`title`/`message` fields → turn off "Ask Before Running". This runs
entirely on-device with no notification-listening permission — the
trade-off is it can't react to an arbitrary app's push notification (Apple
doesn't allow any third-party app that access, Shortcuts included), only
to these built-in trigger types.

### 3. A local automation hub you already trust (Home Assistant / Node-RED / n8n)

If you self-host something like Home Assistant, Node-RED, or n8n on a
Raspberry Pi or PC on your LAN, point its outbound webhook/rest_command at
`/notify` and trigger it from whatever the hub already watches — email
arriving, a calendar event starting, a smart plug turning off, a doorbell
press, etc. The "invasive" part (if any, like reading a mailbox) lives on a
server you fully control, not as a permission on your phone. Example
Home Assistant `configuration.yaml`:
```yaml
rest_command:
  oled_notify:
    url: "http://<device-ip>/notify"
    method: GET
    payload: ""
    content_type: "application/x-www-form-urlencoded"
```
Then call `rest_command.oled_notify` (with `title`/`message` as query
params on the URL, or switch `method` to `POST` and add a payload) from any
automation.

### 4. Android — Tasker / MacroDroid, or the lighter Termux option

Android is the only platform that can mirror *arbitrary* app notifications
automatically, because that requires the OS's system-wide "Notification
Access" permission — whichever app holds it can read the text of every
notification you get, not just the ones you care about, which is why it
feels invasive. There's no way around needing that permission on Android,
but you have a choice of *which* app holds it:

1. Install Tasker (and the AutoNotification plugin if you want per-app
   filtering).
2. Profile trigger: "Notification" (or the AutoNotification plugin's
   "Received Notification" event), filtered to the app you care about.
3. Task: **HTTP Request** action, Method `POST`, URL
   `http://<device-ip>/notify`, Body `app=%an&title=%antitle&message=%anmessage`,
   body type `x-www-form-urlencoded`.

This runs directly on your phone over your home WiFi — no cloud service,
no port-forwarding — but does require granting that broad permission to a
general-purpose automation app.

**Lighter alternative:** `pc-notify/android_termux_relay.py` does the same
job (via Termux + Termux:API, both open source) without installing a
full automation app — see the `pc-notify/` README for setup. It needs the
same OS-level permission, just granted to a smaller, single-purpose app.

### 5. PC — automatic relay scripts (see `pc-notify/`)

For a computer rather than a phone, the `pc-notify/` folder next to this
sketch has ready-to-run scripts:
- **Linux:** `linux_dbus_relay.py` forwards essentially all desktop
  notifications automatically and reliably, via D-Bus — no special
  permission beyond your normal session.
- **Windows:** `windows_toast_relay.py` forwards toast notifications via
  the built-in notification-listener API — one-time permission prompt,
  best-effort coverage.
- **Android:** `android_termux_relay.py` (see option 4 above).
- **macOS:** no automatic option exists (Apple blocks it for all
  third-party software, not just this project) — the folder's README
  covers the manual/per-app-hook workarounds that do work.
- Any OS: `notify.py` is a plain command-line sender you can bolt onto any
  script, cron job, or automation tool.

**Quickest test from a computer (no phone involved at all):**
```bash
curl "http://<device-ip>/notify?app=curl&title=Reminder&message=Meeting+in+10+minutes&duration=8000"
```

**Why not IFTTT / cloud webhooks directly?** Cloud services like IFTTT run
on the internet and can't reach a private local IP address like
`192.168.x.x` — they'd need the device exposed to the internet (port
forwarding / dynamic DNS), which isn't recommended for a device with no
authentication. Options 1–3 above all run *on your network*, so they can
call the device directly and safely. If you really want cloud triggers, the
usual pattern is a small always-on local relay (e.g. Home Assistant) that
receives the cloud webhook and forwards it to the ESP32 on your LAN.

## The onboard BOOT button (GPIO0)

- **Single press:**
  - Pomodoro on screen: pause / resume / start.
  - Stopwatch on screen: start / stop.
  - Otherwise: switches to the clock and toggles between two clock faces.
- **Three quick presses** (within ~0.5s): shows a QR code linking to the
  web control page for 20 seconds, then returns to whatever was showing.

## Notes & limits

- Screen is assumed to be 128×64. If yours is 128×32, change
  `SCREEN_HEIGHT` at the top of the sketch (layouts may need tweaks).
- "Video" frames are stored on SPIFFS flash — a handful of frames (1KB
  each) is fine; very long animations will eat available space.
- The web control page and the `/notify` endpoint have no login/password —
  treat it like any other local IoT gadget on a trusted home network.
- Full CJK (Chinese/Japanese/Korean) text needs a different, larger font
  table than the default Unicode font used here — see the comment next to
  `u8g2_font.setFont(...)` in `setup()` for how to swap it.
