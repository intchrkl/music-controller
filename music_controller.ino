#include <Adafruit_NeoPixel.h>

#define PIXEL_PIN   2
#define NUM_PIXELS  1

#define PLAYPAUSE_PIN 8
#define FORWARD_PIN   7
#define BACKWARD_PIN  6
#define MUTE_PIN      5
#define POT_PIN       A7

Adafruit_NeoPixel pixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Track last button states
bool lastPlayPauseButtonState = HIGH;
bool lastForwardButtonState   = HIGH;
bool lastBackwardButtonState  = HIGH;
bool lastMuteButtonState      = HIGH;

// Potentiometer smoothing / sending
float smoothedPotValue = 0.0;
int lastSentVolume = -1;

const float alpha = 0.1;                // smoothing factor
const int volumeThreshold = 2;          // only send if changed by >= 2
const unsigned long sendInterval = 50;  // ms between volume sends
unsigned long lastVolumeSendTime = 0;

void setup() {
  pinMode(PLAYPAUSE_PIN, INPUT_PULLUP);
  pinMode(FORWARD_PIN, INPUT_PULLUP);
  pinMode(BACKWARD_PIN, INPUT_PULLUP);
  pinMode(MUTE_PIN, INPUT_PULLUP);

  pixel.begin();
  pixel.clear();
  pixel.show();

  Serial.begin(9600);
}

void handleButtons() {
  // --- PLAY/PAUSE ---
  bool currentPlayPauseButtonState = digitalRead(PLAYPAUSE_PIN);

  if (lastPlayPauseButtonState == HIGH && currentPlayPauseButtonState == LOW) {
    Serial.println("BTN|PLAYPAUSE");
    pixel.setPixelColor(0, pixel.Color(0, 150, 0)); // green
    pixel.show();
  }

  if (lastPlayPauseButtonState == LOW && currentPlayPauseButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }

  lastPlayPauseButtonState = currentPlayPauseButtonState;

  // --- FORWARD ---
  bool currentForwardButtonState = digitalRead(FORWARD_PIN);

  if (lastForwardButtonState == HIGH && currentForwardButtonState == LOW) {
    Serial.println("BTN|NEXT");
    pixel.setPixelColor(0, pixel.Color(0, 0, 150)); // blue
    pixel.show();
  }

  if (lastForwardButtonState == LOW && currentForwardButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }

  lastForwardButtonState = currentForwardButtonState;

  // --- BACKWARD ---
  bool currentBackwardButtonState = digitalRead(BACKWARD_PIN);

  if (lastBackwardButtonState == HIGH && currentBackwardButtonState == LOW) {
    Serial.println("BTN|PREV");
    pixel.setPixelColor(0, pixel.Color(150, 0, 0)); // red
    pixel.show();
  }

  if (lastBackwardButtonState == LOW && currentBackwardButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }

  lastBackwardButtonState = currentBackwardButtonState;

  // --- MUTE ---
  bool currentMuteButtonState = digitalRead(MUTE_PIN);

  if (lastMuteButtonState == HIGH && currentMuteButtonState == LOW) {
    Serial.println("BTN|MUTE");
    pixel.setPixelColor(0, pixel.Color(150, 150, 0)); // yellow
    pixel.show();
  }

  if (lastMuteButtonState == LOW && currentMuteButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }

  lastMuteButtonState = currentMuteButtonState;
}

void handleVolume() {
  int rawValue = analogRead(POT_PIN);

  smoothedPotValue = alpha * rawValue + (1.0 - alpha) * smoothedPotValue;
  int smoothValue = (int)smoothedPotValue;

  int volume = map(smoothValue, 0, 4095, 0, 100);

  // only send smoothed and updated volumes
  if (abs(volume - lastSentVolume) >= volumeThreshold) {
    if (millis() - lastVolumeSendTime >= sendInterval) {
      Serial.println("VOL|" + String(volume));
      lastSentVolume = volume;
      lastVolumeSendTime = millis();
    }
  }
}

void loop() {
  handleButtons();
  handleVolume();
  delay(10);
}