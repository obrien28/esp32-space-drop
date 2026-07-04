# esp32-space-drop
Space Drop Game (by Chad Kapper) ported to ESP32

![breadboard](images/esp32_space_drop_breadboard.jpg)

## Hardware Setup

- ESP32S3 or similar XIAO board
- SH1106 type OLED connected to SDA,SCL
- buttonPin1 D7 (button should connect to GND when pressed, add a 10k pullup)
- 10k pot A0 (connect wiper of pot to A0, other two pins to GND and VCC)
- buzzer/speaker D8 (connect with a series resistor around 100-220ohm)

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
3. Build and upload with the `seeed_xiao_esp32s3` environment (defined in [platformio.ini](platformio.ini)).

Dependencies (Adafruit GFX Library, Adafruit SH110X, Adafruit BusIO) and the ESP32 board platform are declared in `platformio.ini` and installed automatically — no manual library or board manager setup required.

Source code lives in [src/main.cpp](src/main.cpp).

### CLI usage

```sh
pio run              # build
pio run -t upload    # build and flash
pio device monitor    # serial monitor (9600 baud)
```
