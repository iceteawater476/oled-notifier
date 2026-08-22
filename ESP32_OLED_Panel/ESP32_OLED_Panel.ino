/*
  ESP32 + 0.96" SSD1306 OLED (I2C) Multi-Function Panel
  ------------------------------------------------------
  Features (all controllable from a web page hosted by the ESP32):
    - First-boot WiFi setup via QR code (no hardcoded WiFi credentials needed)
    - Clock (NTP synced, editable timezone) with a button-toggled alternate
      clock face, and a triple-click shortcut that shows a QR code linking
      straight to the control website
    - Static or scrolling text, with a selectable size, and automatic
      support for both plain ASCII and Unicode (accented letters, Cyrillic,
      Greek, many symbols, etc.)
    - Monochrome images (converted from any photo in the browser)
    - Simple "video" playback (sequence of images played with a delay)
    - QR codes, auto-sized to the smallest QR version that fits the content
      so the modules are as large (and scannable) as possible on a tiny
      128x64 screen
    - Editable Pomodoro timer (work / short break / long break / cycles),
      with a running total of completed pomodoros, working buzzer tones on
      phase changes, and the onboard BOOT button as a pause/resume control
    - Stopwatch with start/stop/lap/reset, also controllable from the
      onboard button
    - A simple HTTP notification endpoint (POST /notify) that pops up a
      title + message on the OLED for a few seconds -- see the Notify tab
      on the web page (and the README) for how to trigger these from your
      phone or a script

  WIRING (ESP32 30-pin dev board, default I2C pins):
    OLED VCC -> 3V3
    OLED GND -> GND
    OLED SDA -> GPIO21
    OLED SCL -> GPIO22
    Button   -> none needed! Uses the onboard BOOT button (GPIO0) that
                already exists on essentially every ESP32 dev board.
    Buzzer (optional) -> GPIO25 (+) and GND (-)

  ABOUT THE BUZZER:
    Most small buzzers sold with ESP32 starter kits are PASSIVE (no
    built-in oscillator) -- they need a driven square wave at an audible
    frequency to make sound, which is what this sketch generates in
    software. If yours is an ACTIVE buzzer (beeps on its own the instant
    you apply power, usually has "ACTIVE" printed on it, and only 2 legs
    with no visible internals through a hole), set BUZZER_IS_ACTIVE to
    true below.

  REQUIRED LIBRARIES (Arduino Library Manager -> search these exact names):
    - "Adafruit GFX Library"          (by Adafruit)
    - "Adafruit SSD1306"              (by Adafruit)
    - "QRCode"                        (by Richard Moore -- github.com/ricmoo/QRCode)
                                       If you see several "QRCode" results,
                                       pick the one whose author is
                                       "Richard Moore".
    - "U8g2_for_Adafruit_GFX"         (by olikraus / Oliver Kraus)
                                       This adds Unicode/UTF-8 text support
                                       on top of the existing Adafruit_GFX
                                       display driver -- it does NOT replace
                                       Adafruit_SSD1306, it just adds extra
                                       fonts you can draw with.
    - ESP32 board package (Boards Manager -> "esp32" by Espressif Systems).
      This automatically provides WiFi, WebServer, DNSServer, Preferences,
      and SPIFFS -- nothing extra to install for those.

  FIRST-TIME SETUP:
    1. Upload this sketch as-is (no WiFi credentials need to be hardcoded).
    2. The OLED will show a QR code captioned "Scan to setup WiFi".
       Scan it with your phone -- this connects your phone to the device's
       own temporary WiFi hotspot ("ESP32-OLED-Setup").
    3. Your phone should pop up a "Sign in to network" / captive portal
       page automatically. If it doesn't, open a browser and go to
       http://192.168.4.1/
    4. Pick your home WiFi network (or type it in) and its password, tap
       Connect. The device saves it, restarts, and joins your home network.
    5. From then on the device boots straight into normal operation and is
       reachable at its new IP address on your home network -- printed on
       the OLED briefly at boot, and in the Serial Monitor at 115200 baud.
    6. To re-run setup later (e.g. new WiFi network), use the "Forget WiFi
       network" button on the web page's Setup tab.

  ONBOARD BOOT BUTTON:
    - Single press:
        - Pomodoro on screen: pause / resume / start.
        - Stopwatch on screen: start / stop.
        - Otherwise: switches to the clock and toggles between two clock
          faces.
    - Three quick presses (within ~0.5s of each other): shows a QR code
      linking straight to the control website for 20 seconds.
    Note: GPIO0 is also the pin used to enter the ESP32's flashing mode, but
    that only matters while the board is powering on / resetting. Once the
    sketch is running normally, pressing BOOT is completely safe and just
    acts as a regular button.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <time.h>
#include <qrcodelib.h>

// ---------------------------------------------------------------------
// USER CONFIG
// ---------------------------------------------------------------------
#define BUZZER_PIN 4   // set to -1 if you have no buzzer connected
#define BUTTON_PIN 0    // onboard BOOT button on most ESP32 dev boards
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

const bool BUZZER_IS_ACTIVE = false; // true = active buzzer (just needs power), false = passive (needs a tone)

const char *PROV_AP_SSID = "ESP32-OLED-Setup";
const char *PROV_AP_PASS = "12345678"; // must be 8+ chars for WPA2

// ---------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
U8G2_FOR_ADAFRUIT_GFX u8g2_font; // draws Unicode/UTF-8 text onto the same display buffer
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
Preferences prefs;

const char *ntpServer = "pool.ntp.org";
long gmtOffset_sec = 6 * 3600; // default UTC+6, editable from the web page
long daylightOffset_sec = 0;

enum AppState { STATE_PROVISIONING, STATE_NORMAL };
AppState appState = STATE_NORMAL;

enum DisplayMode { MODE_CLOCK, MODE_TEXT, MODE_IMAGE, MODE_VIDEO, MODE_QR, MODE_QR_WEBSITE, MODE_POMODORO, MODE_STOPWATCH };
DisplayMode currentMode = MODE_CLOCK;

String websiteURL = "";

// Text
String textContent = "Hello World!";
bool textScroll = true;
int scrollSpeed = 20;      // ms per pixel step
int textSizeSetting = 2;   // 1 = small, 2 = medium, 3 = large (ASCII mode only)
bool textIsUnicode = false; // auto-detected: true if textContent has any non-ASCII bytes
int16_t scrollX = SCREEN_WIDTH;
bool textNeedsRedraw = true;
const int UNICODE_LINE_HEIGHT = 16;
const int UNICODE_SCROLL_BASELINE = 40;

// Static (non-scrolling) text is pre-wrapped into lines and paginated, so
// text that's too long for one screen just flips to the next page every
// few seconds instead of being cut off and lost.
#define MAX_WRAP_LINES 20
String wrappedLines[MAX_WRAP_LINES];
int wrappedLineCount = 0;
int linesPerPage = 4;
int currentPage = 0;
unsigned long lastPageChange = 0;
const unsigned long PAGE_INTERVAL_MS = 3000;

// Image
bool imageNeedsRedraw = true;

// Video
int videoFrameCount = 0;
int videoFrameDelay = 300;
int videoCurrentFrame = 0;
unsigned long lastVideoUpdate = 0;

// QR (user-set content)
String qrContent = "https://example.com";
bool qrNeedsRedraw = true;
const int QR_MAX_CHARS = 271; // capacity of the largest QR version we support (10) at ECC_LOW

// Website-shortcut QR (button triple-click / web button)
bool qrAutoRevert = false;
unsigned long qrRevertAt = 0;
DisplayMode modeBeforeQRShortcut = MODE_CLOCK;

// Clock
bool clockFaceAlt = false;
bool clockForceRedraw = true;

// Button (debounce + multi-click)
bool buttonStableState = HIGH;
bool buttonLastReading = HIGH;
unsigned long buttonLastDebounce = 0;
const unsigned long DEBOUNCE_MS = 40;
int clickCount = 0;
unsigned long lastClickTime = 0;
const unsigned long CLICK_WINDOW_MS = 500;

// Pomodoro
enum PomoPhase { POMO_IDLE, POMO_WORK, POMO_SHORT_BREAK, POMO_LONG_BREAK, POMO_PAUSED };
PomoPhase pomoPhase = POMO_IDLE;
PomoPhase pomoPhaseBeforePause = POMO_WORK;
int workMin = 25, shortBreakMin = 5, longBreakMin = 15, cyclesBeforeLong = 4;
int currentCycle = 1;
unsigned long pomoRemainingSec = 0;
int totalPomodorosCompleted = 0; // lifetime count of finished work sessions

// Stopwatch
bool stopwatchRunning = false;
unsigned long stopwatchStartMillis = 0;
unsigned long stopwatchElapsedMillis = 0;
#define MAX_LAPS 10
unsigned long stopwatchLaps[MAX_LAPS];
int stopwatchLapCount = 0;

// Notification overlay (shows on top of whatever mode is active)
bool notificationActive = false;
bool notificationNeedsRedraw = false;
String notificationApp = "";
String notificationTitle = "";
String notificationMessage = "";
String notificationFile = "";
bool notificationIsUnicode = false;
unsigned long notificationExpireAt = 0;

// Notifications are queued (not overwritten) so a burst of them all get
// shown in turn instead of the earlier ones being silently lost.
struct NotificationItem {
  String app;
  String title;
  String message;
  String file;
  unsigned long duration;
};
#define NOTIFY_QUEUE_SIZE 6
NotificationItem notifyQueue[NOTIFY_QUEUE_SIZE];
int notifyQueueHead = 0;
int notifyQueueTail = 0;
int notifyQueueCount = 0;

// ---------------------------------------------------------------------
// PROVISIONING WEB PAGE (WiFi setup, served only while STATE_PROVISIONING)
// ---------------------------------------------------------------------
const char PROV_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Setup WiFi</title>
<style>
 body{background:#12141a;color:#e8ebf0;font-family:system-ui,Arial,sans-serif;padding:16px;max-width:420px;margin:0 auto;}
 h1{font-size:1.2rem;}
 select,input{width:100%;background:#0f1117;color:#e8ebf0;border:1px solid #333a4a;border-radius:6px;padding:8px;margin:6px 0;box-sizing:border-box;}
 button{background:#5ec2ff;color:#00202e;border:none;padding:10px 14px;border-radius:6px;font-weight:600;cursor:pointer;margin-top:8px;}
 p{font-size:0.9rem;color:#8a90a0;}
</style>
</head>
<body>
<h1>Connect your OLED panel to WiFi</h1>
<p>Pick your home WiFi network below, enter its password, and tap Connect. The device will join your network and this setup hotspot will turn off.</p>
<button onclick="scanNetworks()">Scan for networks</button>
<select id="ssidSelect" onchange="pickNetwork()"><option value="">-- scan results --</option></select>
<input type="text" id="ssidInput" placeholder="Network name (SSID)">
<input type="password" id="passInput" placeholder="Password">
<button onclick="connectNetwork()">Connect</button>
<p id="msg"></p>
<script>
function scanNetworks(){
  document.getElementById('msg').innerText = 'Scanning...';
  fetch('/scan').then(r=>r.json()).then(list=>{
    const sel = document.getElementById('ssidSelect');
    sel.innerHTML = '<option value="">-- scan results --</option>';
    list.forEach(n=>{
      const opt = document.createElement('option');
      opt.value = n.ssid;
      opt.text = n.ssid + ' (' + n.rssi + ' dBm)' + (n.secure ? '' : ' [open]');
      sel.appendChild(opt);
    });
    document.getElementById('msg').innerText = list.length > 0
      ? (list.length + ' networks found')
      : 'No networks found (scanning while broadcasting its own hotspot can be unreliable on ESP32) -- just type your network name and password below instead.';
  }).catch(()=>{ document.getElementById('msg').innerText = 'Scan failed, try again'; });
}
function pickNetwork(){
  document.getElementById('ssidInput').value = document.getElementById('ssidSelect').value;
}
function connectNetwork(){
  const ssid = document.getElementById('ssidInput').value;
  const pass = document.getElementById('passInput').value;
  if(!ssid){ alert('Enter a network name'); return; }
  document.getElementById('msg').innerText = 'Saving and restarting device...';
  const p = new URLSearchParams({ssid, pass});
  fetch('/provision/save', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:p.toString()})
    .then(()=>{ document.getElementById('msg').innerText = 'Restarting. Reconnect your phone to your normal WiFi, then open the device on your network.'; });
}
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------
// NORMAL-OPERATION WEB PAGE (control panel)
// ---------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 OLED Panel</title>
<style>
  :root{--bg:#12141a;--panel:#1c1f28;--accent:#5ec2ff;--text:#e8ebf0;--muted:#8a90a0;}
  *{box-sizing:border-box;}
  body{background:var(--bg);color:var(--text);font-family:system-ui,Arial,sans-serif;margin:0;padding:16px;max-width:520px;margin:0 auto;}
  h1{font-size:1.3rem;margin:8px 0 4px;}
  .status{color:var(--muted);margin-bottom:12px;font-size:0.9rem;}
  .tabs{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:14px;}
  .tabbtn{background:var(--panel);color:var(--text);border:1px solid #333a4a;padding:8px 12px;border-radius:8px;cursor:pointer;font-size:0.9rem;}
  .tabbtn.active{background:var(--accent);color:#00202e;border-color:var(--accent);}
  .tab{background:var(--panel);border-radius:10px;padding:14px;margin-bottom:12px;}
  input[type=text], input[type=number], textarea, select{width:100%;background:#0f1117;color:var(--text);border:1px solid #333a4a;border-radius:6px;padding:8px;margin:4px 0 10px;font-size:0.95rem;}
  input[type=range]{width:100%;}
  input[type=file]{margin:6px 0 10px;}
  button{background:var(--accent);color:#00202e;border:none;padding:9px 14px;border-radius:6px;cursor:pointer;font-weight:600;margin:4px 6px 4px 0;}
  button.secondary{background:#333a4a;color:var(--text);}
  button.danger{background:#e05a5a;color:#210000;}
  label{display:block;margin:6px 0;font-size:0.9rem;}
  p{font-size:0.9rem;}
  code{background:#0f1117;padding:1px 5px;border-radius:4px;}
  .hint{color:var(--muted);font-size:0.8rem;margin-top:-4px;}
  .swtime{font-size:1.8rem;font-weight:700;margin:8px 0;}
</style>
</head>
<body>
<h1>ESP32 OLED Panel</h1>
<div class="status">Current display mode: <b id="currentModeLabel">-</b></div>

<div class="tabs">
 <button class="tabbtn" id="btn_clock" onclick="showTab('clock')">Clock</button>
 <button class="tabbtn" id="btn_text" onclick="showTab('text')">Text</button>
 <button class="tabbtn" id="btn_image" onclick="showTab('image')">Image</button>
 <button class="tabbtn" id="btn_video" onclick="showTab('video')">Video</button>
 <button class="tabbtn" id="btn_qr" onclick="showTab('qr')">QR Code</button>
 <button class="tabbtn" id="btn_pomodoro" onclick="showTab('pomodoro')">Pomodoro</button>
 <button class="tabbtn" id="btn_stopwatch" onclick="showTab('stopwatch')">Stopwatch</button>
 <button class="tabbtn" id="btn_notify" onclick="showTab('notify')">Notify</button>
 <button class="tabbtn" id="btn_setup" onclick="showTab('setup')">Setup</button>
</div>

<div id="clock" class="tab">
  <p>Device time: <b id="clockNow">--:--:--</b></p>
  <label>GMT offset (hours): <input id="gmtInput" type="number" step="0.5" value="6"></label>
  <label>DST offset (hours): <input id="dstInput" type="number" step="0.5" value="0"></label>
  <button onclick="applyTimezone()">Apply &amp; show clock</button>
  <p>Tip: the onboard BOOT button toggles between two clock faces (or pauses/resumes Pomodoro / starts&amp;stops the Stopwatch if those are on screen). Press it three times quickly to show a QR code linking to this page.</p>
</div>

<div id="text" class="tab" style="display:none">
  <textarea id="textInput" rows="3" placeholder="Enter text to display (ASCII or Unicode)"></textarea>
  <label><input type="checkbox" id="scrollCheck" checked style="width:auto"> Scroll horizontally</label>
  <label>Scroll speed (ms per step, lower = faster): <input id="speedRange" type="range" min="5" max="80" value="20"></label>
  <label>Text size (height):
    <select id="textSize">
      <option value="1">Small</option>
      <option value="2" selected>Medium</option>
      <option value="3">Large</option>
    </select>
  </label>
  <p class="hint">Size only applies to plain ASCII text. If your message contains accented letters, Cyrillic, Greek, symbols, emoji, etc., the device automatically switches to a universal Unicode font at a fixed size.</p>
  <button onclick="applyText()">Apply &amp; show</button>
</div>

<div id="image" class="tab" style="display:none">
  <p>Pick any photo — it will be converted to a 128x64 black &amp; white image.</p>
  <input type="file" id="imageFile" accept="image/*">
  <label><input type="checkbox" id="invertCheck" style="width:auto"> Invert black/white</label>
  <button onclick="uploadImage()">Upload &amp; show</button>
</div>

<div id="video" class="tab" style="display:none">
  <p>Pick several photos in order — they'll play back as an animation loop.</p>
  <input type="file" id="videoFiles" accept="image/*" multiple>
  <label>Frame delay (ms): <input id="videoDelay" type="number" value="300"></label>
  <button onclick="uploadVideo()">Upload &amp; play</button>
  <button class="secondary" onclick="clearVideo()">Clear frames</button>
  <p id="videoStatus"></p>
</div>

<div id="qr" class="tab" style="display:none">
  <label>URL or text: <input type="text" id="qrInput" placeholder="https://..."></label>
  <p class="hint">The device automatically picks the smallest QR version that fits your text, so it stays as large (and scannable) as possible on the small screen. Very long text will be truncated.</p>
  <button onclick="applyQR()">Generate &amp; show</button>
</div>

<div id="pomodoro" class="tab" style="display:none">
  <p>Phase: <b id="pomoPhase">-</b> &nbsp;|&nbsp; Remaining: <b id="pomoRemaining">--:--</b> &nbsp;|&nbsp; Cycle: <b id="pomoCycleInfo">-</b></p>
  <p>Total completed pomodoros: <b id="pomoTotal">0</b></p>
  <label>Work (min): <input id="pomoWork" type="number" value="25"></label>
  <label>Short break (min): <input id="pomoShort" type="number" value="5"></label>
  <label>Long break (min): <input id="pomoLong" type="number" value="15"></label>
  <label>Cycles before long break: <input id="pomoCycles" type="number" value="4"></label>
  <button onclick="applyPomoConfig()">Save settings</button>
  <div>
    <button onclick="pomoStart()">Start</button>
    <button onclick="pomoPause()">Pause</button>
    <button onclick="pomoResume()">Resume</button>
    <button class="secondary" onclick="pomoReset()">Reset</button>
  </div>
  <button class="secondary" onclick="pomoResetCount()">Reset completed count</button>
  <p class="hint">While the Pomodoro screen is showing, the onboard BOOT button pauses/resumes (or starts, if idle) without needing the web page.</p>
</div>

<div id="stopwatch" class="tab" style="display:none">
  <p>Status: <b id="swStatus">-</b></p>
  <div class="swtime" id="swTime">00:00.0</div>
  <button onclick="swStart()">Start</button>
  <button onclick="swStop()">Stop</button>
  <button onclick="swLap()">Lap</button>
  <button class="secondary" onclick="swReset()">Reset</button>
  <div id="swLaps"></div>
  <p class="hint">While the Stopwatch screen is showing, the onboard BOOT button starts/stops it without needing the web page.</p>
</div>

<div id="notify" class="tab" style="display:none">
  <p>Pops an app name, title/message, and (if given) a file name up on the OLED for a few seconds, on top of whatever is currently showing, then returns to it automatically. Several sent close together are queued and shown one after another, not dropped. See the project README for setup — including scripts that forward real PC/phone notifications automatically.</p>
  <p>Pending in queue: <b id="notifyQueueDepth">0</b></p>
  <label>App name: <input type="text" id="notifyApp" placeholder="e.g. WhatsApp, Outlook, Terminal"></label>
  <label>Title: <input type="text" id="notifyTitle" placeholder="Title"></label>
  <label>Message: <input type="text" id="notifyMessage" placeholder="Message text"></label>
  <label>File name (optional): <input type="text" id="notifyFile" placeholder="e.g. report.pdf"></label>
  <label>Duration (ms): <input type="number" id="notifyDuration" value="6000"></label>
  <button onclick="sendTestNotification()">Send test notification</button>
  <p class="hint">Endpoint: <code>GET</code> or <code>POST /notify</code> with fields <code>app</code>, <code>title</code>, <code>message</code>, <code>file</code> (all optional strings) and <code>duration</code> (ms, 1000–30000, default 6000).</p>
</div>

<div id="setup" class="tab" style="display:none">
  <p>This page's address: <b id="myUrl">-</b></p>
  <button onclick="showConnectQR()">Show connect QR on device</button>
  <p>Displays a QR code on the OLED for 20 seconds that links straight to this control page — handy for letting someone else's phone connect.</p>
  <hr style="border-color:#333a4a">
  <button class="danger" onclick="forgetWifi()">Forget WiFi network</button>
  <p>Clears the saved WiFi network and restarts the device into setup mode (it will show a new QR code to reconnect it to a network).</p>
</div>

<script>
const $ = id => document.getElementById(id);
$('myUrl').innerText = window.location.href;

function showTab(name){
  document.querySelectorAll('.tab').forEach(t=>t.style.display='none');
  document.querySelectorAll('.tabbtn').forEach(b=>b.classList.remove('active'));
  $(name).style.display='block';
  $('btn_'+name).classList.add('active');
}

function postForm(url, params){
  return fetch(url, {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: params.toString()});
}

function setMode(mode){ postForm('/mode', new URLSearchParams({mode})); }

function applyTimezone(){
  const p = new URLSearchParams({gmt: Math.round($('gmtInput').value*3600), dst: Math.round($('dstInput').value*3600)});
  postForm('/settings/timezone', p).then(()=>setMode('clock'));
}

function applyText(){
  const p = new URLSearchParams({
    text: $('textInput').value,
    scroll: $('scrollCheck').checked ? '1' : '0',
    speed: $('speedRange').value,
    size: $('textSize').value
  });
  postForm('/text', p).then(()=>setMode('text'));
}

function applyQR(){
  const p = new URLSearchParams({text: $('qrInput').value});
  postForm('/qr', p).then(()=>setMode('qr'));
}

function fileToBitmap(file){
  return new Promise((resolve)=>{
    const reader = new FileReader();
    reader.onload = e => {
      const img = new Image();
      img.onload = () => {
        const canvas = document.createElement('canvas');
        canvas.width = 128; canvas.height = 64;
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = 'black';
        ctx.fillRect(0,0,128,64);
        const scale = Math.min(128/img.width, 64/img.height);
        const w = img.width*scale, h = img.height*scale;
        ctx.drawImage(img, (128-w)/2, (64-h)/2, w, h);
        const data = ctx.getImageData(0,0,128,64).data;
        const invert = $('invertCheck') ? $('invertCheck').checked : false;
        const bytes = new Uint8Array(1024);
        for(let y=0;y<64;y++){
          for(let bx=0;bx<16;bx++){
            let b=0;
            for(let bit=0;bit<8;bit++){
              const x = bx*8+bit;
              const idx = (y*128+x)*4;
              const gray = (data[idx]+data[idx+1]+data[idx+2])/3;
              let on = gray > 128;
              if(invert) on = !on;
              if(on) b |= (1 << (7-bit));
            }
            bytes[y*16+bx] = b;
          }
        }
        resolve(bytes);
      };
      img.src = e.target.result;
    };
    reader.readAsDataURL(file);
  });
}

async function uploadImage(){
  const file = $('imageFile').files[0];
  if(!file){ alert('Choose an image first'); return; }
  const bytes = await fileToBitmap(file);
  const fd = new FormData();
  fd.append('file', new Blob([bytes]), 'img.bin');
  await fetch('/upload/image', {method:'POST', body: fd});
  setMode('image');
}

async function uploadVideo(){
  const files = Array.from($('videoFiles').files);
  if(!files.length){ alert('Choose frame images first'); return; }
  $('videoStatus').innerText = 'Uploading 0/'+files.length;
  for(let i=0;i<files.length;i++){
    const bytes = await fileToBitmap(files[i]);
    const fd = new FormData();
    fd.append('file', new Blob([bytes]), 'f'+i+'.bin');
    await fetch('/upload/video?frame='+i, {method:'POST', body: fd});
    $('videoStatus').innerText = 'Uploading '+(i+1)+'/'+files.length;
  }
  const p = new URLSearchParams({count: files.length, delay: $('videoDelay').value});
  await postForm('/video/config', p);
  $('videoStatus').innerText = 'Done: '+files.length+' frames uploaded';
  setMode('video');
}

function clearVideo(){
  fetch('/video/clear', {method:'POST'});
  $('videoStatus').innerText = 'Cleared';
}

function applyPomoConfig(){
  const p = new URLSearchParams({
    work: $('pomoWork').value,
    shortBreak: $('pomoShort').value,
    longBreak: $('pomoLong').value,
    cycles: $('pomoCycles').value
  });
  postForm('/pomodoro/config', p);
}
function pomoStart(){ setMode('pomodoro'); fetch('/pomodoro/start', {method:'POST'}); }
function pomoPause(){ fetch('/pomodoro/pause', {method:'POST'}); }
function pomoResume(){ fetch('/pomodoro/resume', {method:'POST'}); }
function pomoReset(){ fetch('/pomodoro/reset', {method:'POST'}); }
function pomoResetCount(){ fetch('/pomodoro/resetCount', {method:'POST'}); }

function swStart(){ setMode('stopwatch'); fetch('/stopwatch/start', {method:'POST'}); }
function swStop(){ fetch('/stopwatch/stop', {method:'POST'}); }
function swLap(){ fetch('/stopwatch/lap', {method:'POST'}); }
function swReset(){ fetch('/stopwatch/reset', {method:'POST'}); }

function fmtMs(ms){
  const totalSec = Math.floor(ms/1000);
  const hh = Math.floor(totalSec/3600);
  const mm = Math.floor((totalSec%3600)/60);
  const ss = totalSec%60;
  const tenths = Math.floor((ms%1000)/100);
  if(hh>0) return String(hh).padStart(2,'0')+':'+String(mm).padStart(2,'0')+':'+String(ss).padStart(2,'0');
  return String(mm).padStart(2,'0')+':'+String(ss).padStart(2,'0')+'.'+tenths;
}

function sendTestNotification(){
  const p = new URLSearchParams({
    app: $('notifyApp').value,
    title: $('notifyTitle').value,
    message: $('notifyMessage').value,
    file: $('notifyFile').value,
    duration: $('notifyDuration').value
  });
  postForm('/notify', p);
}

function showConnectQR(){ fetch('/showConnectQR', {method:'POST'}); }
function forgetWifi(){
  if(!confirm('This will disconnect the device from WiFi and restart it into setup mode. Continue?')) return;
  fetch('/wifi/reset', {method:'POST'});
}

function pollStatus(){
  fetch('/status').then(r=>r.json()).then(s=>{
    $('clockNow').innerText = s.time;
    $('pomoPhase').innerText = s.pomodoro.phase;
    const m = String(Math.floor(s.pomodoro.remaining/60)).padStart(2,'0');
    const sec = String(s.pomodoro.remaining%60).padStart(2,'0');
    $('pomoRemaining').innerText = m+':'+sec;
    $('pomoCycleInfo').innerText = s.pomodoro.cycle+'/'+s.pomodoro.cyclesBeforeLong;
    $('pomoTotal').innerText = s.pomodoro.totalCompleted;
    $('currentModeLabel').innerText = s.mode;

    $('swStatus').innerText = s.stopwatch.running ? 'Running' : 'Stopped';
    $('swTime').innerText = fmtMs(s.stopwatch.elapsedMs);
    $('swLaps').innerHTML = s.stopwatch.laps.map((l,i)=>'Lap '+(i+1)+': '+fmtMs(l)).join('<br>');

    $('notifyQueueDepth').innerText = s.notification.queued + (s.notification.active ? ' (+1 showing)' : '');
  }).catch(()=>{});
}
setInterval(pollStatus, 1000);
window.onload = () => { showTab('clock'); pollStatus(); };
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------
// SETTINGS PERSISTENCE
// ---------------------------------------------------------------------
void loadSettings() {
  textContent       = prefs.getString("textContent", "Hello World!");
  textScroll        = prefs.getBool("textScroll", true);
  scrollSpeed       = prefs.getInt("scrollSpeed", 20);
  textSizeSetting   = prefs.getInt("textSize", 2);
  qrContent         = prefs.getString("qrContent", "https://example.com");
  workMin           = prefs.getInt("workMin", 25);
  shortBreakMin     = prefs.getInt("shortBreakMin", 5);
  longBreakMin      = prefs.getInt("longBreakMin", 15);
  cyclesBeforeLong  = prefs.getInt("cyclesBeforeLong", 4);
  totalPomodorosCompleted = prefs.getInt("totalPomodoros", 0);
  gmtOffset_sec     = prefs.getLong("gmtOffset", 6 * 3600);
  daylightOffset_sec = prefs.getLong("dstOffset", 0);
}

// ---------------------------------------------------------------------
// HELPERS
// ---------------------------------------------------------------------
String jsonEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') { out += "\\n"; continue; }
    out += c;
  }
  return out;
}

bool isAsciiOnly(const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    if ((uint8_t)s[i] > 127) return false;
  }
  return true;
}

// ---------------------------------------------------------------------
// BUZZER
// ---------------------------------------------------------------------
void buzzerTone(unsigned int freq, unsigned int durationMs) {
#if BUZZER_PIN >= 0
  if (BUZZER_IS_ACTIVE || freq == 0) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(durationMs);
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
  unsigned long periodUs = 1000000UL / freq;
  unsigned long halfUs = periodUs / 2;
  unsigned long cycles = (unsigned long)durationMs * 1000UL / periodUs;
  for (unsigned long i = 0; i < cycles; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(halfUs);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(halfUs);
  }
#else
  (void)freq;
  (void)durationMs;
#endif
}

void buzzerWorkDone() {
  buzzerTone(2200, 120);
  delay(80);
  buzzerTone(2200, 120);
}

void buzzerBreakDone() {
  buzzerTone(1500, 300);
}

void buzzerNotification() {
  buzzerTone(2500, 80);
  delay(60);
  buzzerTone(2500, 80);
}

// ---------------------------------------------------------------------
// TEXT WRAPPING
// ---------------------------------------------------------------------
// Wraps textContent into wrappedLines[] (ASCII font), one entry per line,
// and works out how many lines fit on screen at once (linesPerPage).
void rewrapTextAscii() {
  int charW = 6 * textSizeSetting;
  int lineH = 8 * textSizeSetting + 2;
  int charsPerLine = max(1, SCREEN_WIDTH / charW);
  linesPerPage = max(1, SCREEN_HEIGHT / lineH);
  wrappedLineCount = 0;
  int start = 0;
  while (start < (int)textContent.length() && wrappedLineCount < MAX_WRAP_LINES) {
    int end = start + charsPerLine;
    if (end >= (int)textContent.length()) {
      end = textContent.length();
    } else {
      int lastSpace = textContent.lastIndexOf(' ', end);
      if (lastSpace > start) end = lastSpace;
    }
    wrappedLines[wrappedLineCount++] = textContent.substring(start, end);
    start = end;
    while (start < (int)textContent.length() && textContent[start] == ' ') start++;
  }
}

// Same idea, but measuring width with the Unicode font (so it never splits
// a multi-byte UTF-8 character).
void rewrapTextUnicode() {
  linesPerPage = max(1, SCREEN_HEIGHT / UNICODE_LINE_HEIGHT);
  wrappedLineCount = 0;
  int start = 0;
  while (start < (int)textContent.length() && wrappedLineCount < MAX_WRAP_LINES) {
    String line = textContent.substring(start);
    while (u8g2_font.getUTF8Width(line.c_str()) > SCREEN_WIDTH && line.indexOf(' ') != -1) {
      int lastSpace = line.lastIndexOf(' ');
      line = line.substring(0, lastSpace);
    }
    if (line.length() == 0) {
      int nextSpace = textContent.indexOf(' ', start);
      line = (nextSpace == -1) ? textContent.substring(start) : textContent.substring(start, nextSpace);
    }
    wrappedLines[wrappedLineCount++] = line;
    start += line.length();
    while (start < (int)textContent.length() && textContent[start] == ' ') start++;
  }
}

// Recomputes the wrapped/paginated lines for the current text, size, and
// font (ASCII vs Unicode). Call this any time textContent, textSizeSetting,
// or textIsUnicode changes.
void rewrapText() {
  if (textIsUnicode) rewrapTextUnicode();
  else rewrapTextAscii();
  currentPage = 0;
  lastPageChange = millis();
}

// Word-wraps a UTF-8 string for the Unicode font, only ever splitting on
// space characters so multi-byte sequences never get cut in half.
void printWrappedUnicode(const String &text, int startY) {
  int y = startY;
  int start = 0;
  while (start < (int)text.length() && y <= SCREEN_HEIGHT) {
    String line = text.substring(start);
    while (u8g2_font.getUTF8Width(line.c_str()) > SCREEN_WIDTH && line.indexOf(' ') != -1) {
      int lastSpace = line.lastIndexOf(' ');
      line = line.substring(0, lastSpace);
    }
    if (line.length() == 0) {
      int nextSpace = text.indexOf(' ', start);
      line = (nextSpace == -1) ? text.substring(start) : text.substring(start, nextSpace);
    }
    u8g2_font.setCursor(0, y);
    u8g2_font.print(line);
    start += line.length();
    while (start < (int)text.length() && text[start] == ' ') start++;
    y += UNICODE_LINE_HEIGHT;
  }
}

// ---------------------------------------------------------------------
// QR CODE DRAWING (auto-sized to the smallest version that fits)
// ---------------------------------------------------------------------
int pickQRVersion(const String &content) {
  // Byte-mode capacity at ECC_LOW for QR versions 1-10 (from the QR spec)
  const int capacities[] = {17, 32, 53, 78, 106, 134, 154, 192, 230, 271};
  int len = content.length();
  for (int v = 1; v <= 10; v++) {
    if (len <= capacities[v - 1]) return v;
  }
  return 10;
}

// Draws a QR code that fills the whole screen (used for the normal QR mode
// and the website-shortcut QR). Automatically uses the smallest QR version
// that fits the content, so modules stay as large as possible.
void drawQRGeneric(const String &contentIn) {
  String content = contentIn;
  if (content.length() > QR_MAX_CHARS) content = content.substring(0, QR_MAX_CHARS);
  int version = pickQRVersion(content);

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(10)]; // sized for the largest version we support
  qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, content.c_str());
  display.clearDisplay();
  int modules = qrcode.size;
  int scale = min(SCREEN_WIDTH, SCREEN_HEIGHT) / modules;
  if (scale < 1) scale = 1;
  int offsetX = (SCREEN_WIDTH - modules * scale) / 2;
  int offsetY = (SCREEN_HEIGHT - modules * scale) / 2;
  for (int y = 0; y < modules; y++) {
    for (int x = 0; x < modules; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.fillRect(offsetX + x * scale, offsetY + y * scale, scale, scale, SSD1306_WHITE);
      }
    }
  }
  display.display();
}

// Draws a QR code with a one-line caption underneath (used for WiFi setup).
void drawQRWithCaption(const String &contentIn, const String &caption) {
  String content = contentIn;
  if (content.length() > QR_MAX_CHARS) content = content.substring(0, QR_MAX_CHARS);
  int version = pickQRVersion(content);

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(10)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, content.c_str());
  display.clearDisplay();
  int modules = qrcode.size;
  int areaH = SCREEN_HEIGHT - 10;
  int scale = min(SCREEN_WIDTH, areaH) / modules;
  if (scale < 1) scale = 1;
  int offsetX = (SCREEN_WIDTH - modules * scale) / 2;
  int offsetY = (areaH - modules * scale) / 2;
  for (int y = 0; y < modules; y++) {
    for (int x = 0; x < modules; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.fillRect(offsetX + x * scale, offsetY + y * scale, scale, scale, SSD1306_WHITE);
      }
    }
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(caption, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, SCREEN_HEIGHT - 9);
  display.print(caption);
  display.display();
}

void showProvisioningQR() {
  String qrText = "WIFI:T:WPA;S:" + String(PROV_AP_SSID) + ";P:" + String(PROV_AP_PASS) + ";;";
  drawQRWithCaption(qrText, "Scan to setup WiFi");
}

// ---------------------------------------------------------------------
// POMODORO LOGIC
// ---------------------------------------------------------------------
unsigned long phaseTotalSeconds() {
  switch (pomoPhase) {
    case POMO_WORK: return (unsigned long)workMin * 60UL;
    case POMO_SHORT_BREAK: return (unsigned long)shortBreakMin * 60UL;
    case POMO_LONG_BREAK: return (unsigned long)longBreakMin * 60UL;
    default: return 0;
  }
}

void pomodoroAdvance() {
  if (pomoPhase == POMO_WORK) {
    totalPomodorosCompleted++;
    prefs.putInt("totalPomodoros", totalPomodorosCompleted);
    if (currentCycle >= cyclesBeforeLong) {
      pomoPhase = POMO_LONG_BREAK;
      currentCycle = 1;
    } else {
      pomoPhase = POMO_SHORT_BREAK;
    }
    buzzerWorkDone();
  } else if (pomoPhase == POMO_SHORT_BREAK) {
    currentCycle++;
    pomoPhase = POMO_WORK;
    buzzerBreakDone();
  } else if (pomoPhase == POMO_LONG_BREAK) {
    pomoPhase = POMO_WORK;
    buzzerBreakDone();
  }
  pomoRemainingSec = phaseTotalSeconds();
}

void pomodoroStart() {
  pomoPhase = POMO_WORK;
  currentCycle = 1;
  pomoRemainingSec = (unsigned long)workMin * 60UL;
}
void pomodoroPause() {
  if (pomoPhase == POMO_WORK || pomoPhase == POMO_SHORT_BREAK || pomoPhase == POMO_LONG_BREAK) {
    pomoPhaseBeforePause = pomoPhase;
    pomoPhase = POMO_PAUSED;
  }
}
void pomodoroResume() {
  if (pomoPhase == POMO_PAUSED) pomoPhase = pomoPhaseBeforePause;
}
void pomodoroReset() {
  pomoPhase = POMO_IDLE;
  currentCycle = 1;
  pomoRemainingSec = 0;
}
void pomodoroResetCount() {
  totalPomodorosCompleted = 0;
  prefs.putInt("totalPomodoros", 0);
}

// ---------------------------------------------------------------------
// STOPWATCH LOGIC
// ---------------------------------------------------------------------
unsigned long stopwatchCurrentMillis() {
  if (stopwatchRunning) return stopwatchElapsedMillis + (millis() - stopwatchStartMillis);
  return stopwatchElapsedMillis;
}
void stopwatchStart() {
  if (!stopwatchRunning) {
    stopwatchRunning = true;
    stopwatchStartMillis = millis();
  }
}
void stopwatchStop() {
  if (stopwatchRunning) {
    stopwatchElapsedMillis += millis() - stopwatchStartMillis;
    stopwatchRunning = false;
  }
}
void stopwatchReset() {
  stopwatchRunning = false;
  stopwatchElapsedMillis = 0;
  stopwatchLapCount = 0;
}
void stopwatchLap() {
  if (stopwatchLapCount < MAX_LAPS) {
    stopwatchLaps[stopwatchLapCount++] = stopwatchCurrentMillis();
  }
}

// ---------------------------------------------------------------------
// BUTTON HANDLING (debounce + single-click / triple-click)
// ---------------------------------------------------------------------
void toggleClockFace() {
  clockFaceAlt = !clockFaceAlt;
  currentMode = MODE_CLOCK;
  clockForceRedraw = true;
}

void showWebsiteQR() {
  if (currentMode != MODE_QR_WEBSITE) {
    modeBeforeQRShortcut = currentMode;
  }
  currentMode = MODE_QR_WEBSITE;
  qrNeedsRedraw = true;
  qrAutoRevert = true;
  qrRevertAt = millis() + 20000; // show for 20 seconds, then revert
}

// A single press does different things depending on what's on screen.
void handleSingleClick() {
  if (currentMode == MODE_POMODORO) {
    if (pomoPhase == POMO_PAUSED) {
      pomodoroResume();
    } else if (pomoPhase == POMO_WORK || pomoPhase == POMO_SHORT_BREAK || pomoPhase == POMO_LONG_BREAK) {
      pomodoroPause();
    } else {
      pomodoroStart();
    }
  } else if (currentMode == MODE_STOPWATCH) {
    if (stopwatchRunning) stopwatchStop();
    else stopwatchStart();
  } else {
    toggleClockFace();
  }
}

void checkButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != buttonLastReading) {
    buttonLastDebounce = millis();
  }
  if (millis() - buttonLastDebounce > DEBOUNCE_MS) {
    if (reading != buttonStableState) {
      buttonStableState = reading;
      if (buttonStableState == LOW) { // button pressed (active low, pull-up)
        clickCount++;
        lastClickTime = millis();
      }
    }
  }
  buttonLastReading = reading;

  if (clickCount > 0 && millis() - lastClickTime > CLICK_WINDOW_MS) {
    if (clickCount >= 3) {
      showWebsiteQR();
    } else {
      handleSingleClick();
    }
    clickCount = 0;
  }
}

// ---------------------------------------------------------------------
// RENDER FUNCTIONS (one per mode, each rate-limits itself with millis)
// ---------------------------------------------------------------------
void drawClockFaceA(struct tm &timeinfo) {
  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  display.setTextSize(2);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 18);
  display.print(timeStr);

  char dateStr[16];
  strftime(dateStr, sizeof(dateStr), "%a %d %b", &timeinfo);
  display.setTextSize(1);
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 44);
  display.print(dateStr);
}

void drawClockFaceB(struct tm &timeinfo) {
  char dayStr[12];
  strftime(dayStr, sizeof(dayStr), "%A", &timeinfo);
  display.setTextSize(1);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(dayStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 2);
  display.print(dayStr);
  display.drawFastHLine(10, 13, SCREEN_WIDTH - 20, SSD1306_WHITE);

  char hm[6];
  strftime(hm, sizeof(hm), "%H:%M", &timeinfo);
  display.setTextSize(3);
  display.getTextBounds(hm, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 22);
  display.print(hm);

  char sec[4];
  strftime(sec, sizeof(sec), "%S", &timeinfo);
  display.setTextSize(1);
  display.setCursor(SCREEN_WIDTH - 18, 56);
  display.print(sec);

  char dateStr[8];
  strftime(dateStr, sizeof(dateStr), "%d/%m", &timeinfo);
  display.setCursor(2, 56);
  display.print(dateStr);
}

void renderClock() {
  static unsigned long last = 0;
  if (!clockForceRedraw && millis() - last < 1000) return;
  last = millis();
  clockForceRedraw = false;

  struct tm timeinfo;
  bool haveTime = getLocalTime(&timeinfo);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (!haveTime) {
    display.setTextSize(1);
    display.setCursor(0, 28);
    display.print("Time not synced");
  } else if (!clockFaceAlt) {
    drawClockFaceA(timeinfo);
  } else {
    drawClockFaceB(timeinfo);
  }
  display.display();
}

void renderTextAscii() {
  if (textScroll) {
    static unsigned long last = 0;
    if (millis() - last < (unsigned long)scrollSpeed) return;
    last = millis();
    display.clearDisplay();
    display.setTextSize(textSizeSetting);
    display.setTextColor(SSD1306_WHITE);
    int y = (SCREEN_HEIGHT - 8 * textSizeSetting) / 2;
    display.setCursor(scrollX, y);
    display.print(textContent);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(textContent, 0, 0, &x1, &y1, &w, &h);
    scrollX--;
    if (scrollX < -(int)w) scrollX = SCREEN_WIDTH;
    display.display();
  } else {
    int totalPages = max(1, (wrappedLineCount + linesPerPage - 1) / linesPerPage);
    if (totalPages > 1 && currentPage < totalPages - 1 && millis() - lastPageChange > PAGE_INTERVAL_MS) {
      currentPage++;
      lastPageChange = millis();
      textNeedsRedraw = true;
    }
    if (textNeedsRedraw) {
      display.clearDisplay();
      display.setTextSize(textSizeSetting);
      display.setTextColor(SSD1306_WHITE);
      int lineH = 8 * textSizeSetting + 2;
      int startLine = currentPage * linesPerPage;
      int y = 0;
      for (int i = startLine; i < wrappedLineCount && i < startLine + linesPerPage; i++) {
        display.setCursor(0, y);
        display.print(wrappedLines[i]);
        y += lineH;
      }
      if (totalPages > 1) {
        display.setTextSize(1);
        display.setCursor(SCREEN_WIDTH - 20, SCREEN_HEIGHT - 8);
        display.print(currentPage + 1);
        display.print("/");
        display.print(totalPages);
      }
      display.display();
      textNeedsRedraw = false;
    }
  }
}

void renderTextUnicode() {
  if (textScroll) {
    static unsigned long last = 0;
    if (millis() - last < (unsigned long)scrollSpeed) return;
    last = millis();
    display.clearDisplay();
    u8g2_font.setCursor(scrollX, UNICODE_SCROLL_BASELINE);
    u8g2_font.print(textContent);
    int w = u8g2_font.getUTF8Width(textContent.c_str());
    scrollX--;
    if (scrollX < -w) scrollX = SCREEN_WIDTH;
    display.display();
  } else {
    int totalPages = max(1, (wrappedLineCount + linesPerPage - 1) / linesPerPage);
    if (totalPages > 1 && currentPage < totalPages - 1 && millis() - lastPageChange > PAGE_INTERVAL_MS) {
      currentPage++;
      lastPageChange = millis();
      textNeedsRedraw = true;
    }
    if (textNeedsRedraw) {
      display.clearDisplay();
      int startLine = currentPage * linesPerPage;
      int y = UNICODE_LINE_HEIGHT - 2;
      for (int i = startLine; i < wrappedLineCount && i < startLine + linesPerPage; i++) {
        u8g2_font.setCursor(0, y);
        u8g2_font.print(wrappedLines[i]);
        y += UNICODE_LINE_HEIGHT;
      }
      if (totalPages > 1) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(SCREEN_WIDTH - 20, SCREEN_HEIGHT - 8);
        display.print(currentPage + 1);
        display.print("/");
        display.print(totalPages);
      }
      display.display();
      textNeedsRedraw = false;
    }
  }
}

void renderText() {
  if (textIsUnicode) renderTextUnicode();
  else renderTextAscii();
}

void renderImage() {
  if (!imageNeedsRedraw) return;
  imageNeedsRedraw = false;
  File f = SPIFFS.open("/image.bin", "r");
  display.clearDisplay();
  if (!f) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 28);
    display.print("No image uploaded");
  } else {
    static uint8_t buf[1024];
    f.read(buf, 1024);
    f.close();
    display.drawBitmap(0, 0, buf, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  }
  display.display();
}

void renderVideo() {
  if (videoFrameCount <= 0) {
    static unsigned long lastMsg = 0;
    if (millis() - lastMsg > 1000) {
      lastMsg = millis();
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 28);
      display.print("No video uploaded");
      display.display();
    }
    return;
  }
  if (millis() - lastVideoUpdate < (unsigned long)videoFrameDelay) return;
  lastVideoUpdate = millis();
  String path = "/vid" + String(videoCurrentFrame) + ".bin";
  File f = SPIFFS.open(path, "r");
  if (f) {
    static uint8_t buf[1024];
    f.read(buf, 1024);
    f.close();
    display.clearDisplay();
    display.drawBitmap(0, 0, buf, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.display();
  }
  videoCurrentFrame = (videoCurrentFrame + 1) % videoFrameCount;
}

void renderQR() {
  if (!qrNeedsRedraw) return;
  qrNeedsRedraw = false;
  drawQRGeneric(qrContent);
}

void renderQRWebsite() {
  if (!qrNeedsRedraw) return;
  qrNeedsRedraw = false;
  drawQRGeneric(websiteURL);
}

void renderPomodoro() {
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  if (pomoPhase == POMO_WORK || pomoPhase == POMO_SHORT_BREAK || pomoPhase == POMO_LONG_BREAK) {
    if (pomoRemainingSec > 0) pomoRemainingSec--;
    else pomodoroAdvance();
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  String label;
  switch (pomoPhase) {
    case POMO_WORK: label = "WORK"; break;
    case POMO_SHORT_BREAK: label = "SHORT BREAK"; break;
    case POMO_LONG_BREAK: label = "LONG BREAK"; break;
    case POMO_PAUSED: label = "PAUSED"; break;
    default: label = "READY"; break;
  }
  display.print(label);
  display.setCursor(96, 0);
  display.print(currentCycle);
  display.print("/");
  display.print(cyclesBeforeLong);

  int mm = pomoRemainingSec / 60;
  int ss = pomoRemainingSec % 60;
  char buf[6];
  sprintf(buf, "%02d:%02d", mm, ss);
  display.setTextSize(3);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 18);
  display.print(buf);

  display.setTextSize(1);
  String totalStr = "Completed: " + String(totalPomodorosCompleted);
  display.getTextBounds(totalStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 46);
  display.print(totalStr);

  unsigned long total = phaseTotalSeconds();
  if (total > 0) {
    int barW = map(total - pomoRemainingSec, 0, total, 0, SCREEN_WIDTH - 4);
    display.drawRect(2, 56, SCREEN_WIDTH - 4, 8, SSD1306_WHITE);
    display.fillRect(2, 56, barW, 8, SSD1306_WHITE);
  }
  display.display();
}

void renderStopwatch() {
  static unsigned long last = 0;
  if (millis() - last < 100) return;
  last = millis();

  unsigned long ms = stopwatchCurrentMillis();
  unsigned long totalSec = ms / 1000;
  unsigned int hh = totalSec / 3600;
  unsigned int mm = (totalSec % 3600) / 60;
  unsigned int ss = totalSec % 60;
  unsigned int tenths = (ms % 1000) / 100;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(stopwatchRunning ? "RUNNING" : "STOPPED");
  display.setCursor(104, 0);
  display.print("L");
  display.print(stopwatchLapCount);

  char buf[16];
  if (hh > 0) sprintf(buf, "%u:%02u:%02u", hh, mm, ss);
  else sprintf(buf, "%02u:%02u.%u", mm, ss, tenths);
  display.setTextSize(hh > 0 ? 2 : 3);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 20);
  display.print(buf);

  if (stopwatchLapCount > 0) {
    unsigned long lastLapMs = stopwatchLaps[stopwatchLapCount - 1];
    unsigned long ls = lastLapMs / 1000;
    char lapBuf[20];
    sprintf(lapBuf, "Lap %d  %02u:%02u", stopwatchLapCount, (unsigned)((ls / 60) % 60), (unsigned)(ls % 60));
    display.setTextSize(1);
    display.getTextBounds(lapBuf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 52);
    display.print(lapBuf);
  }
  display.display();
}

// ---------------------------------------------------------------------
// NOTIFICATION OVERLAY
// ---------------------------------------------------------------------

// Like printWrappedUnicode, but with a left margin and a bottom bound, for
// laying out the notification body above an optional file-name footer.
void printWrappedUnicodeBounded(const String &text, int startY, int maxY) {
  int y = startY;
  int start = 0;
  while (start < (int)text.length() && y <= maxY) {
    String line = text.substring(start);
    while (u8g2_font.getUTF8Width(line.c_str()) > SCREEN_WIDTH - 8 && line.indexOf(' ') != -1) {
      int lastSpace = line.lastIndexOf(' ');
      line = line.substring(0, lastSpace);
    }
    if (line.length() == 0) {
      int nextSpace = text.indexOf(' ', start);
      line = (nextSpace == -1) ? text.substring(start) : text.substring(start, nextSpace);
    }
    u8g2_font.setCursor(4, y);
    u8g2_font.print(line);
    start += line.length();
    while (start < (int)text.length() && text[start] == ' ') start++;
    y += UNICODE_LINE_HEIGHT;
  }
}

void drawNotification() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);

  bool hasFile = notificationFile.length() > 0;
  String appLabel = notificationApp.length() > 0 ? notificationApp : "Notification";

  // Combine title + message into one flowing body of text (title first).
  String body = notificationTitle;
  if (notificationMessage.length() > 0) {
    if (body.length() > 0) body += "  ";
    body += notificationMessage;
  }

  if (!notificationIsUnicode) {
    int bottomLimit = hasFile ? (SCREEN_HEIGHT - 12) : (SCREEN_HEIGHT - 2);

    display.setTextSize(1);
    display.setCursor(4, 3);
    display.print(appLabel);
    display.drawFastHLine(3, 13, SCREEN_WIDTH - 6, SSD1306_WHITE);

    int charsPerLine = (SCREEN_WIDTH - 8) / 6;
    int start = 0, y = 17;
    while (start < (int)body.length() && y <= bottomLimit) {
      int end = start + charsPerLine;
      if (end >= (int)body.length()) {
        end = body.length();
      } else {
        int lastSpace = body.lastIndexOf(' ', end);
        if (lastSpace > start) end = lastSpace;
      }
      display.setCursor(4, y);
      display.print(body.substring(start, end));
      y += 9;
      start = end;
      while (start < (int)body.length() && body[start] == ' ') start++;
    }

    if (hasFile) {
      display.drawFastHLine(3, SCREEN_HEIGHT - 11, SCREEN_WIDTH - 6, SSD1306_WHITE);
      String fileLine = "File: " + notificationFile;
      int maxChars = (SCREEN_WIDTH - 8) / 6;
      if ((int)fileLine.length() > maxChars) fileLine = fileLine.substring(0, maxChars - 1) + ">";
      display.setCursor(4, SCREEN_HEIGHT - 9);
      display.print(fileLine);
    }
  } else {
    int bottomLimit = hasFile ? (SCREEN_HEIGHT - 12) : (SCREEN_HEIGHT - 2);

    u8g2_font.setCursor(4, 13);
    u8g2_font.print(appLabel);
    display.drawFastHLine(3, 16, SCREEN_WIDTH - 6, SSD1306_WHITE);

    printWrappedUnicodeBounded(body, 30, bottomLimit);

    if (hasFile) {
      display.drawFastHLine(3, SCREEN_HEIGHT - 11, SCREEN_WIDTH - 6, SSD1306_WHITE);
      u8g2_font.setCursor(4, SCREEN_HEIGHT - 3);
      u8g2_font.print(String("File: ") + notificationFile);
    }
  }
  display.display();
}


void renderNotification() {
  if (!notificationNeedsRedraw) return;
  notificationNeedsRedraw = false;
  drawNotification();
}

// ---------------------------------------------------------------------
// NORMAL-MODE WEB HANDLERS
// ---------------------------------------------------------------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  struct tm timeinfo;
  char timeStr[9] = "--:--:--";
  if (getLocalTime(&timeinfo)) strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

  String modeStr;
  switch (currentMode) {
    case MODE_CLOCK: modeStr = "clock"; break;
    case MODE_TEXT: modeStr = "text"; break;
    case MODE_IMAGE: modeStr = "image"; break;
    case MODE_VIDEO: modeStr = "video"; break;
    case MODE_QR: modeStr = "qr"; break;
    case MODE_QR_WEBSITE: modeStr = "qr_website"; break;
    case MODE_POMODORO: modeStr = "pomodoro"; break;
    case MODE_STOPWATCH: modeStr = "stopwatch"; break;
  }
  String phaseStr;
  switch (pomoPhase) {
    case POMO_IDLE: phaseStr = "idle"; break;
    case POMO_WORK: phaseStr = "work"; break;
    case POMO_SHORT_BREAK: phaseStr = "short_break"; break;
    case POMO_LONG_BREAK: phaseStr = "long_break"; break;
    case POMO_PAUSED: phaseStr = "paused"; break;
  }

  String lapsJson = "[";
  for (int i = 0; i < stopwatchLapCount; i++) {
    if (i > 0) lapsJson += ",";
    lapsJson += String(stopwatchLaps[i]);
  }
  lapsJson += "]";

  String json = "{";
  json += "\"mode\":\"" + modeStr + "\",";
  json += "\"time\":\"" + String(timeStr) + "\",";
  json += "\"pomodoro\":{";
  json += "\"phase\":\"" + phaseStr + "\",";
  json += "\"remaining\":" + String(pomoRemainingSec) + ",";
  json += "\"cycle\":" + String(currentCycle) + ",";
  json += "\"cyclesBeforeLong\":" + String(cyclesBeforeLong) + ",";
  json += "\"totalCompleted\":" + String(totalPomodorosCompleted) + ",";
  json += "\"workMin\":" + String(workMin) + ",";
  json += "\"shortBreakMin\":" + String(shortBreakMin) + ",";
  json += "\"longBreakMin\":" + String(longBreakMin);
  json += "},";
  json += "\"stopwatch\":{";
  json += "\"running\":" + String(stopwatchRunning ? "true" : "false") + ",";
  json += "\"elapsedMs\":" + String(stopwatchCurrentMillis()) + ",";
  json += "\"laps\":" + lapsJson;
  json += "},";
  json += "\"text\":\"" + jsonEscape(textContent) + "\",";
  json += "\"scroll\":" + String(textScroll ? "true" : "false") + ",";
  json += "\"textSize\":" + String(textSizeSetting) + ",";
  json += "\"textIsUnicode\":" + String(textIsUnicode ? "true" : "false") + ",";
  json += "\"qr\":\"" + jsonEscape(qrContent) + "\",";
  json += "\"videoFrameCount\":" + String(videoFrameCount) + ",";
  json += "\"notification\":{";
  json += "\"active\":" + String(notificationActive ? "true" : "false") + ",";
  json += "\"queued\":" + String(notifyQueueCount);
  json += "}";
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetMode() {
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    if (m == "clock") currentMode = MODE_CLOCK;
    else if (m == "text") { currentMode = MODE_TEXT; textNeedsRedraw = true; scrollX = SCREEN_WIDTH; }
    else if (m == "image") { currentMode = MODE_IMAGE; imageNeedsRedraw = true; }
    else if (m == "video") { currentMode = MODE_VIDEO; videoCurrentFrame = 0; }
    else if (m == "qr") { currentMode = MODE_QR; qrNeedsRedraw = true; }
    else if (m == "pomodoro") currentMode = MODE_POMODORO;
    else if (m == "stopwatch") currentMode = MODE_STOPWATCH;
  }
  server.send(200, "text/plain", "OK");
}

void handleSetText() {
  if (server.hasArg("text")) textContent = server.arg("text");
  if (server.hasArg("scroll")) textScroll = server.arg("scroll") == "1";
  if (server.hasArg("speed")) scrollSpeed = server.arg("speed").toInt();
  if (server.hasArg("size")) textSizeSetting = constrain(server.arg("size").toInt(), 1, 3);
  textIsUnicode = !isAsciiOnly(textContent);
  rewrapText();
  textNeedsRedraw = true;
  scrollX = SCREEN_WIDTH;
  prefs.putString("textContent", textContent);
  prefs.putBool("textScroll", textScroll);
  prefs.putInt("scrollSpeed", scrollSpeed);
  prefs.putInt("textSize", textSizeSetting);
  server.send(200, "text/plain", "OK");
}

void handleSetQR() {
  if (server.hasArg("text")) qrContent = server.arg("text");
  qrNeedsRedraw = true;
  prefs.putString("qrContent", qrContent);
  server.send(200, "text/plain", "OK");
}

void handleSetTimezone() {
  if (server.hasArg("gmt")) gmtOffset_sec = server.arg("gmt").toInt();
  if (server.hasArg("dst")) daylightOffset_sec = server.arg("dst").toInt();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  prefs.putLong("gmtOffset", gmtOffset_sec);
  prefs.putLong("dstOffset", daylightOffset_sec);
  server.send(200, "text/plain", "OK");
}

void handleImageUpload() {
  HTTPUpload &upload = server.upload();
  static File uploadFile;
  if (upload.status == UPLOAD_FILE_START) {
    uploadFile = SPIFFS.open("/image.bin", "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    imageNeedsRedraw = true;
  }
}

void handleVideoUpload() {
  HTTPUpload &upload = server.upload();
  static File uploadFile;
  static String path;
  if (upload.status == UPLOAD_FILE_START) {
    int frame = server.hasArg("frame") ? server.arg("frame").toInt() : 0;
    path = "/vid" + String(frame) + ".bin";
    uploadFile = SPIFFS.open(path, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  }
}

void handleVideoConfig() {
  if (server.hasArg("count")) videoFrameCount = server.arg("count").toInt();
  if (server.hasArg("delay")) videoFrameDelay = server.arg("delay").toInt();
  videoCurrentFrame = 0;
  server.send(200, "text/plain", "OK");
}

void handleVideoClear() {
  for (int i = 0; i < 50; i++) {
    String path = "/vid" + String(i) + ".bin";
    if (SPIFFS.exists(path)) SPIFFS.remove(path);
  }
  videoFrameCount = 0;
  videoCurrentFrame = 0;
  server.send(200, "text/plain", "OK");
}

void handlePomoConfig() {
  if (server.hasArg("work")) workMin = server.arg("work").toInt();
  if (server.hasArg("shortBreak")) shortBreakMin = server.arg("shortBreak").toInt();
  if (server.hasArg("longBreak")) longBreakMin = server.arg("longBreak").toInt();
  if (server.hasArg("cycles")) cyclesBeforeLong = server.arg("cycles").toInt();
  prefs.putInt("workMin", workMin);
  prefs.putInt("shortBreakMin", shortBreakMin);
  prefs.putInt("longBreakMin", longBreakMin);
  prefs.putInt("cyclesBeforeLong", cyclesBeforeLong);
  server.send(200, "text/plain", "OK");
}

void handlePomoResetCount() {
  pomodoroResetCount();
  server.send(200, "text/plain", "OK");
}

void handleStopwatchStart() {
  currentMode = MODE_STOPWATCH;
  stopwatchStart();
  server.send(200, "text/plain", "OK");
}
void handleStopwatchStop() {
  stopwatchStop();
  server.send(200, "text/plain", "OK");
}
void handleStopwatchLap() {
  stopwatchLap();
  server.send(200, "text/plain", "OK");
}
void handleStopwatchReset() {
  stopwatchReset();
  server.send(200, "text/plain", "OK");
}

void handleNotify() {
  NotificationItem item;
  item.app = server.hasArg("app") ? server.arg("app") : "";
  item.title = server.hasArg("title") ? server.arg("title") : "";
  item.message = server.hasArg("message") ? server.arg("message") : "";
  item.file = server.hasArg("file") ? server.arg("file") : "";
  unsigned long dur = server.hasArg("duration") ? (unsigned long)server.arg("duration").toInt() : 6000UL;
  item.duration = constrain(dur, 1000UL, 30000UL);

  if (notifyQueueCount >= NOTIFY_QUEUE_SIZE) {
    // Queue is full: drop the oldest pending one so the newest is never lost.
    notifyQueueHead = (notifyQueueHead + 1) % NOTIFY_QUEUE_SIZE;
    notifyQueueCount--;
  }
  notifyQueue[notifyQueueTail] = item;
  notifyQueueTail = (notifyQueueTail + 1) % NOTIFY_QUEUE_SIZE;
  notifyQueueCount++;

  Serial.print("Notification queued: app=");
  Serial.print(item.app);
  Serial.print(" title=");
  Serial.print(item.title);
  Serial.print(" message=");
  Serial.print(item.message);
  Serial.print(" file=");
  Serial.print(item.file);
  Serial.print(" (queue depth ");
  Serial.print(notifyQueueCount);
  Serial.println(")");

  server.send(200, "text/plain", "OK");
}

void handleShowConnectQR() {
  showWebsiteQR();
  server.send(200, "text/plain", "OK");
}

void handleWifiForget() {
  prefs.remove("wifiSSID");
  prefs.remove("wifiPass");
  server.send(200, "text/plain", "Resetting...");
  delay(600);
  ESP.restart();
}

void setupNormalRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/mode", HTTP_POST, handleSetMode);
  server.on("/text", HTTP_POST, handleSetText);
  server.on("/qr", HTTP_POST, handleSetQR);
  server.on("/settings/timezone", HTTP_POST, handleSetTimezone);

  server.on("/upload/image", HTTP_POST, [](){ server.send(200, "text/plain", "OK"); }, handleImageUpload);
  server.on("/upload/video", HTTP_POST, [](){ server.send(200, "text/plain", "OK"); }, handleVideoUpload);
  server.on("/video/config", HTTP_POST, handleVideoConfig);
  server.on("/video/clear", HTTP_POST, handleVideoClear);

  server.on("/pomodoro/config", HTTP_POST, handlePomoConfig);
  server.on("/pomodoro/start", HTTP_POST, [](){ pomodoroStart(); server.send(200, "text/plain", "OK"); });
  server.on("/pomodoro/pause", HTTP_POST, [](){ pomodoroPause(); server.send(200, "text/plain", "OK"); });
  server.on("/pomodoro/resume", HTTP_POST, [](){ pomodoroResume(); server.send(200, "text/plain", "OK"); });
  server.on("/pomodoro/reset", HTTP_POST, [](){ pomodoroReset(); server.send(200, "text/plain", "OK"); });
  server.on("/pomodoro/resetCount", HTTP_POST, handlePomoResetCount);

  server.on("/stopwatch/start", HTTP_POST, handleStopwatchStart);
  server.on("/stopwatch/stop", HTTP_POST, handleStopwatchStop);
  server.on("/stopwatch/lap", HTTP_POST, handleStopwatchLap);
  server.on("/stopwatch/reset", HTTP_POST, handleStopwatchReset);

  server.on("/notify", HTTP_POST, handleNotify);
  server.on("/notify", HTTP_GET, handleNotify); // lets a plain URL (NFC tag, bookmark, QR) trigger one too

  server.on("/showConnectQR", HTTP_POST, handleShowConnectQR);
  server.on("/wifi/reset", HTTP_POST, handleWifiForget);

  server.onNotFound([](){ server.send(404, "text/plain", "Not found"); });
}

// ---------------------------------------------------------------------
// PROVISIONING-MODE WEB HANDLERS
// ---------------------------------------------------------------------
void handleProvisionRoot() {
  server.send_P(200, "text/html", PROV_HTML);
}

void handleWifiScan() {
  // ESP32 has one radio, so scanning while the setup hotspot is actively
  // broadcasting is occasionally flaky -- retry a couple of times before
  // giving up, and use a longer per-channel dwell time to catch more
  // networks.
  int n = -100;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt > 0) delay(300);
    n = WiFi.scanNetworks(false, false, false, 400);
  }

  if (n < 0) {
    Serial.print("WiFi scan failed after retries, code: ");
    Serial.println(n);
  }

  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleProvisionSave() {
  String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID required");
    return;
  }
  prefs.putString("wifiSSID", ssid);
  prefs.putString("wifiPass", pass);
  server.send(200, "text/plain", "Saved. Restarting...");
  delay(800);
  ESP.restart();
}

void setupProvisioningRoutes() {
  server.on("/", HTTP_GET, handleProvisionRoot);
  server.on("/generate_204", HTTP_GET, handleProvisionRoot);      // Android captive portal check
  server.on("/hotspot-detect.html", HTTP_GET, handleProvisionRoot); // iOS captive portal check
  server.on("/ncsi.txt", HTTP_GET, handleProvisionRoot);           // Windows captive portal check
  server.on("/scan", HTTP_GET, handleWifiScan);
  server.on("/provision/save", HTTP_POST, handleProvisionSave);
  server.onNotFound(handleProvisionRoot); // catch-all redirect for captive portal pop-up
}

// ---------------------------------------------------------------------
// WIFI CONNECT HELPER
// ---------------------------------------------------------------------
const char *wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "idle (hasn't started trying yet)";
    case WL_NO_SSID_AVAIL: return "network not found (can't see that SSID at all -- check range/power/2.4GHz)";
    case WL_SCAN_COMPLETED: return "scan completed, still connecting";
    case WL_CONNECTED: return "connected";
    case WL_CONNECT_FAILED: return "connect failed (often a wrong password)";
    case WL_CONNECTION_LOST: return "connection lost mid-handshake";
    case WL_DISCONNECTED: return "disconnected/timed out";
    default: return "unknown status";
  }
}

bool connectToWiFi(const String &ssid, const String &pass, unsigned long timeoutMs) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting to WiFi:");
  display.println(ssid);
  display.display();

  Serial.print("Connecting to WiFi SSID '");
  Serial.print(ssid);
  Serial.println("'...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  wl_status_t finalStatus = WiFi.status();
  if (finalStatus == WL_CONNECTED) {
    Serial.print("Connected, RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.print("WiFi connect failed. Status code ");
    Serial.print((int)finalStatus);
    Serial.print(": ");
    Serial.println(wifiStatusText(finalStatus));
  }
  return finalStatus == WL_CONNECTED;
}

// ---------------------------------------------------------------------
// SETUP / LOOP
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  prefs.begin("cfg", false);
  loadSettings();
  textIsUnicode = !isAsciiOnly(textContent);

  Wire.begin(21, 22);
  // (rewrapText() is called further below, once the Unicode font is ready)
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
  }
  // We always position text ourselves (clock, pomodoro, wrapped/paginated
  // text, scrolling text, etc.), so the built-in auto-wrap must stay off --
  // otherwise long scrolling text runs past the right edge, auto-wraps onto
  // a second line at a shifting point, and looks garbled/broken.
  display.setTextWrap(false);

  // Set up the Unicode text renderer on top of the same display buffer.
  u8g2_font.begin(display);
  u8g2_font.setFontMode(1); // transparent background
  u8g2_font.setFontDirection(0);
  u8g2_font.setForegroundColor(SSD1306_WHITE);
  u8g2_font.setBackgroundColor(SSD1306_BLACK);
  u8g2_font.setFont(u8g2_font_unifont_t_symbols);
  rewrapText();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

#if BUZZER_PIN >= 0
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  buzzerTone(1800, 100); // quick self-test beep so you can confirm wiring; remove if unwanted
#endif

  String savedSSID = prefs.getString("wifiSSID", "");
  String savedPass = prefs.getString("wifiPass", "");

  bool connected = false;
  if (savedSSID.length() > 0) {
    connected = connectToWiFi(savedSSID, savedPass, 15000);
  }

  if (connected) {
    appState = STATE_NORMAL;
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    websiteURL = "http://" + WiFi.localIP().toString() + "/";

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi connected");
    display.println(WiFi.localIP().toString());
    display.display();
    delay(1500);

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    setupNormalRoutes();
    server.begin();
    Serial.println("HTTP server started");
  } else {
    appState = STATE_PROVISIONING;
    Serial.println("No/failed WiFi credentials -- starting setup hotspot");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(PROV_AP_SSID, PROV_AP_PASS);
    delay(200);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    setupProvisioningRoutes();
    server.begin();
    showProvisioningQR();
    Serial.print("Provisioning AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

void loop() {
  if (appState == STATE_PROVISIONING) {
    dnsServer.processNextRequest();
    server.handleClient();
    return;
  }

  server.handleClient();
  checkButton();

  // Show queued notifications one at a time, in the order they arrived.
  if (!notificationActive && notifyQueueCount > 0) {
    NotificationItem item = notifyQueue[notifyQueueHead];
    notifyQueueHead = (notifyQueueHead + 1) % NOTIFY_QUEUE_SIZE;
    notifyQueueCount--;

    notificationApp = item.app;
    notificationTitle = item.title;
    notificationMessage = item.message;
    notificationFile = item.file;
    notificationIsUnicode = !isAsciiOnly(item.app) || !isAsciiOnly(item.title) ||
                             !isAsciiOnly(item.message) || !isAsciiOnly(item.file);
    notificationActive = true;
    notificationNeedsRedraw = true;
    notificationExpireAt = millis() + item.duration;
    buzzerNotification();
  }

  // A notification, if active, overlays whatever mode is currently showing.
  if (notificationActive) {
    if (millis() > notificationExpireAt) {
      notificationActive = false;
      if (notifyQueueCount == 0) {
        // Nothing else queued -- restore whatever was showing before.
        clockForceRedraw = true;
        textNeedsRedraw = true;
        imageNeedsRedraw = true;
        qrNeedsRedraw = true;
      }
    } else {
      renderNotification();
      return;
    }
  }

  if (qrAutoRevert && currentMode == MODE_QR_WEBSITE && millis() > qrRevertAt) {
    currentMode = modeBeforeQRShortcut;
    qrAutoRevert = false;
    if (currentMode == MODE_CLOCK) clockForceRedraw = true;
    if (currentMode == MODE_TEXT) textNeedsRedraw = true;
    if (currentMode == MODE_IMAGE) imageNeedsRedraw = true;
    if (currentMode == MODE_QR) qrNeedsRedraw = true;
  }

  switch (currentMode) {
    case MODE_CLOCK: renderClock(); break;
    case MODE_TEXT: renderText(); break;
    case MODE_IMAGE: renderImage(); break;
    case MODE_VIDEO: renderVideo(); break;
    case MODE_QR: renderQR(); break;
    case MODE_QR_WEBSITE: renderQRWebsite(); break;
    case MODE_POMODORO: renderPomodoro(); break;
    case MODE_STOPWATCH: renderStopwatch(); break;
  }
}
