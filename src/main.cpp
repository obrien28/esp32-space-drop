//Space Drop Game for Arduino Nano or D1 Mini
//By: Chad Kapper @HackMakeMod https://hackmakemod.com/
//
//=== ADJUSTABLE GAME PARAMETERS ===
// isPotInverted: Set false if gun moves opposite direction from pot
// bulletAmount: Max bullets allowed on screen (default 5)
// circleAmount: Max falling objects allowed (default 8)
// starAmount: Background stars (default 20)
// fireDelay: Milliseconds between shots (default 200)
// maxLives: Consecutive misses allowed before game over (default 3).
//           Hitting a circle refills lives, so misses must be back-to-back.
// Difficulty scaling: Score 0-100 increases active circles from 3→8 and speeds scale up
//===================================

//
// ported to XIAO ESP32 board with the following hardware interface:
//  - buttonPin1 D7
//  - SH1106 type OLED connected to SDA,SCL
//  - 10k pot A0
//  - buzzer/speaker D8
//
// note: EEPROM (persistent high score) is gated behind the USE_EEPROM build
//       flag in platformio.ini -- not all boards support it (e.g. XIAO SAMD21)
// note: auto-shutoff is ignored
//
// tested on XIAO SAM21, XIAO ESP32S3
// -rolf  feb 2026
//
// debug gotcha:
// Wire.getClock() is not implemented on XIAO SAMD21.  Weird.
//  was trying to see if I can run I2C faster than default.
//

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>

// some MCUs (like SAMD21) do not have separate eeprom
#ifdef USE_EEPROM
#include <EEPROM.h>
#endif
#include <math.h>

#include "pins.h"

const uint8_t SCREEN_WIDTH = 128;
const uint8_t SCREEN_HEIGHT = 64;
//Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#ifdef USE_SPI_DISPLAY
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, DISPLAY_DC_PIN, DISPLAY_RST_PIN, DISPLAY_CS_PIN);
#else
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#endif

#define I2C_ADDRESS 0x3C //initialize with the I2C addr 0x3C (try 0x3D if display is blank)

// quick hack to fix missing color constant
#define SSD1306_WHITE SH110X_WHITE

const bool isPotInverted = false;  // default is false!
const uint8_t EEPROM_SIZE = 16;  // Increased from 8

// Forward declarations (Arduino IDE auto-generates these; PlatformIO's .cpp build does not)
int readPotValue();
#ifdef SERIAL_DEBUG_CONTROLS
void handleSerialDebugInput();
#endif
void checkButtonState();
void updateGunPosition();
void generateBullet();
void updateBullets(float deltaTime);
void updateCircles(float deltaTime);
void generateCircles();
void generateStars();
void generateMountains();
void checkCollisions();
void drawExplosion(int x, int y, int radius);
void drawHeart(int cx, int cy);
void updateAndDrawExplosions();
void drawGame();
void drawStars();
void drawMountains();
void writeIntIntoEEPROM(int address, int number);
int readIntFromEEPROM(int address);
void setIgnoreInputForDuration(unsigned long duration);
void updateIgnoreInputTimer();
void UpdateMenuScreenAfterInputAllowed();
void InitializeHighScore();
void UpdateHighScore();
void ResetHighScore();
void TitleScreenSequence();
void gameOverSequence();
void ResetScoresSequence();
void resetGame();

#ifdef SERIAL_DEBUG_CONTROLS
// Simulated pot reading, driven by arrow keys / a,d over Serial (see handleSerialDebugInput)
int debugPotValue = 511;
// Fine step for a single tap; step ramps up toward the max while a direction
// is held (i.e. repeated key events arrive faster than debugRepeatWindow),
// so short taps give precise control and holding the key still crosses the
// screen quickly.
const int debugPotStepMin = 12;
const int debugPotStepMax = 140;
const int debugPotStepRamp = 10;
const unsigned long debugRepeatWindow = 150;  // ms; faster than this = "held"
int debugPotStep = debugPotStepMin;
unsigned long lastDebugMoveTime = 0;
int8_t lastDebugDirection = 0;  // -1 left, +1 right, 0 = none yet
// Set for one checkButtonState() cycle to simulate a physical button press+release
bool debugFirePulse = false;

