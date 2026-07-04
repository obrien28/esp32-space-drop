# esp32-space-drop
Space Drop Game (by Chad Kapper) ported to ESP32

![breadboard](images/esp32_space_drop_breadboard.jpg)

## Gameplay

Steer the gun with the potentiometer and fire at the falling objects. The game
now tracks **3 player lives**: a falling object that reaches the bottom is a
"miss" and costs a life, but landing a hit refills your lives — so only 3 misses
*in a row* end the game. Difficulty scales with your score (more objects, faster
drops), and the high score can be stored persistently in EEPROM (see build flags
below).

## Hardware Setup

Two board layouts are supported and selected at build time via the environment in
[platformio.ini](platformio.ini):

### Seeed XIAO ESP32-S3 (I²C OLED) — default `env:seeed_xiao_esp32s3`

- SH1106 type OLED connected to SDA, SCL
- buttonPin1 D7 (button should connect to GND when pressed, add a 10k pullup)
- 10k pot A0 (connect wiper of pot to A0, other two pins to GND and VCC)
- buzzer/speaker D8 (connect with a series resistor around 100-220ohm)

### ESP32-S3-DevKitC-1 (SPI OLED) — `env:esp32-s3-devkitc-1`

Uses an SPI OLED and a distinct pin map (built with the `USE_SPI_DISPLAY` flag).
Pins are defined in [include/pins.h](include/pins.h): RST=8, CLK=12, MOSI=11,
DC=18, CS=17. The pot, button, and buzzer pins are placeholders — update them in
`pins.h` to match your wiring.

## Parts List

**Reference**|**Value**|**Package**
:-----:|:-------------:|:-------------:
C1|0.1uF|Cer. Capacitor
R1|220|Resistor
R2|10K|Resistor
SP1|8-32ohm|12mm
SW1|SPST|BF-6
VR1|10K|3-pin
MCU|Seeed ESP32S3|custom
OLED|128x64 pixels,I2C|1.3in

## Software Setup

This project is built with [PlatformIO](https://platformio.org/) (VS Code extension or CLI).

1. Install PlatformIO.
2. Open this folder as a PlatformIO project.
3. Build and upload with the environment for your board (defined in [platformio.ini](platformio.ini)):
   - `esp32-s3-devkitc-1` — ESP32-S3-DevKitC-1 with SPI OLED (active/default)
   - `seeed_xiao_esp32s3` — Seeed XIAO ESP32-S3 with I²C OLED (commented out; uncomment to use)

Dependencies (Adafruit GFX Library, Adafruit SH110X, Adafruit BusIO) and the ESP32 board platform are declared in `platformio.ini` and installed automatically — no manual library or board manager setup required.

Source code lives in [src/main.cpp](src/main.cpp).

### Build flags

Behaviour is toggled with `build_flags` in [platformio.ini](platformio.ini):

- `USE_SPI_DISPLAY` — select the SPI OLED wiring + pin map
- `USE_EEPROM` — enable persistent high-score storage. Supported on ESP32-S3; remove it on boards without EEPROM support (e.g. XIAO SAMD21).
- `SERIAL_DEBUG_CONTROLS` — play over the Serial monitor with no pot/button wired up (see below).
- `ARDUINO_USB_CDC_ON_BOOT=1` — route Serial over the DevKitC's native USB port (it has no separate UART bridge chip).

### Serial debug controls

Build with `SERIAL_DEBUG_CONTROLS` to control the game entirely from the Serial
monitor — handy for testing without hardware wired up:

- **Left / Right arrow keys** (or `a` / `d` if your terminal doesn't forward arrow escape codes) — steer the gun
- **`f`** — fire

### CLI usage

```sh
pio run              # build
pio run -t upload    # build and flash
pio device monitor    # serial monitor (9600 baud)
```
