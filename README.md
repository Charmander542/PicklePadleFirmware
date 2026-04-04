# PicklePaddle

Firmware for an ESP32 “paddle” build: **BNO055** IMU, **TCA9548A** I²C mux with **DRV2605** haptics and a **0.91″ OLED**, **NeoPixel** strip, **PWM speaker**, **SD card** logging, **Wi‑Fi** (captive portal + STA), and **UDP** to a game host.  

Stack: **PlatformIO**, **Arduino + ESP-IDF** hybrid (`platformio.ini`), **FreeRTOS** (network on one core, UI/sensors on the other).

---

## Install PlatformIO

You can use either the **CLI** or **VS Code**.

### Option A — PlatformIO CLI (recommended for terminals)

1. Install **Python 3** from [python.org](https://www.python.org/downloads/) and ensure `python` / `pip` work in a terminal.
2. Install the core:

   ```bash
   pip install -U platformio
   ```

3. Confirm:

   ```bash
   pio --version
   ```

### Option B — VS Code / Cursor

1. Install the **PlatformIO IDE** extension in the editor.
2. Open this project folder as the workspace; the extension bundles the CLI and will offer to build/upload from the status bar.

### Windows note: paths with spaces

ESP-IDF tooling used by this project can fail if PlatformIO’s packages live under a user folder with **spaces** (for example `C:\Users\First Last\`). On Windows, this repo used to set **`core_dir = C:/.platformio`** in **`platformio.ini`** so tools install on **`C:\.platformio`**. On macOS, leave `core_dir` unset and use PlatformIO’s default install location (typically `~/.platformio`).

---

## Project layout

| Path | Role |
|------|------|
| **`platformio.ini`** | Board (`esp32dev`), framework, upload speed, library deps, `core_dir`, `extra_script.py`. |
| **`extra_script.py`** | Adds `include/`, `lib/OLED091/`, `lib/HapticMux/` to the compiler include path. |
| **`src/main.cpp`** | Startup: probes, Wi‑Fi, tasks, idle/gameplay loop. |
| **`src/app_state.cpp`** | Shared mode, queues, host IP stored in NVS. |
| **`src/CMakeLists.txt`** | ESP-IDF component glob for `src/` (used with the hybrid framework). |
| **`include/pins.h`** | GPIO/I²C: single bus **`BUS_SDA` / `BUS_SCL`**, mux, NeoPixel pin, button (**BOOT / GPIO0**), SD SPI, speaker PWM. |
| **`include/app_config.h`** | Wi‑Fi NVS keys, UDP ports, jerk/button timing defaults. |
| **`include/app_state.h`** | `RunMode`, FreeRTOS queue handles. |
| **`include/jerk_detect.h`** | Impulse/jerk helper from linear accel. |
| **`include/paddle_fx_config.h`** | NeoPixel **count** and **brightness**. |
| **`include/paddle_fx_led.h`** | NeoPixel **animations** (colors, delays). |
| **`include/paddle_fx.h`** | **Haptic** step tables (DRV effects + mux channel). |
| **Adafruit BNO055** (PIO lib) | IMU over **`Wire`**; I2C addr `kBno055I2cAddr` in `app_config.h` (0x29 = SA0 high). |
| **`lib/HapticMux/`** | TCA9548A + **Adafruit DRV2605**. |
| **`lib/DisplayManager/`** | Waveshare-style **0.91″ OLED** via **`lib/OLED091/`**. |
| **`lib/NeoPixelStrip/`** | **Adafruit NeoPixel** wrapper. |
| **`lib/SpeakerDriver/`** | PWM tones (LEDC). |
| **`lib/SdLogger/`** | Append log on SD (`/paddle.log`). |
| **`lib/WifiPortal/`** | AP + web form if no STA credentials. |
| **`lib/NetUdp/`** | UDP RX/TX (serviced on the network task). |
| **`.pio/`** | Build output (generated; safe to delete for a clean build). |

---

## Common commands

Open a terminal **in the project root** (the folder that contains **`platformio.ini`**).

| Task | Command |
|------|---------|
| Build only | `pio run` |
| Build and upload | `pio run -t upload` |
| Serial monitor (115200 baud) | `pio device monitor` |
| Upload then monitor (common workflow) | `pio run -t upload && pio device monitor` |
| List USB serial ports | `pio device list` |
| Clean build files | `pio run -t clean` |

**Specific environment** (if you add more `[env:...]` blocks later):

```bash
pio run -e esp32dev -t upload
```

**Wrong port on Windows:** if upload fails, pass the COM port (Device Manager):

```bash
pio run -t upload --upload-port COM5
```

**Monitor on a specific port:**

```bash
pio device monitor --port COM5
```

---

## First-time build

1. Clone or copy the repo and `cd` into it.
2. Run **`pio run`**. PlatformIO will install the Espressif platform, toolchains, and libraries from **`lib_deps`** (first run can take several minutes).
3. Connect the board, run **`pio run -t upload`**, then **`pio device monitor`** to see boot logs.

---

## Tweaking effects (no driver surgery)

You do **not** need to open **`HapticMux`** or **`NeoPixelStrip`** for normal tuning—only the headers below, then **`pio run`** (and upload).

### Haptics (DRV2605)

Edit **`include/paddle_fx.h`**.

Each line is one step:

- **`channel`** — TCA9548A mux channel **0–7**.
- **`effect`** — waveform **0–123** (Adafruit DRV2605 / TI table). Examples: **1** strong click, **14** buzz, **47** sharp tick.
- **`holdMs`** — pause after that step before the next.

Built-in arrays:

- **`kFxBootHaptic`** — startup.
- **`kFxBallHitHaptic`** — host “swing hit”.
- **`kFxMenuTick`** — template for short taps.

To add a new sequence: copy an array, rename it, then in **`src/main.cpp`** call:

`paddleFx_playSteps(gMux, kYourArray, PADDLE_FX_STEP_COUNT(kYourArray));`

### NeoPixel strip

- **Count / brightness** — **`include/paddle_fx_config.h`** (`PADDLEFX_NEO_COUNT`, `PADDLEFX_NEO_BRIGHTNESS`).
- **Data pin** — **`include/pins.h`** → **`NEOPIXEL_PIN`**.
- **Colors / motion** — **`include/paddle_fx_led.h`** (`fxNeoBootChase`, `fxNeoBallHit`, helper **`fxColor`**).
- **Wrong R/G order** — **`lib/NeoPixelStrip/NeoPixelStrip.h`** → **`PADDLEFX_NEO_ORDER`** (e.g. `NEO_RGB` vs `NEO_GRB`).

### Quick checklist

1. Edit the `.h` file you need.  
2. Save.  
3. **`pio run -t upload`** (and **`pio device monitor`** if you want to verify).  

---

## License / upstream

Vendor-derived OLED code lives under **`lib/OLED091/`** (Waveshare-style 0.91″ driver, adapted for ESP32 I²C). Respect their license headers in those files.