// Advances debugPotValue by one step in the given direction (-1 or +1),
// accelerating the step size on rapid repeats and resetting it on a fresh
// tap or direction change.
void applyDebugMove(int8_t direction) {
  unsigned long now = millis();
  if (direction == lastDebugDirection && (now - lastDebugMoveTime) < debugRepeatWindow) {
    debugPotStep = min(debugPotStep + debugPotStepRamp, debugPotStepMax);
  } else {
    debugPotStep = debugPotStepMin;
  }
  lastDebugDirection = direction;
  lastDebugMoveTime = now;
  debugPotValue = constrain(debugPotValue + direction * debugPotStep, 0, 1023);
}

// Reads bytes from Serial and updates debugPotValue / debugFirePulse.
// Recognizes ANSI arrow-key escape sequences (ESC [ C / ESC [ D) as well as
// plain 'a'/'d' as a fallback for terminals that don't forward arrow keys raw,
// and 'f' to fire.
void handleSerialDebugInput() {
  static uint8_t escState = 0;  // 0 = idle, 1 = saw ESC, 2 = saw ESC [

  while (Serial.available() > 0) {
    int c = Serial.read();

    if (escState == 1) {
      escState = (c == '[') ? 2 : 0;
      continue;
    }
    if (escState == 2) {
      if (c == 'D') {  // left arrow
        applyDebugMove(-1);
        Serial.println("[debug] LEFT arrow");
      } else if (c == 'C') {  // right arrow
        applyDebugMove(1);
        Serial.println("[debug] RIGHT arrow");
      }
      escState = 0;
      continue;
    }

    if (c == 0x1B) {  // ESC
      escState = 1;
    } else if (c == 'a' || c == 'A') {
      applyDebugMove(-1);
      Serial.println("[debug] 'a' (left)");
    } else if (c == 'd' || c == 'D') {
      applyDebugMove(1);
      Serial.println("[debug] 'd' (right)");
    } else if (c == 'f' || c == 'F') {
      debugFirePulse = true;
      Serial.println("[debug] 'f' (fire)");
    }
  }
}
#endif

int readPotValue() {
#ifdef SERIAL_DEBUG_CONTROLS
  int potValue = debugPotValue;
#else
  int potValue = analogRead(potPin);
#endif

  if (isPotInverted) {
    potValue = map(potValue, 0, 1023, 1023, 0);
  }

  return potValue;
}

// enable with the USE_EEPROM build flag (platformio.ini) on boards that
// support it (e.g. ESP32); not available on XIAO SAMD21
#ifdef USE_EEPROM
#define initEEPROM() EEPROM.begin(EEPROM_SIZE)
#define commitEEPROM() EEPROM.commit()
#else
#define initEEPROM()
#define commitEEPROM()
#endif

unsigned int scoreCount = 0;  // Global variable to keep track of the number of points
unsigned int savedHighScore;
const uint8_t scoreInitCode = 12;      // keep it if you want
const uint8_t EEPROM_SIG_ADDR = 4;     // 1 byte signature
const uint8_t EEPROM_SIG_VALUE = 0xA5; // signature value
uint8_t mountainPoints[19][2];  // Global array to store mountain points

//button properties
unsigned long buttonTimer = 0;
unsigned long inactivityTimer = 0;
const unsigned long longPressTime = 1200;
const unsigned long shutoffPressTime = 6000;
const unsigned long autoShutoffTime = 30000;
unsigned long ignoreInputStartTime = 0;  // To store the start time when ignoreInput is set to true
unsigned long ignoreInputDuration = 0;   // Duration to keep ignoreInput true
bool buttonActive = false;
bool longPressActive = false;
bool ignoreInput = false;  // Boolean flag to ignore input

// Button state tracking
bool lastButtonState = HIGH;
bool buttonPressed = false;  // True for one frame when button is pressed
bool buttonHeld = false;     // True while button is held

