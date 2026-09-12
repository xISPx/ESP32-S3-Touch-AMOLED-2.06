# ESP32-S3-Touch-AMOLED-2.06 — Smartwatch Firmware

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/xISPx/ESP32-S3-Touch-AMOLED-2.06)](https://github.com/xISPx/ESP32-S3-Touch-AMOLED-2.06/releases)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)](https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm)
[![LVGL](https://img.shields.io/badge/UI-LVGL%209-0D8E30?logo=lvgl&logoColor=white)](https://lvgl.io/)
[![Stars](https://img.shields.io/github/stars/xISPx/ESP32-S3-Touch-AMOLED-2.06?style=flat&color=8b5cf6)](https://github.com/xISPx/ESP32-S3-Touch-AMOLED-2.06/stargazers)

English | [Русский](README.ru.md)

<!-- TODO: drop demo.gif here — 10–15 s of the watchface loop, max 5 MB -->

A working smartwatch firmware for the **Waveshare ESP32-S3-Touch-AMOLED-2.06**
board (ESP32-S3R8, 32 MB flash, 8 MB octal PSRAM, 2.06" AMOLED 410×502 CO5300
QSPI, FT3168 touch, AXP2101 PMU, PCF85063 RTC, QMI8658 IMU, ES8311 audio codec
with speaker and microphone).

Built with **PlatformIO + Arduino framework (core ≥ 3.2.0)**.

**The screen has rounded corners** (corner radius ≈ 38 px at 410×502): the whole
UI is designed around the corner cutouts — a floating app header (round "back"
button), watchface complications at the bottom center, padding in menus/chats;
nothing interactive lands in the corner zones.

**The board has 32 MB of flash** (verified with `esptool flash_id`: GigaDevice,
quad). The `partitions_32mb.csv` table gives a 6.25 MB app + ~25.7 MB SPIFFS
(headroom for on-device music/content storage) + NVS/PHY at standard offsets
(settings survive re-flashing).

---

## Features

| Feature | Implementation |
|---|---|
| Watchface | Clock/date (LVGL 9.3), battery arc, step counter, Wi-Fi/charging indicators |
| App launcher | Smartwatch-style launcher: swipe up/left from the watchface, 2-column icon grid, swipe right / button — back |
| Games | "Snake" (240×240 canvas, swipes + D-pad, acceleration, restart) and "2048" (swipes, score, auto game-over detection) |
| Image viewer | BMP (24/32-bit) from `/pictures` on the TF card: decoded into an RGB565 PSRAM buffer, scaled to the screen, swipe left/right |
| Steps app | Progress ring towards a 6000-step goal, km, kcal, live \|a\| chart (5 Hz). The pedometer is an adaptive algorithm: gravity vector filter → \|linear acceleration\| → peak detector with adaptive threshold → cadence windows of 0.5–3.6 steps/s → a "4 steps in a row" pattern against single jerks; the daily counter lives in NVS and resets at midnight |
| Stopwatch app | min:sec.ms, start/pause/reset, runs in the background |
| Status app | Real-time IMU, heap/PSRAM, RSSI, uptime |
| **AI assistant (XiaoZhi)** | **Official protocol and official board configuration** (main/boards/waveshare/esp32-s3-touch-amoled-2.06): WebSocket `hello` handshake, text queries (`listen/detect`), TTS sentences, push-to-talk voice mode (Opus 16 kHz/60 ms over a 24 kHz I2S duplex), TTS playback through the speaker, LLM emotions in the chat header |
| AI assistant app | Two modes: **FACE** — like the official client (a face with LLM emotions, status, tap = start/finish a voice conversation) and **CHAT** — bubbles, text, RU/EN keyboard, PTT button; a switch in the header |
| XiaoZhi activation | Official OTA flow: `POST api.tenclass.net/xiaozhi/ota/` → activation code on screen → link on xiaozhi.me → websocket url/token saved to NVS automatically |
| OpenAI-compatible backend | Alternative: any `/v1/chat/completions` endpoint (URL/key/model in settings), 4-turn history |
| **Music (TF card)** | WAV player (16-bit PCM, 8–48 kHz, mono/stereo) from the flash card: file list from `/` and `/music`, play/pause/prev/next, progress and time, volume; I2S is reconfigured to the file's sample rate, codecs stay at 256·fs ratios |
| On-device settings | A detailed UI built for the rounded screen: list with round badge icons, status cards, a "Save" pill at the bottom center (safe zone), the on-screen keyboard never covers buttons and closes by tapping empty space. Wi-Fi SSID/password, brightness (on the fly), backlight timeout, timezone (10 presets), AI backend + URL/token/key/model — all in NVS |
| Time | PCF85063 → system clock; SNTP correction; written back to the RTC; timezone from settings |
| On-screen keyboard | Russian (ЙЦУКЕН, lower/upper case) + English + symbols, En/Аа toggle |
| Fonts | Cyrillic: ui_font_ru_16/20/28 (Arial + FontAwesome5-Solid + LVGL symbols, lv_font_conv; verified coverage of ALL LVGL 9.3 glyphs, including keyboard backspace/newline), tools in `tools/fonts` |
| Fallback Wi-Fi AP | If the saved network is unreachable 3 times in a row or credentials are missing, the watch brings up an **"Watch-XXXX"** AP (XXXX = last 4 hex of the MAC, password `12345678`): connect and open `http://192.168.4.1/` — the same web setup panel. Once STA connects, the AP goes down |
| Web server (remote access) | On Wi-Fi: `http://<watch-ip>/` — status (battery/IP/uptime) and file upload from the browser: BMP → `/pictures`, WAV → `/music` (no need to pull the card). Requires disabling client isolation on the router |
| Shake-to-wake | A sharp shake (\|a\| > 2.2 g) wakes the dimmed screen |
| AXP2101 charging | Official profile: CV 4.1 V, 400 mA charge, 50 mA pre-charge, 25 mA termination |
| Power saving | Backlight dims after an idle timeout (configurable), PWR tap — brightness toggle, PWR 6 s — hardware off |
| Navigation | Swipes, BOOT button (watchface → menu → back), app header with a "back" button |

**Voice (PTT):** hold the green microphone button in the chat — the recording is
streamed to the server as binary Opus frames (v1, raw), the reply arrives as
text (STT + TTS sentences) and is spoken through the speaker. On-screen states:
"Speak…" → "Recognizing…" → transcript → answer. The chat header shows the
assistant's "face" (LLM emotions) and a spinner while the watch is talking.

## Official audio configuration (xiaozhi-esp32, 2.06 board)

The audio stack mirrors `main/boards/waveshare/esp32-s3-touch-amoled-2.06`:

* **ES8311** (0x18) — DAC (speaker); **ES7210** (0x40) — ADC, differential MIC1,
  24 dB; **AXP2101 ALDO1/ALDO2 3.3 V** — microphone power;
* I2S duplex **24 kHz**, 16-bit, stereo slots, MCLK 256·fs = 6.144 MHz
  (ES7210 doubles it to 12.288 MHz internally);
* 24 kHz capture → 2/3 resample → Opus 16 kHz (protocol); TTS is decoded at the
  `sample_rate` from hello (24 kHz) and played directly.

## AI first run

1. Settings → Wi-Fi → SSID/password → Save.
2. Open the "AI assistant". If the device is not yet linked to a xiaozhi.me
   account, the watch will call the OTA server itself and show an
   **activation code**.
3. Enter the code at [xiaozhi.me](https://xiaozhi.me) (Console → devices).
4. Hold the microphone or type a text — the connection happens with the token
   the server returns automatically.

## Build & flash

```bash
build.cmd                      # build (PIO)
build.cmd -t upload --upload-port COM3
monitor.cmd                    # USB CDC logs 115200
```

`build.cmd` sets up a clean Windows environment (including Git — PIO needs it
for the arduino-libopus git dependency). The toolchain is not found when run
from Git Bash directly.

Building from **VS Code** (the **PlatformIO IDE** extension) works the same:
Build/Upload from the PlatformIO panel. `platformio.ini` pins the pioarduino
platform **54.03.21-2** and wires in a real **esptool 5.0.2**
(`platform_packages`): the 54.03.21 release ships esptool 5.0.0-dev1, which
fails on the `bootloader.bin` step with the new click (≥ 8.2) inside the
extension environment, and the esptoolpy meta-package in 54.03.21-2 may
silently fail to unpack on PIO 6.2 — the direct zip solves both on any machine.

Verified: `RAM 53.1 %`, `Flash 31.0 %` (2.60 MB out of the 6.5 MB app
partition). Partitions: `partitions_32mb.csv`, memory type `qio_opi`. For the
first flash, hold **BOOT** while connecting USB.

---

## Architecture

```
  Core      config.hpp · Logger · EventBus (POD events, fan-out) · Error/Health
            I2cBus (mutex) · Settings (NVS: Wi-Fi/screen/timezone/AI)
  ─────────────────────────────────────────────────────────────────────────
  HAL       DisplayHal(CO5300) TouchHal(FT3168) PowerHal(AXP2101) RtcHal
            ImuHal(QMI8658) IoExpanderHal AudioHal(ES8311+ESP_I2S)
  ─────────────────────────────────────────────────────────────────────────
  Tasks     UiTask c1 p5      SensorTask c0 p3   PowerTask c0 p2   NetTask c0 p2   AiTask c0 p2
            LVGL+AppHost      50 Hz IMU+steps    battery+buttons   Wi-Fi/SNTP      XiaoZhi/OpenAI
            16-KB mail        publisher          debounce          SettingsChanged listen/opus/TTS
  ─────────────────────────────────────────────────────────────────────────
  UI        AppHost: slot 0 watchface · slot 1 menu · slots 2+ apps
            App (base): createChrome/header "back"/gestures/onBack
            WatchfaceApp · AppMenuScreen · StatusScreen · FitnessScreen ·
            StopwatchScreen · ChatScreen (PTT+RU/EN keyboard) · SettingsScreen
```

* **Events** — `Event` (POD, ~520 B because of AI text) is fanned out by the
  bus to all subscribers without locking. New: `SettingsChanged{mask}`
  (Wi-Fi/screen/timezone/AI) and `AiText{kind, text[512]}`
  (Query/Reply/Error/Stt/VoiceStart/VoiceStop/Info).
* **The only LVGL owner** is UiTask (single-threaded, `LV_USE_OS=NONE`).
  DIRECT rendering: empty `flush_cb` + a full-frame `draw16bitRGBBitmap` from
  the task loop (the vendor pattern for CO5300).
* **Audio** — AudioHal: ES8311 registers via Wire (the es8311 component port
  moved off legacy I2C to share the bus with Wire 3.x), ESP_I2S STD mode,
  16 kHz/16-bit/stereo slots (BCLK 41, WS 45, DOUT 40, DIN 42, MCLK 16,
  PA = GPIO46). Server TTS (usually 24 kHz) is resampled 2/3 → 16 kHz.
* **Task stacks** are tuned by high-water-mark (the health monitor prints
  min-free every 10 s): ui 20 KB, sensor 14 KB, power 12 KB, net 16 KB,
  ai 20 KB. Gotcha: the event is now ~520 B — every task that puts an `Event`
  on the stack lost half a kilobyte.

## Project layout

```
platformio.ini                 # pioarduino core 3.2.x, LVGL 9.3, opus, WebSockets
include/
  lv_conf.h                    # + LV_FONT_CUSTOM_DECLARE (Cyrillic), 192 KB pool
  config/config.hpp            # pins, task topology, audio, AI
  core/…                       # Error, Event, Logger, I2cBus, TaskBase, Settings
  hal/…                        # + AudioHal.hpp (ES8311/I2S)
  tasks/…                      # + AiTask.hpp (XiaoZhi/OpenAI/voice)
  ui/App.hpp, AppHost.hpp      # app framework
  ui/…                         # Watchface/Menu/Status/Fitness/Stopwatch/Chat/Settings
src/ui/fonts/                   # ui_font_ru_{16,20,28}.c (lv_font_conv)
tools/fonts/                    # ttf + font generation command
tools/websocket.md              # official XiaoZhi protocol (docs/78/xiaozhi-esp32)
```

## Dependencies

GFX Library 1.6.0 · LVGL 9.3.0 · SensorLib 0.3.1 · XPowersLib 0.2.6 ·
ArduinoJson 7.x · WebSockets 2.x · [arduino-libopus](https://github.com/pschatzmann/arduino-libopus)

## Verified against the Waveshare wiki/demos

* Pins and addresses: LCD 4/5/6/7/11/12/8, I2C 15/14, TP 38/9, BOOT 0;
  PMU 0x34, touch 0x38, RTC 0x51, IMU 0x6B, ES8311 0x18.
* **CO5300 constructor gotcha:** the registry-GFX 1.6.0 has a 4th argument
  `bool ips` — pass `false` explicitly (otherwise you get pink garbage on
  screen).
* No XL9555 on this revision (the module degrades; the PWR button is read via
  the AXP2101 IRQ: PKEY_SHORT/LONG, 6 s hold = off).
* Audio follows the vendor `08_ES8311`: I2S STD 16-bit stereo, MCLK 256·fs
  from pin 16, PA GPIO46; ES8311 re-initialized on Wire.

## Known limitations

* Music is WAV-only (16-bit PCM); MP3/FLAC are not decoded.
* Voice works only with the XiaoZhi backend (the OpenAI backend is text-only).
* No deep-sleep.
* BT is unused.
* AEC/noise suppression is on the server side (the device sends raw opus).

## License

Project code — [MIT](LICENSE). Third-party fonts in `tools/fonts` (Arial,
FontAwesome) and dependency libraries are subject to their own licenses.
