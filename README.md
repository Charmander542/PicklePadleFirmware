# PicklePaddle

Firmware for an ESP32 "paddle" build: **BNO055** IMU, **TCA9548A** I²C mux with **DRV2605** haptics and a **0.91″ OLED**, **NeoPixel** strip, **PWM speaker**, **SD card** logging, and **ESP-NOW** wireless in a star topology.

Stack: **PlatformIO**, **Arduino + ESP-IDF** hybrid (`platformio.ini`), **FreeRTOS** (network task on core 0, UI/sensors on core 1).

---

## ESP-NOW Star Topology Architecture

```
        ┌──────────────────────────────────────────┐
        │  PC / Game Host                          │
        │  (USB serial — 115 200 baud)             │
        └───────────────┬──────────────────────────┘
                        │ serial
        ┌───────────────▼──────────────────────────┐
        │  Central Node  (ESP32 dev board)         │
        │  central_node/                           │
        │  • Bridges serial ↔ ESP-NOW              │
        │  • Maintains node-ID → MAC table (NVS)   │
        └───────┬──────────┬──────────┬────────────┘
       ESP-NOW  │ ESP-NOW  │ ESP-NOW  │  …
        ┌───────▼──┐ ┌─────▼────┐ ┌──▼───────┐
        │ Node 0   │ │ Node 1   │ │ Node N   │
        │ Paddle   │ │ Paddle   │ │ Paddle   │
        └──────────┘ └──────────┘ └──────────┘
```

### How it works

| Layer | Detail |
|-------|--------|
| **Transport** | ESP-NOW (802.11 Wi-Fi radio, no AP or DHCP needed). All nodes and the central node use the **broadcast MAC** (`FF:FF:FF:FF:FF:FF`) so bring-up requires no MAC provisioning. |
| **Addressing** | Each packet carries a 1-byte `src_id` and `dst_id`. Nodes filter on `dst_id`; the central routes on `src_id`. `0xFF` in `dst_id` = broadcast to all nodes. |
| **Node → Central** | Paddle sends `EspNowPacket{src_id=nodeId, dst_id=0xFF, text="…"}` via broadcast. |
| **Central → Node** | After a node is registered (auto or manual) the central sends unicast to that node's MAC with `dst_id` set to the target node ID. |
| **Serial protocol** | See table below. |

### Serial protocol (PC ↔ Central Node)

| Direction | Format | Meaning |
|-----------|--------|---------|
| PC → Central | `<id>:<message>` | Send `message` to paddle `id`. |
| PC → Central | `*:<message>` or `255:<message>` | Broadcast to every registered paddle. |
| PC → Central | `reg <id> AA:BB:CC:DD:EE:FF` | Register a paddle's MAC for unicast delivery (persisted to NVS). |
| PC → Central | `unreg <id>` | Remove a paddle from the peer table. |
| PC → Central | `list` | Print all registered nodes. |
| PC → Central | `mac` | Print the central node's own MAC address. |
| Central → PC | `<id>:<message>` | Data from paddle `id` (jerk impulse, CSV row, button event, …). |
| Central → PC | `[cn] …` | Status / error messages from the central node. |

> **Auto-registration:** The first time a paddle sends data, the central node automatically registers its MAC and prints a `[cn] auto-registering` line. This means paddles start streaming data immediately without any manual `reg` step; `reg` is only needed when the central must *send first* (e.g., setting game mode before the paddle has booted).

### Incoming commands (Central → Paddle)

These are the same text strings that the old UDP host sent; no game-host changes are needed:

| Message | Action |
|---------|--------|
| `swing hit` | Trigger ball-hit haptics + LED + speaker. |
| `idle` | Switch to Idle mode, show "Idle / Node ID: N" on OLED. |
| `gameplay` | Switch to Gameplay mode (jerk-detect IMU streaming). |
| `tutorial` | Switch to Tutorial mode (high-rate CSV IMU streaming). |

### Setting a node's ID

Each paddle stores its node ID as a `uint8` in NVS (key `node_id`, namespace `paddle`). The default is `0`. To assign a different ID, write it with `Preferences` via a one-time sketch, or use the Arduino serial monitor at boot to send the setting using a custom flash tool.

**Long-hold reset:** In Idle mode, hold the button for ≥ 8 s to clear the stored node ID (resets to 0) and reboot.

---

## Project layout

