# PicklePaddel — tweaking effects

You do **not** need to open the drivers. Change these header files and rebuild (`pio run`).

## Haptics (DRV2605 buzz patterns)

Edit **`include/paddle_fx.h`**.

Each line is one step:

- **`channel`** — TCA9548A mux channel **0–7** (which motor).
- **`effect`** — waveform number **0–123** (see Adafruit DRV2605 guide / TI waveform table). Examples: **1** strong click, **14** buzz, **47** sharp tick.
- **`holdMs`** — how long to wait after that effect before the next step (feel/timing).

There are three built-in arrays you can edit or duplicate:

- **`kFxBootHaptic`** — plays at startup.
- **`kFxBallHitHaptic`** — plays when the host sends a “swing hit”.
- **`kFxMenuTick`** — optional; use as a template for short taps.

To add a new sequence: copy an array, rename it, change the lines, then in **`src/main.cpp`** call:

`paddleFx_playSteps(gMux, kYourArray, PADDLE_FX_STEP_COUNT(kYourArray));`

## NeoPixel strip

**Size and brightness** — **`include/paddle_fx_config.h`**

- **`PADDLEFX_NEO_COUNT`** — number of LEDs.
- **`PADDLEFX_NEO_BRIGHTNESS`** — **0–255** (lower is safer on USB power).

**Pin** — **`include/pins.h`** → **`NEOPIXEL_PIN`**.

**Colors and motion** — **`include/paddle_fx_led.h`**

- **`fxNeoBootChase`** — green chase used at boot; change **`fxColor(r,g,b)`**, **`delay(35)`**, or the loop logic.
- **`fxNeoBallHit`** — flash used on “swing hit”; change **`fxColor`** and **`delay(80)`**.

Helper: **`fxColor(red, green, blue)`** in the same file.

If red/green are swapped, set **`PADDLEFX_NEO_ORDER`** in **`lib/NeoPixelStrip/NeoPixelStrip.h`** (e.g. **`NEO_RGB`** instead of **`NEO_GRB`**).

## Quick checklist

1. Edit the right `.h` file above.  
2. Save.  
3. **`pio run`** (and upload).  

No need to touch **`HapticMux`**, **`NeoPixelStrip`**, or **`main.cpp`** unless you add a **new** pattern name and want to trigger it from a new place in code.