struct Bullet {
  float x, y;
  bool active;
};

const uint8_t circleRadius = 3;  // You can set the radius of the circles

struct Circle {
  float x, y, speed;
  bool active;
};

struct Star {
  uint8_t x;
  uint8_t y;
};

struct Explosion {
  uint8_t x, y;
  uint8_t framesLeft;  // Counts down to 0
  bool active;
};

// Define a structure for sound sequences
struct SoundSequence {
  uint16_t frequency;  // Frequency of the tone
  uint16_t duration;   // Duration to play the tone
};

const SoundSequence explosionSequence[] = {
  { 500, 75 },  // Frequency in Hz, Duration in milliseconds
  { 400, 50 },
  { 300, 50 },
  { 200, 25 }
};
const uint8_t explosionSequenceLength = sizeof(explosionSequence) / sizeof(SoundSequence);

SoundSequence bulletSequence[] = {
  { 2500, 25 },  // Frequency in Hz, Duration in milliseconds
  { 1000, 25 },
};
const uint8_t bulletSequenceLength = sizeof(bulletSequence) / sizeof(SoundSequence);

const SoundSequence crashSequence[] = {
  { 800, 100 },
  { 600, 100 },
  { 400, 150 },
  { 200, 200 }
};
const uint8_t crashSequenceLength = sizeof(crashSequence) / sizeof(SoundSequence);

struct SoundManager {
  const SoundSequence* sequence;
  const uint8_t sequenceLength;
  unsigned long toneStartTime = 0;
  bool isSequencePlaying = false;
  int8_t currentToneIndex = -1;

  SoundManager(const SoundSequence* seq, uint8_t length)
    : sequence(seq), sequenceLength(length) {}

  void playSequence() {
    currentToneIndex = 0;
    toneStartTime = millis();
    isSequencePlaying = true;
    tone(buzzerPin, sequence[0].frequency, sequence[0].duration);
  }

  void updateSequence() {
    if (isSequencePlaying && currentToneIndex >= 0 && currentToneIndex < sequenceLength) {
      if (millis() - toneStartTime >= sequence[currentToneIndex].duration) {
        currentToneIndex++;
        if (currentToneIndex < sequenceLength) {
          toneStartTime = millis();
          tone(buzzerPin, sequence[currentToneIndex].frequency, sequence[currentToneIndex].duration);
        } else {
          noTone(buzzerPin);
          isSequencePlaying = false;
        }
      }
    }
  }
};

SoundManager explosionSoundManager = SoundManager(explosionSequence, explosionSequenceLength);
SoundManager bulletSoundManager = SoundManager(bulletSequence, bulletSequenceLength);
SoundManager crashSoundManager = SoundManager(crashSequence, crashSequenceLength);

// Gun properties
uint8_t gunX = SCREEN_WIDTH / 2;
const uint8_t gunY = SCREEN_HEIGHT - 10;
const uint8_t gunWidth = 10;
const uint8_t gunHeight = 5;

// Game properties
const uint8_t bulletAmount = 5;
Bullet bullets[bulletAmount];  // Limited number of bullets on screen
const uint8_t circleAmount = 8;
Circle circles[circleAmount];  // Limited number of circles on screen
const uint8_t starAmount = 20;
Star stars[starAmount];  // Limited stars for background
const uint8_t explosionAmount = 8;
Explosion explosions[explosionAmount];  // Explosion effects
uint8_t maxCirclesAllowed = 3;

// Lives: player has maxLives, loses one each time a circle reaches the bottom.
// A successful hit refills lives, so only 3 misses IN A ROW ends the game.
const uint8_t maxLives = 3;
uint8_t livesRemaining = maxLives;

bool gameOver = true;
uint8_t gameScreenState = 0;  //0 title screen -- 1 game -- 2 gameover -- 3 reset scores

// Fire rate control
unsigned long lastFireTime = 0;
const unsigned long fireDelay = 200;  // ms between shots (increased from 100)