| Path | Role |
|------|------|
| **`platformio.ini`** | Board (`esp32dev`), framework, upload speed, library deps, `extra_script.py`. |
| **`extra_script.py`** | Adds `include/`, `lib/OLED091/`, `lib/HapticMux/` to the compiler include path. |
| **`src/main.cpp`** | Startup: probes, ESP-NOW init, tasks, idle/gameplay loop. |
| **`src/app_state.cpp`** | Shared mode, FreeRTOS queues. |
| **`src/CMakeLists.txt`** | ESP-IDF component glob for `src/` (used with the hybrid framework). |
| **`include/pins.h`** | GPIO/I²C: single bus **`BUS_SDA` / `BUS_SCL`**, mux, NeoPixel, button, SD SPI, speaker. |
| **`include/app_config.h`** | NVS keys, jerk/button timing constants. |
| **`include/app_state.h`** | `RunMode`, `UiEvent`, FreeRTOS queue handles. |
| **`include/jerk_detect.h`** | Impulse/jerk helper from linear accel. |
| **`include/paddle_fx_config.h`** | NeoPixel **count** and **brightness**. |
| **`include/paddle_fx_led.h`** | NeoPixel **animations** (colors, delays). |
| **`include/paddle_fx.h`** | **Haptic** step tables (DRV effects + mux channel). |
| **`lib/EspNowNet/`** | ESP-NOW transport replacing the old `NetUdp`. Handles broadcast peer, TX queue drain, RX dispatch to `g_uiEventQueue`. |
| **`lib/HapticMux/`** | TCA9548A + **Adafruit DRV2605**. |
| **`lib/DisplayManager/`** | Waveshare-style **0.91″ OLED** via **`lib/OLED091/`**. |
| **`lib/NeoPixelStrip/`** | **Adafruit NeoPixel** wrapper. |
| **`lib/SpeakerDriver/`** | PWM tones (LEDC). |
| **`lib/SdLogger/`** | Append log on SD (`/PADDLE.LOG`). |
| **`lib/WifiPortal/`** | *(Unused in ESP-NOW build — kept for reference.)* |
| **`lib/NetUdp/`** | *(Unused in ESP-NOW build — kept for reference.)* |
| **`central_node/`** | Separate PlatformIO project for the central hub. |
| **`central_node/platformio.ini`** | Central node build config (plain Arduino, no ESP-IDF). |
| **`central_node/src/main.cpp`** | Serial ↔ ESP-NOW bridge, peer table, NVS persistence. |
| **`central_node/include/cn_config.h`** | `kMaxNodes`, `EspNowPacket` struct (must match `EspNowNet.h`). |
| **`.pio/`** | Build output (generated; safe to delete for a clean build). |

---

## Install PlatformIO

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

1. Install the **PlatformIO IDE** extension.
2. Open this project folder; the extension bundles the CLI and offers build/upload from the status bar.

### Windows note: paths with spaces

ESP-IDF tooling used by this project can fail if PlatformIO's packages live under a user folder with **spaces** (e.g. `C:\Users\First Last\`). Uncomment **`core_dir = C:/.platformio`** in the relevant `platformio.ini` (top-level or `central_node/`) to install tools on `C:\.platformio`.

---

## Building and flashing

### Paddle nodes

Open a terminal in the **project root** (the folder containing the top-level `platformio.ini`).

| Task | Command |
|------|---------|
| Build | `pio run` |
| Build + upload | `pio run -t upload` |
| Serial monitor | `pio device monitor` |
| Upload then monitor | `pio run -t upload` then `pio device monitor` |
| Production build | `pio run -e esp32dev_prod -t upload` |
| Clean | `pio run -t clean` |

### Central node

Open a terminal in **`central_node/`**.

| Task | Command |
|------|---------|
| Build | `pio run` |
| Build + upload | `pio run -t upload` |
| Serial monitor | `pio device monitor` |

**Wrong port on Windows:** pass the COM port explicitly:

```bash
pio run -t upload --upload-port COM5
pio device monitor --port COM5
```

---

## First-time bring-up

1. **Flash the central node** from `central_node/`:
   ```bash
   cd central_node
   pio run -t upload
   pio device monitor
   ```
   You will see:
   ```
   [cn] Central MAC: AA:BB:CC:DD:EE:FF
   [cn] Ready. 0 node(s) loaded from NVS.
   ```
   Note the central node's MAC address — you do **not** need it for most workflows (paddles auto-register on first contact), but it is useful for debugging.

2. **Flash each paddle node** from the project root:
   ```bash
   pio run -t upload
   pio device monitor
   ```
   On boot the paddle prints its MAC, initialises ESP-NOW, and shows `Idle / Node ID: 0` on the OLED.

3. **Assign unique node IDs** (required if you have more than one paddle). Write the desired ID to NVS once using a short helper sketch that calls:
   ```cpp
   Preferences p;
   p.begin("paddle", false);
   p.putUChar("node_id", 1);  // change to the desired ID
   p.end();
   ```
   Alternatively, hold the button for ≥ 8 s in Idle mode to reset the ID back to 0.

4. **Verify** by sending a command from the PC serial terminal:
   ```
   0:idle
   0:gameplay
   ```
   The paddle with node ID 0 should switch mode and show it on the OLED.

---

## Tweaking effects (no driver surgery)

You do **not** need to open **`HapticMux`** or **`NeoPixelStrip`** for normal tuning — only the headers below, then **`pio run`** (and upload).

### Haptics (DRV2605)

Edit **`include/paddle_fx.h`**.

Each line is one step:

- **`channel`** — TCA9548A mux channel **0–7**.
- **`effect`** — waveform **0–123** (Adafruit DRV2605 / TI table). Examples: **1** strong click, **14** buzz, **47** sharp tick.
- **`holdMs`** — pause after that step before the next.

Built-in arrays:

- **`kFxBootHaptic`** — startup.
- **`kFxBallHitHaptic`** — host "swing hit".
- **`kFxMenuTick`** — template for short taps.

To add a new sequence: copy an array, rename it, then call:

```cpp
paddleFx_playSteps(gMux, kYourArray, PADDLE_FX_STEP_COUNT(kYourArray));
```

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
