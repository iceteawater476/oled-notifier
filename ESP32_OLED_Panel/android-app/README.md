# OLED Notify Relay (Android app)

A minimal, purpose-built Android app that does exactly one thing: watch
your phone's notifications and forward matching ones to the ESP32 OLED
panel's `/notify` endpoint. This is the "proper app" alternative to using
Tasker/MacroDroid or the Termux script — no automation-app feature
surface, just `NotificationListenerService` and a settings screen.

It still needs the same Android "Notification Access" permission any
notification-reading app needs (there's no way around that on Android),
but this app can't do anything else with it — no other permissions, no
other features.

## What it does

- Watches for new notifications system-wide.
- For each one, reads: the source app's name, the notification title, and
  its text body.
- Sends those to `http://<device-ip>/notify` (the same endpoint the PC/
  Termux scripts use), which shows it on the OLED, beeped and queued the
  same way as any other notification source.
- Optional include/exclude filters by app name, so you can limit it to
  just the apps you care about (e.g. only WhatsApp and Telegram).

## Getting an APK

There are two ways to build this. Neither requires you to write any code.

### Option A: Build it in the cloud with GitHub Actions (no Android Studio needed)

The parent project folder includes `.github/workflows/build-apk.yml` (one
level above this `android-app` folder), which builds a real `.apk` on
GitHub's servers automatically.

1. Push the **whole project** (the folder containing `.github/`,
   `android-app/`, the `.ino` sketch, `pc-notify/`, etc.) to a GitHub
   repository as-is — don't push just the `android-app` subfolder on its
   own, and don't move `.github` into it either. GitHub only looks for
   workflows at `.github/workflows/` in the true root of the repo, so the
   nesting has to stay exactly like this.
2. Open the repo on github.com → **Actions** tab. The workflow runs
   automatically on push, or click **Run workflow** to trigger it by hand.
   (If nothing shows up in the Actions tab at all, double-check `.github`
   ended up at the repo root and not inside `android-app/` — that's the
   most common cause.)
3. Wait for the run to finish (a few minutes), open it, and download the
   **oled-notify-relay-debug-apk** artifact — it's a zip containing
   `app-debug.apk`.
4. Transfer that `.apk` to your phone (email it to yourself, use a cloud
   drive, USB cable, etc.) and open it to install. You'll need to allow
   "install unknown apps" for whatever app you use to open it (Android
   will prompt you the first time).

This is the easiest path if you don't want to install Android Studio just
for this — GitHub's build servers have full access to Google's Android SDK
and don't need anything from you beyond a free GitHub account.

### Option B: Build it locally with Android Studio

1. Install [Android Studio](https://developer.android.com/studio) if you
   don't have it.
2. Open this `android-app` folder as a project (File → Open).
3. If Android Studio prompts you to update the Android Gradle Plugin or
   Kotlin version, accept it — that's normal and expected as tooling moves
   on since this was written.
4. Let it sync, then either:
   - Run it straight to a phone connected via USB (with Developer Options
     → USB debugging enabled) using the Run button, or
   - Build → Build Bundle(s)/APK(s) → Build APK(s), then copy the
     resulting `.apk` from `app/build/outputs/apk/debug/` to your phone
     and install it (you'll need to allow "install unknown apps" for
     whatever app you use to open it).

## Using it

1. Open the app. Enter your ESP32 panel's IP address (the same one you use
   to reach the web control page).
2. Tap **Grant notification access**, find "OLED Notify Relay" in the list
   that opens, and enable it. Android will show a warning about the scope
   of this permission — that's standard for any notification-reading app,
   not specific to this one.
3. Optionally fill in **Only forward these apps** (e.g. `whatsapp,
   telegram`) to limit it, or **Ignore these apps** to exclude noisy ones
   (e.g. `spotify`). Leave both blank to forward everything.
4. Tap **Save settings**, then **Send test notification** to confirm the
   device receives it.
5. Leave the app installed — the listener service keeps running in the
   background even if you don't reopen the app, as long as Android hasn't
   force-stopped it (see below).

## If it stops working after a while

Some phone manufacturers (Xiaomi, Huawei, OnePlus, Samsung, etc.) apply
aggressive battery optimization that can kill background listener
services. If notifications stop forwarding after some idle time:
- Find the app in Settings → Apps → OLED Notify Relay → Battery, and set
  it to "Unrestricted" / disable battery optimization for it.
- Some phones have an additional "Autostart" or "Protected apps" list
  (common on Xiaomi/MIUI) — add this app to it if available.

## Privacy note

Notification text and the source app name are sent as a plain HTTP request
to whatever IP address you enter, and nowhere else. Nothing is sent to any
server on the internet — this only works while your phone and the ESP32
are on the same local network. There's no analytics, no other network
calls, and no permissions requested beyond internet access and
notification listening.
