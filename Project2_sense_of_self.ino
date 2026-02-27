#include <Arduino.h>
#include <ESP32Servo.h>
#include <DFRobot_MAX98357A.h>

// ===================== STATE MACHINE TYPES (MUST BE EARLY) =====================
enum State { IDLE, MOVING_TO_HIFIVE, HOLDING, RETURNING, WAIT_RELEASE };

// ===================== PINS (EDIT IF NEEDED) =====================
// Buttons (pressed = LOW with INPUT_PULLUP)
const int BUTTON1_PIN = GPIO_NUM_14;   // Feather V2 user button pin
const int BUTTON2_PIN = GPIO_NUM_32;   // your 2nd button GPIO

// Servos
const int SERVO1_PIN  = GPIO_NUM_25;
const int SERVO2_PIN  = GPIO_NUM_26;

// I2S pins -> MAX98357A
const gpio_num_t I2S_BCLK = GPIO_NUM_27;
const gpio_num_t I2S_LRCK = GPIO_NUM_33;   // WS/LRC/LRCLK
const gpio_num_t I2S_DIN  = GPIO_NUM_13;

// SD card CS pin (SPI)
// Feather V2 SPI: SCK=5, MOSI=19, MISO=21 (use labeled SCK/MO/MI pads)
const gpio_num_t SD_CS = GPIO_NUM_4;

// WAV file on SD (try "HIFIVE.WAV" if /HIFIVE.WAV fails)
const char* WAV_FILE = "/HIFIVE.WAV";

// ===================== SERVO ANGLES =====================
const int SERVO1_REST   = 30;
const int SERVO2_REST   = 30;
const int SERVO1_HIFIVE = 110;
const int SERVO2_HIFIVE = 110;

const uint16_t SERVO_STEP_DELAY_MS = 5;
const uint16_t HIFIVE_HOLD_MS      = 600;

// ===================== BUTTON DEBOUNCE =====================
const uint16_t DEBOUNCE_MS = 25;

struct DebouncedButton {
  int pin;
  bool stablePressed = false;
  bool lastRaw = HIGH;
  uint32_t lastChangeMs = 0;

  void begin(int p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    lastRaw = digitalRead(pin);
    stablePressed = (lastRaw == LOW);
    lastChangeMs = millis();
  }

  void update() {
    bool raw = digitalRead(pin);
    if (raw != lastRaw) {
      lastRaw = raw;
      lastChangeMs = millis();
    }
    if (millis() - lastChangeMs >= DEBOUNCE_MS) {
      stablePressed = (raw == LOW);
    }
  }

  bool pressed() const { return stablePressed; }
};

// ===================== GLOBALS =====================
DebouncedButton b1, b2;

Servo s1, s2;

DFRobot_MAX98357A amplifier;

State state = IDLE;
uint32_t stateMs = 0;

// ===================== FUNCTIONS =====================
void setState(State s) {
  state = s;
  stateMs = millis();
}

void sweepServosTo(int target1, int target2) {
  int a1 = s1.read();
  int a2 = s2.read();

  // If Servo.read() isn't reliable before first move, start from rest:
  if (a1 == 0 && SERVO1_REST != 0) a1 = SERVO1_REST;
  if (a2 == 0 && SERVO2_REST != 0) a2 = SERVO2_REST;

  while (a1 != target1 || a2 != target2) {
    if (a1 < target1) a1++;
    else if (a1 > target1) a1--;

    if (a2 < target2) a2++;
    else if (a2 > target2) a2--;

    s1.write(a1);
    s2.write(a2);
    delay(SERVO_STEP_DELAY_MS);
  }
}

bool initAudio() {
  Serial.println("Init I2S...");
  while (!amplifier.initI2S(I2S_BCLK, I2S_LRCK, I2S_DIN)) {
    Serial.println("I2S init failed. Check BCLK/LRCK/DIN wiring.");
    delay(1200);
  }

  Serial.println("Init SD...");
  while (!amplifier.initSDCard(SD_CS)) {
    Serial.println("SD init failed. Check CS/SPI wiring + FAT32 format.");
    delay(1200);
  }

  amplifier.setVolume(6);
  amplifier.closeFilter();

  Serial.println("Audio ready.");
  return true;
}

void playHighFiveSound() {
  amplifier.SDPlayerControl(SD_AMPLIFIER_STOP);
  delay(30);

  Serial.print("Playing: ");
  Serial.println(WAV_FILE);

  amplifier.playSDMusic(WAV_FILE);
  amplifier.SDPlayerControl(SD_AMPLIFIER_PLAY);
}

// ===================== SETUP / LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(200);

  // Buttons
  b1.begin(BUTTON1_PIN);
  b2.begin(BUTTON2_PIN);

  // Servos (ESP32Servo)
  s1.setPeriodHertz(50);
  s2.setPeriodHertz(50);
  s1.attach(SERVO1_PIN, 500, 2400);
  s2.attach(SERVO2_PIN, 500, 2400);
  s1.write(SERVO1_REST);
  s2.write(SERVO2_REST);

  // Audio
  initAudio();

  Serial.println("Ready: press BOTH buttons for high five!");
}

void loop() {
  b1.update();
  b2.update();

  bool bothPressed = b1.pressed() && b2.pressed();

  switch (state) {
    case IDLE:
      if (bothPressed) setState(MOVING_TO_HIFIVE);
      break;

    case MOVING_TO_HIFIVE:
      sweepServosTo(SERVO1_HIFIVE, SERVO2_HIFIVE);
      playHighFiveSound();
      setState(HOLDING);
      break;

    case HOLDING:
      if (millis() - stateMs >= HIFIVE_HOLD_MS) setState(RETURNING);
      break;

    case RETURNING:
      sweepServosTo(SERVO1_REST, SERVO2_REST);
      setState(WAIT_RELEASE);
      break;

    case WAIT_RELEASE:
      if (!bothPressed) setState(IDLE);
      break;
  }
}