void setup() {
  Serial.begin(9600);                 // Start serial communication at 9600 baud rate
  Serial.println("Space Drop - Starting...");
#ifdef SERIAL_DEBUG_CONTROLS
  Serial.println("SERIAL_DEBUG_CONTROLS enabled: Left/Right arrows (or a/d) to steer, f to fire.");
#endif

  pinMode(buttonPin1, INPUT_PULLUP);  // Initialize shooting button pin as input with internal pull-up
  pinMode(potPin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(controlPin, OUTPUT);
  digitalWrite(controlPin, HIGH);  // Ensure controlPin is HIGH on boot to keep the unit powered
  pinMode(flashPin, OUTPUT);
  digitalWrite(flashPin, LOW);

#ifdef USE_SPI_DISPLAY
  // Initialize SPI (custom pins) before display
  SPI.begin(DISPLAY_CLK_PIN, -1, DISPLAY_MOSI_PIN, DISPLAY_CS_PIN);
#else
  // Initialize I2C before display
  Wire.begin();
  // go faster
  Wire.setClock(400000);  // argh, this is ignored by ESP32
#endif

  initEEPROM();

  InitializeHighScore();

  delay(250); // wait for the OLED to power up
  display.begin(I2C_ADDRESS, true); // initialize OLED display

  // Initialize bullets and circles
  for (int i = 0; i < bulletAmount; i++) {
    bullets[i].active = false;
  }
  for (int i = 0; i < circleAmount; i++) {
    circles[i].active = false;
  }
  for (int i = 0; i < explosionAmount; i++) {
    explosions[i].active = false;
  }

  display.clearDisplay();
  display.display();
  delay(100);  // Pause for 0.1 seconds

  gameScreenState = 0;
  gameOver = true;
  TitleScreenSequence();

}

unsigned long lastFrameTime = 0;
unsigned long lastButtonCheckTime = 0;
const unsigned long buttonCheckInterval = 20;  // Check button every 20ms

void loop() {
  unsigned long currentMillis = millis();

#ifdef SERIAL_DEBUG_CONTROLS
  handleSerialDebugInput();
#endif

  // Check button at a fixed interval (not every frame)
  if (currentMillis - lastButtonCheckTime >= buttonCheckInterval) {
    checkButtonState();
    lastButtonCheckTime = currentMillis;
  }

  if (!gameOver) {
    uint32_t currentTime = micros();
    uint32_t deltaTimeMicros = currentTime - lastFrameTime;
    float deltaTime = deltaTimeMicros / 1000000.0f;
    // Constrain game speed between 30 and 60 fps for smoother gameplay
    deltaTime = constrain(deltaTime, 0.0166667f, 0.033333f);  // 30-60 FPS
    lastFrameTime = currentTime;

    updateGunPosition();

    // Only fire on button press, not hold
    if (buttonPressed) {
      generateBullet();
      buttonPressed = false;  // Reset for next frame
    }

    updateBullets(deltaTime);
    updateCircles(deltaTime);
    generateCircles();
    checkCollisions();
    drawGame();

    // A circle reaching the bottom is a "miss": lose a life. Game over only
    // once all lives are gone (i.e. 3 misses in a row -- hits refill lives).
    for (int i = 0; i < circleAmount; i++) {
      if (circles[i].active && circles[i].y >= SCREEN_HEIGHT - circleRadius) {
        circles[i].active = false;  // Clear the missed circle so play continues

        if (livesRemaining > 0) {
          livesRemaining--;
        }
        Serial.print("Missed a circle! Lives remaining: ");
        Serial.println(livesRemaining);

        if (livesRemaining == 0) {
          gameOver = true;
          gameOverSequence();
          break;
        }

        // Audible feedback for a non-fatal miss
        if (!crashSoundManager.isSequencePlaying) {
          crashSoundManager.playSequence();
        }
      }
    }
  }

  explosionSoundManager.updateSequence();
  bulletSoundManager.updateSequence();
  crashSoundManager.updateSequence();
  updateIgnoreInputTimer();

  // Reset buttonPressed flag after processing
  if (buttonPressed) {
    buttonPressed = false;
  }
}

void checkButtonState() {
  int currentButtonState = digitalRead(buttonPin1);

#ifdef SERIAL_DEBUG_CONTROLS
  // Simulate a press for this one cycle; it'll read back HIGH (released) on
  // the next cycle, giving the same press+release edges as a real button tap.
  if (debugFirePulse) {
    currentButtonState = LOW;
    debugFirePulse = false;
  }
#endif

  // Detect button press (falling edge)
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    buttonPressed = true;
    buttonActive = true;
    buttonTimer = millis();
    Serial.println("Button pressed");
  }

  // Detect button release (rising edge)
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    buttonActive = false;
    bool wasLongPress = longPressActive;
    longPressActive = false;

    // Process button release for menu navigation
    if (!ignoreInput) {
      int tempPotValue = readPotValue();
      Serial.print("Button released. Game state: ");
      Serial.println(gameScreenState);

      switch (gameScreenState) {
        case 0:  // title screen
          Serial.println("Starting game from title screen");
          resetGame();
          break;
        case 2:  // game over screen
          if (wasLongPress && tempPotValue > 1000) {
            Serial.println("Entering reset scores screen");
            ResetScoresSequence();
          } else {
            Serial.println("Restarting game");
            resetGame();
          }
          break;
        case 3:  // reset scores screen
          if (tempPotValue < 24) {
            Serial.println("Resetting high score");
            ResetHighScore();
          }
          Serial.println("Returning to title screen");
          TitleScreenSequence();
          break;
        default:
          // In game, button release doesn't do anything for menu
          break;
      }
    }

    inactivityTimer = millis(); // Reset inactivity timer
  }

  // Check for long press while button is held
  if (currentButtonState == LOW && buttonActive) {
    if (millis() - buttonTimer > longPressTime && !longPressActive) {
      longPressActive = true;
      Serial.println("Long press detected");
    }

    // Check for shutoff press
    if (millis() - buttonTimer > shutoffPressTime) {
      digitalWrite(controlPin, LOW);
      Serial.println("Shutting down...");
    }
  }

  lastButtonState = currentButtonState;

  // Auto-shutoff after inactivity
  if (millis() - inactivityTimer > autoShutoffTime) {
    digitalWrite(controlPin, LOW);
    Serial.println("Auto-shutting down due to inactivity...");
  }
}

