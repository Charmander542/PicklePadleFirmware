# IMU Visualizer

Real-time 3D visualization of a BNO055 IMU on an ESP32-class board.
The firmware streams quaternion data over USB serial and the Python viewer
renders an STL model rotating to match.

## Hardware

- **ESP32** (Dev Module) or **ESP32-S3** (DevKitC-1)
- Adafruit BNO055 breakout (or compatible)
- I2C: edit `firmware/include/pins.h` for your wiring.
  - Default **classic ESP32** dev module: SDA **21**, SCL **22** (same as the main paddle `pins.h` dev comment). PCB bus **26 / 27** if that matches your board.
  - **ESP32-S3** DevKitC-1 is often **8 / 9** — change `pins.h` when using S3.
- BNO055 I2C address defaults to **0x29** (matches the main paddle firmware). Use **0x28** if SA0 is low.

## Firmware

Built with PlatformIO (Arduino framework). Pick the env that matches your chip:

```
cd firmware
pio run -e esp32dev -t upload      # classic ESP32
pio run -e esp32s3 -t upload       # ESP32-S3
pio device monitor
```

Classic ESP32 must not block forever in `setup()` waiting on USB serial, or the
task watchdog can reset in a tight loop (`TG1WDT_SYS_RESET`). This sketch only
waits on `Serial` when native USB CDC on boot is enabled; otherwise it uses a
short delay after `Serial.begin`.

Serial output format (115200 baud):
```
Q,w,x,y,z,euler_x,euler_y,euler_z,cal_sys,cal_gyro,cal_accel,cal_mag
```

## Python Visualizer

The viewer uses **GLFW** and **PyOpenGL** (not pygame) so `pip install` works on
recent Python on Windows without compiling pygame from source.

```
cd visualizer
pip install -r requirements.txt

# Basic — renders a default box shape
python imu_viewer.py COM5

# With your own STL model
python imu_viewer.py COM5 --stl ../path/to/paddle.stl

# Linux / macOS
python imu_viewer.py /dev/ttyUSB0 --stl model.stl
```

### Controls

| Key   | Action                                             |
|-------|----------------------------------------------------|
| **R** | Reset orientation (current pose becomes "forward") |
| **ESC** | Quit                                             |

### Status

The **window title** shows connection state, calibration (0–3 per sensor),
and Euler angles (updated at most `--title-hz` times/sec to save CPU). Optional
`--console-hz N` prints quaternion/Euler to the terminal.

### Responsiveness

The viewer uploads the mesh to a **GPU VBO** (one `glDrawArrays` per frame) and
defaults to **V-Sync off** for the shortest motion-to-display latency. Use
`--vsync` if you prefer a capped, tear-free frame rate. Firmware streams IMU at
~100 Hz (`kStreamIntervalMs` in `firmware/src/main.cpp`).
