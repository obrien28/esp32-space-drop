#pragma once

#ifdef USE_SPI_DISPLAY
// ==================== esp32-s3-devkitc-1 ====================
// SPI OLED DISPLAY
// Verified by Liam April 23 2026
#define DISPLAY_RST_PIN  GPIO_NUM_8     // RST
#define DISPLAY_CLK_PIN GPIO_NUM_12     // CLK
#define DISPLAY_MOSI_PIN GPIO_NUM_11    // MOSI
#define DISPLAY_DC_PIN GPIO_NUM_18      // DC
#define DISPLAY_CS_PIN GPIO_NUM_17      // CS

const uint8_t flashPin = 10;

const int potPin = A0;      // Pin connected to potentiometer wiper
// Placeholder pins -- esp32-s3-devkitc-1's generic variant doesn't define
// D-style aliases like the XIAO board does. Update these once wired.
const int buttonPin1 = 4;   // Button for shooting
const int buzzerPin = 5;    // Buzzer for sound effects
const int controlPin = 6;   // Pin for Auto-shutoff functionality - NOTUSED!
#else
// ==================== seeed_xiao_esp32s3 ====================
// buttonPin1 D7
// SH1106 type OLED connected to SDA,SCL
// 10k pot A0
// buzzer/speaker D8

const uint8_t flashPin = 10;

const int potPin = A0;      // Pin connected to potentiometer wiper
const int buttonPin1 = D7;  // Button for shooting
const int buzzerPin = D8;   // Buzzer for sound effects
const int controlPin = D3;  // Pin for Auto-shutoff functionality - NOTUSED!
#endif