void updateGunPosition() {
  int potValue = readPotValue();

  // Add some smoothing to reduce twitchiness
  static uint8_t smoothedGunX = SCREEN_WIDTH / 2;
  uint8_t targetX = map(potValue, 0, 1023, 0, SCREEN_WIDTH - gunWidth);
  targetX = max((uint8_t)0, min(targetX, (uint8_t)(SCREEN_WIDTH - gunWidth)));

#ifdef SERIAL_DEBUG_CONTROLS
  // Keyboard input already moves in discrete steps (see applyDebugMove);
  // smoothing here would just add extra lag on top of that, so snap directly.
  smoothedGunX = targetX;
#else
  // Smooth movement (40% old position, 60% new position) to tame analog pot noise
  smoothedGunX = (smoothedGunX * 4 + targetX * 6) / 10;
#endif
  gunX = smoothedGunX;
}

void generateBullet() {
  // Check fire rate limit
  if (millis() - lastFireTime < fireDelay) {
    return;  // Too soon to fire again
  }

  lastFireTime = millis();

  // Find an inactive bullet
  for (int i = 0; i < bulletAmount; i++) {
    if (!bullets[i].active) {
      bullets[i].x = gunX + gunWidth / 2;
      bullets[i].y = gunY;
      bullets[i].active = true;

      if (!bulletSoundManager.isSequencePlaying && !explosionSoundManager.isSequencePlaying) {
        bulletSoundManager.playSequence();
      }

      Serial.println("Fired bullet");
      break;
    }
  }
}

void updateBullets(float deltaTime) {
  for (int i = 0; i < bulletAmount; i++) {
    if (bullets[i].active) {
      bullets[i].y -= 96 * deltaTime;  // Slightly faster bullets
      if (bullets[i].y < 0) {
        bullets[i].active = false;
      }
    }
  }
}

void updateCircles(float deltaTime) {
  for (int i = 0; i < circleAmount; i++) {
    Circle& circle = circles[i];
    if (!circle.active) continue;

    circle.y += circle.speed * deltaTime;

    // Check for deactivation at bottom of screen
    if (circle.y > SCREEN_HEIGHT) {
      circle.active = false;
    }
  }
}

void generateCircles() {
  float difficulty = (scoreCount < 100) ? (float)scoreCount / 100.0f : 1.0f;
  maxCirclesAllowed = 3 + (int)(difficulty * 5);

  // Lower spawn rate to reduce twitchiness
  static unsigned long lastSpawnTime = 0;
  const unsigned long minSpawnInterval = 2000;  // ms between spawns

  if (millis() - lastSpawnTime < minSpawnInterval) {
    return;
  }

  for (int i = 0; i < maxCirclesAllowed; i++) {
    if (!circles[i].active) {
      if (random(0, 40) == 0) {  // Reduced spawn chance
        circles[i].x = random(6, SCREEN_WIDTH - 6);
        circles[i].y = 0;
        circles[i].active = true;

        // Smoother difficulty scaling
        float baseSpeed = 2.0f;
        float maxSpeed = 14.0f;
        circles[i].speed = baseSpeed + difficulty * (maxSpeed - baseSpeed);

        lastSpawnTime = millis();
        break;
      }
    }
  }
}

void generateStars() {
  for (int i = 0; i < starAmount; i++) {
    stars[i].x = random(0, SCREEN_WIDTH);
    stars[i].y = random(0, SCREEN_HEIGHT - 15);
  }
}

void generateMountains() {
  int mountainBaseHeight = 55;
  mountainPoints[0][0] = 0;
  mountainPoints[0][1] = mountainBaseHeight;
  for (int i = 1; i < 18; i++) {
    mountainPoints[i][0] = SCREEN_WIDTH * i / 18;
    mountainPoints[i][1] = (i % 2 == 0) ? mountainBaseHeight : mountainBaseHeight - random(0, 3);
  }
  mountainPoints[18][0] = SCREEN_WIDTH;
  mountainPoints[18][1] = mountainBaseHeight - random(0, 4);
}

void checkCollisions() {
  for (int i = 0; i < bulletAmount; i++) {
    if (bullets[i].active) {
      for (int j = 0; j < maxCirclesAllowed; j++) {
        if (circles[j].active) {
          // Calculate actual distance for collision
          float dx = bullets[i].x - circles[j].x;
          float dy = bullets[i].y - circles[j].y;
          float distance = sqrt(dx * dx + dy * dy);

          if (distance < (circleRadius + 2)) {  // 2 = bullet radius
            bullets[i].active = false;
            drawExplosion(circles[j].x, circles[j].y, circleRadius);
            circles[j].active = false;

            if (!explosionSoundManager.isSequencePlaying) {
              explosionSoundManager.playSequence();
            }

            digitalWrite(flashPin, HIGH);
            delay(30); // Shorter flash
            digitalWrite(flashPin, LOW);
            scoreCount++;
            livesRemaining = maxLives;  // A hit refills lives -> misses must be consecutive
            Serial.print("Score: ");
            Serial.println(scoreCount);
          }
        }
      }
    }
  }
}

void drawExplosion(int x, int y, int radius) {
  // Find an inactive explosion slot and activate it
  for (int i = 0; i < explosionAmount; i++) {
    if (!explosions[i].active) {
      explosions[i].x = x;
      explosions[i].y = y;
      explosions[i].framesLeft = 4;  // Show for 4 frames
      explosions[i].active = true;
      break;
    }
  }
}

// Draws a small (~5px wide) filled heart. (cx, cy) is the center of the two
// top lobes; the point extends a few pixels below.
void drawHeart(int cx, int cy) {
  display.fillCircle(cx - 1, cy, 1, SSD1306_WHITE);   // left lobe
  display.fillCircle(cx + 1, cy, 1, SSD1306_WHITE);   // right lobe
  display.fillTriangle(
    cx - 2, cy + 1,
    cx + 2, cy + 1,
    cx,     cy + 3,
    SSD1306_WHITE);                                    // bottom point
}

void updateAndDrawExplosions() {
  for (int i = 0; i < explosionAmount; i++) {
    if (explosions[i].active) {
      int x = explosions[i].x;
      int y = explosions[i].y;
      int explosionRadius = circleRadius * 3;

      display.drawLine(x - explosionRadius, y, x + explosionRadius, y, SSD1306_WHITE);
      display.drawLine(x, y - explosionRadius, x, y + explosionRadius, SSD1306_WHITE);
      display.drawLine(x - explosionRadius / 1.414, y - explosionRadius / 1.414, x + explosionRadius / 1.414, y + explosionRadius / 1.414, SSD1306_WHITE);
      display.drawLine(x - explosionRadius / 1.414, y + explosionRadius / 1.414, x + explosionRadius / 1.414, y - explosionRadius / 1.414, SSD1306_WHITE);

      explosions[i].framesLeft--;
      if (explosions[i].framesLeft == 0) {
        explosions[i].active = false;
      }
    }
  }
}

void drawGame() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  drawMountains();
  drawStars();

  display.setCursor(60, 57);
  display.print(scoreCount);

  // Draw remaining lives as small hearts in the top-left corner
  for (int i = 0; i < livesRemaining; i++) {
    drawHeart(4 + i * 8, 3);
  }

  // Draw gun as a triangle
  display.drawTriangle(
    gunX, gunY,
    gunX + gunWidth, gunY,
    gunX + gunWidth / 2, gunY - gunHeight,
    SSD1306_WHITE
  );

  // Draw bullets as small lines instead of pixels
  for (int i = 0; i < bulletAmount; i++) {
    if (bullets[i].active) {
      display.drawLine(bullets[i].x, bullets[i].y, bullets[i].x, bullets[i].y - 2, SSD1306_WHITE);
    }
  }

  // Draw circles
  for (int i = 0; i < circleAmount; i++) {
    if (circles[i].active) {
      display.fillCircle(circles[i].x, circles[i].y, circleRadius - 1, SSD1306_WHITE);
    }
  }

  updateAndDrawExplosions();

  display.display();
}

void drawStars() {
  for (int i = 0; i < starAmount; i++) {
    display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
  }
}

void drawMountains() {
  const uint8_t numPoints = sizeof(mountainPoints) / sizeof(mountainPoints[0]);

  for (int i = 0; i < numPoints - 1; i++) {
    display.drawLine(
      mountainPoints[i][0], mountainPoints[i][1],
      mountainPoints[i + 1][0], mountainPoints[i + 1][1],
      SSD1306_WHITE
    );
  }
}

void writeIntIntoEEPROM(int address, int number) {
  byte byte1 = number >> 8;
  byte byte2 = number & 0xFF;
#ifdef USE_EEPROM
  EEPROM.write(address, byte1);
  EEPROM.write(address + 1, byte2);
#endif
}

int readIntFromEEPROM(int address) {
#ifdef USE_EEPROM
  byte byte1 = EEPROM.read(address);
  byte byte2 = EEPROM.read(address + 1);
  return (byte1 << 8) + byte2;
#else
  return 0;
#endif
}

void setIgnoreInputForDuration(unsigned long duration) {
  ignoreInput = true;
  ignoreInputDuration = duration;
  ignoreInputStartTime = millis();
  Serial.print("Ignoring input for ");
  Serial.print(duration);
  Serial.println(" ms");
}

void updateIgnoreInputTimer() {
  if (ignoreInput && millis() - ignoreInputStartTime >= ignoreInputDuration) {
    ignoreInput = false;
    Serial.println("Input allowed again");
    UpdateMenuScreenAfterInputAllowed();
  }
}

void UpdateMenuScreenAfterInputAllowed() {
  if (gameScreenState == 0) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(5, 10);
    display.print(F("SPACE DROP"));
    display.setTextSize(1);
    display.setCursor(10, 50);
    display.print(F("High Score: "));
    display.print(savedHighScore);
    display.setCursor(20, 40);
    display.print(F("Press to Start"));
    display.display();
  } else if (gameScreenState == 2) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 5);
    display.print(F("GAME OVER"));
    display.setTextSize(1);
    display.setCursor(40, 25);
    display.print(F("Score: "));
    display.print(scoreCount);
    display.setCursor(10, 35);
    display.print(F("High Score: "));
    display.print(savedHighScore);
    display.setCursor(30, 45);
    display.print(F("Play Again?"));
    display.display();
  }
}

void InitializeHighScore() {
#ifdef USE_EEPROM
  if (EEPROM.read(EEPROM_SIG_ADDR) != EEPROM_SIG_VALUE) {
    EEPROM.write(EEPROM_SIG_ADDR, EEPROM_SIG_VALUE);
    writeIntIntoEEPROM(0, 0);
    commitEEPROM();
  }
  savedHighScore = readIntFromEEPROM(0);
#else
  savedHighScore = 0;
#endif
  Serial.print("Loaded high score: ");
  Serial.println(savedHighScore);
}

void UpdateHighScore() {
#ifdef USE_EEPROM
  savedHighScore = readIntFromEEPROM(0);
  if (scoreCount > savedHighScore) {
    savedHighScore = scoreCount;
    writeIntIntoEEPROM(0, savedHighScore);
    commitEEPROM();
    Serial.print("New high score saved: ");
    Serial.println(savedHighScore);
  }
#endif
}

void ResetHighScore() {
#ifdef USE_EEPROM
  savedHighScore = 0;
  writeIntIntoEEPROM(0, 0);
  commitEEPROM();
  Serial.println("High score reset to 0");
#endif
}

void TitleScreenSequence() {
  gameScreenState = 0;
  Serial.println("Showing title screen");

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 10);
  display.print(F("SPACE DROP"));
  display.setTextSize(1);
  display.setCursor(10, 50);
  display.print(F("High Score: "));
  display.print(savedHighScore);
  display.display();
  setIgnoreInputForDuration(800);
}

void gameOverSequence() {
  gameScreenState = 2;
  Serial.println("Game over sequence");

  if (!crashSoundManager.isSequencePlaying) {
    crashSoundManager.playSequence();
  }

  UpdateHighScore();

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 5);
  display.print(F("GAME OVER"));
  display.setTextSize(1);
  display.setCursor(40, 25);
  display.print(F("Score: "));
  display.print(scoreCount);
  display.setCursor(10, 35);
  display.print(F("High Score: "));
  display.print(savedHighScore);
  display.display();
  setIgnoreInputForDuration(1500);
}

void ResetScoresSequence() {
  gameScreenState = 3;
  Serial.println("Reset scores sequence");

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.print(F("Reset high scores?"));
  display.setCursor(5, 40);
  display.print(F("Full left to confirm"));
  display.setCursor(15, 50);
  display.print(F("Right to cancel"));
  display.display();
}

void resetGame() {
  Serial.println("Resetting game...");

  gameScreenState = 1;

  for (int i = 0; i < bulletAmount; i++) {
    bullets[i].active = false;
  }

  for (int i = 0; i < circleAmount; i++) {
    circles[i].active = false;
  }

  for (int i = 0; i < explosionAmount; i++) {
    explosions[i].active = false;
  }

  gunX = SCREEN_WIDTH / 2;
  gameOver = false;
  scoreCount = 0;
  livesRemaining = maxLives;

  display.clearDisplay();
  display.display();

  generateMountains();
  generateStars();

  inactivityTimer = millis(); // Reset inactivity timer
  lastFireTime = 0; // Reset fire timer
  Serial.println("Game reset complete - Ready to play!");
}
